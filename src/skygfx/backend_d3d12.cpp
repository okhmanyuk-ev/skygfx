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
static HANDLE g_hSwapChainWaitableObject = NULL;
static ID3D12GraphicsCommandList* D3D12CommandList = NULL;

static ID3D12Fence* g_fence = NULL;
static HANDLE g_fenceEvent = NULL;

static UINT64 g_fenceLastSignaledValue = 0;

static ID3D12Resource* g_mainRenderTargetResource[NUM_BACK_BUFFERS] = {};
static FrameContext* gFrameCtx = NULL;
static UINT gBackBufferIdx = 0;

static D3D12_CPU_DESCRIPTOR_HANDLE g_mainRenderTargetDescriptor[NUM_BACK_BUFFERS] = {};

static ID3D12DescriptorHeap* g_pd3dSrvDescHeap = NULL;
static ID3D12DescriptorHeap* g_pd3dRtvDescHeap = NULL;
static ID3D12RootSignature* g_pRootSignature = NULL;

class ShaderD3D12;

static ShaderD3D12* gShader = nullptr;

template <typename T>
inline void D3D12Release(T& a)
{
	if (!a)
		return;

	a->Release();

	if constexpr (!std::is_const<T>())
		a = NULL;
}

using namespace skygfx;

class ShaderD3D12
{
	friend class BackendD3D12;

private:
	ID3D12PipelineState* pipeline_state; // TODO: this is bad place for it

public:
	ShaderD3D12(const Vertex::Layout& layout, const std::string& vertex_code, const std::string& fragment_code,
		std::vector<std::string> defines)
	{
		ID3DBlob* vertexShaderBlob;
		ID3DBlob* pixelShaderBlob;

		ID3DBlob* vertex_shader_error;
		ID3DBlob* pixel_shader_error;
		
		AddShaderLocationDefines(layout, defines);

		auto vertex_shader_spirv = CompileGlslToSpirv(ShaderStage::Vertex, vertex_code, defines);
		auto fragment_shader_spirv = CompileGlslToSpirv(ShaderStage::Fragment, fragment_code, defines);

		auto hlsl_vert = CompileSpirvToHlsl(vertex_shader_spirv, HlslVersion::v5_0);
		auto hlsl_frag = CompileSpirvToHlsl(fragment_shader_spirv, HlslVersion::v5_0);

		D3DCompile(hlsl_vert.c_str(), hlsl_vert.size(), NULL, NULL, NULL, "main", "vs_5_0", 0, 0, &vertexShaderBlob, &vertex_shader_error);
		D3DCompile(hlsl_frag.c_str(), hlsl_frag.size(), NULL, NULL, NULL, "main", "ps_5_0", 0, 0, &pixelShaderBlob, &pixel_shader_error);

		std::string vertex_shader_error_string = "";
		std::string pixel_shader_error_string = "";

		if (vertex_shader_error != nullptr)
			vertex_shader_error_string = std::string((char*)vertex_shader_error->GetBufferPointer(), vertex_shader_error->GetBufferSize());

		if (pixel_shader_error != nullptr)
			pixel_shader_error_string = std::string((char*)pixel_shader_error->GetBufferPointer(), pixel_shader_error->GetBufferSize());

		if (vertexShaderBlob == nullptr)
			throw std::runtime_error(vertex_shader_error_string);

		if (pixelShaderBlob == nullptr)
			throw std::runtime_error(pixel_shader_error_string);

		D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc = {};
		pso_desc.NodeMask = 1;
		pso_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		pso_desc.pRootSignature = g_pRootSignature;
		pso_desc.SampleMask = UINT_MAX;
		pso_desc.NumRenderTargets = 1;
		pso_desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
		pso_desc.SampleDesc.Count = 1;
		pso_desc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;

		pso_desc.VS = { vertexShaderBlob->GetBufferPointer(), vertexShaderBlob->GetBufferSize() };
		pso_desc.PS = { pixelShaderBlob->GetBufferPointer(), pixelShaderBlob->GetBufferSize() };

		//D3D12Device->CreateVertexShader(vertexShaderBlob->GetBufferPointer(), vertexShaderBlob->GetBufferSize(), nullptr, &vertex_shader);
		//D3D12Device->CreatePixelShader(pixelShaderBlob->GetBufferPointer(), pixelShaderBlob->GetBufferSize(), nullptr, &pixel_shader);

		static const std::unordered_map<Vertex::Attribute::Format, DXGI_FORMAT> Format = {
			{ Vertex::Attribute::Format::R32F, DXGI_FORMAT_R32_FLOAT },
			{ Vertex::Attribute::Format::R32G32F, DXGI_FORMAT_R32G32_FLOAT },
			{ Vertex::Attribute::Format::R32G32B32F, DXGI_FORMAT_R32G32B32_FLOAT },
			{ Vertex::Attribute::Format::R32G32B32A32F, DXGI_FORMAT_R32G32B32A32_FLOAT },
			{ Vertex::Attribute::Format::R8UN, DXGI_FORMAT_R8_UNORM },
			{ Vertex::Attribute::Format::R8G8UN, DXGI_FORMAT_R8G8_UNORM },
			//	{ Vertex::Attribute::Format::R8G8B8UN, DXGI_FORMAT_R8G8B8_UNORM }, // TODO: fix
			{ Vertex::Attribute::Format::R8G8B8A8UN, DXGI_FORMAT_R8G8B8A8_UNORM }
		};

		std::vector<D3D12_INPUT_ELEMENT_DESC> input;

		UINT i = 0;

		for (auto& attrib : layout.attributes)
		{
			input.push_back({ "TEXCOORD", i, Format.at(attrib.format), 0,
				static_cast<UINT>(attrib.offset), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 });
			i++;
		}

		pso_desc.InputLayout = { input.data(), (UINT)input.size() };

		{
			D3D12_BLEND_DESC& desc = pso_desc.BlendState;
			desc.AlphaToCoverageEnable = false;
			desc.RenderTarget[0].BlendEnable = true;
			desc.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
			desc.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
			desc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
			desc.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
			desc.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
			desc.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
			desc.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
		}

		// Create the rasterizer state
		{
			D3D12_RASTERIZER_DESC& desc = pso_desc.RasterizerState;
			desc.FillMode = D3D12_FILL_MODE_SOLID;
			desc.CullMode = D3D12_CULL_MODE_NONE;
			desc.FrontCounterClockwise = FALSE;
			desc.DepthBias = D3D12_DEFAULT_DEPTH_BIAS;
			desc.DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
			desc.SlopeScaledDepthBias = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
			desc.DepthClipEnable = true;
			desc.MultisampleEnable = FALSE;
			desc.AntialiasedLineEnable = FALSE;
			desc.ForcedSampleCount = 0;
			desc.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;
		}

		// Create depth-stencil State
		{
			D3D12_DEPTH_STENCIL_DESC& desc = pso_desc.DepthStencilState;
			desc.DepthEnable = false;
			desc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
			desc.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
			desc.StencilEnable = false;
			desc.FrontFace.StencilFailOp = desc.FrontFace.StencilDepthFailOp = desc.FrontFace.StencilPassOp = D3D12_STENCIL_OP_KEEP;
			desc.FrontFace.StencilFunc = D3D12_COMPARISON_FUNC_ALWAYS;
			desc.BackFace = desc.FrontFace;
		}

		D3D12Device->CreateGraphicsPipelineState(&pso_desc, IID_PPV_ARGS(&pipeline_state));
		vertexShaderBlob->Release();
		pixelShaderBlob->Release();
	}
};

class BufferD3D12
{
	friend class BackendD3D12;

private:
	ID3D12Resource* buffer = nullptr;
	size_t size = 0;

public:
	BufferD3D12(void* memory, size_t _size) : size(_size)
	{
		D3D12_HEAP_PROPERTIES props = {};
		props.Type = D3D12_HEAP_TYPE_UPLOAD;
		props.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
		props.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;

		D3D12_RESOURCE_DESC desc = {};
		desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		desc.Width = size;
		desc.Height = 1;
		desc.DepthOrArraySize = 1;
		desc.MipLevels = 1;
		desc.Format = DXGI_FORMAT_UNKNOWN;
		desc.SampleDesc.Count = 1;
		desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
		desc.Flags = D3D12_RESOURCE_FLAG_NONE;

		D3D12Device->CreateCommittedResource(&props, D3D12_HEAP_FLAG_NONE, &desc, 
			D3D12_RESOURCE_STATE_GENERIC_READ, NULL, IID_PPV_ARGS(&buffer));

		write(memory, size);
	}

	void write(void* memory, size_t _size)
	{
		assert(_size <= size);

		D3D12_RANGE range = {};
		void* dst_memory;

		buffer->Map(0, &range, &dst_memory);
		memcpy(dst_memory, memory, _size);
		buffer->Unmap(0, &range);
	}
};

class VertexBufferD3D12 : public BufferD3D12
{
	friend class BackendD3D12;

private:
	size_t stride = 0;

public:
	VertexBufferD3D12(void* memory, size_t size, size_t _stride) : BufferD3D12(memory, size),
		stride(_stride)
	{
	}
};

class IndexBufferD3D12 : public BufferD3D12
{
	friend class BackendD3D12;

private:
	size_t stride = 0;

public:
	IndexBufferD3D12(void* memory, size_t size, size_t _stride) : BufferD3D12(memory, size),
		stride(_stride)
	{
	}
};

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

	g_fenceEvent = CreateEvent(NULL, FALSE, FALSE, NULL);

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
		g_hSwapChainWaitableObject = D3D12SwapChain->GetFrameLatencyWaitableObject();
	}










	// Create the root signature
	{
		D3D12_DESCRIPTOR_RANGE descRange = {};
		descRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
		descRange.NumDescriptors = 1;
		descRange.BaseShaderRegister = 0;
		descRange.RegisterSpace = 0;
		descRange.OffsetInDescriptorsFromTableStart = 0;

		D3D12_ROOT_PARAMETER param[2] = {};

		param[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
		param[0].Constants.ShaderRegister = 0;
		param[0].Constants.RegisterSpace = 0;
		param[0].Constants.Num32BitValues = 16;
		param[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;

		param[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
		param[1].DescriptorTable.NumDescriptorRanges = 1;
		param[1].DescriptorTable.pDescriptorRanges = &descRange;
		param[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

		D3D12_STATIC_SAMPLER_DESC staticSampler = {};
		staticSampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
		staticSampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		staticSampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		staticSampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		staticSampler.MipLODBias = 0.f;
		staticSampler.MaxAnisotropy = 0;
		staticSampler.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
		staticSampler.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
		staticSampler.MinLOD = 0.f;
		staticSampler.MaxLOD = 0.f;
		staticSampler.ShaderRegister = 0;
		staticSampler.RegisterSpace = 0;
		staticSampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

		D3D12_ROOT_SIGNATURE_DESC desc = {};
		desc.NumParameters = _countof(param);
		desc.pParameters = param;
		desc.NumStaticSamplers = 1;
		desc.pStaticSamplers = &staticSampler;
		desc.Flags =
			D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
			D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
			D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
			D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS;

		ID3DBlob* blob = NULL;
		D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, NULL);

		D3D12Device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&g_pRootSignature));
		blob->Release();
	}











	createMainRenderTarget(width, height);

	begin();
	setRenderTarget(nullptr);
}

BackendD3D12::~BackendD3D12()
{
	end();
	destroyMainRenderTarget();
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

void BackendD3D12::createMainRenderTarget(uint32_t width, uint32_t height)
{
	for (UINT i = 0; i < NUM_BACK_BUFFERS; i++)
	{
		ID3D12Resource* pBackBuffer = NULL;
		D3D12SwapChain->GetBuffer(i, IID_PPV_ARGS(&pBackBuffer));
		D3D12Device->CreateRenderTargetView(pBackBuffer, NULL, g_mainRenderTargetDescriptor[i]);
		g_mainRenderTargetResource[i] = pBackBuffer;
	}
}

void BackendD3D12::destroyMainRenderTarget()
{
	WaitForLastSubmittedFrame();

	for (UINT i = 0; i < NUM_BACK_BUFFERS; i++)
		if (g_mainRenderTargetResource[i]) { g_mainRenderTargetResource[i]->Release(); g_mainRenderTargetResource[i] = NULL; }
}

void BackendD3D12::resize(uint32_t width, uint32_t height)
{
	destroyMainRenderTarget();
	D3D12SwapChain->ResizeBuffers(0, (UINT)width, (UINT)height, DXGI_FORMAT_R8G8B8A8_UNORM, 0);
	createMainRenderTarget(width, height);
}

void BackendD3D12::setTopology(Topology topology)
{
	const static std::unordered_map<Topology, D3D_PRIMITIVE_TOPOLOGY> TopologyMap = {
		{ Topology::PointList, D3D_PRIMITIVE_TOPOLOGY_POINTLIST },
		{ Topology::LineList, D3D_PRIMITIVE_TOPOLOGY_LINELIST },
		{ Topology::LineStrip, D3D_PRIMITIVE_TOPOLOGY_LINESTRIP },
		{ Topology::TriangleList, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST },
		{ Topology::TriangleStrip, D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP }
	};

	D3D12CommandList->IASetPrimitiveTopology(TopologyMap.at(topology));
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
	gShader = (ShaderD3D12*)handle;
}

void BackendD3D12::setVertexBuffer(VertexBufferHandle* handle)
{
	auto buffer = (VertexBufferD3D12*)handle;

	D3D12_VERTEX_BUFFER_VIEW buffer_view = {};
	buffer_view.BufferLocation = buffer->buffer->GetGPUVirtualAddress();
	buffer_view.SizeInBytes = buffer->size;
	buffer_view.StrideInBytes = buffer->stride;

	D3D12CommandList->IASetVertexBuffers(0, 1, &buffer_view);
}

void BackendD3D12::setIndexBuffer(IndexBufferHandle* handle)
{
	auto buffer = (IndexBufferD3D12*)handle;

	D3D12_INDEX_BUFFER_VIEW buffer_view = {};
	buffer_view.BufferLocation = buffer->buffer->GetGPUVirtualAddress();
	buffer_view.SizeInBytes = buffer->size;
	buffer_view.Format = buffer->stride == 2 ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT;

	D3D12CommandList->IASetIndexBuffer(&buffer_view);
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
	if (color.has_value())
		D3D12CommandList->ClearRenderTargetView(g_mainRenderTargetDescriptor[gBackBufferIdx], (float*)&color.value(), 0, NULL);
}

void BackendD3D12::draw(uint32_t vertex_count, uint32_t vertex_offset)
{
	prepareForDrawing();
	D3D12CommandList->DrawInstanced((UINT)vertex_count, 1, (UINT)vertex_offset, 0);
}

void BackendD3D12::drawIndexed(uint32_t index_count, uint32_t index_offset)
{
	prepareForDrawing();
	D3D12CommandList->DrawIndexedInstanced((UINT)index_count, 1, (UINT)index_offset, 0, 0);
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

void BackendD3D12::prepareForDrawing()
{
	assert(gShader);

	D3D12CommandList->OMSetRenderTargets(1, &g_mainRenderTargetDescriptor[gBackBufferIdx], FALSE, NULL);
	D3D12CommandList->SetDescriptorHeaps(1, &g_pd3dSrvDescHeap);
	D3D12CommandList->SetPipelineState(gShader->pipeline_state);
	D3D12CommandList->SetGraphicsRootSignature(g_pRootSignature);

	D3D12_VIEWPORT vp = {};
	vp.Width = 800;
	vp.Height = 600;
	vp.MinDepth = 0.0f;
	vp.MaxDepth = 1.0f;
	vp.TopLeftX = 0.0f;
	vp.TopLeftY = 0.0f;
	D3D12CommandList->RSSetViewports(1, &vp);

	D3D12_RECT rect;
	rect.left = static_cast<LONG>(0);
	rect.top = static_cast<LONG>(0);
	rect.right = static_cast<LONG>(800);
	rect.bottom = static_cast<LONG>(600);
	D3D12CommandList->RSSetScissorRects(1, &rect);
}

void BackendD3D12::present()
{
	end();

	bool vsync = true;

	D3D12SwapChain->Present(vsync ? 1 : 0, 0);

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

	D3D12CommandQueue->ExecuteCommandLists(1, (ID3D12CommandList* const*)&D3D12CommandList);
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
	auto shader = new ShaderD3D12(layout, vertex_code, fragment_code, defines);
	return (ShaderHandle*)shader;
}

void BackendD3D12::destroyShader(ShaderHandle* handle)
{
	auto shader = (ShaderD3D12*)handle;
	delete shader;
}

VertexBufferHandle* BackendD3D12::createVertexBuffer(void* memory, size_t size, size_t stride)
{
	auto buffer = new VertexBufferD3D12(memory, size, stride);
	return (VertexBufferHandle*)buffer;
}

void BackendD3D12::destroyVertexBuffer(VertexBufferHandle* handle)
{
	auto buffer = (VertexBufferD3D12*)handle;
	delete buffer;
}

void BackendD3D12::writeVertexBufferMemory(VertexBufferHandle* handle, void* memory, size_t size, size_t stride)
{
}

IndexBufferHandle* BackendD3D12::createIndexBuffer(void* memory, size_t size, size_t stride)
{
	auto buffer = new IndexBufferD3D12(memory, size, stride);
	return (IndexBufferHandle*)buffer;
}

void BackendD3D12::writeIndexBufferMemory(IndexBufferHandle* handle, void* memory, size_t size, size_t stride)
{
}

void BackendD3D12::destroyIndexBuffer(IndexBufferHandle* handle)
{
	auto buffer = (IndexBufferD3D12*)handle;
	delete buffer;
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