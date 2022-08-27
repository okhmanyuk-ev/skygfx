#include "backend_d3d12.h"

#ifdef SKYGFX_HAS_D3D12

#include <stdexcept>

#include <d3dcompiler.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <dxgidebug.h>
#include <d3dx12.h>

#pragma comment(lib, "d3d12")
#pragma comment(lib, "d3dcompiler")
#pragma comment(lib, "dxgi")
#pragma comment(lib, "dxguid.lib")

#include <wrl.h>

using Microsoft::WRL::ComPtr;
using namespace skygfx;

class ShaderD3D12;

struct RasterizerStateD3D12
{
	CullMode cull_mode = CullMode::None;

	bool operator==(const RasterizerStateD3D12& value) const
	{
		return 
			cull_mode == value.cull_mode;
	}
};

SKYGFX_MAKE_HASHABLE(RasterizerStateD3D12,
	t.cull_mode);

struct PipelineStateD3D12
{
	ShaderD3D12* shader = nullptr;
	RasterizerStateD3D12 rasterizer_state;
	std::optional<DepthMode> depth_mode;
	BlendMode blend_mode = BlendMode(Blend::One, Blend::One);
	
	bool operator==(const PipelineStateD3D12& value) const
	{
		return 
			shader == value.shader &&
			rasterizer_state == value.rasterizer_state &&
			depth_mode == value.depth_mode &&
			blend_mode == value.blend_mode;
	}
};

SKYGFX_MAKE_HASHABLE(PipelineStateD3D12,
	t.shader,
	t.rasterizer_state,
	t.depth_mode,
	t.blend_mode);

static int const NUM_BACK_BUFFERS = 2;

static ComPtr<ID3D12Device> gDevice;
static ComPtr<ID3D12CommandAllocator> gCommandAllocator;
static ComPtr<ID3D12CommandQueue> gCommandQueue;
static ComPtr<IDXGISwapChain3> gSwapChain;
static HANDLE gSwapChainWaitableObject = NULL;
static ComPtr<ID3D12GraphicsCommandList> gCommandList;

struct MainRenderTarget
{
	D3D12_CPU_DESCRIPTOR_HANDLE descriptor;
	ComPtr<ID3D12Resource> resource;
};

static ComPtr<ID3D12DescriptorHeap> gRtvDescHeap;
static ComPtr<ID3D12DescriptorHeap> gDsvDescHeap;

static MainRenderTarget gMainRenderTarget[NUM_BACK_BUFFERS];
static ComPtr<ID3D12Resource> gDepthStencilResource;

static UINT gFrameIndex = 0;
static HANDLE gFenceEvent = NULL;
static ComPtr<ID3D12Fence> gFence;
static UINT64 gFenceValue;

class TextureD3D12;
class UniformBufferD3D12;

static std::unordered_map<uint32_t, TextureD3D12*> gTextures;
static std::unordered_map<uint32_t, UniformBufferD3D12*> gUniformBuffers;

static PipelineStateD3D12 gPipelineState;
static std::unordered_map<PipelineStateD3D12, ComPtr<ID3D12PipelineState>> gPipelineStates;

static ExecuteList gExecuteAfterPresent;

static std::optional<Viewport> gViewport;
static bool gViewportDirty = true;

static std::optional<Scissor> gScissor;
static bool gScissorDirty = true;

static uint32_t gBackbufferWidth = 0;
static uint32_t gBackbufferHeight = 0;

void OneTimeSubmit(ID3D12Device* device, const std::function<void(ID3D12GraphicsCommandList*)> func)
{
	D3D12_COMMAND_QUEUE_DESC queue_desc = {};
	queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
	queue_desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
	queue_desc.NodeMask = 1;

	ComPtr<ID3D12CommandQueue> cmd_queue;
	device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(cmd_queue.GetAddressOf()));

	ComPtr<ID3D12CommandAllocator> cmd_alloc;
	device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(cmd_alloc.GetAddressOf()));

	ComPtr<ID3D12GraphicsCommandList> cmd_list;
	device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, cmd_alloc.Get(), NULL, 
		IID_PPV_ARGS(cmd_list.GetAddressOf()));

	ComPtr<ID3D12Fence> fence = NULL;
	device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(fence.GetAddressOf()));

	HANDLE event = CreateEvent(0, 0, 0, 0);

	func(cmd_list.Get());
	cmd_list->Close();
	cmd_queue->ExecuteCommandLists(1, CommandListCast(cmd_list.GetAddressOf()));
	cmd_queue->Signal(fence.Get(), 1);

	fence->SetEventOnCompletion(1, event);
	WaitForSingleObject(event, INFINITE);
}

class ShaderD3D12
{
	friend class BackendD3D12;

private:
	ComPtr<ID3D12RootSignature> root_signature;
	std::map<uint32_t, ShaderReflection::Descriptor> required_descriptor_bindings; // TODO: del
	std::map<ShaderStage, std::map<uint32_t/*set*/, std::set<uint32_t>/*bindings*/>> required_descriptor_sets;
	std::map<uint32_t, uint32_t> binding_to_root_index;
	ComPtr<ID3DBlob> vertex_shader_blob;
	ComPtr<ID3DBlob> pixel_shader_blob;
	std::vector<D3D12_INPUT_ELEMENT_DESC> input;

public:
	ShaderD3D12(const Vertex::Layout& layout, const std::string& vertex_code, const std::string& fragment_code,
		std::vector<std::string> defines)
	{
		AddShaderLocationDefines(layout, defines);

		auto vertex_shader_spirv = CompileGlslToSpirv(ShaderStage::Vertex, vertex_code, defines);
		auto fragment_shader_spirv = CompileGlslToSpirv(ShaderStage::Fragment, fragment_code, defines);

		auto hlsl_vert = CompileSpirvToHlsl(vertex_shader_spirv, 50);
		auto hlsl_frag = CompileSpirvToHlsl(fragment_shader_spirv, 50);

		ComPtr<ID3DBlob> vertex_shader_error;
		ComPtr<ID3DBlob> pixel_shader_error;

#if defined(_DEBUG)
		// Enable better shader debugging with the graphics debugging tools.
		UINT compile_flags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
		UINT compile_flags = 0;
#endif
		D3DCompile(hlsl_vert.c_str(), hlsl_vert.size(), NULL, NULL, NULL, "main", "vs_5_0", compile_flags, 0, 
			&vertex_shader_blob, &vertex_shader_error);
		D3DCompile(hlsl_frag.c_str(), hlsl_frag.size(), NULL, NULL, NULL, "main", "ps_5_0", compile_flags, 0, 
			&pixel_shader_blob, &pixel_shader_error);

		std::string vertex_shader_error_string = "";
		std::string pixel_shader_error_string = "";

		if (vertex_shader_error != NULL)
			vertex_shader_error_string = std::string((char*)vertex_shader_error->GetBufferPointer(), vertex_shader_error->GetBufferSize());

		if (pixel_shader_error != NULL)
			pixel_shader_error_string = std::string((char*)pixel_shader_error->GetBufferPointer(), pixel_shader_error->GetBufferSize());

		if (vertex_shader_blob == NULL)
			throw std::runtime_error(vertex_shader_error_string);

		if (pixel_shader_blob == NULL)
			throw std::runtime_error(pixel_shader_error_string);

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

		UINT i = 0;

		for (auto& attrib : layout.attributes)
		{
			input.push_back({ "TEXCOORD", i, Format.at(attrib.format), 0,
				static_cast<UINT>(attrib.offset), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 });
			i++;
		}

		auto vertex_shader_reflection = MakeSpirvReflection(vertex_shader_spirv);
		auto fragment_shader_reflection = MakeSpirvReflection(fragment_shader_spirv);

		for (const auto& reflection : { vertex_shader_reflection, fragment_shader_reflection })
		{
			for (const auto& [binding, descriptor] : reflection.descriptor_bindings)
			{
				if (required_descriptor_bindings.contains(binding))
					continue;

				required_descriptor_bindings.insert({ binding, descriptor });
			}

			for (const auto& [set, bindings] : reflection.descriptor_sets)
			{
				required_descriptor_sets[reflection.stage][set] = bindings;
			}
		}

		{
			std::vector<CD3DX12_ROOT_PARAMETER> params;
			std::vector<D3D12_STATIC_SAMPLER_DESC> static_samplers;
			std::vector<CD3DX12_DESCRIPTOR_RANGE> ranges(32);

			for (const auto& [binding, descriptor] : required_descriptor_bindings)
			{
				CD3DX12_ROOT_PARAMETER param;

				if (descriptor.type == ShaderReflection::Descriptor::Type::UniformBuffer)
				{
					param.InitAsConstantBufferView(binding);
				}
				else if (descriptor.type == ShaderReflection::Descriptor::Type::CombinedImageSampler)
				{
					auto range = CD3DX12_DESCRIPTOR_RANGE(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, binding);
					ranges.push_back(range);

					param.InitAsDescriptorTable(1, &ranges[ranges.size() - 1], D3D12_SHADER_VISIBILITY_ALL);

					static_samplers.push_back(CD3DX12_STATIC_SAMPLER_DESC(binding, D3D12_FILTER_MIN_MAG_MIP_LINEAR));
				}
				else
				{
					assert(false);
				}

				binding_to_root_index.insert({ binding, (uint32_t)params.size() });
				params.push_back(param);
			}

			CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC desc((UINT)params.size(), params.data(), (UINT)static_samplers.size(),
				static_samplers.data(), D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

			ComPtr<ID3DBlob> blob;
			ComPtr<ID3DBlob> error;

			D3DX12SerializeVersionedRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &error);

			std::string error_string;

			if (error != NULL)
				error_string = std::string((char*)error->GetBufferPointer(), error->GetBufferSize());

			gDevice->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&root_signature));
		}
	}
};

class TextureD3D12
{
	friend class RenderTargetD3D12;
	friend class BackendD3D12;

private:
	uint32_t width;
	uint32_t height;
	bool mipmap;
	ComPtr<ID3D12Resource> texture;
	ComPtr<ID3D12DescriptorHeap> heap;
	
public:
	TextureD3D12(uint32_t _width, uint32_t _height, uint32_t channels, void* memory, bool _mipmap) :
		width(_width),
		height(_height),
		mipmap(_mipmap)
	{
		const auto format = DXGI_FORMAT_R8G8B8A8_UNORM;

		auto prop = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
		auto desc = CD3DX12_RESOURCE_DESC::Tex2D(format, width, height);

		gDevice->CreateCommittedResource(&prop, D3D12_HEAP_FLAG_NONE, &desc,
			D3D12_RESOURCE_STATE_COPY_DEST, NULL, IID_PPV_ARGS(texture.GetAddressOf()));

		const UINT64 upload_size = GetRequiredIntermediateSize(texture.Get(), 0, 1);

		desc = CD3DX12_RESOURCE_DESC::Buffer(upload_size);
		prop = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);

		ComPtr<ID3D12Resource> upload_buffer = NULL;
		gDevice->CreateCommittedResource(&prop, D3D12_HEAP_FLAG_NONE, &desc,
			D3D12_RESOURCE_STATE_GENERIC_READ, NULL, IID_PPV_ARGS(upload_buffer.GetAddressOf()));

		D3D12_SUBRESOURCE_DATA subersource_data = {};
		subersource_data.pData = memory;
		subersource_data.RowPitch = width * channels;
		subersource_data.SlicePitch = width * height * channels;

		OneTimeSubmit(gDevice.Get(), [&](ID3D12GraphicsCommandList* cmdlist) {
			UpdateSubresources(cmdlist, texture.Get(), upload_buffer.Get(), 0, 0, 1, &subersource_data);

			auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(texture.Get(),
				D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

			cmdlist->ResourceBarrier(1, &barrier);
		});

		D3D12_DESCRIPTOR_HEAP_DESC heap_desc = {};
		heap_desc.NumDescriptors = 1;
		heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		gDevice->CreateDescriptorHeap(&heap_desc, IID_PPV_ARGS(heap.GetAddressOf()));

		D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
		srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srv_desc.Format = format;
		srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		srv_desc.Texture2D.MipLevels = 1;
		gDevice->CreateShaderResourceView(texture.Get(), &srv_desc, heap->GetCPUDescriptorHandleForHeapStart());
	}
};

class RenderTargetD3D12
{
	friend class BackendD3D11;

public:
	RenderTargetD3D12(uint32_t _width, uint32_t _height, TextureD3D12* _texture_data)
	{
	}

	~RenderTargetD3D12()
	{
	}
};

class BufferD3D12
{
	friend class BackendD3D12;

protected:
	ComPtr<ID3D12Resource> buffer;
	size_t size = 0;

public:
	BufferD3D12(size_t _size) : size(_size)
	{
		auto prop = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
		auto desc = CD3DX12_RESOURCE_DESC::Buffer(size);

		gDevice->CreateCommittedResource(&prop, D3D12_HEAP_FLAG_NONE, &desc,
			D3D12_RESOURCE_STATE_GENERIC_READ, NULL, IID_PPV_ARGS(buffer.GetAddressOf()));
	}

	void write(void* memory, size_t _size)
	{
		assert(_size <= size);
		CD3DX12_RANGE range(0, 0);
		void* dst_memory = nullptr;
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
	VertexBufferD3D12(size_t _size, size_t _stride) : BufferD3D12(_size),
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
	IndexBufferD3D12(size_t _size, size_t _stride) : BufferD3D12(_size),
		stride(_stride)
	{
	}
};

class UniformBufferD3D12 : public BufferD3D12
{
	friend class BackendD3D12;

public:
	UniformBufferD3D12(size_t _size) : BufferD3D12(_size)
	{
	}
};

// Prepare to render the next frame.
void MoveToNextFrame()
{
	gCommandQueue->Signal(gFence.Get(), gFenceValue);
	gFrameIndex = gSwapChain->GetCurrentBackBufferIndex();
	if (gFence->GetCompletedValue() < gFenceValue)
	{
		gFence->SetEventOnCompletion(gFenceValue, gFenceEvent);
		WaitForSingleObject(gFenceEvent, INFINITE);
	}
	gFenceValue++;
}

// Wait for pending GPU work to complete.
void WaitForGpu()
{
	gCommandQueue->Signal(gFence.Get(), gFenceValue);
	gFence->SetEventOnCompletion(gFenceValue, gFenceEvent);
	WaitForSingleObject(gFenceEvent, INFINITE);
	gFenceValue++;
}

BackendD3D12::BackendD3D12(void* window, uint32_t width, uint32_t height)
{
	ComPtr<ID3D12Debug> debug;
	D3D12GetDebugInterface(IID_PPV_ARGS(debug.GetAddressOf()));
	debug->EnableDebugLayer();

	D3D12CreateDevice(NULL, D3D_FEATURE_LEVEL_12_1, IID_PPV_ARGS(gDevice.GetAddressOf()));

	ComPtr<ID3D12InfoQueue> info_queue;
	gDevice.As(&info_queue);
	info_queue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, true);
	info_queue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, true);
	info_queue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING, true);

	D3D12_DESCRIPTOR_HEAP_DESC rtv_heap_desc = {};
	rtv_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	rtv_heap_desc.NumDescriptors = NUM_BACK_BUFFERS;
	rtv_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	rtv_heap_desc.NodeMask = 1;
	gDevice->CreateDescriptorHeap(&rtv_heap_desc, IID_PPV_ARGS(gRtvDescHeap.GetAddressOf()));

	D3D12_DESCRIPTOR_HEAP_DESC dsv_heap_desc = {};
	dsv_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
	dsv_heap_desc.NumDescriptors = 1;
	gDevice->CreateDescriptorHeap(&dsv_heap_desc, IID_PPV_ARGS(gDsvDescHeap.GetAddressOf()));
		
	auto rtv_increment_size = gDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	auto rtv_heap_start = gRtvDescHeap->GetCPUDescriptorHandleForHeapStart();

	for (UINT i = 0; i < NUM_BACK_BUFFERS; i++)
	{
		auto handle = CD3DX12_CPU_DESCRIPTOR_HANDLE(rtv_heap_start, i, rtv_increment_size);
		gMainRenderTarget[i].descriptor = handle;
	}

	D3D12_COMMAND_QUEUE_DESC queue_desc = {};
	queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
	queue_desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
	queue_desc.NodeMask = 1;
	gDevice->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(gCommandQueue.GetAddressOf()));
	
	gDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, 
		IID_PPV_ARGS(gCommandAllocator.GetAddressOf()));

	gDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, gCommandAllocator.Get(), 
		NULL, IID_PPV_ARGS(gCommandList.GetAddressOf()));
	
	gCommandList->Close();

	DXGI_SWAP_CHAIN_DESC1 sd = {};
	sd.BufferCount = NUM_BACK_BUFFERS;
	sd.Width = width;
	sd.Height = height;
	sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	sd.Flags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;
	sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	sd.SampleDesc.Count = 1;
	sd.SampleDesc.Quality = 0;
	sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	sd.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
	sd.Scaling = DXGI_SCALING_STRETCH;
	sd.Stereo = FALSE;

	ComPtr<IDXGIFactory4> dxgiFactory;
	CreateDXGIFactory1(IID_PPV_ARGS(dxgiFactory.GetAddressOf()));

	ComPtr<IDXGISwapChain1> swapChain1;
	dxgiFactory->CreateSwapChainForHwnd(gCommandQueue.Get(), (HWND)window, 
		&sd, NULL, NULL, swapChain1.GetAddressOf());
		
	swapChain1.As(&gSwapChain);
		
	gSwapChain->SetMaximumFrameLatency(NUM_BACK_BUFFERS);
	gSwapChainWaitableObject = gSwapChain->GetFrameLatencyWaitableObject();

	gDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(gFence.GetAddressOf()));
	gFenceValue = 1;
	gFenceEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
	assert(gFenceEvent != NULL);
	
	createMainRenderTarget(width, height);

	begin();
	setRenderTarget(nullptr);
}

BackendD3D12::~BackendD3D12()
{
	end();
	WaitForGpu();
	gExecuteAfterPresent.flush();
	destroyMainRenderTarget();
	gSwapChain.Reset();
	gDevice.Reset();
}

void BackendD3D12::createMainRenderTarget(uint32_t width, uint32_t height)
{
	for (UINT i = 0; i < NUM_BACK_BUFFERS; i++)
	{
		gSwapChain->GetBuffer(i, IID_PPV_ARGS(gMainRenderTarget[i].resource.GetAddressOf()));
		gDevice->CreateRenderTargetView(gMainRenderTarget[i].resource.Get(), NULL, gMainRenderTarget[i].descriptor);
	}

	auto depth_heap_props = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
	auto depth_desc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_D24_UNORM_S8_UINT, 
		(UINT64)width, (UINT)height, 1, 1);
	auto depth_clear_value = CD3DX12_CLEAR_VALUE(depth_desc.Format, 1.0f, 0);

	depth_desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

	gDevice->CreateCommittedResource(&depth_heap_props, D3D12_HEAP_FLAG_NONE, &depth_desc, 
		D3D12_RESOURCE_STATE_DEPTH_WRITE, &depth_clear_value, 
		IID_PPV_ARGS(gDepthStencilResource.GetAddressOf()));

	D3D12_DEPTH_STENCIL_VIEW_DESC dsv_desc = {};
	dsv_desc.Format = depth_desc.Format;
	dsv_desc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
	
	gDevice->CreateDepthStencilView(gDepthStencilResource.Get(), &dsv_desc, 
		gDsvDescHeap->GetCPUDescriptorHandleForHeapStart());

	gBackbufferWidth = width;
	gBackbufferHeight = height;

	gFrameIndex = gSwapChain->GetCurrentBackBufferIndex();
}

void BackendD3D12::destroyMainRenderTarget()
{
	for (UINT i = 0; i < NUM_BACK_BUFFERS; i++)
	{
		gMainRenderTarget[i].resource.Reset();
	}

	gDepthStencilResource.Reset();
}

void BackendD3D12::resize(uint32_t width, uint32_t height)
{
	WaitForGpu();

	destroyMainRenderTarget();
	gSwapChain->ResizeBuffers(NUM_BACK_BUFFERS, (UINT)width, (UINT)height, DXGI_FORMAT_R8G8B8A8_UNORM, 0);
	createMainRenderTarget(width, height);	

	if (!gViewport.has_value())
		gViewportDirty = true;
	
	if (!gScissor.has_value())
		gScissorDirty = true;
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

	gCommandList->IASetPrimitiveTopology(TopologyMap.at(topology));
}

void BackendD3D12::setViewport(std::optional<Viewport> viewport)
{
	if (gViewport != viewport)
		gViewportDirty = true;

	gViewport = viewport;
}

void BackendD3D12::setScissor(std::optional<Scissor> scissor)
{
	if (gScissor != scissor)
		gScissorDirty = true;

	gScissor = scissor;
}

void BackendD3D12::setTexture(uint32_t binding, TextureHandle* handle)
{
	auto texture = (TextureD3D12*)handle;
	gTextures[binding] = texture;
}

void BackendD3D12::setRenderTarget(RenderTargetHandle* handle)
{
	if (!gViewport.has_value())
		gViewportDirty = true;

	if (!gScissor.has_value())
		gScissorDirty = true;
}

void BackendD3D12::setRenderTarget(std::nullptr_t value)
{
	if (!gViewport.has_value())
		gViewportDirty = true;

	if (!gScissor.has_value())
		gScissorDirty = true;
}

void BackendD3D12::setShader(ShaderHandle* handle)
{
	gPipelineState.shader = (ShaderD3D12*)handle;
}

void BackendD3D12::setVertexBuffer(VertexBufferHandle* handle)
{
	auto buffer = (VertexBufferD3D12*)handle;

	D3D12_VERTEX_BUFFER_VIEW buffer_view = {};
	buffer_view.BufferLocation = buffer->buffer->GetGPUVirtualAddress();
	buffer_view.SizeInBytes = (UINT)buffer->size;
	buffer_view.StrideInBytes = (UINT)buffer->stride;

	gCommandList->IASetVertexBuffers(0, 1, &buffer_view);
}

void BackendD3D12::setIndexBuffer(IndexBufferHandle* handle)
{
	auto buffer = (IndexBufferD3D12*)handle;

	D3D12_INDEX_BUFFER_VIEW buffer_view = {};
	buffer_view.BufferLocation = buffer->buffer->GetGPUVirtualAddress();
	buffer_view.SizeInBytes = (UINT)buffer->size;
	buffer_view.Format = buffer->stride == 2 ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT;

	gCommandList->IASetIndexBuffer(&buffer_view);
}

void BackendD3D12::setUniformBuffer(uint32_t binding, UniformBufferHandle* handle)
{
	auto buffer = (UniformBufferD3D12*)handle;
	gUniformBuffers[binding] = buffer;
}

void BackendD3D12::setBlendMode(const BlendMode& value)
{
	gPipelineState.blend_mode = value;
}

void BackendD3D12::setDepthMode(std::optional<DepthMode> depth_mode)
{
	gPipelineState.depth_mode = depth_mode;
}

void BackendD3D12::setStencilMode(std::optional<StencilMode> stencil_mode)
{
}

void BackendD3D12::setCullMode(CullMode cull_mode)
{
	gPipelineState.rasterizer_state.cull_mode = cull_mode;;
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
	{
		gCommandList->ClearRenderTargetView(gMainRenderTarget[gFrameIndex].descriptor, (float*)&color.value(), 0, NULL);
	}

	if (depth.has_value() || stencil.has_value())
	{
		D3D12_CLEAR_FLAGS flags = {};

		if (depth.has_value())
			flags |= D3D12_CLEAR_FLAG_DEPTH;

		if (stencil.has_value())
			flags |= D3D12_CLEAR_FLAG_STENCIL;

		gCommandList->ClearDepthStencilView(gDsvDescHeap->GetCPUDescriptorHandleForHeapStart(), flags,
			depth.value_or(1.0f), stencil.value_or(0), 0, NULL);
	}
}

void BackendD3D12::draw(uint32_t vertex_count, uint32_t vertex_offset)
{
	prepareForDrawing();
	gCommandList->DrawInstanced((UINT)vertex_count, 1, (UINT)vertex_offset, 0);
}

void BackendD3D12::drawIndexed(uint32_t index_count, uint32_t index_offset)
{
	prepareForDrawing();
	gCommandList->DrawIndexedInstanced((UINT)index_count, 1, (UINT)index_offset, 0, 0);
}

void BackendD3D12::readPixels(const glm::ivec2& pos, const glm::ivec2& size, TextureHandle* dst_texture_handle)
{
}

void BackendD3D12::prepareForDrawing()
{
	auto shader = gPipelineState.shader;
	assert(shader);

	if (!gPipelineStates.contains(gPipelineState))
	{
		const static std::unordered_map<CullMode, D3D12_CULL_MODE> CullMap = {
			{ CullMode::None, D3D12_CULL_MODE_NONE },
			{ CullMode::Front, D3D12_CULL_MODE_FRONT },
			{ CullMode::Back, D3D12_CULL_MODE_BACK }
		};

		const static std::unordered_map<ComparisonFunc, D3D12_COMPARISON_FUNC> ComparisonFuncMap = {
			{ ComparisonFunc::Always, D3D12_COMPARISON_FUNC_ALWAYS },
			{ ComparisonFunc::Never, D3D12_COMPARISON_FUNC_NEVER },
			{ ComparisonFunc::Less, D3D12_COMPARISON_FUNC_LESS },
			{ ComparisonFunc::Equal, D3D12_COMPARISON_FUNC_EQUAL },
			{ ComparisonFunc::NotEqual, D3D12_COMPARISON_FUNC_NOT_EQUAL },
			{ ComparisonFunc::LessEqual, D3D12_COMPARISON_FUNC_LESS_EQUAL },
			{ ComparisonFunc::Greater, D3D12_COMPARISON_FUNC_GREATER },
			{ ComparisonFunc::GreaterEqual, D3D12_COMPARISON_FUNC_GREATER_EQUAL }
		};

		const static std::unordered_map<Blend, D3D12_BLEND> BlendMap = {
			{ Blend::One, D3D12_BLEND_ONE },
			{ Blend::Zero, D3D12_BLEND_ZERO },
			{ Blend::SrcColor, D3D12_BLEND_SRC_COLOR },
			{ Blend::InvSrcColor, D3D12_BLEND_INV_SRC_COLOR },
			{ Blend::SrcAlpha, D3D12_BLEND_SRC_ALPHA },
			{ Blend::InvSrcAlpha, D3D12_BLEND_INV_SRC_ALPHA },
			{ Blend::DstColor, D3D12_BLEND_DEST_COLOR },
			{ Blend::InvDstColor, D3D12_BLEND_INV_DEST_COLOR },
			{ Blend::DstAlpha, D3D12_BLEND_DEST_ALPHA },
			{ Blend::InvDstAlpha, D3D12_BLEND_INV_DEST_ALPHA }
		};

		const static std::unordered_map<BlendFunction, D3D12_BLEND_OP> BlendOpMap = {
			{ BlendFunction::Add, D3D12_BLEND_OP_ADD },
			{ BlendFunction::Subtract, D3D12_BLEND_OP_SUBTRACT },
			{ BlendFunction::ReverseSubtract, D3D12_BLEND_OP_REV_SUBTRACT },
			{ BlendFunction::Min, D3D12_BLEND_OP_MIN },
			{ BlendFunction::Max, D3D12_BLEND_OP_MAX },
		};

		auto depth_mode = gPipelineState.depth_mode.value_or(DepthMode());
		const auto& blend_mode = gPipelineState.blend_mode;

		D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc = {};
		pso_desc.VS = CD3DX12_SHADER_BYTECODE(shader->vertex_shader_blob.Get());
		pso_desc.PS = CD3DX12_SHADER_BYTECODE(shader->pixel_shader_blob.Get());
		pso_desc.InputLayout = { shader->input.data(), (UINT)shader->input.size() };
		pso_desc.NodeMask = 1;
		pso_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		pso_desc.pRootSignature = shader->root_signature.Get();
		pso_desc.SampleMask = UINT_MAX;
		pso_desc.NumRenderTargets = 1;
		pso_desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
		pso_desc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
		pso_desc.SampleDesc.Count = 1;
		pso_desc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
		pso_desc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
		pso_desc.RasterizerState.CullMode = CullMap.at(gPipelineState.rasterizer_state.cull_mode);

		pso_desc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
		pso_desc.DepthStencilState.DepthEnable = gPipelineState.depth_mode.has_value();
		pso_desc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
		pso_desc.DepthStencilState.DepthFunc = ComparisonFuncMap.at(depth_mode.func);

		pso_desc.DepthStencilState.StencilEnable = false;

		pso_desc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
		pso_desc.BlendState.AlphaToCoverageEnable = false;

		auto& blend = pso_desc.BlendState.RenderTarget[0];

		if (blend_mode.color_mask.red)
			blend.RenderTargetWriteMask |= D3D12_COLOR_WRITE_ENABLE_RED;

		if (blend_mode.color_mask.green)
			blend.RenderTargetWriteMask |= D3D12_COLOR_WRITE_ENABLE_GREEN;

		if (blend_mode.color_mask.blue)
			blend.RenderTargetWriteMask |= D3D12_COLOR_WRITE_ENABLE_BLUE;

		if (blend_mode.color_mask.alpha)
			blend.RenderTargetWriteMask |= D3D12_COLOR_WRITE_ENABLE_ALPHA;

		blend.BlendEnable = true;

		blend.SrcBlend = BlendMap.at(blend_mode.color_src_blend);
		blend.DestBlend = BlendMap.at(blend_mode.color_dst_blend);
		blend.BlendOp = BlendOpMap.at(blend_mode.color_blend_func);

		blend.SrcBlendAlpha = BlendMap.at(blend_mode.alpha_src_blend);
		blend.DestBlendAlpha = BlendMap.at(blend_mode.alpha_dst_blend);
		blend.BlendOpAlpha = BlendOpMap.at(blend_mode.alpha_blend_func);

		gDevice->CreateGraphicsPipelineState(&pso_desc, IID_PPV_ARGS(&gPipelineStates[gPipelineState]));
	}

	auto pipeline_state = gPipelineStates.at(gPipelineState).Get();
	const auto& rtv_cpu_descriptor = gMainRenderTarget[gFrameIndex].descriptor;
	auto dsv_cpu_descriptor = gDsvDescHeap->GetCPUDescriptorHandleForHeapStart();

	gCommandList->OMSetRenderTargets(1, &rtv_cpu_descriptor, FALSE, &dsv_cpu_descriptor);
	gCommandList->SetPipelineState(pipeline_state);
	gCommandList->SetGraphicsRootSignature(shader->root_signature.Get());

	for (const auto& [binding, descriptor] : shader->required_descriptor_bindings)
	{
		auto root_index = shader->binding_to_root_index.at(binding);

		if (descriptor.type == ShaderReflection::Descriptor::Type::CombinedImageSampler)
		{
			const auto& texture = gTextures.at(binding);
			gCommandList->SetDescriptorHeaps(1, texture->heap.GetAddressOf());
			gCommandList->SetGraphicsRootDescriptorTable(root_index, texture->heap->GetGPUDescriptorHandleForHeapStart());
		}
		else if (descriptor.type == ShaderReflection::Descriptor::Type::UniformBuffer)
		{
			const auto& buffer = gUniformBuffers.at(binding);
			gCommandList->SetGraphicsRootConstantBufferView(root_index, buffer->buffer->GetGPUVirtualAddress());
		}
		else
		{
			assert(false);
		}
	}

	gViewportDirty = true; // TODO: everything should work without this two lines
	gScissorDirty = true;

	if (gViewportDirty || gScissorDirty)
	{
		float width;
		float height;

		// TODO: uncomment when render targets are ready
		// 
		//if (gRenderTarget == nullptr)
		{
			width = static_cast<float>(gBackbufferWidth);
			height = static_cast<float>(gBackbufferHeight);
		}
		//else
		//{
		//	width = static_cast<float>(gRenderTarget->width);
		//	height = static_cast<float>(gRenderTarget->height);
		//}

		if (gViewportDirty)
		{
			auto viewport = gViewport.value_or(Viewport{ { 0.0f, 0.0f }, { width, height } });

			D3D12_VIEWPORT vp = {};
			vp.Width = viewport.size.x;
			vp.Height = viewport.size.y;
			vp.MinDepth = viewport.min_depth;
			vp.MaxDepth = viewport.max_depth;
			vp.TopLeftX = viewport.position.x;
			vp.TopLeftY = viewport.position.y;
			gCommandList->RSSetViewports(1, &vp);

			gViewportDirty = false;
		}

		if (gScissorDirty)
		{
			auto scissor = gScissor.value_or(Scissor{ { 0.0f, 0.0f }, { width, height } });

			D3D12_RECT rect;
			rect.left = static_cast<LONG>(scissor.position.x);
			rect.top = static_cast<LONG>(scissor.position.y);
			rect.right = static_cast<LONG>(scissor.position.x + scissor.size.x);
			rect.bottom = static_cast<LONG>(scissor.position.y + scissor.size.y);
			gCommandList->RSSetScissorRects(1, &rect);

			gScissorDirty = false;
		}
	}
}

void BackendD3D12::present()
{
	end();
	bool vsync = true;
	gSwapChain->Present(vsync ? 1 : 0, 0);
	MoveToNextFrame();
	gExecuteAfterPresent.flush();
	begin();
}

void BackendD3D12::begin()
{
	gCommandAllocator->Reset();

	auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(gMainRenderTarget[gFrameIndex].resource.Get(),
		D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);

	gCommandList->Reset(gCommandAllocator.Get(), NULL);
	gCommandList->ResourceBarrier(1, &barrier);
}

void BackendD3D12::end()
{
	auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(gMainRenderTarget[gFrameIndex].resource.Get(),
		D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);

	gCommandList->ResourceBarrier(1, &barrier);
	gCommandList->Close();
	
	gCommandQueue->ExecuteCommandLists(1, CommandListCast(gCommandList.GetAddressOf()));
}

TextureHandle* BackendD3D12::createTexture(uint32_t width, uint32_t height, uint32_t channels, void* memory, bool mipmap)
{
	auto texture = new TextureD3D12(width, height, channels, memory, mipmap);
	return (TextureHandle*)texture;
}

void BackendD3D12::destroyTexture(TextureHandle* handle)
{
	auto texture = (TextureD3D12*)handle;
	delete texture;
}

RenderTargetHandle* BackendD3D12::createRenderTarget(uint32_t width, uint32_t height, TextureHandle* texture_handle)
{
	auto texture = (TextureD3D12*)texture_handle;
	auto render_target = new RenderTargetD3D12(width, height, texture);
	return (RenderTargetHandle*)render_target;
}

void BackendD3D12::destroyRenderTarget(RenderTargetHandle* handle)
{
	auto render_target = (RenderTargetD3D12*)handle;
	delete render_target;
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

VertexBufferHandle* BackendD3D12::createVertexBuffer(size_t size, size_t stride)
{
	auto buffer = new VertexBufferD3D12(size, stride);
	return (VertexBufferHandle*)buffer;
}

void BackendD3D12::destroyVertexBuffer(VertexBufferHandle* handle)
{
	gExecuteAfterPresent.add([handle] {
		auto buffer = (VertexBufferD3D12*)handle;
		delete buffer;
	});
}

void BackendD3D12::writeVertexBufferMemory(VertexBufferHandle* handle, void* memory, size_t size, size_t stride)
{
	auto buffer = (VertexBufferD3D12*)handle;
	buffer->write(memory, size);
	buffer->stride = stride;
}

IndexBufferHandle* BackendD3D12::createIndexBuffer(size_t size, size_t stride)
{
	auto buffer = new IndexBufferD3D12(size, stride);
	return (IndexBufferHandle*)buffer;
}

void BackendD3D12::writeIndexBufferMemory(IndexBufferHandle* handle, void* memory, size_t size, size_t stride)
{
	auto buffer = (IndexBufferD3D12*)handle;
	buffer->write(memory, size);
	buffer->stride = stride;
}

void BackendD3D12::destroyIndexBuffer(IndexBufferHandle* handle)
{
	gExecuteAfterPresent.add([handle] {
		auto buffer = (IndexBufferD3D12*)handle;
		delete buffer;
	});
}

UniformBufferHandle* BackendD3D12::createUniformBuffer(size_t size)
{
	auto buffer = new UniformBufferD3D12(size);
	return (UniformBufferHandle*)buffer;
}

void BackendD3D12::destroyUniformBuffer(UniformBufferHandle* handle)
{
	gExecuteAfterPresent.add([handle] {
		auto buffer = (UniformBufferD3D12*)handle;
		delete buffer;
	});
}

void BackendD3D12::writeUniformBufferMemory(UniformBufferHandle* handle, void* memory, size_t size)
{
	auto buffer = (UniformBufferD3D12*)handle;
	buffer->write(memory, size);
}

#endif