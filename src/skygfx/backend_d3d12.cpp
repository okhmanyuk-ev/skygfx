#include "backend_d3d12.h"

#ifdef SKYGFX_HAS_D3D12

#include <stdexcept>
#include <unordered_set>

#include <d3dcompiler.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <dxgidebug.h>
#include <d3dx12/d3dx12.h>
#include <d3dx12/d3d12generatemips.h>
#include <d3dx12/DirectXHelpers.h>
#include <d3dx12/PlatformHelpers.h>

#pragma comment(lib, "d3d12")
#pragma comment(lib, "d3dcompiler")
#pragma comment(lib, "dxgi")
#pragma comment(lib, "dxguid.lib")

#include <wrl.h>

using Microsoft::WRL::ComPtr;
using namespace skygfx;
using namespace DirectX;

class BufferD3D12;
class ShaderD3D12;
class TextureD3D12;
class RenderTargetD3D12;
class VertexBufferD3D12;
class IndexBufferD3D12;
class UniformBufferD3D12;

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
	BlendMode blend_mode = BlendStates::Opaque;
	TopologyKind topology_kind = TopologyKind::Triangles;
	
	bool operator==(const PipelineStateD3D12& value) const
	{
		return 
			shader == value.shader &&
			rasterizer_state == value.rasterizer_state &&
			depth_mode == value.depth_mode &&
			blend_mode == value.blend_mode &&
			topology_kind == value.topology_kind;
	}
};

SKYGFX_MAKE_HASHABLE(PipelineStateD3D12,
	t.shader,
	t.rasterizer_state,
	t.depth_mode,
	t.blend_mode,
	t.topology_kind);

static int const NUM_BACK_BUFFERS = 2;

static ComPtr<ID3D12Device> gDevice;
static ComPtr<ID3D12CommandAllocator> gCommandAllocator;
static ComPtr<ID3D12CommandQueue> gCommandQueue;
static ComPtr<IDXGISwapChain3> gSwapChain;
static ComPtr<ID3D12GraphicsCommandList4> gCommandList;
static ComPtr<ID3D12DescriptorHeap> gDescriptorHeap;
static CD3DX12_CPU_DESCRIPTOR_HANDLE gDescriptorHeapCpuHandle;
static CD3DX12_GPU_DESCRIPTOR_HANDLE gDescriptorHeapGpuHandle;
static UINT gDescriptorHandleIncrementSize = 0;

static struct 
{
	struct Frame
	{
		D3D12_CPU_DESCRIPTOR_HANDLE rtv_descriptor;
		ComPtr<ID3D12Resource> resource;
	};

	Frame frames[NUM_BACK_BUFFERS];

	ComPtr<ID3D12DescriptorHeap> rtv_heap;
	ComPtr<ID3D12DescriptorHeap> dsv_heap;
	ComPtr<ID3D12Resource> depth_stencil_resource;
} gMainRenderTarget;

static UINT gFrameIndex = 0;
static HANDLE gFenceEvent = NULL;
static ComPtr<ID3D12Fence> gFence;
static UINT64 gFenceValue;

static std::unordered_map<uint32_t, TextureD3D12*> gTextures;
static std::unordered_map<uint32_t, UniformBufferD3D12*> gUniformBuffers;
static RenderTargetD3D12* gRenderTarget = nullptr;

static PipelineStateD3D12 gPipelineState;
static std::unordered_map<PipelineStateD3D12, ComPtr<ID3D12PipelineState>> gPipelineStates;

static ExecuteList gExecuteAfterPresent;

static std::optional<Viewport> gViewport;
static bool gViewportDirty = true;

static std::optional<Scissor> gScissor;
static bool gScissorDirty = true;

static Topology gTopology = Topology::TriangleList;
static bool gTopologyDirty = true;

static IndexBufferD3D12* gIndexBuffer = nullptr;
static bool gIndexBufferDirty = true;

static VertexBufferD3D12* gVertexBuffer = nullptr;
static bool gVertexBufferDirty = true;

static uint32_t gBackbufferWidth = 0;
static uint32_t gBackbufferHeight = 0;

static std::vector<ComPtr<ID3D12DeviceChild>> gStagingObjects;

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

	ComPtr<ID3D12Fence> fence;
	device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(fence.GetAddressOf()));

	auto event = CreateEvent(0, 0, 0, 0);

	func(cmd_list.Get());
	cmd_list->Close();
	cmd_queue->ExecuteCommandLists(1, CommandListCast(cmd_list.GetAddressOf()));
	cmd_queue->Signal(fence.Get(), 1);

	fence->SetEventOnCompletion(1, event);
	WaitForSingleObject(event, INFINITE);
}

class ShaderD3D12
{
public:
	const auto& getRootSignature() const { return mRootSignature; }
	const auto& getRequiredDescriptorBindings() const { return mRequiredDescriptorBindings; }
	const auto& getBindingToRootIndexMap() const { return mBindingToRootIndexMap; }
	const auto& getVertexShaderBlob() const { return mVertexShaderBlob; }
	const auto& getPixelShaderBlob() const { return mPixelShaderBlob; }
	const auto& getInput() const { return mInput; }

private:
	ComPtr<ID3D12RootSignature> mRootSignature;
	std::map<uint32_t, ShaderReflection::Descriptor> mRequiredDescriptorBindings;
	std::map<ShaderStage, std::map<uint32_t/*set*/, std::set<uint32_t>/*bindings*/>> mRequiredDescriptorSets;
	std::map<uint32_t, uint32_t> mBindingToRootIndexMap;
	ComPtr<ID3DBlob> mVertexShaderBlob;
	ComPtr<ID3DBlob> mPixelShaderBlob;
	std::vector<D3D12_INPUT_ELEMENT_DESC> mInput;

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
			&mVertexShaderBlob, &vertex_shader_error);
		D3DCompile(hlsl_frag.c_str(), hlsl_frag.size(), NULL, NULL, NULL, "main", "ps_5_0", compile_flags, 0, 
			&mPixelShaderBlob, &pixel_shader_error);

		std::string vertex_shader_error_string = "";
		std::string pixel_shader_error_string = "";

		if (vertex_shader_error != NULL)
			vertex_shader_error_string = std::string((char*)vertex_shader_error->GetBufferPointer(), vertex_shader_error->GetBufferSize());

		if (pixel_shader_error != NULL)
			pixel_shader_error_string = std::string((char*)pixel_shader_error->GetBufferPointer(), pixel_shader_error->GetBufferSize());

		if (mVertexShaderBlob == NULL)
			throw std::runtime_error(vertex_shader_error_string);

		if (mPixelShaderBlob == NULL)
			throw std::runtime_error(pixel_shader_error_string);

		static const std::unordered_map<Vertex::Attribute::Format, DXGI_FORMAT> Format = {
			{ Vertex::Attribute::Format::Float1, DXGI_FORMAT_R32_FLOAT },
			{ Vertex::Attribute::Format::Float2, DXGI_FORMAT_R32G32_FLOAT },
			{ Vertex::Attribute::Format::Float3, DXGI_FORMAT_R32G32B32_FLOAT },
			{ Vertex::Attribute::Format::Float4, DXGI_FORMAT_R32G32B32A32_FLOAT },
			{ Vertex::Attribute::Format::Byte1, DXGI_FORMAT_R8_UNORM },
			{ Vertex::Attribute::Format::Byte2, DXGI_FORMAT_R8G8_UNORM },
			//	{ Vertex::Attribute::Format::Byte3, DXGI_FORMAT_R8G8B8_UNORM }, // TODO: fix
			{ Vertex::Attribute::Format::Byte4, DXGI_FORMAT_R8G8B8A8_UNORM }
		};

		UINT i = 0;

		for (auto& attrib : layout.attributes)
		{
			mInput.push_back({ "TEXCOORD", i, Format.at(attrib.format), 0,
				static_cast<UINT>(attrib.offset), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 });
			i++;
		}

		auto vertex_shader_reflection = MakeSpirvReflection(vertex_shader_spirv);
		auto fragment_shader_reflection = MakeSpirvReflection(fragment_shader_spirv);

		for (const auto& reflection : { vertex_shader_reflection, fragment_shader_reflection })
		{
			for (const auto& [binding, descriptor] : reflection.descriptor_bindings)
			{
				if (mRequiredDescriptorBindings.contains(binding))
					continue;

				mRequiredDescriptorBindings.insert({ binding, descriptor });
			}

			for (const auto& [set, bindings] : reflection.descriptor_sets)
			{
				mRequiredDescriptorSets[reflection.stage][set] = bindings;
			}
		}

		{
			std::vector<CD3DX12_ROOT_PARAMETER> params;
			std::vector<D3D12_STATIC_SAMPLER_DESC> static_samplers;
			std::vector<CD3DX12_DESCRIPTOR_RANGE> ranges(32);

			for (const auto& [binding, descriptor] : mRequiredDescriptorBindings)
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

				mBindingToRootIndexMap.insert({ binding, (uint32_t)params.size() });
				params.push_back(param);
			}

			auto desc = CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC((UINT)params.size(), params.data(), (UINT)static_samplers.size(),
				static_samplers.data(), D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

			CreateRootSignature(gDevice.Get(), &desc.Desc_1_0, mRootSignature.GetAddressOf());
		}
	}
};

class TextureD3D12
{
public:
	auto getWidth() const { return mWidth; }
	auto getHeight() const { return mHeight; }
	auto isMipmap() const { return mMipmap; }
	const auto& getD3D12Texture() const { return mTexture; }
	auto getGpuDescriptorHandle() const { return mGpuDescriptorHandle; }

private:
	uint32_t mWidth;
	uint32_t mHeight;
	bool mMipmap;
	ComPtr<ID3D12Resource> mTexture;
	CD3DX12_GPU_DESCRIPTOR_HANDLE mGpuDescriptorHandle;
	
public:
	TextureD3D12(uint32_t width, uint32_t height, uint32_t channels, void* memory, bool mipmap) :
		mWidth(width),
		mHeight(height),
		mMipmap(mipmap)
	{
		const auto format = DXGI_FORMAT_R8G8B8A8_UNORM;

		uint32_t mip_levels = 1;

		if (mipmap)
		{
			mip_levels = static_cast<uint32_t>(glm::floor(glm::log2(glm::max(width, height)))) + 1;
		}

		auto prop = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
		auto desc = CD3DX12_RESOURCE_DESC::Tex2D(format, width, height, 1, (UINT16)mip_levels);

		desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

		gDevice->CreateCommittedResource(&prop, D3D12_HEAP_FLAG_NONE, &desc,
			D3D12_RESOURCE_STATE_COMMON, NULL, IID_PPV_ARGS(mTexture.GetAddressOf()));

		CreateShaderResourceView(gDevice.Get(), mTexture.Get(), gDescriptorHeapCpuHandle);

		mGpuDescriptorHandle = gDescriptorHeapGpuHandle;

		gDescriptorHeapCpuHandle.Offset(1, gDescriptorHandleIncrementSize);
		gDescriptorHeapGpuHandle.Offset(1, gDescriptorHandleIncrementSize);

		if (memory)
		{
			auto upload_size = GetRequiredIntermediateSize(mTexture.Get(), 0, 1);
			auto upload_desc = CD3DX12_RESOURCE_DESC::Buffer(upload_size);
			auto upload_prop = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);

			ComPtr<ID3D12Resource> upload_buffer = NULL;

			gDevice->CreateCommittedResource(&upload_prop, D3D12_HEAP_FLAG_NONE, &upload_desc,
				D3D12_RESOURCE_STATE_GENERIC_READ, NULL, IID_PPV_ARGS(upload_buffer.GetAddressOf()));

			D3D12_SUBRESOURCE_DATA subersource_data = {};
			subersource_data.pData = memory;
			subersource_data.RowPitch = width * channels;
			subersource_data.SlicePitch = width * height * channels;

			OneTimeSubmit(gDevice.Get(), [&](ID3D12GraphicsCommandList* cmdlist) {
				auto barrier = ScopedBarrier(cmdlist, { 
					CD3DX12_RESOURCE_BARRIER::Transition(mTexture.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST) 
				});

				UpdateSubresources(cmdlist, mTexture.Get(), upload_buffer.Get(), 0, 0, 1, &subersource_data);
			});

			if (mipmap)
			{
				generateMips();
			}
		}
	}

	void generateMips()
	{
		D3D12GenerateMips(gDevice.Get(), gCommandQueue.Get(), mTexture.Get());
	}
};

class RenderTargetD3D12
{
public:
	const auto& getRtvHeap() const { return mRtvHeap; }
	const auto& getDsvHeap() const { return mDsvHeap; }
	const auto& getDepthStencilRecource() const { return mDepthStencilResource; }
	const auto& getTexture() const { return *mTexture; }

private:
	ComPtr<ID3D12DescriptorHeap> mRtvHeap;
	ComPtr<ID3D12DescriptorHeap> mDsvHeap;
	ComPtr<ID3D12Resource> mDepthStencilResource;
	TextureD3D12* mTexture;

public:
	RenderTargetD3D12(uint32_t width, uint32_t height, TextureD3D12* texture) : mTexture(texture)
	{
		D3D12_DESCRIPTOR_HEAP_DESC rtv_heap_desc = {};
		rtv_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
		rtv_heap_desc.NumDescriptors = 1;
		gDevice->CreateDescriptorHeap(&rtv_heap_desc, IID_PPV_ARGS(mRtvHeap.GetAddressOf()));

		CreateRenderTargetView(gDevice.Get(), texture->getD3D12Texture().Get(),
			mRtvHeap->GetCPUDescriptorHandleForHeapStart());

		auto depth_heap_props = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
		auto depth_desc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_D24_UNORM_S8_UINT,
			(UINT64)width, (UINT)height, 1, 1);

		depth_desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

		gDevice->CreateCommittedResource(&depth_heap_props, D3D12_HEAP_FLAG_NONE, &depth_desc,
			D3D12_RESOURCE_STATE_DEPTH_WRITE, NULL, IID_PPV_ARGS(mDepthStencilResource.GetAddressOf()));

		D3D12_DESCRIPTOR_HEAP_DESC dsv_heap_desc = {};
		dsv_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
		dsv_heap_desc.NumDescriptors = 1;
		gDevice->CreateDescriptorHeap(&dsv_heap_desc, IID_PPV_ARGS(mDsvHeap.GetAddressOf()));

		D3D12_DEPTH_STENCIL_VIEW_DESC dsv_desc = {};
		dsv_desc.Format = depth_desc.Format;
		dsv_desc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;

		gDevice->CreateDepthStencilView(mDepthStencilResource.Get(), &dsv_desc,
			mDsvHeap->GetCPUDescriptorHandleForHeapStart());

		OneTimeSubmit(gDevice.Get(), [&](ID3D12GraphicsCommandList* cmdlist) {
			TransitionResource(cmdlist, texture->getD3D12Texture().Get(),
				D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_RENDER_TARGET);
		});
	}
};

ComPtr<ID3D12Resource> CreateBuffer(size_t size)
{
	ComPtr<ID3D12Resource> result;

	auto prop = CD3DX12_HEAP_PROPERTIES(D3D12_CPU_PAGE_PROPERTY_WRITE_COMBINE,
		D3D12_MEMORY_POOL_L0);
	auto desc = CD3DX12_RESOURCE_DESC::Buffer(size);

	gDevice->CreateCommittedResource(&prop, D3D12_HEAP_FLAG_NONE, &desc,
		D3D12_RESOURCE_STATE_COMMON, NULL, IID_PPV_ARGS(result.GetAddressOf()));

	return result;
}

class BufferD3D12
{
public:
	const auto& getD3D12Buffer() const { return mBuffer; }
	auto getSize() const { return mSize; }

private:
	ComPtr<ID3D12Resource> mBuffer;
	size_t mSize;
	D3D12_RESOURCE_STATES mState;

public:
	BufferD3D12(size_t size, D3D12_RESOURCE_STATES state) : mSize(size), mState(state)
	{
		mBuffer = CreateBuffer(size);

		if (state != D3D12_RESOURCE_STATE_COMMON)
		{
			OneTimeSubmit(gDevice.Get(), [&](ID3D12GraphicsCommandList* cmdlist) {
				TransitionResource(cmdlist, mBuffer.Get(),
					D3D12_RESOURCE_STATE_COMMON, state);
			});
		}
	}

	void write(void* memory, size_t size)
	{
		auto staging_buffer = CreateBuffer(size);

		void* cpu_memory = nullptr;
		staging_buffer->Map(0, NULL, &cpu_memory);
		memcpy(cpu_memory, memory, size);
		staging_buffer->Unmap(0, NULL);

		auto barrier = ScopedBarrier(gCommandList.Get(), {
			CD3DX12_RESOURCE_BARRIER::Transition(mBuffer.Get(), mState, D3D12_RESOURCE_STATE_COPY_DEST)
		});

		gCommandList->CopyBufferRegion(mBuffer.Get(), 0, staging_buffer.Get(), 0, (UINT64)size);

		gStagingObjects.push_back(staging_buffer);
	}
};

class VertexBufferD3D12 : public BufferD3D12
{
public:
	auto getStride() const { return mStride; }
	void setStride(size_t value) { mStride = value; }

private:
	size_t mStride = 0;

public:
	VertexBufferD3D12(size_t size, size_t stride) : BufferD3D12(size, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER),
		mStride(stride)
	{
	}
};

class IndexBufferD3D12 : public BufferD3D12
{
public:
	auto getStride() const { return mStride; }
	void setStride(size_t value) { mStride = value; }

private:
	size_t mStride = 0;

public:
	IndexBufferD3D12(size_t size, size_t stride) : BufferD3D12(size, D3D12_RESOURCE_STATE_INDEX_BUFFER),
		mStride(stride)
	{
	}
};

class UniformBufferD3D12 : public BufferD3D12
{
public:
	UniformBufferD3D12(size_t size) : BufferD3D12(AlignUp((int)size, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT), D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER)
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

	ComPtr<IDXGIFactory6> dxgi_factory;
	CreateDXGIFactory1(IID_PPV_ARGS(dxgi_factory.GetAddressOf()));

	IDXGIAdapter1* adapter;
	dxgi_factory->EnumAdapterByGpuPreference(0, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&adapter));

	D3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_12_1, IID_PPV_ARGS(gDevice.GetAddressOf()));

	ComPtr<ID3D12InfoQueue> info_queue;
	gDevice.As(&info_queue);
	info_queue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, true);
	info_queue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, true);
	info_queue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING, true);
	
	std::vector<D3D12_MESSAGE_ID> filtered_messages = {
		D3D12_MESSAGE_ID_CLEARRENDERTARGETVIEW_MISMATCHINGCLEARVALUE,
		D3D12_MESSAGE_ID_CLEARDEPTHSTENCILVIEW_MISMATCHINGCLEARVALUE,
		D3D12_MESSAGE_ID_DRAW_EMPTY_SCISSOR_RECTANGLE
	};

	D3D12_INFO_QUEUE_FILTER filter = {};
	filter.DenyList.NumIDs = (UINT)filtered_messages.size();
	filter.DenyList.pIDList = filtered_messages.data();
	info_queue->AddStorageFilterEntries(&filter);

	D3D12_DESCRIPTOR_HEAP_DESC rtv_heap_desc = {};
	rtv_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	rtv_heap_desc.NumDescriptors = NUM_BACK_BUFFERS;
	rtv_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	rtv_heap_desc.NodeMask = 1;
	gDevice->CreateDescriptorHeap(&rtv_heap_desc, IID_PPV_ARGS(gMainRenderTarget.rtv_heap.GetAddressOf()));

	D3D12_DESCRIPTOR_HEAP_DESC dsv_heap_desc = {};
	dsv_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
	dsv_heap_desc.NumDescriptors = 1;
	gDevice->CreateDescriptorHeap(&dsv_heap_desc, IID_PPV_ARGS(gMainRenderTarget.dsv_heap.GetAddressOf()));
		
	auto rtv_increment_size = gDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	auto rtv_heap_start = gMainRenderTarget.rtv_heap->GetCPUDescriptorHandleForHeapStart();

	for (UINT i = 0; i < NUM_BACK_BUFFERS; i++)
	{
		auto handle = CD3DX12_CPU_DESCRIPTOR_HANDLE(rtv_heap_start, i, rtv_increment_size);
		gMainRenderTarget.frames[i].rtv_descriptor = handle;
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
	
	D3D12_DESCRIPTOR_HEAP_DESC heap_desc = {};
	heap_desc.NumDescriptors = 1000; // TODO: make more dynamic
	heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	gDevice->CreateDescriptorHeap(&heap_desc, IID_PPV_ARGS(gDescriptorHeap.GetAddressOf()));

	gDescriptorHandleIncrementSize = gDevice->GetDescriptorHandleIncrementSize(heap_desc.Type);

	gDescriptorHeapCpuHandle = gDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
	gDescriptorHeapGpuHandle = gDescriptorHeap->GetGPUDescriptorHandleForHeapStart();

	DXGI_SWAP_CHAIN_DESC1 swapchain_desc = {};
	swapchain_desc.BufferCount = NUM_BACK_BUFFERS;
	swapchain_desc.Width = width;
	swapchain_desc.Height = height;
	swapchain_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	swapchain_desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swapchain_desc.SampleDesc.Count = 1;
	swapchain_desc.SampleDesc.Quality = 0;
	swapchain_desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	swapchain_desc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
	swapchain_desc.Scaling = DXGI_SCALING_NONE;
	swapchain_desc.Stereo = FALSE;

	DXGI_SWAP_CHAIN_FULLSCREEN_DESC fs_swapchain_desc = { 0 };
	fs_swapchain_desc.Windowed = TRUE;

	ComPtr<IDXGISwapChain1> swapchain;
	dxgi_factory->CreateSwapChainForHwnd(gCommandQueue.Get(), (HWND)window,
		&swapchain_desc, &fs_swapchain_desc, NULL, swapchain.GetAddressOf());
		
	swapchain.As(&gSwapChain);

	gDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(gFence.GetAddressOf()));
	gFenceValue = 1;
	gFenceEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
	assert(gFenceEvent != NULL);
	
	createMainRenderTarget(width, height);

	begin();
	setRenderTarget(std::nullopt);
}

BackendD3D12::~BackendD3D12()
{
	end();
	WaitForGpu();
	gExecuteAfterPresent.flush();
	gStagingObjects.clear();
	destroyMainRenderTarget();
	gSwapChain.Reset();
	gDevice.Reset();
}

void BackendD3D12::createMainRenderTarget(uint32_t width, uint32_t height)
{
	for (UINT i = 0; i < NUM_BACK_BUFFERS; i++)
	{
		gSwapChain->GetBuffer(i, IID_PPV_ARGS(gMainRenderTarget.frames[i].resource.GetAddressOf()));
		CreateRenderTargetView(gDevice.Get(), gMainRenderTarget.frames[i].resource.Get(),
			gMainRenderTarget.frames[i].rtv_descriptor);
	}

	auto depth_heap_props = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
	auto depth_desc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_D24_UNORM_S8_UINT, 
		(UINT64)width, (UINT)height, 1, 1);

	depth_desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

	gDevice->CreateCommittedResource(&depth_heap_props, D3D12_HEAP_FLAG_NONE, &depth_desc, 
		D3D12_RESOURCE_STATE_DEPTH_WRITE, NULL, IID_PPV_ARGS(gMainRenderTarget.depth_stencil_resource.GetAddressOf()));

	D3D12_DEPTH_STENCIL_VIEW_DESC dsv_desc = {};
	dsv_desc.Format = depth_desc.Format;
	dsv_desc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
	
	gDevice->CreateDepthStencilView(gMainRenderTarget.depth_stencil_resource.Get(), &dsv_desc,
		gMainRenderTarget.dsv_heap->GetCPUDescriptorHandleForHeapStart());

	gBackbufferWidth = width;
	gBackbufferHeight = height;

	gFrameIndex = gSwapChain->GetCurrentBackBufferIndex();
}

void BackendD3D12::destroyMainRenderTarget()
{
	for (UINT i = 0; i < NUM_BACK_BUFFERS; i++)
	{
		gMainRenderTarget.frames[i].resource.Reset();
	}

	gMainRenderTarget.depth_stencil_resource.Reset();
}

void BackendD3D12::resize(uint32_t width, uint32_t height)
{
	end();
	WaitForGpu();
	destroyMainRenderTarget();
	gSwapChain->ResizeBuffers(NUM_BACK_BUFFERS, (UINT)width, (UINT)height, DXGI_FORMAT_R8G8B8A8_UNORM, 0);
	createMainRenderTarget(width, height);
	begin();

	if (!gViewport.has_value())
		gViewportDirty = true;
	
	if (!gScissor.has_value())
		gScissorDirty = true;
}

void BackendD3D12::setTopology(Topology topology)
{
	if (gTopology == topology)
		return;

	gTopology = topology;
	gTopologyDirty = true;
	gPipelineState.topology_kind = GetTopologyKind(topology);
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
	auto render_target = (RenderTargetD3D12*)handle;
		
	if (gRenderTarget == render_target)
		return;

	gRenderTarget = render_target;

	if (!gViewport.has_value())
		gViewportDirty = true;

	if (!gScissor.has_value())
		gScissorDirty = true;
}

void BackendD3D12::setRenderTarget(std::nullopt_t value)
{
	if (gRenderTarget == nullptr)
		return;

	gRenderTarget = nullptr;

	if (!gViewport.has_value())
		gViewportDirty = true;

	if (!gScissor.has_value())
		gScissorDirty = true;
}

void BackendD3D12::setShader(ShaderHandle* handle)
{
	gPipelineState.shader = (ShaderD3D12*)handle;
}

void BackendD3D12::setRaytracingShader(RaytracingShaderHandle* handle)
{
}

void BackendD3D12::setVertexBuffer(VertexBufferHandle* handle)
{
	auto buffer = (VertexBufferD3D12*)handle;
	
	if (gVertexBuffer == buffer)
		return;
	
	gVertexBuffer = buffer;
	gVertexBufferDirty = true;
}

void BackendD3D12::setIndexBuffer(IndexBufferHandle* handle)
{
	auto buffer = (IndexBufferD3D12*)handle;

	if (gIndexBuffer == buffer)
		return;

	gIndexBuffer = buffer;
	gIndexBufferDirty = true;
}

void BackendD3D12::setUniformBuffer(uint32_t binding, UniformBufferHandle* handle)
{
	auto buffer = (UniformBufferD3D12*)handle;
	gUniformBuffers[binding] = buffer;
}

void BackendD3D12::setAccelerationStructure(uint32_t binding, AccelerationStructureHandle* handle)
{
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
	const auto& rtv = gRenderTarget ? 
		gRenderTarget->getRtvHeap()->GetCPUDescriptorHandleForHeapStart() :
		gMainRenderTarget.frames[gFrameIndex].rtv_descriptor;

	const auto& dsv = gRenderTarget ?
		gRenderTarget->getDsvHeap()->GetCPUDescriptorHandleForHeapStart() :
		gMainRenderTarget.dsv_heap->GetCPUDescriptorHandleForHeapStart();

	if (color.has_value())
	{
		gCommandList->ClearRenderTargetView(rtv, (float*)&color.value(), 0, NULL);
	}

	if (depth.has_value() || stencil.has_value())
	{
		D3D12_CLEAR_FLAGS flags = {};

		if (depth.has_value())
			flags |= D3D12_CLEAR_FLAG_DEPTH;

		if (stencil.has_value())
			flags |= D3D12_CLEAR_FLAG_STENCIL;

		gCommandList->ClearDepthStencilView(dsv, flags, depth.value_or(1.0f), stencil.value_or(0), 0, NULL);
	}
}

void BackendD3D12::draw(uint32_t vertex_count, uint32_t vertex_offset)
{
	prepareForDrawing(false);
	gCommandList->DrawInstanced((UINT)vertex_count, 1, (UINT)vertex_offset, 0);
}

void BackendD3D12::drawIndexed(uint32_t index_count, uint32_t index_offset)
{
	prepareForDrawing(true);
	gCommandList->DrawIndexedInstanced((UINT)index_count, 1, (UINT)index_offset, 0, 0);
}

void BackendD3D12::readPixels(const glm::i32vec2& pos, const glm::i32vec2& size, TextureHandle* dst_texture_handle)
{
	auto dst_texture = (TextureD3D12*)dst_texture_handle;

	assert(dst_texture->getWidth() == size.x);
	assert(dst_texture->getHeight() == size.y);

	if (size.x <= 0 || size.y <= 0)
		return;

	auto rtv_resource = gRenderTarget ? 
		gRenderTarget->getTexture().getD3D12Texture() :
		gMainRenderTarget.frames[gFrameIndex].resource;

	auto desc = rtv_resource->GetDesc();

	auto back_w = desc.Width;
	auto back_h = desc.Height;

	auto src_x = (UINT)pos.x;
	auto src_y = (UINT)pos.y;
	auto src_w = (UINT)size.x;
	auto src_h = (UINT)size.y;

	UINT dst_x = 0;
	UINT dst_y = 0;

	if (pos.x < 0)
	{
		src_x = 0;
		if (-pos.x > size.x)
			src_w = 0;
		else
			src_w += pos.x;

		dst_x = -pos.x;
	}

	if (pos.y < 0)
	{
		src_y = 0;
		if (-pos.y > size.y)
			src_h = 0;
		else
			src_h += pos.y;

		dst_y = -pos.y;
	}

	if (pos.y >= (int)back_h || pos.x >= (int)back_w)
		return;

	src_w = glm::min(src_w, (UINT)back_w);
	src_h = glm::min(src_h, (UINT)back_h);

	D3D12_BOX box;
	box.left = src_x;
	box.right = src_x + src_w;
	box.top = src_y;
	box.bottom = src_y + src_h;
	box.front = 0;
	box.back = 1;

	auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(rtv_resource.Get(),
		D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);

	gCommandList->ResourceBarrier(1, &barrier);

	D3D12_TEXTURE_COPY_LOCATION src_loc = {};
	src_loc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
	src_loc.pResource = rtv_resource.Get();
	src_loc.SubresourceIndex = 0;

	D3D12_TEXTURE_COPY_LOCATION dst_loc = {};
	dst_loc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
	dst_loc.pResource = dst_texture->getD3D12Texture().Get();
	dst_loc.SubresourceIndex = 0;

	gCommandList->CopyTextureRegion(&dst_loc, dst_x, dst_y, 0, &src_loc, &box);

	barrier = CD3DX12_RESOURCE_BARRIER::Transition(rtv_resource.Get(),
		D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);

	gCommandList->ResourceBarrier(1, &barrier);

	if (dst_texture->isMipmap())
		dst_texture->generateMips();
}

void BackendD3D12::dispatchRays(uint32_t width, uint32_t height, uint32_t depth)
{
	return;

	// TODO: uncompleted
	auto desc = D3D12_DISPATCH_RAYS_DESC{};
	desc.Width = width;
	desc.Height = height;
	desc.Depth = depth;

	gCommandList->DispatchRays(&desc);
}

void BackendD3D12::prepareForDrawing(bool indexed)
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

		auto depth_stencil_state = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
		depth_stencil_state.DepthEnable = gPipelineState.depth_mode.has_value();
		depth_stencil_state.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
		depth_stencil_state.DepthFunc = ComparisonFuncMap.at(depth_mode.func);
		depth_stencil_state.StencilEnable = false;

		auto blend_state = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
		blend_state.AlphaToCoverageEnable = false;

		for (int i = 0; i < D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT; i++)
		{
			auto& blend = blend_state.RenderTarget[i];

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
		}

		auto rasterizer_state = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
		rasterizer_state.CullMode = CullMap.at(gPipelineState.rasterizer_state.cull_mode);

		const static std::unordered_map<TopologyKind, D3D12_PRIMITIVE_TOPOLOGY_TYPE> TopologyTypeMap = {
			{ TopologyKind::Points, D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT },
			{ TopologyKind::Lines, D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE },
			{ TopologyKind::Triangles, D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE }
		};

		auto topology_type = TopologyTypeMap.at(gPipelineState.topology_kind);

		D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc = {};
		pso_desc.VS = CD3DX12_SHADER_BYTECODE(shader->getVertexShaderBlob().Get());
		pso_desc.PS = CD3DX12_SHADER_BYTECODE(shader->getPixelShaderBlob().Get());
		pso_desc.InputLayout = { shader->getInput().data(), (UINT)shader->getInput().size() };
		pso_desc.NodeMask = 1;
		pso_desc.PrimitiveTopologyType = topology_type;
		pso_desc.pRootSignature = shader->getRootSignature().Get();
		pso_desc.SampleMask = UINT_MAX;
		pso_desc.NumRenderTargets = 1;
		pso_desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
		pso_desc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
		pso_desc.SampleDesc.Count = 1;
		pso_desc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
		pso_desc.RasterizerState = rasterizer_state;
		pso_desc.DepthStencilState = depth_stencil_state;
		pso_desc.BlendState = blend_state;

		gDevice->CreateGraphicsPipelineState(&pso_desc, IID_PPV_ARGS(&gPipelineStates[gPipelineState]));
	}

	auto rtv_cpu_descriptor = gRenderTarget ?
		gRenderTarget->getRtvHeap()->GetCPUDescriptorHandleForHeapStart() :
		gMainRenderTarget.frames[gFrameIndex].rtv_descriptor;

	auto dsv_cpu_descriptor = gRenderTarget ? 
		gRenderTarget->getDsvHeap()->GetCPUDescriptorHandleForHeapStart() :
		gMainRenderTarget.dsv_heap->GetCPUDescriptorHandleForHeapStart();

	gCommandList->OMSetRenderTargets(1, &rtv_cpu_descriptor, FALSE, &dsv_cpu_descriptor);
	
	auto pipeline_state = gPipelineStates.at(gPipelineState).Get();
	gCommandList->SetPipelineState(pipeline_state);

	gCommandList->SetGraphicsRootSignature(shader->getRootSignature().Get());

	gCommandList->SetDescriptorHeaps(1, gDescriptorHeap.GetAddressOf());

	const auto& required_descriptor_bindings = shader->getRequiredDescriptorBindings();
	const auto& binding_to_root_index_map = shader->getBindingToRootIndexMap();
	
	for (const auto& [binding, descriptor] : required_descriptor_bindings)
	{
		auto root_index = binding_to_root_index_map.at(binding);

		if (descriptor.type == ShaderReflection::Descriptor::Type::CombinedImageSampler)
		{
			const auto& texture = gTextures.at(binding);
			gCommandList->SetGraphicsRootDescriptorTable(root_index, texture->getGpuDescriptorHandle());
		}
		else if (descriptor.type == ShaderReflection::Descriptor::Type::UniformBuffer)
		{
			auto uniform_buffer = gUniformBuffers.at(binding);
			gCommandList->SetGraphicsRootConstantBufferView(root_index, uniform_buffer->getD3D12Buffer()->GetGPUVirtualAddress());
		}
		else
		{
			assert(false);
		}
	}

	if (gViewportDirty || gScissorDirty)
	{
		float width;
		float height;
 
		if (gRenderTarget == nullptr)
		{
			width = static_cast<float>(gBackbufferWidth);
			height = static_cast<float>(gBackbufferHeight);
		}
		else
		{
			width = static_cast<float>(gRenderTarget->getTexture().getWidth());
			height = static_cast<float>(gRenderTarget->getTexture().getHeight());
		}

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

	if (gTopologyDirty)
	{
		const static std::unordered_map<Topology, D3D_PRIMITIVE_TOPOLOGY> TopologyMap = {
			{ Topology::PointList, D3D_PRIMITIVE_TOPOLOGY_POINTLIST },
			{ Topology::LineList, D3D_PRIMITIVE_TOPOLOGY_LINELIST },
			{ Topology::LineStrip, D3D_PRIMITIVE_TOPOLOGY_LINESTRIP },
			{ Topology::TriangleList, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST },
			{ Topology::TriangleStrip, D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP }
		};

		auto topology = TopologyMap.at(gTopology);
		gCommandList->IASetPrimitiveTopology(topology);

		gTopologyDirty = false;
	}

	if (indexed)// && gIndexBufferDirty) // TODO: buffer stride can be changed in writeIndexBufferMemory
	{
		D3D12_INDEX_BUFFER_VIEW buffer_view = {};
		buffer_view.BufferLocation = gIndexBuffer->getD3D12Buffer()->GetGPUVirtualAddress();
		buffer_view.SizeInBytes = (UINT)gIndexBuffer->getSize();
		buffer_view.Format = gIndexBuffer->getStride() == 2 ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT;

		gCommandList->IASetIndexBuffer(&buffer_view);

		gIndexBufferDirty = false;
	}

	//if (gVertexBufferDirty) // TODO: buffer stride can be changed in writeVertexBufferMemory
	{
		D3D12_VERTEX_BUFFER_VIEW buffer_view = {};
		buffer_view.BufferLocation = gVertexBuffer->getD3D12Buffer()->GetGPUVirtualAddress();
		buffer_view.SizeInBytes = (UINT)gVertexBuffer->getSize();
		buffer_view.StrideInBytes = (UINT)gVertexBuffer->getStride();

		gCommandList->IASetVertexBuffers(0, 1, &buffer_view);

		gVertexBufferDirty = false;
	}
}

void BackendD3D12::present()
{
	end();
	bool vsync = false;
	gSwapChain->Present(vsync ? 1 : 0, 0);
	MoveToNextFrame();
	gExecuteAfterPresent.flush();
	gStagingObjects.clear();
	begin();
}

void BackendD3D12::begin()
{
	gCommandAllocator->Reset();

	auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(gMainRenderTarget.frames[gFrameIndex].resource.Get(),
		D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);

	gCommandList->Reset(gCommandAllocator.Get(), NULL);
	gCommandList->ResourceBarrier(1, &barrier);

	gTopologyDirty = true;
	gViewportDirty = true;
	gScissorDirty = true;
	gIndexBufferDirty = true;
	gVertexBufferDirty = true;
}

void BackendD3D12::end()
{
	auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(gMainRenderTarget.frames[gFrameIndex].resource.Get(),
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
	gExecuteAfterPresent.add([handle] {
		auto texture = (TextureD3D12*)handle;
		delete texture;
	});
}

RenderTargetHandle* BackendD3D12::createRenderTarget(uint32_t width, uint32_t height, TextureHandle* texture_handle)
{
	auto texture = (TextureD3D12*)texture_handle;
	auto render_target = new RenderTargetD3D12(width, height, texture);
	return (RenderTargetHandle*)render_target;
}

void BackendD3D12::destroyRenderTarget(RenderTargetHandle* handle)
{
	gExecuteAfterPresent.add([handle] {
		auto render_target = (RenderTargetD3D12*)handle;
		delete render_target;
	});
}

ShaderHandle* BackendD3D12::createShader(const Vertex::Layout& layout, const std::string& vertex_code,
	const std::string& fragment_code, const std::vector<std::string>& defines)
{
	auto shader = new ShaderD3D12(layout, vertex_code, fragment_code, defines);
	return (ShaderHandle*)shader;
}

void BackendD3D12::destroyShader(ShaderHandle* handle)
{
	gExecuteAfterPresent.add([handle] {
		auto shader = (ShaderD3D12*)handle;
		delete shader;
	});
}

RaytracingShaderHandle* BackendD3D12::createRaytracingShader(const std::string& raygen_code, const std::string& miss_code,
	const std::string& closesthit_code, const std::vector<std::string>& defines)
{
	//auto raygen_shader_spirv = CompileGlslToSpirv(ShaderStage::Raygen, raygen_code, defines);
	//auto hlsl_raygen = CompileSpirvToHlsl(raygen_shader_spirv, 60);
	return nullptr;
}

void BackendD3D12::destroyRaytracingShader(RaytracingShaderHandle* handle)
{
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
	buffer->setStride(stride);
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
	buffer->setStride(stride);
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

AccelerationStructureHandle* BackendD3D12::createAccelerationStructure(const std::vector<glm::vec3>& vertices,
	const std::vector<uint32_t>& indices)
{
	return nullptr;
}

void BackendD3D12::destroyAccelerationStructure(AccelerationStructureHandle* handle)
{
}

#endif
