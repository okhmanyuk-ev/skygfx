#include "backend_d3d12.h"

#ifdef SKYGFX_HAS_D3D12

#include <stdexcept>

#include <d3dcompiler.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <dxgidebug.h>

#pragma comment(lib, "d3d12")
#pragma comment(lib, "d3dcompiler")
#pragma comment(lib, "dxgi")
#pragma comment(lib, "dxguid.lib")

template <typename T>
inline void D3D12Release(T& a)
{
	if (!a)
		return;

	a->Release();

	if constexpr (!std::is_const<T>())
		a = NULL;
}

struct FrameContext
{
	ID3D12CommandAllocator* CommandAllocator;
	UINT64 FenceValue;
};

static int const NUM_BACK_BUFFERS = 3;
static int const NUM_FRAMES_IN_FLIGHT = 3;

static FrameContext g_frameContext[NUM_FRAMES_IN_FLIGHT] = {};
static UINT g_frameIndex = 0;
static ID3D12Device* D3D12Device = NULL;
static ID3D12CommandQueue* D3D12CommandQueue = NULL;
static IDXGISwapChain3* D3D12SwapChain = NULL;
static HANDLE D3D12SwapChainWaitableObject = NULL;
static ID3D12GraphicsCommandList* D3D12CommandList = NULL;

static ID3D12Fence* g_fence = NULL;
static HANDLE g_fenceEvent = NULL;

static HANDLE g_hSwapChainWaitableObject = NULL;
static UINT64 g_fenceLastSignaledValue = 0;

static ID3D12Resource* g_mainRenderTargetResource[NUM_BACK_BUFFERS] = {};
static FrameContext* gFrameCtx = NULL;
static UINT gBackBufferIdx = 0;

static D3D12_CPU_DESCRIPTOR_HANDLE g_mainRenderTargetDescriptor[NUM_BACK_BUFFERS] = {};

static ID3D12DescriptorHeap* g_pd3dSrvDescHeap = NULL;
static ID3D12DescriptorHeap* g_pd3dRtvDescHeap = NULL;

using namespace skygfx;

BackendD3D12::BackendD3D12(void* window, uint32_t width, uint32_t height)
{
	ID3D12Debug* D3D12Debug = NULL;
	D3D12GetDebugInterface(IID_PPV_ARGS(&D3D12Debug));
	D3D12Debug->EnableDebugLayer();

	auto feature = D3D_FEATURE_LEVEL_12_1;
	D3D12CreateDevice(NULL, feature, IID_PPV_ARGS(&D3D12Device));

	if (D3D12Debug != NULL)
	{
		ID3D12InfoQueue* pInfoQueue = NULL;
		D3D12Device->QueryInterface(IID_PPV_ARGS(&pInfoQueue));
		pInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, true);
		pInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, true);
		pInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING, true);
		pInfoQueue->Release();
		D3D12Debug->Release();
	}

	{
		D3D12_DESCRIPTOR_HEAP_DESC desc = {};
		desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
		desc.NumDescriptors = NUM_BACK_BUFFERS;
		desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
		desc.NodeMask = 1;
		D3D12Device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&g_pd3dRtvDescHeap));
		
		SIZE_T rtvDescriptorSize = D3D12Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
		D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = g_pd3dRtvDescHeap->GetCPUDescriptorHandleForHeapStart();
		for (UINT i = 0; i < NUM_BACK_BUFFERS; i++)
		{
			g_mainRenderTargetDescriptor[i] = rtvHandle;
			rtvHandle.ptr += rtvDescriptorSize;
		}
	}

	{
		D3D12_DESCRIPTOR_HEAP_DESC desc = {};
		desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		desc.NumDescriptors = 1;
		desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		D3D12Device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&g_pd3dSrvDescHeap));
	}

	{
		D3D12_COMMAND_QUEUE_DESC desc = {};
		desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
		desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
		desc.NodeMask = 1;
		D3D12Device->CreateCommandQueue(&desc, IID_PPV_ARGS(&D3D12CommandQueue));
	}

	for (UINT i = 0; i < NUM_FRAMES_IN_FLIGHT; i++)
	{
		D3D12Device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&g_frameContext[i].CommandAllocator));
	}

	D3D12Device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, g_frameContext[0].CommandAllocator, NULL, IID_PPV_ARGS(&D3D12CommandList));
	D3D12CommandList->Close();

	D3D12Device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&g_fence));

	{
		DXGI_SWAP_CHAIN_DESC1 sd = {};
		sd.BufferCount = NUM_BACK_BUFFERS;
		sd.Width = 0;
		sd.Height = 0;
		sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		sd.Flags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;
		sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		sd.SampleDesc.Count = 1;
		sd.SampleDesc.Quality = 0;
		sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
		sd.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
		sd.Scaling = DXGI_SCALING_STRETCH;
		sd.Stereo = FALSE;

		IDXGIFactory4* dxgiFactory = NULL;
		IDXGISwapChain1* swapChain1 = NULL;
		CreateDXGIFactory1(IID_PPV_ARGS(&dxgiFactory)) != S_OK ||
			dxgiFactory->CreateSwapChainForHwnd(D3D12CommandQueue, (HWND)window, &sd, NULL, NULL, &swapChain1) != S_OK ||
			swapChain1->QueryInterface(IID_PPV_ARGS(&D3D12SwapChain));
		
		swapChain1->Release();
		dxgiFactory->Release();
		D3D12SwapChain->SetMaximumFrameLatency(NUM_BACK_BUFFERS);
		D3D12SwapChainWaitableObject = D3D12SwapChain->GetFrameLatencyWaitableObject();
	}

	createRenderTarget();

	begin();
}

BackendD3D12::~BackendD3D12()
{
	end();
	cleanupRenderTarget();
}

void WaitForLastSubmittedFrame()
{
	FrameContext* frameCtx = &g_frameContext[g_frameIndex % NUM_FRAMES_IN_FLIGHT];

	UINT64 fenceValue = frameCtx->FenceValue;
	if (fenceValue == 0)
		return; // No fence was signaled

	frameCtx->FenceValue = 0;
	if (g_fence->GetCompletedValue() >= fenceValue)
		return;

	g_fence->SetEventOnCompletion(fenceValue, g_fenceEvent);
	WaitForSingleObject(g_fenceEvent, INFINITE);
}

void BackendD3D12::createRenderTarget()
{
	for (UINT i = 0; i < NUM_BACK_BUFFERS; i++)
	{
		ID3D12Resource* pBackBuffer = NULL;
		D3D12SwapChain->GetBuffer(i, IID_PPV_ARGS(&pBackBuffer));
		D3D12Device->CreateRenderTargetView(pBackBuffer, NULL, g_mainRenderTargetDescriptor[i]);
		g_mainRenderTargetResource[i] = pBackBuffer;
	}
}

void BackendD3D12::cleanupRenderTarget()
{
	WaitForLastSubmittedFrame();

	for (UINT i = 0; i < NUM_BACK_BUFFERS; i++)
		if (g_mainRenderTargetResource[i]) { g_mainRenderTargetResource[i]->Release(); g_mainRenderTargetResource[i] = NULL; }
}

void BackendD3D12::resize(uint32_t width, uint32_t height)
{
}

void BackendD3D12::setTopology(Topology topology)
{
}

void BackendD3D12::setViewport(std::optional<Viewport> viewport)
{
}

void BackendD3D12::setScissor(std::optional<Scissor> scissor)
{
}

void BackendD3D12::setTexture(uint32_t binding, TextureHandle* handle)
{
}

void BackendD3D12::setRenderTarget(RenderTargetHandle* handle)
{
}

void BackendD3D12::setRenderTarget(std::nullptr_t value)
{
}

void BackendD3D12::setShader(ShaderHandle* handle)
{
}

void BackendD3D12::setVertexBuffer(VertexBufferHandle* handle)
{
}

void BackendD3D12::setIndexBuffer(IndexBufferHandle* handle)
{
}

void BackendD3D12::setUniformBuffer(uint32_t binding, UniformBufferHandle* handle)
{
}

void BackendD3D12::setBlendMode(const BlendMode& value)
{
}

void BackendD3D12::setDepthMode(std::optional<DepthMode> depth_mode)
{
}

void BackendD3D12::setStencilMode(std::optional<StencilMode> stencil_mode)
{
}

void BackendD3D12::setCullMode(CullMode cull_mode)
{
}

void BackendD3D12::setSampler(Sampler value)
{
}

void BackendD3D12::setTextureAddress(TextureAddress value)
{
}

void BackendD3D12::clear(const std::optional<glm::vec4>& color, const std::optional<float>& depth,
	const std::optional<uint8_t>& stencil)
{
	glm::vec4 clear_color = { 0.0f, 1.0f, 0.0f, 1.0f };

	D3D12CommandList->ClearRenderTargetView(g_mainRenderTargetDescriptor[gBackBufferIdx], (float*)&clear_color, 0, NULL);
	D3D12CommandList->OMSetRenderTargets(1, &g_mainRenderTargetDescriptor[gBackBufferIdx], FALSE, NULL);
	D3D12CommandList->SetDescriptorHeaps(1, &g_pd3dSrvDescHeap);
}

void BackendD3D12::draw(uint32_t vertex_count, uint32_t vertex_offset)
{
}

void BackendD3D12::drawIndexed(uint32_t index_count, uint32_t index_offset)
{
}

void BackendD3D12::readPixels(const glm::ivec2& pos, const glm::ivec2& size, TextureHandle* dst_texture_handle)
{
}

FrameContext* WaitForNextFrameResources()
{
	UINT nextFrameIndex = g_frameIndex + 1;
	g_frameIndex = nextFrameIndex;

	HANDLE waitableObjects[] = { g_hSwapChainWaitableObject, NULL };
	DWORD numWaitableObjects = 1;

	FrameContext* frameCtx = &g_frameContext[nextFrameIndex % NUM_FRAMES_IN_FLIGHT];
	UINT64 fenceValue = frameCtx->FenceValue;
	if (fenceValue != 0) // means no fence was signaled
	{
		frameCtx->FenceValue = 0;
		g_fence->SetEventOnCompletion(fenceValue, g_fenceEvent);
		waitableObjects[1] = g_fenceEvent;
		numWaitableObjects = 2;
	}

	WaitForMultipleObjects(numWaitableObjects, waitableObjects, TRUE, INFINITE);

	return frameCtx;
}

void BackendD3D12::present()
{
	end();
	
	D3D12CommandQueue->ExecuteCommandLists(1, (ID3D12CommandList* const*)&D3D12CommandList);
	D3D12SwapChain->Present(0, 0);
	
	UINT64 fenceValue = g_fenceLastSignaledValue + 1;
	D3D12CommandQueue->Signal(g_fence, fenceValue);
	g_fenceLastSignaledValue = fenceValue;
	gFrameCtx->FenceValue = fenceValue;

	begin();
}

void BackendD3D12::begin()
{
	gFrameCtx = WaitForNextFrameResources();
	gBackBufferIdx = D3D12SwapChain->GetCurrentBackBufferIndex();
	gFrameCtx->CommandAllocator->Reset();

	D3D12_RESOURCE_BARRIER barrier = {};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
	barrier.Transition.pResource = g_mainRenderTargetResource[gBackBufferIdx];
	barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
	barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;

	D3D12CommandList->Reset(gFrameCtx->CommandAllocator, NULL);
	D3D12CommandList->ResourceBarrier(1, &barrier);
}

void BackendD3D12::end()
{
	D3D12_RESOURCE_BARRIER barrier = {};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
	barrier.Transition.pResource = g_mainRenderTargetResource[gBackBufferIdx];
	barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
	barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;

	D3D12CommandList->ResourceBarrier(1, &barrier);
	D3D12CommandList->Close();
}

TextureHandle* BackendD3D12::createTexture(uint32_t width, uint32_t height, uint32_t channels, void* memory, bool mipmap)
{
	return nullptr;
}

void BackendD3D12::destroyTexture(TextureHandle* handle)
{
}

RenderTargetHandle* BackendD3D12::createRenderTarget(uint32_t width, uint32_t height, TextureHandle* texture_handle)
{
	return nullptr;
}

void BackendD3D12::destroyRenderTarget(RenderTargetHandle* handle)
{
}

ShaderHandle* BackendD3D12::createShader(const Vertex::Layout& layout, const std::string& vertex_code,
	const std::string& fragment_code, const std::vector<std::string>& defines)
{
	return nullptr;
}

void BackendD3D12::destroyShader(ShaderHandle* handle)
{
}

VertexBufferHandle* BackendD3D12::createVertexBuffer(void* memory, size_t size, size_t stride)
{
	return nullptr;
}

void BackendD3D12::destroyVertexBuffer(VertexBufferHandle* handle)
{
}

void BackendD3D12::writeVertexBufferMemory(VertexBufferHandle* handle, void* memory, size_t size, size_t stride)
{
}

IndexBufferHandle* BackendD3D12::createIndexBuffer(void* memory, size_t size, size_t stride)
{
	return nullptr;
}

void BackendD3D12::writeIndexBufferMemory(IndexBufferHandle* handle, void* memory, size_t size, size_t stride)
{
}

void BackendD3D12::destroyIndexBuffer(IndexBufferHandle* handle)
{
}

UniformBufferHandle* BackendD3D12::createUniformBuffer(void* memory, size_t size)
{
	return nullptr;
}

void BackendD3D12::destroyUniformBuffer(UniformBufferHandle* handle)
{
}

void BackendD3D12::writeUniformBufferMemory(UniformBufferHandle* handle, void* memory, size_t size)
{
}

#endif