#include "backend_d3d12.h"

#ifdef SKYGFX_HAS_D3D12

//#define SKYGFX_D3D12_VALIDATION_ENABLED

#include "shader_compiler.h"
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
	FrontFace front_face = FrontFace::Clockwise;

	bool operator==(const RasterizerStateD3D12& other) const = default;
};

SKYGFX_MAKE_HASHABLE(RasterizerStateD3D12,
	t.cull_mode,
	t.front_face
);

struct PipelineStateD3D12
{
	ShaderD3D12* shader = nullptr;
	RasterizerStateD3D12 rasterizer_state;
	std::optional<DepthMode> depth_mode;
	std::optional<BlendMode> blend_mode;
	TopologyKind topology_kind = TopologyKind::Triangles;
	std::vector<DXGI_FORMAT> color_attachment_formats;
	std::optional<DXGI_FORMAT> depth_stencil_format;
	std::vector<InputLayout> input_layouts;

	bool operator==(const PipelineStateD3D12& other) const = default;
};

SKYGFX_MAKE_HASHABLE(PipelineStateD3D12,
	t.shader,
	t.rasterizer_state,
	t.depth_mode,
	t.blend_mode,
	t.topology_kind,
	t.color_attachment_formats,
	t.depth_stencil_format,
	t.input_layouts
);

static int const NUM_BACK_BUFFERS = 2;

const static DXGI_FORMAT MainRenderTargetColorAttachmentFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
const static DXGI_FORMAT MainRenderTargetDepthStencilAttachmentFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;

struct ContextD3D12
{
	ComPtr<ID3D12Device> device;
	ComPtr<ID3D12CommandAllocator> cmd_alloc;
	ComPtr<ID3D12CommandQueue> cmd_queue;
	ComPtr<IDXGISwapChain3> swapchain;
	ComPtr<ID3D12GraphicsCommandList4> cmdlist;
	ComPtr<ID3D12DescriptorHeap> descriptor_heap;
	CD3DX12_CPU_DESCRIPTOR_HANDLE descriptor_heap_cpu_handle;
	CD3DX12_GPU_DESCRIPTOR_HANDLE descriptor_heap_gpu_handle;
	UINT descriptor_handle_increment_size = 0;

	struct Frame
	{
		D3D12_CPU_DESCRIPTOR_HANDLE rtv_descriptor; // TODO: move inside RenderTargetD3D12
		TextureD3D12* backbuffer_texture;
		RenderTargetD3D12* main_render_target;
	};
	ComPtr<ID3D12DescriptorHeap> frame_rtv_heap;

	Frame frames[NUM_BACK_BUFFERS];

	UINT frame_index = 0;
	HANDLE fence_event = NULL;
	ComPtr<ID3D12Fence> fence;
	UINT64 fence_value;

	Frame& getCurrentFrame() { return frames[frame_index]; }

	std::unordered_map<uint32_t, TextureD3D12*> textures;
	std::unordered_map<uint32_t, UniformBufferD3D12*> uniform_buffers;
	std::vector<RenderTargetD3D12*> render_targets;

	PipelineStateD3D12 pipeline_state;
	std::unordered_map<PipelineStateD3D12, ComPtr<ID3D12PipelineState>> pipeline_states;

	Topology topology = Topology::TriangleList;
	std::optional<Viewport> viewport;
	std::optional<Scissor> scissor;
	std::vector<VertexBufferD3D12*> vertex_buffers; // TODO: store pointer and count, not std::vector
	IndexBufferD3D12* index_buffer = nullptr;

	bool topology_dirty = true;
	bool viewport_dirty = true;
	bool scissor_dirty = true;
	bool vertex_buffers_dirty = true;
	bool index_buffer_dirty = true;

	uint32_t width = 0;
	uint32_t height = 0;

	std::vector<ComPtr<ID3D12DeviceChild>> staging_objects;

	uint32_t getBackbufferWidth();
	uint32_t getBackbufferHeight();
	PixelFormat getBackbufferFormat();
};

static ContextD3D12* gContext = nullptr;

static const std::unordered_map<VertexFormat, DXGI_FORMAT> VertexFormatMap = {
	{ VertexFormat::Float1, DXGI_FORMAT_R32_FLOAT },
	{ VertexFormat::Float2, DXGI_FORMAT_R32G32_FLOAT },
	{ VertexFormat::Float3, DXGI_FORMAT_R32G32B32_FLOAT },
	{ VertexFormat::Float4, DXGI_FORMAT_R32G32B32A32_FLOAT },
	{ VertexFormat::UChar1Normalized, DXGI_FORMAT_R8_UNORM },
	{ VertexFormat::UChar2Normalized, DXGI_FORMAT_R8G8_UNORM },
	// { VertexFormat::UChar3Normalized, DXGI_FORMAT_R8G8B8_UNORM }, // TODO: wtf
	{ VertexFormat::UChar4Normalized, DXGI_FORMAT_R8G8B8A8_UNORM },
	{ VertexFormat::UChar4, DXGI_FORMAT_R8_UINT },
	{ VertexFormat::UChar4, DXGI_FORMAT_R8G8_UINT },
	// { VertexFormat::UChar4, DXGI_FORMAT_R8G8B8_UINT }, // TODO: wtf
	{ VertexFormat::UChar4, DXGI_FORMAT_R8G8B8A8_UINT },
};

static const std::unordered_map<PixelFormat, DXGI_FORMAT> PixelFormatMap = {
	{ PixelFormat::R32Float, DXGI_FORMAT_R32_FLOAT },
	{ PixelFormat::RG32Float, DXGI_FORMAT_R32G32_FLOAT },
	{ PixelFormat::RGB32Float, DXGI_FORMAT_R32G32B32_FLOAT },
	{ PixelFormat::RGBA32Float, DXGI_FORMAT_R32G32B32A32_FLOAT },
	{ PixelFormat::R8UNorm, DXGI_FORMAT_R8_UNORM },
	{ PixelFormat::RG8UNorm, DXGI_FORMAT_R8G8_UNORM },
	// { PixelFormat::RGB8UNorm, DXGI_FORMAT_R8G8B8_UNORM }, // TODO: wtf
	{ PixelFormat::RGBA8UNorm, DXGI_FORMAT_R8G8B8A8_UNORM }
};

static void DestroyStaging(ComPtr<ID3D12DeviceChild> object);
static void ReleaseStaging();

void BeginCommandList(ID3D12CommandAllocator* cmd_alloc, ID3D12GraphicsCommandList* cmd_list)
{
	cmd_alloc->Reset();
	cmd_list->Reset(cmd_alloc, NULL);
}

void EndCommandList(ID3D12CommandQueue* cmd_queue, ID3D12GraphicsCommandList* cmd_list, bool wait_for)
{
	cmd_list->Close();
	cmd_queue->ExecuteCommandLists(1, CommandListCast(&cmd_list));
	if (wait_for)
	{
		ComPtr<ID3D12Fence> fence;
		ThrowIfFailed(gContext->device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(fence.GetAddressOf())));

		SetDebugObjectName(fence.Get(), L"ResourceUploadBatch");

		auto event = CreateEventEx(nullptr, nullptr, 0, EVENT_MODIFY_STATE | SYNCHRONIZE);

		ThrowIfFailed(cmd_queue->Signal(fence.Get(), 1ULL));
		ThrowIfFailed(fence->SetEventOnCompletion(1ULL, event));

		WaitForSingleObject(event, INFINITE);
	}
}

void OneTimeSubmit(std::function<void(ID3D12GraphicsCommandList*)> func)
{
	D3D12_COMMAND_QUEUE_DESC queue_desc = {};
	queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
	queue_desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
	queue_desc.NodeMask = 1;

	ComPtr<ID3D12CommandQueue> cmd_queue;
	gContext->device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(cmd_queue.GetAddressOf()));

	ComPtr<ID3D12CommandAllocator> cmd_alloc;
	gContext->device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(cmd_alloc.GetAddressOf()));

	ComPtr<ID3D12GraphicsCommandList> cmd_list;
	gContext->device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, cmd_alloc.Get(), NULL,
		IID_PPV_ARGS(cmd_list.GetAddressOf()));

	cmd_list->Close();

	BeginCommandList(cmd_alloc.Get(), cmd_list.Get());
	func(cmd_list.Get());
	EndCommandList(cmd_queue.Get(), cmd_list.Get(), true);
}

ComPtr<ID3D12Resource> CreateBuffer(uint64_t size)
{
	ComPtr<ID3D12Resource> result;

	auto prop = CD3DX12_HEAP_PROPERTIES(D3D12_CPU_PAGE_PROPERTY_WRITE_COMBINE, D3D12_MEMORY_POOL_L0);
	auto desc = CD3DX12_RESOURCE_DESC::Buffer(size);

	gContext->device->CreateCommittedResource(&prop, D3D12_HEAP_FLAG_NONE, &desc,
		D3D12_RESOURCE_STATE_COMMON, NULL, IID_PPV_ARGS(result.GetAddressOf()));

	return result;
}

class ShaderD3D12
{
public:
	const auto& getRootSignature() const { return mRootSignature; }
	const auto& getRequiredTypedDescriptorBindings() const { return mRequiredTypedDescriptorBindings; }
	const auto& getBindingToRootIndexMap() const { return mBindingToRootIndexMap; }
	const auto& getVertexShaderBlob() const { return mVertexShaderBlob; }
	const auto& getPixelShaderBlob() const { return mPixelShaderBlob; }

private:
	ComPtr<ID3D12RootSignature> mRootSignature;
	std::unordered_map< ShaderReflection::DescriptorType, std::unordered_map<uint32_t, ShaderReflection::Descriptor>> mRequiredTypedDescriptorBindings;
	std::unordered_map<ShaderStage, std::unordered_map<uint32_t/*set*/, std::unordered_set<uint32_t>/*bindings*/>> mRequiredDescriptorSets;
	std::unordered_map<uint32_t, uint32_t> mBindingToRootIndexMap;
	ComPtr<ID3DBlob> mVertexShaderBlob;
	ComPtr<ID3DBlob> mPixelShaderBlob;

public:
	ShaderD3D12(const std::string& vertex_code, const std::string& fragment_code,
		std::vector<std::string> defines)
	{
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

		auto vertex_shader_reflection = MakeSpirvReflection(vertex_shader_spirv);
		auto fragment_shader_reflection = MakeSpirvReflection(fragment_shader_spirv);

		for (const auto& reflection : { vertex_shader_reflection, fragment_shader_reflection })
		{
			for (const auto& [type, descriptor_bindings] : reflection.typed_descriptor_bindings)
			{
				for (const auto& [binding, descriptor] : descriptor_bindings)
				{
					auto& required_descriptor_bindings = mRequiredTypedDescriptorBindings[type];
					if (required_descriptor_bindings.contains(binding))
						continue;

					required_descriptor_bindings[binding] = descriptor;
				}
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

			for (const auto& [type, descriptor_bindings] : mRequiredTypedDescriptorBindings)
			{
				for (const auto& [binding, descriptor] : descriptor_bindings)
				{
					CD3DX12_ROOT_PARAMETER param;

					if (type == ShaderReflection::DescriptorType::UniformBuffer)
					{
						param.InitAsConstantBufferView(binding);
					}
					else if (type == ShaderReflection::DescriptorType::CombinedImageSampler)
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
			}

			auto desc = CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC((UINT)params.size(), params.data(), (UINT)static_samplers.size(),
				static_samplers.data(), D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

			CreateRootSignature(gContext->device.Get(), &desc.Desc_1_0, mRootSignature.GetAddressOf());
		}
	}

	~ShaderD3D12()
	{
		DestroyStaging(mRootSignature);
	}
};

class TextureD3D12
{
public:
	const auto& getD3D12Texture() const { return mTexture; }
	auto getGpuDescriptorHandle() const { return mGpuDescriptorHandle; }
	auto getWidth() const { return mWidth; }
	auto getHeight() const { return mHeight; }
	auto getFormat() const { return mFormat; }

private:
	ComPtr<ID3D12Resource> mTexture;
	CD3DX12_GPU_DESCRIPTOR_HANDLE mGpuDescriptorHandle;
	D3D12_RESOURCE_STATES mCurrentState = D3D12_RESOURCE_STATE_COMMON;
	uint32_t mWidth = 0;
	uint32_t mHeight = 0;
	uint32_t mMipCount = 0;
	PixelFormat mFormat;
	
public:
	TextureD3D12(uint32_t width, uint32_t height, PixelFormat format, uint32_t mip_count) :
		mWidth(width),
		mHeight(height),
		mMipCount(mip_count),
		mFormat(format)
	{
		auto prop = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
		auto desc = CD3DX12_RESOURCE_DESC::Tex2D(PixelFormatMap.at(mFormat), width, height, 1, (UINT16)mip_count);

		desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

		gContext->device->CreateCommittedResource(&prop, D3D12_HEAP_FLAG_NONE, &desc,
			D3D12_RESOURCE_STATE_COMMON, NULL, IID_PPV_ARGS(mTexture.GetAddressOf()));

		CreateShaderResourceView(gContext->device.Get(), mTexture.Get(), gContext->descriptor_heap_cpu_handle);

		mGpuDescriptorHandle = gContext->descriptor_heap_gpu_handle;

		gContext->descriptor_heap_cpu_handle.Offset(1, gContext->descriptor_handle_increment_size);
		gContext->descriptor_heap_gpu_handle.Offset(1, gContext->descriptor_handle_increment_size);
	}

	TextureD3D12(uint32_t width, uint32_t height, PixelFormat format, ComPtr<ID3D12Resource> texture) :
		mWidth(width),
		mHeight(height),
		mFormat(format),
		mTexture(texture)
	{
	}

	~TextureD3D12()
	{
		DestroyStaging(mTexture);
	}

	void write(uint32_t width, uint32_t height, PixelFormat format, const void* memory,
		uint32_t mip_level, uint32_t offset_x, uint32_t offset_y)
	{
		auto upload_size = GetRequiredIntermediateSize(mTexture.Get(), mip_level, 1);
		auto upload_desc = CD3DX12_RESOURCE_DESC::Buffer(upload_size);
		auto upload_prop = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);

		ComPtr<ID3D12Resource> upload_buffer = NULL;

		gContext->device->CreateCommittedResource(&upload_prop, D3D12_HEAP_FLAG_NONE, &upload_desc,
			D3D12_RESOURCE_STATE_GENERIC_READ, NULL, IID_PPV_ARGS(upload_buffer.GetAddressOf()));

		auto channels = GetFormatChannelsCount(format);
		auto channel_size = GetFormatChannelSize(format);

		D3D12_SUBRESOURCE_DATA subersource_data = {};
		subersource_data.pData = memory;
		subersource_data.RowPitch = width * channels * channel_size;
		subersource_data.SlicePitch = width * height * channels * channel_size;

		OneTimeSubmit([&](ID3D12GraphicsCommandList* cmdlist) {
			ensureState(cmdlist, D3D12_RESOURCE_STATE_COPY_DEST);
			UpdateSubresources(cmdlist, mTexture.Get(), upload_buffer.Get(), 0, mip_level, 1, &subersource_data);
		});
	}

	void read(uint32_t pos_x, uint32_t pos_y, uint32_t width, uint32_t height,
		uint32_t mip_level, void* dst_memory)
	{
		EndCommandList(gContext->cmd_queue.Get(), gContext->cmdlist.Get(), true);

		auto texture_desc = mTexture->GetDesc();

		UINT64 required_size = 0;
		D3D12_PLACED_SUBRESOURCE_FOOTPRINT layout;
		UINT num_rows = 0;
		UINT64 row_sizes_in_bytes = 0;
		gContext->device->GetCopyableFootprints(&texture_desc, mip_level, 1, 0, &layout, &num_rows, &row_sizes_in_bytes, &required_size);

		auto staging_buffer = CreateBuffer(required_size);

		OneTimeSubmit([&](ID3D12GraphicsCommandList* cmdlist) {
			ensureState(cmdlist, D3D12_RESOURCE_STATE_COPY_SOURCE);
			auto src_loc = CD3DX12_TEXTURE_COPY_LOCATION(mTexture.Get(), mip_level);
			auto dst_loc = CD3DX12_TEXTURE_COPY_LOCATION(staging_buffer.Get(), layout);
			cmdlist->CopyTextureRegion(&dst_loc, 0, 0, 0, &src_loc, NULL); // TODO: box
		});

		UINT8* ptr = nullptr;
		staging_buffer->Map(0, nullptr, reinterpret_cast<void**>(&ptr));

		auto channels_count = GetFormatChannelsCount(mFormat);
		auto channel_size = GetFormatChannelSize(mFormat);
		auto dst_row_size = width * channels_count * channel_size;

		UINT8* src_ptr = ptr + layout.Offset;
		UINT8* dst_ptr = (uint8_t*)dst_memory;// +layouts[i].Offset;

		for (UINT j = 0; j < num_rows; ++j)
		{
			memcpy(dst_ptr, src_ptr, dst_row_size);
			src_ptr += layout.Footprint.RowPitch;
			dst_ptr += dst_row_size;
		}

		staging_buffer->Unmap(0, nullptr);

		BeginCommandList(gContext->cmd_alloc.Get(), gContext->cmdlist.Get());
	}

	void generateMips(ID3D12GraphicsCommandList* cmdlist, std::vector<ComPtr<ID3D12DeviceChild>>& staging_objects)
	{
		ensureState(cmdlist, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
		D3D12GenerateMips(gContext->device.Get(), cmdlist, mTexture.Get(), staging_objects);
		mCurrentState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
	}

	void generateMips()
	{
		OneTimeSubmit([&](ID3D12GraphicsCommandList* cmdlist) {
			generateMips(cmdlist, gContext->staging_objects);
		});
	}

	void ensureState(ID3D12GraphicsCommandList* cmdlist, D3D12_RESOURCE_STATES state)
	{
		if (mCurrentState == state)
			return;

		TransitionResource(cmdlist, mTexture.Get(), mCurrentState, state);
		mCurrentState = state;
	}
};

class RenderTargetD3D12
{
public:
	const auto& getRtvHeap() const { return mRtvHeap; }
	const auto& getDsvHeap() const { return mDsvHeap; }
	const auto& getDepthStencilRecource() const { return mDepthStencilResource; }
	auto getTexture() const { return mTexture; }
	auto getDepthStencilFormat() const { return MainRenderTargetDepthStencilAttachmentFormat; }

private:
	ComPtr<ID3D12DescriptorHeap> mRtvHeap;
	ComPtr<ID3D12DescriptorHeap> mDsvHeap;
	ComPtr<ID3D12Resource> mDepthStencilResource;
	TextureD3D12* mTexture;

public:
	RenderTargetD3D12(uint32_t width, uint32_t height, TextureD3D12* texture,
		D3D12_CPU_DESCRIPTOR_HANDLE rtv_descriptor) : mTexture(texture)
	{
		create(width, height, texture->getD3D12Texture(), rtv_descriptor);
	}

	RenderTargetD3D12(uint32_t width, uint32_t height, TextureD3D12* texture) : mTexture(texture)
	{
		D3D12_DESCRIPTOR_HEAP_DESC rtv_heap_desc = {};
		rtv_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
		rtv_heap_desc.NumDescriptors = 1;
		gContext->device->CreateDescriptorHeap(&rtv_heap_desc, IID_PPV_ARGS(mRtvHeap.GetAddressOf()));

		auto rtv_descriptor = mRtvHeap->GetCPUDescriptorHandleForHeapStart();

		create(width, height, texture->getD3D12Texture(), rtv_descriptor);
	}

	~RenderTargetD3D12()
	{
		DestroyStaging(mRtvHeap);
		DestroyStaging(mDsvHeap);
		DestroyStaging(mDepthStencilResource);
	}

private:
	void create(uint32_t width, uint32_t height, ComPtr<ID3D12Resource> texture_resource,
		D3D12_CPU_DESCRIPTOR_HANDLE rtv_descriptor)
	{
		CreateRenderTargetView(gContext->device.Get(), texture_resource.Get(), rtv_descriptor);

		auto depth_heap_props = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
		auto depth_desc = CD3DX12_RESOURCE_DESC::Tex2D(getDepthStencilFormat(),
			(UINT64)width, (UINT)height, 1, 1);

		depth_desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

		gContext->device->CreateCommittedResource(&depth_heap_props, D3D12_HEAP_FLAG_NONE, &depth_desc,
			D3D12_RESOURCE_STATE_DEPTH_WRITE, NULL, IID_PPV_ARGS(mDepthStencilResource.GetAddressOf()));

		D3D12_DESCRIPTOR_HEAP_DESC dsv_heap_desc = {};
		dsv_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
		dsv_heap_desc.NumDescriptors = 1;
		gContext->device->CreateDescriptorHeap(&dsv_heap_desc, IID_PPV_ARGS(mDsvHeap.GetAddressOf()));

		D3D12_DEPTH_STENCIL_VIEW_DESC dsv_desc = {};
		dsv_desc.Format = depth_desc.Format;
		dsv_desc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;

		gContext->device->CreateDepthStencilView(mDepthStencilResource.Get(), &dsv_desc,
			mDsvHeap->GetCPUDescriptorHandleForHeapStart());
	}
};

uint32_t ContextD3D12::getBackbufferWidth()
{
	return !render_targets.empty() ? render_targets.at(0)->getTexture()->getWidth() : width;
}

uint32_t ContextD3D12::getBackbufferHeight()
{
	return !render_targets.empty() ? render_targets.at(0)->getTexture()->getHeight() : height;
}

PixelFormat ContextD3D12::getBackbufferFormat()
{
	return !render_targets.empty() ? render_targets.at(0)->getTexture()->getFormat() : PixelFormat::RGBA8UNorm;
}

static void DestroyStaging(ComPtr<ID3D12DeviceChild> object)
{
	gContext->staging_objects.push_back(object);
}

static void ReleaseStaging()
{
	gContext->staging_objects.clear();
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
			OneTimeSubmit([&](ID3D12GraphicsCommandList* cmdlist) {
				TransitionResource(cmdlist, mBuffer.Get(), D3D12_RESOURCE_STATE_COMMON, state);
			});
		}
	}

	virtual ~BufferD3D12()
	{
		DestroyStaging(mBuffer);
	}

	void write(const void* memory, size_t size)
	{
		auto staging_buffer = CreateBuffer(size);

		void* cpu_memory = nullptr;
		staging_buffer->Map(0, NULL, &cpu_memory);
		memcpy(cpu_memory, memory, size);
		staging_buffer->Unmap(0, NULL);

		auto barrier = ScopedBarrier(gContext->cmdlist.Get(), {
			CD3DX12_RESOURCE_BARRIER::Transition(mBuffer.Get(), mState, D3D12_RESOURCE_STATE_COPY_DEST)
		});
		gContext->cmdlist->CopyBufferRegion(mBuffer.Get(), 0, staging_buffer.Get(), 0, (UINT64)size);
		DestroyStaging(staging_buffer);
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

static void WaitForGpu()
{
	gContext->cmd_queue->Signal(gContext->fence.Get(), gContext->fence_value);
	gContext->fence->SetEventOnCompletion(gContext->fence_value, gContext->fence_event);
	WaitForSingleObject(gContext->fence_event, INFINITE);
	gContext->fence_value++;
	ReleaseStaging();
}

static void CreateMainRenderTarget(uint32_t width, uint32_t height)
{
	D3D12_DESCRIPTOR_HEAP_DESC rtv_heap_desc = {};
	rtv_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	rtv_heap_desc.NumDescriptors = NUM_BACK_BUFFERS;
	rtv_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	rtv_heap_desc.NodeMask = 1;
	gContext->device->CreateDescriptorHeap(&rtv_heap_desc, IID_PPV_ARGS(gContext->frame_rtv_heap.GetAddressOf()));

	auto rtv_increment_size = gContext->device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	auto rtv_heap_start = gContext->frame_rtv_heap->GetCPUDescriptorHandleForHeapStart();

	for (UINT i = 0; i < NUM_BACK_BUFFERS; i++)
	{
		auto& frame = gContext->frames[i];

		ComPtr<ID3D12Resource> backbuffer;
		gContext->swapchain->GetBuffer(i, IID_PPV_ARGS(backbuffer.GetAddressOf()));

		frame.backbuffer_texture = new TextureD3D12(width, height, skygfx::PixelFormat::RGBA8UNorm, backbuffer);
		frame.rtv_descriptor = CD3DX12_CPU_DESCRIPTOR_HANDLE(rtv_heap_start, i, rtv_increment_size);
		frame.main_render_target = new RenderTargetD3D12(width, height, frame.backbuffer_texture, frame.rtv_descriptor);
	}

	gContext->width = width;
	gContext->height = height;

	gContext->frame_index = gContext->swapchain->GetCurrentBackBufferIndex();
}

static void DestroyMainRenderTarget()
{
	for (UINT i = 0; i < NUM_BACK_BUFFERS; i++)
	{
		delete gContext->frames[i].backbuffer_texture;
		delete gContext->frames[i].main_render_target;
	}
}

static ComPtr<ID3D12PipelineState> CreateGraphicsPipelineState(const PipelineStateD3D12& pipeline_state)
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

	auto depth_mode = pipeline_state.depth_mode.value_or(DepthMode());
	const auto& blend_mode = pipeline_state.blend_mode;

	auto depth_stencil_state = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	depth_stencil_state.DepthEnable = pipeline_state.depth_mode.has_value();
	depth_stencil_state.DepthWriteMask = depth_mode.write_mask ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
	depth_stencil_state.DepthFunc = ComparisonFuncMap.at(depth_mode.func);
	depth_stencil_state.StencilEnable = false;

	auto blend_state = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	blend_state.AlphaToCoverageEnable = false;

	for (int i = 0; i < D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT; i++)
	{
		auto& blend = blend_state.RenderTarget[i];

		blend.BlendEnable = blend_mode.has_value();

		if (!blend.BlendEnable)
			continue;

		const auto& blend_mode_nn = blend_mode.value();

		if (blend_mode_nn.color_mask.red)
			blend.RenderTargetWriteMask |= D3D12_COLOR_WRITE_ENABLE_RED;

		if (blend_mode_nn.color_mask.green)
			blend.RenderTargetWriteMask |= D3D12_COLOR_WRITE_ENABLE_GREEN;

		if (blend_mode_nn.color_mask.blue)
			blend.RenderTargetWriteMask |= D3D12_COLOR_WRITE_ENABLE_BLUE;

		if (blend_mode_nn.color_mask.alpha)
			blend.RenderTargetWriteMask |= D3D12_COLOR_WRITE_ENABLE_ALPHA;

		blend.SrcBlend = BlendMap.at(blend_mode_nn.color_src);
		blend.DestBlend = BlendMap.at(blend_mode_nn.color_dst);
		blend.BlendOp = BlendOpMap.at(blend_mode_nn.color_func);

		blend.SrcBlendAlpha = BlendMap.at(blend_mode_nn.alpha_src);
		blend.DestBlendAlpha = BlendMap.at(blend_mode_nn.alpha_dst);
		blend.BlendOpAlpha = BlendOpMap.at(blend_mode_nn.alpha_func);
	}

	auto rasterizer_state = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	rasterizer_state.CullMode = CullMap.at(pipeline_state.rasterizer_state.cull_mode);
	rasterizer_state.FrontCounterClockwise = pipeline_state.rasterizer_state.front_face == FrontFace::CounterClockwise;

	const static std::unordered_map<TopologyKind, D3D12_PRIMITIVE_TOPOLOGY_TYPE> TopologyTypeMap = {
		{ TopologyKind::Points, D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT },
		{ TopologyKind::Lines, D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE },
		{ TopologyKind::Triangles, D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE }
	};

	auto topology_type = TopologyTypeMap.at(pipeline_state.topology_kind);

	std::vector<D3D12_INPUT_ELEMENT_DESC> input_elements;

	for (size_t i = 0; i < pipeline_state.input_layouts.size(); i++)
	{
		const auto& input_layout = pipeline_state.input_layouts.at(i);

		for (const auto& [location, attribute] : input_layout.attributes)
		{
			static const std::unordered_map<InputLayout::Rate, D3D12_INPUT_CLASSIFICATION> InputRateMap = {
				{ InputLayout::Rate::Vertex, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA },
				{ InputLayout::Rate::Instance, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA },
			};

			input_elements.push_back(D3D12_INPUT_ELEMENT_DESC{
				.SemanticName = "TEXCOORD",
				.SemanticIndex = (UINT)location,
				.Format = VertexFormatMap.at(attribute.format),
				.InputSlot = (UINT)i,
				.AlignedByteOffset = (UINT)attribute.offset,
				.InputSlotClass = InputRateMap.at(input_layout.rate),
				.InstanceDataStepRate = (UINT)(input_layout.rate == InputLayout::Rate::Vertex ? 0 : 1)
			});
		}
	}

	D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc = {};
	pso_desc.VS = CD3DX12_SHADER_BYTECODE(pipeline_state.shader->getVertexShaderBlob().Get());
	pso_desc.PS = CD3DX12_SHADER_BYTECODE(pipeline_state.shader->getPixelShaderBlob().Get());
	pso_desc.InputLayout = { input_elements.data(), (UINT)input_elements.size() };
	pso_desc.NodeMask = 1;
	pso_desc.PrimitiveTopologyType = topology_type;
	pso_desc.pRootSignature = pipeline_state.shader->getRootSignature().Get();
	pso_desc.SampleMask = UINT_MAX;
	pso_desc.NumRenderTargets = (UINT)pipeline_state.color_attachment_formats.size();
	for (size_t i = 0; i < pipeline_state.color_attachment_formats.size(); i++)
	{
		pso_desc.RTVFormats[i] = pipeline_state.color_attachment_formats.at(i);
	}
	pso_desc.DSVFormat = pipeline_state.depth_stencil_format.value();
	pso_desc.SampleDesc.Count = 1;
	pso_desc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
	pso_desc.RasterizerState = rasterizer_state;
	pso_desc.DepthStencilState = depth_stencil_state;
	pso_desc.BlendState = blend_state;

	ComPtr<ID3D12PipelineState> result;
	gContext->device->CreateGraphicsPipelineState(&pso_desc, IID_PPV_ARGS(&result));

	return result;
}

static void EnsureViewport()
{
	if (!gContext->viewport_dirty)
		return;

	gContext->viewport_dirty = false;

	auto width = static_cast<float>(gContext->getBackbufferWidth());
	auto height = static_cast<float>(gContext->getBackbufferHeight());

	auto viewport = gContext->viewport.value_or(Viewport{ { 0.0f, 0.0f }, { width, height } });

	D3D12_VIEWPORT vp = {};
	vp.Width = viewport.size.x;
	vp.Height = viewport.size.y;
	vp.MinDepth = viewport.min_depth;
	vp.MaxDepth = viewport.max_depth;
	vp.TopLeftX = viewport.position.x;
	vp.TopLeftY = viewport.position.y;
	gContext->cmdlist->RSSetViewports(1, &vp);
}

static void EnsureScissor()
{
	if (!gContext->scissor_dirty)
		return;

	gContext->scissor_dirty = false;

	auto width = static_cast<float>(gContext->getBackbufferWidth());
	auto height = static_cast<float>(gContext->getBackbufferHeight());

	auto scissor = gContext->scissor.value_or(Scissor{ { 0.0f, 0.0f }, { width, height } });

	D3D12_RECT rect;
	rect.left = static_cast<LONG>(scissor.position.x);
	rect.top = static_cast<LONG>(scissor.position.y);
	rect.right = static_cast<LONG>(scissor.position.x + scissor.size.x);
	rect.bottom = static_cast<LONG>(scissor.position.y + scissor.size.y);
	gContext->cmdlist->RSSetScissorRects(1, &rect);
}

static void EnsureTopology()
{
	if (!gContext->topology_dirty)
		return;

	gContext->topology_dirty = false;
	
	const static std::unordered_map<Topology, D3D_PRIMITIVE_TOPOLOGY> TopologyMap = {
		{ Topology::PointList, D3D_PRIMITIVE_TOPOLOGY_POINTLIST },
		{ Topology::LineList, D3D_PRIMITIVE_TOPOLOGY_LINELIST },
		{ Topology::LineStrip, D3D_PRIMITIVE_TOPOLOGY_LINESTRIP },
		{ Topology::TriangleList, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST },
		{ Topology::TriangleStrip, D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP }
	};

	auto topology = TopologyMap.at(gContext->topology);
	gContext->cmdlist->IASetPrimitiveTopology(topology);
}

static void EnsureIndexBuffer()
{
	if (!gContext->index_buffer_dirty)
		return;

	gContext->index_buffer_dirty = false;

	D3D12_INDEX_BUFFER_VIEW buffer_view = {};
	buffer_view.BufferLocation = gContext->index_buffer->getD3D12Buffer()->GetGPUVirtualAddress();
	buffer_view.SizeInBytes = (UINT)gContext->index_buffer->getSize();
	buffer_view.Format = gContext->index_buffer->getStride() == 2 ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT;

	gContext->cmdlist->IASetIndexBuffer(&buffer_view);
}

static void EnsureVertexBuffer()
{
	if (!gContext->vertex_buffers_dirty)
		return;

	gContext->vertex_buffers_dirty = false;

	std::vector<D3D12_VERTEX_BUFFER_VIEW> buffer_views;

	for (auto vertex_buffer : gContext->vertex_buffers)
	{
		auto buffer_view = D3D12_VERTEX_BUFFER_VIEW{
			.BufferLocation = vertex_buffer->getD3D12Buffer()->GetGPUVirtualAddress(),
			.SizeInBytes = (UINT)vertex_buffer->getSize(),
			.StrideInBytes = (UINT)vertex_buffer->getStride()
		};

		buffer_views.push_back(buffer_view);
	}

	gContext->cmdlist->IASetVertexBuffers(0, (UINT)buffer_views.size(), buffer_views.data());
}

static void EnsureGraphicsState(bool draw_indexed)
{
	auto shader = gContext->pipeline_state.shader;
	assert(shader);

	if (!gContext->pipeline_states.contains(gContext->pipeline_state))
	{
		auto pipeline_state = CreateGraphicsPipelineState(gContext->pipeline_state);
		gContext->pipeline_states[gContext->pipeline_state] = pipeline_state;
	}

	auto targets = gContext->render_targets;
	std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> rtv_descriptors;

	if (targets.empty())
	{
		targets = { gContext->getCurrentFrame().main_render_target };
		rtv_descriptors = { gContext->getCurrentFrame().rtv_descriptor };
	}
	else
	{
		for (auto target : targets)
		{
			rtv_descriptors.push_back(target->getRtvHeap()->GetCPUDescriptorHandleForHeapStart());
		}
	}

	for (auto target : targets)
	{
		target->getTexture()->ensureState(gContext->cmdlist.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET);
	}

	auto dsv_descriptor = targets.at(0)->getDsvHeap()->GetCPUDescriptorHandleForHeapStart();

	gContext->cmdlist->OMSetRenderTargets((UINT)rtv_descriptors.size(), rtv_descriptors.data(),
		FALSE, &dsv_descriptor);

	auto pipeline_state = gContext->pipeline_states.at(gContext->pipeline_state).Get();

	gContext->cmdlist->SetPipelineState(pipeline_state);
	gContext->cmdlist->SetGraphicsRootSignature(shader->getRootSignature().Get());
	gContext->cmdlist->SetDescriptorHeaps(1, gContext->descriptor_heap.GetAddressOf());

	const auto& required_typed_descriptor_bindings = shader->getRequiredTypedDescriptorBindings();
	const auto& binding_to_root_index_map = shader->getBindingToRootIndexMap();

	for (const auto& [type, required_descriptor_bindings] : required_typed_descriptor_bindings)
	{
		for (const auto& [binding, descriptor] : required_descriptor_bindings)
		{
			auto root_index = binding_to_root_index_map.at(binding);
			if (type == ShaderReflection::DescriptorType::CombinedImageSampler)
			{
				const auto& texture = gContext->textures.at(binding);
				texture->ensureState(gContext->cmdlist.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
				gContext->cmdlist->SetGraphicsRootDescriptorTable(root_index, texture->getGpuDescriptorHandle());
			}
			else if (type == ShaderReflection::DescriptorType::UniformBuffer)
			{
				auto uniform_buffer = gContext->uniform_buffers.at(binding);
				gContext->cmdlist->SetGraphicsRootConstantBufferView(root_index, uniform_buffer->getD3D12Buffer()->GetGPUVirtualAddress());
			}
			else
			{
				assert(false);
			}
		}
	}

	EnsureViewport();
	EnsureScissor();
	EnsureTopology();

	if (draw_indexed)
		EnsureIndexBuffer();

	EnsureVertexBuffer();
}

static void Begin()
{
	BeginCommandList(gContext->cmd_alloc.Get(), gContext->cmdlist.Get());

	gContext->topology_dirty = true;
	gContext->viewport_dirty = true;
	gContext->scissor_dirty = true;
	gContext->index_buffer_dirty = true;
	gContext->vertex_buffers_dirty = true;
}

static void End()
{
	gContext->getCurrentFrame().backbuffer_texture->ensureState(gContext->cmdlist.Get(),
		D3D12_RESOURCE_STATE_PRESENT);

	EndCommandList(gContext->cmd_queue.Get(), gContext->cmdlist.Get(), false);
}

BackendD3D12::BackendD3D12(void* window, uint32_t width, uint32_t height, Adapter _adapter)
{
	gContext = new ContextD3D12();

#ifdef SKYGFX_D3D12_VALIDATION_ENABLED
	ComPtr<ID3D12Debug6> debug;
	D3D12GetDebugInterface(IID_PPV_ARGS(debug.GetAddressOf()));
	debug->EnableDebugLayer();
	debug->SetEnableAutoName(true);
	debug->SetEnableGPUBasedValidation(true);
	debug->SetEnableSynchronizedCommandQueueValidation(true);
	debug->SetForceLegacyBarrierValidation(true);
#endif

	ComPtr<IDXGIFactory6> dxgi_factory;
	CreateDXGIFactory1(IID_PPV_ARGS(dxgi_factory.GetAddressOf()));

	IDXGIAdapter1* adapter;
	auto gpu_preference = _adapter == Adapter::HighPerformance ? DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE : DXGI_GPU_PREFERENCE_MINIMUM_POWER;
	dxgi_factory->EnumAdapterByGpuPreference(0, gpu_preference, IID_PPV_ARGS(&adapter));

	D3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_12_1, IID_PPV_ARGS(gContext->device.GetAddressOf()));

#ifdef SKYGFX_D3D12_VALIDATION_ENABLED
	ComPtr<ID3D12InfoQueue> info_queue;
	gContext->device.As(&info_queue);
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
#endif

	D3D12_COMMAND_QUEUE_DESC queue_desc = {};
	queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
	queue_desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
	queue_desc.NodeMask = 1;
	gContext->device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(gContext->cmd_queue.GetAddressOf()));
	
	gContext->device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
		IID_PPV_ARGS(gContext->cmd_alloc.GetAddressOf()));

	gContext->device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, gContext->cmd_alloc.Get(),
		NULL, IID_PPV_ARGS(gContext->cmdlist.GetAddressOf()));
	
	gContext->cmdlist->Close();
	
	D3D12_DESCRIPTOR_HEAP_DESC heap_desc = {};
	heap_desc.NumDescriptors = 1000; // TODO: make more dynamic
	heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	gContext->device->CreateDescriptorHeap(&heap_desc, IID_PPV_ARGS(gContext->descriptor_heap.GetAddressOf()));

	gContext->descriptor_handle_increment_size = gContext->device->GetDescriptorHandleIncrementSize(heap_desc.Type);

	gContext->descriptor_heap_cpu_handle = gContext->descriptor_heap->GetCPUDescriptorHandleForHeapStart();
	gContext->descriptor_heap_gpu_handle = gContext->descriptor_heap->GetGPUDescriptorHandleForHeapStart();

	DXGI_SWAP_CHAIN_DESC1 swapchain_desc = {};
	swapchain_desc.BufferCount = NUM_BACK_BUFFERS;
	swapchain_desc.Width = width;
	swapchain_desc.Height = height;
	swapchain_desc.Format = MainRenderTargetColorAttachmentFormat;
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
	dxgi_factory->CreateSwapChainForHwnd(gContext->cmd_queue.Get(), (HWND)window,
		&swapchain_desc, &fs_swapchain_desc, NULL, swapchain.GetAddressOf());

	swapchain.As(&gContext->swapchain);

	gContext->device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(gContext->fence.GetAddressOf()));
	gContext->fence_value = 1;
	gContext->fence_event = CreateEvent(NULL, FALSE, FALSE, NULL);
	assert(gContext->fence_event != NULL);

	gContext->pipeline_state.color_attachment_formats = { MainRenderTargetColorAttachmentFormat };
	gContext->pipeline_state.depth_stencil_format = MainRenderTargetDepthStencilAttachmentFormat;

	CreateMainRenderTarget(width, height);
	Begin();
}

BackendD3D12::~BackendD3D12()
{
	End();
	DestroyMainRenderTarget();
	WaitForGpu();

	delete gContext;
	gContext = nullptr;
}

void BackendD3D12::resize(uint32_t width, uint32_t height)
{
	End();
	DestroyMainRenderTarget();
	WaitForGpu();
	gContext->swapchain->ResizeBuffers(NUM_BACK_BUFFERS, (UINT)width, (UINT)height, MainRenderTargetColorAttachmentFormat, 0);
	CreateMainRenderTarget(width, height);
	Begin();

	if (!gContext->viewport.has_value())
		gContext->viewport_dirty = true;
	
	if (!gContext->scissor.has_value())
		gContext->scissor_dirty = true;
}

void BackendD3D12::setVsync(bool value)
{
	// TODO: implement
}

void BackendD3D12::setTopology(Topology topology)
{
	gContext->topology = topology;
	gContext->topology_dirty = true;
	gContext->pipeline_state.topology_kind = GetTopologyKind(topology);
}

void BackendD3D12::setViewport(std::optional<Viewport> viewport)
{
	gContext->viewport = viewport;
	gContext->viewport_dirty = true;
}

void BackendD3D12::setScissor(std::optional<Scissor> scissor)
{
	gContext->scissor = scissor;
	gContext->scissor_dirty = true;
}

void BackendD3D12::setTexture(uint32_t binding, TextureHandle* handle)
{
	gContext->textures[binding] = (TextureD3D12*)handle;
}

void BackendD3D12::setRenderTarget(const RenderTarget** render_target, size_t count)
{
	std::vector<RenderTargetD3D12*> render_targets;
	std::vector<DXGI_FORMAT> color_attachment_formats;
	std::optional<DXGI_FORMAT> depth_stencil_format;

	if (count == 0)
	{
		color_attachment_formats = { MainRenderTargetColorAttachmentFormat };
		depth_stencil_format = MainRenderTargetDepthStencilAttachmentFormat;
	}
	else
	{
		for (size_t i = 0; i < count; i++)
		{
			auto target = (RenderTargetD3D12*)(RenderTargetHandle*)*(RenderTarget*)render_target[i];

			render_targets.push_back(target);
			color_attachment_formats.push_back(PixelFormatMap.at(target->getTexture()->getFormat()));

			if (!depth_stencil_format.has_value())
				depth_stencil_format = target->getDepthStencilFormat();
		}
	}

	gContext->pipeline_state.color_attachment_formats = color_attachment_formats;
	gContext->pipeline_state.depth_stencil_format = depth_stencil_format;
	gContext->render_targets = render_targets;

	if (!gContext->viewport.has_value())
		gContext->viewport_dirty = true;

	if (!gContext->scissor.has_value())
		gContext->scissor_dirty = true;
}

void BackendD3D12::setShader(ShaderHandle* handle)
{
	gContext->pipeline_state.shader = (ShaderD3D12*)handle;
}

void BackendD3D12::setInputLayout(const std::vector<InputLayout>& value)
{
	gContext->pipeline_state.input_layouts = value;
}

void BackendD3D12::setVertexBuffer(const VertexBuffer** vertex_buffer, size_t count)
{
	gContext->vertex_buffers.clear();
	for (size_t i = 0; i < count; i++)
	{
		auto buffer = (VertexBufferD3D12*)(VertexBufferHandle*)*(VertexBuffer*)vertex_buffer[i];
		gContext->vertex_buffers.push_back(buffer);
	}
	gContext->vertex_buffers_dirty = true;
}

void BackendD3D12::setIndexBuffer(IndexBufferHandle* handle)
{
	gContext->index_buffer = (IndexBufferD3D12*)handle;
	gContext->index_buffer_dirty = true;
}

void BackendD3D12::setUniformBuffer(uint32_t binding, UniformBufferHandle* handle)
{
	gContext->uniform_buffers[binding] = (UniformBufferD3D12*)handle;
}

void BackendD3D12::setBlendMode(const std::optional<BlendMode>& blend_mode)
{
	gContext->pipeline_state.blend_mode = blend_mode;
}

void BackendD3D12::setDepthMode(const std::optional<DepthMode>& depth_mode)
{
	gContext->pipeline_state.depth_mode = depth_mode;
}

void BackendD3D12::setStencilMode(const std::optional<StencilMode>& stencil_mode)
{
}

void BackendD3D12::setCullMode(CullMode cull_mode)
{
	gContext->pipeline_state.rasterizer_state.cull_mode = cull_mode;;
}

void BackendD3D12::setSampler(Sampler value)
{
}

void BackendD3D12::setTextureAddress(TextureAddress value)
{
}

void BackendD3D12::setFrontFace(FrontFace value)
{
}

void BackendD3D12::setDepthBias(const std::optional<DepthBias> depth_bias)
{
}

void BackendD3D12::clear(const std::optional<glm::vec4>& color, const std::optional<float>& depth,
	const std::optional<uint8_t>& stencil)
{
	auto targets = gContext->render_targets;
	std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> rtv_descriptors;

	if (targets.empty())
	{
		targets = { gContext->getCurrentFrame().main_render_target };
		rtv_descriptors = { gContext->getCurrentFrame().rtv_descriptor };
	}
	else
	{
		for (auto target : targets)
		{
			rtv_descriptors.push_back(target->getRtvHeap()->GetCPUDescriptorHandleForHeapStart());
		}
	}

	for (auto target : targets)
	{
		target->getTexture()->ensureState(gContext->cmdlist.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET);
	}

	auto dsv_descriptor = targets.at(0)->getDsvHeap()->GetCPUDescriptorHandleForHeapStart();

	if (color.has_value())
	{
		for (auto rtv_descriptor : rtv_descriptors)
		{
			gContext->cmdlist->ClearRenderTargetView(rtv_descriptor, (float*)&color.value(), 0, NULL);
		}		
	}

	if (depth.has_value() || stencil.has_value())
	{
		D3D12_CLEAR_FLAGS flags = {};

		if (depth.has_value())
			flags |= D3D12_CLEAR_FLAG_DEPTH;

		if (stencil.has_value())
			flags |= D3D12_CLEAR_FLAG_STENCIL;

		gContext->cmdlist->ClearDepthStencilView(dsv_descriptor, flags, depth.value_or(1.0f),
			stencil.value_or(0), 0, NULL);
	}
}

void BackendD3D12::draw(uint32_t vertex_count, uint32_t vertex_offset, uint32_t instance_count)
{
	EnsureGraphicsState(false);
	gContext->cmdlist->DrawInstanced((UINT)vertex_count, (UINT)instance_count, (UINT)vertex_offset, 0);
}

void BackendD3D12::drawIndexed(uint32_t index_count, uint32_t index_offset, uint32_t instance_count)
{
	EnsureGraphicsState(true);
	gContext->cmdlist->DrawIndexedInstanced((UINT)index_count, (UINT)instance_count, (UINT)index_offset, 0, 0);
}

void BackendD3D12::readPixels(const glm::i32vec2& pos, const glm::i32vec2& size, TextureHandle* dst_texture_handle)
{
	auto dst_texture = (TextureD3D12*)dst_texture_handle;
	auto format = gContext->getBackbufferFormat();

	assert(dst_texture->getWidth() == size.x);
	assert(dst_texture->getHeight() == size.y);
	assert(dst_texture->getFormat() == format);

	if (size.x <= 0 || size.y <= 0)
		return;

	auto src_texture = !gContext->render_targets.empty() ?
		gContext->render_targets.at(0)->getTexture() :
		gContext->getCurrentFrame().main_render_target->getTexture();

	auto desc = src_texture->getD3D12Texture()->GetDesc();

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

	src_texture->ensureState(gContext->cmdlist.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE);
	dst_texture->ensureState(gContext->cmdlist.Get(), D3D12_RESOURCE_STATE_COPY_DEST);

	auto src_location = CD3DX12_TEXTURE_COPY_LOCATION(src_texture->getD3D12Texture().Get(), 0);
	auto dst_location = CD3DX12_TEXTURE_COPY_LOCATION(dst_texture->getD3D12Texture().Get(), 0);

	gContext->cmdlist->CopyTextureRegion(&dst_location, dst_x, dst_y, 0, &src_location, &box);
}

void BackendD3D12::present()
{
	End();
	bool vsync = false;
	gContext->swapchain->Present(vsync ? 1 : 0, 0);
	gContext->frame_index = gContext->swapchain->GetCurrentBackBufferIndex();
	WaitForGpu();
	Begin();
}

TextureHandle* BackendD3D12::createTexture(uint32_t width, uint32_t height, PixelFormat format,
	uint32_t mip_count)
{
	auto texture = new TextureD3D12(width, height, format, mip_count);
	return (TextureHandle*)texture;
}

void BackendD3D12::writeTexturePixels(TextureHandle* handle, uint32_t width, uint32_t height, PixelFormat format,
	const void* memory, uint32_t mip_level, uint32_t offset_x, uint32_t offset_y)
{
	auto texture = (TextureD3D12*)handle;
	texture->write(width, height, format, memory, mip_level, offset_x, offset_y);
}

void BackendD3D12::readTexturePixels(TextureHandle* handle, uint32_t pos_x, uint32_t pos_y, uint32_t width,
	uint32_t height, uint32_t mip_level, void* dst_memory)
{
	auto texture = (TextureD3D12*)handle;
	texture->read(pos_x, pos_y, width, height, mip_level, dst_memory);
}

void BackendD3D12::generateMips(TextureHandle* handle)
{
	auto texture = (TextureD3D12*)handle;
	texture->generateMips();
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

ShaderHandle* BackendD3D12::createShader(const std::string& vertex_code,
	const std::string& fragment_code, const std::vector<std::string>& defines)
{
	auto shader = new ShaderD3D12(vertex_code, fragment_code, defines);
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
	auto buffer = (VertexBufferD3D12*)handle;
	delete buffer;
}

void BackendD3D12::writeVertexBufferMemory(VertexBufferHandle* handle, const void* memory, size_t size, size_t stride)
{
	auto buffer = (VertexBufferD3D12*)handle;
	buffer->write(memory, size);
	buffer->setStride(stride);

	for (auto vertex_buffer : gContext->vertex_buffers)
	{
		if (vertex_buffer != buffer)
			continue;

		gContext->vertex_buffers_dirty = true;
		break;
	}
}

IndexBufferHandle* BackendD3D12::createIndexBuffer(size_t size, size_t stride)
{
	auto buffer = new IndexBufferD3D12(size, stride);
	return (IndexBufferHandle*)buffer;
}

void BackendD3D12::writeIndexBufferMemory(IndexBufferHandle* handle, const void* memory, size_t size, size_t stride)
{
	auto buffer = (IndexBufferD3D12*)handle;
	buffer->write(memory, size);
	buffer->setStride(stride);

	if (gContext->index_buffer == buffer)
		gContext->index_buffer_dirty = true;
}

void BackendD3D12::destroyIndexBuffer(IndexBufferHandle* handle)
{
	auto buffer = (IndexBufferD3D12*)handle;
	delete buffer;
}

UniformBufferHandle* BackendD3D12::createUniformBuffer(size_t size)
{
	auto buffer = new UniformBufferD3D12(size);
	return (UniformBufferHandle*)buffer;
}

void BackendD3D12::destroyUniformBuffer(UniformBufferHandle* handle)
{
	auto buffer = (UniformBufferD3D12*)handle;
	delete buffer;
}

void BackendD3D12::writeUniformBufferMemory(UniformBufferHandle* handle, const void* memory, size_t size)
{
	auto buffer = (UniformBufferD3D12*)handle;
	buffer->write(memory, size);
}

#endif
