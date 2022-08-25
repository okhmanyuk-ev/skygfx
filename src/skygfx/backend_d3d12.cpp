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

struct RasterizerState
{
	bool scissor_enabled = false;
	CullMode cull_mode = CullMode::None;

	bool operator==(const RasterizerState& value) const
	{
		return scissor_enabled == value.scissor_enabled && cull_mode == value.cull_mode;
	}
};

SKYGFX_MAKE_HASHABLE(RasterizerState,
	t.cull_mode,
	t.scissor_enabled);

struct PipelineState
{
	ShaderD3D12* shader = nullptr;
	RasterizerState rasterizer_state;
	std::optional<DepthMode> depth_mode;
	
	bool operator==(const PipelineState& value) const
	{
		return 
			shader == value.shader &&
			rasterizer_state == value.rasterizer_state &&
			depth_mode == value.depth_mode;
	}
};

SKYGFX_MAKE_HASHABLE(PipelineState,
	t.shader,
	t.rasterizer_state,
	t.depth_mode);

static int const NUM_BACK_BUFFERS = 2;

static UINT g_frameIndex = 0;
static ID3D12Device* D3D12Device = NULL;
static ID3D12CommandAllocator* D3D12CommandAllocator;
static ID3D12CommandQueue* D3D12CommandQueue = NULL;
static IDXGISwapChain3* D3D12SwapChain = NULL;
static HANDLE g_hSwapChainWaitableObject = NULL;
static ID3D12GraphicsCommandList* D3D12CommandList = NULL;

static ID3D12Resource* g_mainRenderTargetResource[NUM_BACK_BUFFERS] = {};
static UINT gFrameIndex = 0;
static HANDLE gFenceEvent = NULL;
static ID3D12Fence* gFence = NULL;
static UINT64 gFenceValue;

static D3D12_CPU_DESCRIPTOR_HANDLE g_mainRenderTargetDescriptor[NUM_BACK_BUFFERS] = {};

static ID3D12DescriptorHeap* g_pd3dRtvDescHeap = NULL;

class TextureD3D12;
class UniformBufferD3D12;

static std::unordered_map<uint32_t, TextureD3D12*> gTextures;
static std::unordered_map<uint32_t, UniformBufferD3D12*> gUniformBuffers;

static PipelineState gPipelineState;
static std::unordered_map<PipelineState, ComPtr<ID3D12PipelineState>> gPipelineStates;

static ExecuteList gExecuteAfterPresent;

template <typename T>
inline void SafeRelease(T& a)
{
	if (!a)
		return;

	a->Release();

	if constexpr (!std::is_const<T>())
		a = NULL;
}

void OneTimeSubmit(ID3D12Device* device, const std::function<void(ID3D12GraphicsCommandList*)> func)
{
	D3D12_COMMAND_QUEUE_DESC queue_desc = {};
	queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
	queue_desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
	queue_desc.NodeMask = 1;

	ID3D12CommandQueue* cmd_queue = NULL;
	device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&cmd_queue));

	ID3D12CommandAllocator* cmd_alloc = NULL;
	device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&cmd_alloc));

	ID3D12GraphicsCommandList* cmd_list = NULL;
	device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, cmd_alloc, NULL, IID_PPV_ARGS(&cmd_list));

	ID3D12Fence* fence = NULL;
	device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));

	HANDLE event = CreateEvent(0, 0, 0, 0);

	func(cmd_list);
	cmd_list->Close();
	cmd_queue->ExecuteCommandLists(1, CommandListCast(&cmd_list));
	cmd_queue->Signal(fence, 1);

	fence->SetEventOnCompletion(1, event);
	WaitForSingleObject(event, INFINITE);

	fence->Release();
	cmd_list->Release();
	cmd_alloc->Release();
	cmd_queue->Release();
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

		if (vertex_shader_error != nullptr)
			vertex_shader_error_string = std::string((char*)vertex_shader_error->GetBufferPointer(), vertex_shader_error->GetBufferSize());

		if (pixel_shader_error != nullptr)
			pixel_shader_error_string = std::string((char*)pixel_shader_error->GetBufferPointer(), pixel_shader_error->GetBufferSize());

		if (vertex_shader_blob == nullptr)
			throw std::runtime_error(vertex_shader_error_string);

		if (pixel_shader_blob == nullptr)
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
			std::vector<std::vector<CD3DX12_DESCRIPTOR_RANGE>> ranges_vector;

			for (const auto& [stage, sets] : required_descriptor_sets)
			{
				for (const auto& [set, bindings] : sets)
				{
					ranges_vector.push_back({});
					auto& ranges = ranges_vector[ranges_vector.size() - 1];

					for (const auto& binding : bindings)
					{
						const auto& descriptor = required_descriptor_bindings.at(binding);

						if (descriptor.type == ShaderReflection::Descriptor::Type::UniformBuffer)
							continue;
						
						static const std::unordered_map<ShaderReflection::Descriptor::Type, D3D12_DESCRIPTOR_RANGE_TYPE> RangeTypeMap = {
							{ ShaderReflection::Descriptor::Type::CombinedImageSampler, D3D12_DESCRIPTOR_RANGE_TYPE_SRV },
							{ ShaderReflection::Descriptor::Type::UniformBuffer, D3D12_DESCRIPTOR_RANGE_TYPE_CBV },
						};

						auto range_type = RangeTypeMap.at(descriptor.type);
						auto range = CD3DX12_DESCRIPTOR_RANGE(range_type, 1, binding);
						ranges.push_back(range);

						binding_to_root_index.insert({ binding, (uint32_t)params.size() });

						if (descriptor.type == ShaderReflection::Descriptor::Type::CombinedImageSampler)
						{
							static_samplers.push_back(CD3DX12_STATIC_SAMPLER_DESC(binding, D3D12_FILTER_MIN_MAG_MIP_LINEAR));
						}
					}

					if (ranges.empty())
						continue;

					static const std::unordered_map<ShaderStage, D3D12_SHADER_VISIBILITY> VisibilityMap = {
						{ ShaderStage::Vertex, D3D12_SHADER_VISIBILITY_VERTEX },
						{ ShaderStage::Fragment, D3D12_SHADER_VISIBILITY_PIXEL },
					};

					auto visibility = VisibilityMap.at(stage);

					CD3DX12_ROOT_PARAMETER param;
					param.InitAsDescriptorTable((UINT)ranges.size(), ranges.data(), visibility);
					params.push_back(param);
				}
			}

			for (const auto& [binding, descriptor] : required_descriptor_bindings)
			{
				if (descriptor.type != ShaderReflection::Descriptor::Type::UniformBuffer)
					continue;

				binding_to_root_index.insert({ binding, (uint32_t)params.size() });

				CD3DX12_ROOT_PARAMETER param;
				param.InitAsConstantBufferView(binding);
				params.push_back(param);
			}

			CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC desc((UINT)params.size(), params.data(), (UINT)static_samplers.size(),
				static_samplers.data(), D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

			ComPtr<ID3DBlob> blob;
			ComPtr<ID3DBlob> error;

			D3DX12SerializeVersionedRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &error);

			std::string error_string;

			if (error != nullptr)
				error_string = std::string((char*)error->GetBufferPointer(), error->GetBufferSize());

			D3D12Device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&root_signature));
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
	ID3D12Resource* texture = nullptr;
	ID3D12DescriptorHeap* heap = nullptr;
	
public:
	TextureD3D12(uint32_t _width, uint32_t _height, uint32_t channels, void* memory, bool _mipmap) :
		width(_width),
		height(_height),
		mipmap(_mipmap)
	{
		const auto format = DXGI_FORMAT_R8G8B8A8_UNORM;

		auto prop = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
		auto desc = CD3DX12_RESOURCE_DESC::Tex2D(format, width, height);

		D3D12Device->CreateCommittedResource(&prop, D3D12_HEAP_FLAG_NONE, &desc,
			D3D12_RESOURCE_STATE_COPY_DEST, NULL, IID_PPV_ARGS(&texture));

		const UINT64 upload_size = GetRequiredIntermediateSize(texture, 0, 1);

		desc = CD3DX12_RESOURCE_DESC::Buffer(upload_size);
		prop = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);

		ID3D12Resource* upload_buffer = NULL;
		D3D12Device->CreateCommittedResource(&prop, D3D12_HEAP_FLAG_NONE, &desc,
			D3D12_RESOURCE_STATE_GENERIC_READ, NULL, IID_PPV_ARGS(&upload_buffer));

		D3D12_SUBRESOURCE_DATA subersource_data = {};
		subersource_data.pData = memory;
		subersource_data.RowPitch = width * channels;
		subersource_data.SlicePitch = width * height * channels;

		OneTimeSubmit(D3D12Device, [&](ID3D12GraphicsCommandList* cmdlist) {
			UpdateSubresources(cmdlist, texture, upload_buffer, 0, 0, 1, &subersource_data);

			auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(texture,
				D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

			cmdlist->ResourceBarrier(1, &barrier);
		});

		D3D12_DESCRIPTOR_HEAP_DESC heap_desc = {};
		heap_desc.NumDescriptors = 1;
		heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		D3D12Device->CreateDescriptorHeap(&heap_desc, IID_PPV_ARGS(&heap));

		D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
		srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srv_desc.Format = format;
		srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		srv_desc.Texture2D.MipLevels = 1;
		D3D12Device->CreateShaderResourceView(texture, &srv_desc, heap->GetCPUDescriptorHandleForHeapStart());
	}

	~TextureD3D12()
	{
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
	ID3D12Resource* buffer = nullptr;
	size_t size = 0;

public:
	BufferD3D12(size_t _size) : size(_size)
	{
		auto prop = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
		auto desc = CD3DX12_RESOURCE_DESC::Buffer(size);

		D3D12Device->CreateCommittedResource(&prop, D3D12_HEAP_FLAG_NONE, &desc, 
			D3D12_RESOURCE_STATE_GENERIC_READ, NULL, IID_PPV_ARGS(&buffer));
	}

	~BufferD3D12()
	{
		SafeRelease(buffer);
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

void WaitForPreviousFrame()
{
	const UINT64 fence = gFenceValue;
	D3D12CommandQueue->Signal(gFence, fence);
	gFenceValue++;
	if (gFence->GetCompletedValue() < fence)
	{
		gFence->SetEventOnCompletion(fence, gFenceEvent);
		WaitForSingleObject(gFenceEvent, INFINITE);
	}
	gFrameIndex = D3D12SwapChain->GetCurrentBackBufferIndex();
}

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
		
		auto rtvDescriptorSize = D3D12Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
		auto rtvHandle = g_pd3dRtvDescHeap->GetCPUDescriptorHandleForHeapStart();
		for (UINT i = 0; i < NUM_BACK_BUFFERS; i++)
		{
			g_mainRenderTargetDescriptor[i] = rtvHandle;
			rtvHandle.ptr += rtvDescriptorSize;
		}
	}

	{
		D3D12_COMMAND_QUEUE_DESC desc = {};
		desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
		desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
		desc.NodeMask = 1;
		D3D12Device->CreateCommandQueue(&desc, IID_PPV_ARGS(&D3D12CommandQueue));
	}

	D3D12Device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&D3D12CommandAllocator));

	D3D12Device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, D3D12CommandAllocator, NULL, IID_PPV_ARGS(&D3D12CommandList));
	D3D12CommandList->Close();

	{
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

		IDXGIFactory4* dxgiFactory = NULL;
		IDXGISwapChain1* swapChain1 = NULL;
		CreateDXGIFactory1(IID_PPV_ARGS(&dxgiFactory)) != S_OK ||
			dxgiFactory->CreateSwapChainForHwnd(D3D12CommandQueue, (HWND)window, &sd, NULL, NULL, &swapChain1) != S_OK ||
			swapChain1->QueryInterface(IID_PPV_ARGS(&D3D12SwapChain)); // TODO: wtf is this line
		
		swapChain1->Release();
		dxgiFactory->Release();
		D3D12SwapChain->SetMaximumFrameLatency(NUM_BACK_BUFFERS);
		g_hSwapChainWaitableObject = D3D12SwapChain->GetFrameLatencyWaitableObject();

		gFrameIndex = D3D12SwapChain->GetCurrentBackBufferIndex();
	}

	{
		D3D12Device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&gFence));
		gFenceValue = 1;
		gFenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
		assert(gFenceEvent != nullptr);
		WaitForPreviousFrame();
	}

	createMainRenderTarget(width, height);

	begin();
	setRenderTarget(nullptr);
}

BackendD3D12::~BackendD3D12()
{
	end();
	gExecuteAfterPresent.flush();
	destroyMainRenderTarget();
}

void BackendD3D12::createMainRenderTarget(uint32_t width, uint32_t height)
{
	for (UINT i = 0; i < NUM_BACK_BUFFERS; i++)
	{
		ID3D12Resource* pBackBuffer = NULL;
		D3D12SwapChain->GetBuffer(i, IID_PPV_ARGS(&pBackBuffer));
		D3D12Device->CreateRenderTargetView(pBackBuffer, NULL, g_mainRenderTargetDescriptor[i]);
		
		g_mainRenderTargetResource[i] = pBackBuffer; // TODO: in d3d11 pBackbuffer released here
	}
}

void BackendD3D12::destroyMainRenderTarget()
{
	WaitForPreviousFrame();

	for (UINT i = 0; i < NUM_BACK_BUFFERS; i++)
		if (g_mainRenderTargetResource[i]) 
		{ 
			g_mainRenderTargetResource[i]->Release(); 
			g_mainRenderTargetResource[i] = NULL; 
		}
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
	auto texture = (TextureD3D12*)handle;
	gTextures[binding] = texture;
}

void BackendD3D12::setRenderTarget(RenderTargetHandle* handle)
{
}

void BackendD3D12::setRenderTarget(std::nullptr_t value)
{
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

	D3D12CommandList->IASetVertexBuffers(0, 1, &buffer_view);
}

void BackendD3D12::setIndexBuffer(IndexBufferHandle* handle)
{
	auto buffer = (IndexBufferD3D12*)handle;

	D3D12_INDEX_BUFFER_VIEW buffer_view = {};
	buffer_view.BufferLocation = buffer->buffer->GetGPUVirtualAddress();
	buffer_view.SizeInBytes = (UINT)buffer->size;
	buffer_view.Format = buffer->stride == 2 ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT;

	D3D12CommandList->IASetIndexBuffer(&buffer_view);
}

void BackendD3D12::setUniformBuffer(uint32_t binding, UniformBufferHandle* handle)
{
	auto buffer = (UniformBufferD3D12*)handle;
	gUniformBuffers[binding] = buffer;
}

void BackendD3D12::setBlendMode(const BlendMode& value)
{
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
		D3D12CommandList->ClearRenderTargetView(g_mainRenderTargetDescriptor[gFrameIndex], (float*)&color.value(), 0, NULL);
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
			{ ComparisonFunc::NotEqual, D3D12_COMPARISON_FUNC_EQUAL },
			{ ComparisonFunc::LessEqual, D3D12_COMPARISON_FUNC_EQUAL },
			{ ComparisonFunc::Greater, D3D12_COMPARISON_FUNC_GREATER },
			{ ComparisonFunc::GreaterEqual, D3D12_COMPARISON_FUNC_EQUAL }
		};

		auto depth_mode = gPipelineState.depth_mode.value_or(DepthMode());

		D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc = {};
		pso_desc.VS = CD3DX12_SHADER_BYTECODE(shader->vertex_shader_blob.Get());
		pso_desc.PS = CD3DX12_SHADER_BYTECODE(shader->pixel_shader_blob.Get());
		pso_desc.InputLayout = { shader->input.data(), (UINT)shader->input.size() };
		pso_desc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
		pso_desc.NodeMask = 1;
		pso_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		pso_desc.pRootSignature = shader->root_signature.Get();
		pso_desc.SampleMask = UINT_MAX;
		pso_desc.NumRenderTargets = 1;
		pso_desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
		pso_desc.SampleDesc.Count = 1;
		pso_desc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;

		pso_desc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
		pso_desc.RasterizerState.CullMode = CullMap.at(gPipelineState.rasterizer_state.cull_mode);
		
		pso_desc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
		pso_desc.DepthStencilState.DepthEnable = FALSE;
		//pso_desc.DepthStencilState.DepthEnable = gPipelineState.depth_mode.has_value();
		//pso_desc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
		//pso_desc.DepthStencilState.DepthFunc = ComparisonFuncMap.at(depth_mode.func);

		D3D12Device->CreateGraphicsPipelineState(&pso_desc, IID_PPV_ARGS(&gPipelineStates[gPipelineState]));
	}

	auto pipeline_state = gPipelineStates.at(gPipelineState).Get();

	D3D12CommandList->OMSetRenderTargets(1, &g_mainRenderTargetDescriptor[gFrameIndex], FALSE, NULL);
	D3D12CommandList->SetPipelineState(pipeline_state);
	D3D12CommandList->SetGraphicsRootSignature(shader->root_signature.Get());

	for (const auto& [binding, descriptor] : shader->required_descriptor_bindings)
	{
		auto root_index = shader->binding_to_root_index.at(binding);

		if (descriptor.type == ShaderReflection::Descriptor::Type::CombinedImageSampler)
		{
			const auto& texture = gTextures.at(binding);
			D3D12CommandList->SetDescriptorHeaps(1, &texture->heap);
			D3D12CommandList->SetGraphicsRootDescriptorTable(root_index, texture->heap->GetGPUDescriptorHandleForHeapStart());
		}
		else if (descriptor.type == ShaderReflection::Descriptor::Type::UniformBuffer)
		{
			const auto& buffer = gUniformBuffers.at(binding);
			D3D12CommandList->SetGraphicsRootConstantBufferView(root_index, buffer->buffer->GetGPUVirtualAddress());
		}
		else
		{
			assert(false);
		}
	}

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

	WaitForPreviousFrame();

	gExecuteAfterPresent.flush();

	begin();
}

void BackendD3D12::begin()
{
	D3D12CommandAllocator->Reset();

	auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(g_mainRenderTargetResource[gFrameIndex], 
		D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);

	D3D12CommandList->Reset(D3D12CommandAllocator, NULL);
	D3D12CommandList->ResourceBarrier(1, &barrier);
}

void BackendD3D12::end()
{
	auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(g_mainRenderTargetResource[gFrameIndex],
		D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);

	D3D12CommandList->ResourceBarrier(1, &barrier);
	D3D12CommandList->Close();
	
	D3D12CommandQueue->ExecuteCommandLists(1, CommandListCast(&D3D12CommandList));
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