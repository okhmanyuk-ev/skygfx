#include "backend_d3d11.h"

#ifdef SKYGFX_HAS_D3D11

#include <stdexcept>
#include <vector>
#include <unordered_map>

#include <d3dcompiler.h>
#include <d3d11.h>

#pragma comment(lib, "d3d11")
#pragma comment(lib, "d3dcompiler")

using namespace skygfx;

struct DepthStencilState
{
	std::optional<DepthMode> depth_mode;
	std::optional<StencilMode> stencil_mode;

	bool operator==(const DepthStencilState& value) const
	{
		return depth_mode == value.depth_mode && stencil_mode == value.stencil_mode;
	}
};

SKYGFX_MAKE_HASHABLE(DepthStencilState,
	t.depth_mode,
	t.stencil_mode);

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

struct SamplerState
{
	Sampler sampler = Sampler::Linear;
	TextureAddress textureAddress = TextureAddress::Clamp;

	bool operator==(const SamplerState& value) const
	{
		return
			sampler == value.sampler &&
			textureAddress == value.textureAddress;
	}
};

SKYGFX_MAKE_HASHABLE(SamplerState,
	t.sampler,
	t.textureAddress);

class RenderTargetD3D11;

static IDXGISwapChain* gSwapChain = nullptr;
static ID3D11Device* gDevice = nullptr;
static ID3D11DeviceContext* gContext = nullptr;
static std::unordered_map<BlendMode, ID3D11BlendState*> gBlendModes;
static DepthStencilState gDepthStencilState;
static bool gDepthStencilStateDirty = true;
static std::unordered_map<DepthStencilState, ID3D11DepthStencilState*> gDepthStencilStates;
static std::unordered_map<RasterizerState, ID3D11RasterizerState*> gRasterizerStates;
static RasterizerState gRasterizerState;
static bool gRasterizerStateDirty = true;
static std::unordered_map<SamplerState, ID3D11SamplerState*> gSamplerStates;
static SamplerState gSamplerState;
static bool gSamplerStateDirty = true;
static RenderTargetD3D11* gCurrentRenderTarget = nullptr;

static struct
{
	ID3D11Texture2D* texture2d;
	ID3D11RenderTargetView* render_taget_view;
	ID3D11DepthStencilView* depth_stencil_view;
} gMainRenderTarget;

template <typename T>
inline void SafeRelease(T& a) // TODO: rename to safe release
{
	if (!a)
		return;

	a->Release();

	if constexpr (!std::is_const<T>())
		a = nullptr;
}

class ShaderD3D11
{
private:
	ID3D11VertexShader* vertex_shader = nullptr;
	ID3D11PixelShader* pixel_shader = nullptr;
	ID3D11InputLayout* input_layout = nullptr;

public:
	ShaderD3D11(const Vertex::Layout& layout, const std::string& vertex_code, const std::string& fragment_code,
		std::vector<std::string> defines)
	{
		ID3DBlob* vertexShaderBlob;
		ID3DBlob* pixelShaderBlob;

		ID3DBlob* vertex_shader_error;
		ID3DBlob* pixel_shader_error;
		
		AddShaderLocationDefines(layout, defines);

		auto vertex_shader_spirv = CompileGlslToSpirv(ShaderStage::Vertex, vertex_code, defines);
		auto fragment_shader_spirv = CompileGlslToSpirv(ShaderStage::Fragment, fragment_code, defines);

		auto hlsl_vert = CompileSpirvToHlsl(vertex_shader_spirv, HlslVersion::v4_0);
		auto hlsl_frag = CompileSpirvToHlsl(fragment_shader_spirv, HlslVersion::v4_0);

		D3DCompile(hlsl_vert.c_str(), hlsl_vert.size(), NULL, NULL, NULL, "main", "vs_4_0", 0, 0, &vertexShaderBlob, &vertex_shader_error);
		D3DCompile(hlsl_frag.c_str(), hlsl_frag.size(), NULL, NULL, NULL, "main", "ps_4_0", 0, 0, &pixelShaderBlob, &pixel_shader_error);

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

		gDevice->CreateVertexShader(vertexShaderBlob->GetBufferPointer(), vertexShaderBlob->GetBufferSize(), nullptr, &vertex_shader);
		gDevice->CreatePixelShader(pixelShaderBlob->GetBufferPointer(), pixelShaderBlob->GetBufferSize(), nullptr, &pixel_shader);

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

		std::vector<D3D11_INPUT_ELEMENT_DESC> input;

		UINT i = 0;

		for (auto& attrib : layout.attributes)
		{
			input.push_back({ "TEXCOORD", i, Format.at(attrib.format), 0,
				static_cast<UINT>(attrib.offset), D3D11_INPUT_PER_VERTEX_DATA, 0 });
			i++;
		}

		gDevice->CreateInputLayout(input.data(), static_cast<UINT>(input.size()), vertexShaderBlob->GetBufferPointer(), vertexShaderBlob->GetBufferSize(), &input_layout);
	}

	~ShaderD3D11()
	{
		SafeRelease(vertex_shader);
		SafeRelease(pixel_shader);
		SafeRelease(input_layout);
	}

	void apply()
	{
		gContext->IASetInputLayout(input_layout);
		gContext->VSSetShader(vertex_shader, nullptr, 0);
		gContext->PSSetShader(pixel_shader, nullptr, 0);
	}
};

class TextureD3D11
{
	friend class RenderTargetD3D11;
	friend class BackendD3D11;

private:
	ID3D11Texture2D* texture2d;
	ID3D11ShaderResourceView* shader_resource_view;
	uint32_t width;
	uint32_t height;
	bool mipmap;

public:
	TextureD3D11(uint32_t _width, uint32_t _height, uint32_t channels, void* memory, bool _mipmap) :
		width(_width),
		height(_height),
		mipmap(_mipmap)
	{
		D3D11_TEXTURE2D_DESC texture2d_desc = { };
		texture2d_desc.Width = width;
		texture2d_desc.Height = height;
		texture2d_desc.MipLevels = mipmap ? 0 : 1;
		texture2d_desc.ArraySize = 1;
		texture2d_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		texture2d_desc.SampleDesc.Count = 1;
		texture2d_desc.SampleDesc.Quality = 0;
		texture2d_desc.Usage = D3D11_USAGE_DEFAULT;
		texture2d_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
		texture2d_desc.CPUAccessFlags = 0;
		texture2d_desc.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS; // TODO: only in mapmap mode ?
		gDevice->CreateTexture2D(&texture2d_desc, nullptr, &texture2d);

		D3D11_SHADER_RESOURCE_VIEW_DESC shader_resource_view_desc = { };
		shader_resource_view_desc.Format = texture2d_desc.Format;
		shader_resource_view_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		shader_resource_view_desc.Texture2D.MipLevels = -1;
		shader_resource_view_desc.Texture2D.MostDetailedMip = 0;
		gDevice->CreateShaderResourceView(texture2d, &shader_resource_view_desc, &shader_resource_view);

		if (memory)
		{
			auto memPitch = width * channels;
			auto memSlicePitch = width * height * channels;
			gContext->UpdateSubresource(texture2d, 0, nullptr, memory, memPitch, memSlicePitch);

			if (mipmap)
				gContext->GenerateMips(shader_resource_view);
		}
	}

	~TextureD3D11()
	{
		SafeRelease(shader_resource_view);
		SafeRelease(texture2d);
	}

	void bind(uint32_t binding)
	{
		gContext->PSSetShaderResources((UINT)binding, 1, &shader_resource_view);
	}
};

class RenderTargetD3D11
{
	friend class BackendD3D11;

private:
	ID3D11RenderTargetView* render_target_view;
	ID3D11Texture2D* depth_stencil_texture;
	ID3D11DepthStencilView* depth_stencil_view;
	TextureD3D11* texture_data;
	uint32_t width;
	uint32_t height;

public:
	RenderTargetD3D11(uint32_t _width, uint32_t _height, TextureD3D11* _texture_data) :
		texture_data(_texture_data), width(_width), height(_height)
	{
		D3D11_RENDER_TARGET_VIEW_DESC render_target_view_desc = { };
		render_target_view_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		render_target_view_desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
		render_target_view_desc.Texture2D.MipSlice = 0;
		gDevice->CreateRenderTargetView(texture_data->texture2d, &render_target_view_desc, &render_target_view);

		D3D11_TEXTURE2D_DESC texture2d_desc = { };
		texture2d_desc.Width = width;
		texture2d_desc.Height = height;
		texture2d_desc.MipLevels = 1;
		texture2d_desc.ArraySize = 1;
		texture2d_desc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
		texture2d_desc.SampleDesc.Count = 1;
		texture2d_desc.SampleDesc.Quality = 0;
		texture2d_desc.Usage = D3D11_USAGE_DEFAULT;
		texture2d_desc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
		texture2d_desc.CPUAccessFlags = 0;
		texture2d_desc.MiscFlags = 0;
		gDevice->CreateTexture2D(&texture2d_desc, nullptr, &depth_stencil_texture);

		D3D11_DEPTH_STENCIL_VIEW_DESC depth_stencil_view_desc = {};
		depth_stencil_view_desc.Format = texture2d_desc.Format;
		depth_stencil_view_desc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
		gDevice->CreateDepthStencilView(depth_stencil_texture, &depth_stencil_view_desc, &depth_stencil_view);
	}

	~RenderTargetD3D11()
	{
		SafeRelease(render_target_view);
		SafeRelease(depth_stencil_texture);
		SafeRelease(depth_stencil_view);
	}
};

class BufferD3D11
{
	friend class BackendD3D11;

private:
	ID3D11Buffer* buffer;
	size_t size;

public:
	BufferD3D11(void* memory, size_t _size, D3D11_BIND_FLAG bind_flags) : size(_size)
	{
		D3D11_BUFFER_DESC desc = {};
		desc.ByteWidth = static_cast<UINT>(size);
		desc.Usage = D3D11_USAGE_DYNAMIC;
		desc.BindFlags = bind_flags;
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		gDevice->CreateBuffer(&desc, nullptr, &buffer);

		write(memory, size);
	}

	~BufferD3D11()
	{
		SafeRelease(buffer);
	}

	void write(void* memory, size_t _size)
	{
		assert(_size <= size);
		D3D11_MAPPED_SUBRESOURCE resource;
		gContext->Map(buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &resource);
		memcpy(resource.pData, memory, _size);
		gContext->Unmap(buffer, 0);
	}
};

class VertexBufferD3D11 : public BufferD3D11
{
	friend class BackendD3D11;

private:
	size_t stride;

public:
	VertexBufferD3D11(void* memory, size_t size, size_t _stride) :
		BufferD3D11(memory, size, D3D11_BIND_VERTEX_BUFFER),
		stride(_stride)
	{
	}
};

class IndexBufferD3D11 : public BufferD3D11
{
	friend class BackendD3D11;

private:
	size_t stride;

public:
	IndexBufferD3D11(void* memory, size_t size, size_t _stride) :
		BufferD3D11(memory, size, D3D11_BIND_INDEX_BUFFER),
		stride(_stride)
	{
	}
};

class UniformBufferD3D11 : public BufferD3D11
{
	friend class BackendD3D11;

public:
	UniformBufferD3D11(void* memory, size_t size) :
		BufferD3D11(memory, size, D3D11_BIND_CONSTANT_BUFFER)
	{
		assert(size % 16 == 0);
	}
};

BackendD3D11::BackendD3D11(void* window, uint32_t width, uint32_t height)
{
	DXGI_SWAP_CHAIN_DESC sd = {};
	sd.BufferCount = 2;
	sd.BufferDesc.Width = 0;
	sd.BufferDesc.Height = 0;
	sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	sd.BufferDesc.RefreshRate.Numerator = 60;
	sd.BufferDesc.RefreshRate.Denominator = 5;
	sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
	sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	sd.OutputWindow = (HWND)window;
	sd.SampleDesc.Count = 1;
	sd.SampleDesc.Quality = 0;
	sd.Windowed = TRUE; // TODO: make false when fullscreen ?		
	sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

	std::vector<D3D_FEATURE_LEVEL> features = { D3D_FEATURE_LEVEL_11_0, };
	UINT flags = 0;// D3D11_CREATE_DEVICE_DEBUG | D3D11_CREATE_DEVICE_SINGLETHREADED;

	D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, features.data(),
		static_cast<UINT>(features.size()), D3D11_SDK_VERSION, &sd, &gSwapChain, &gDevice,
		nullptr, &gContext);

	createMainRenderTarget(width, height);
	setRenderTarget(nullptr);
}

BackendD3D11::~BackendD3D11()
{
	destroyMainRenderTarget();

	SafeRelease(gSwapChain);
	SafeRelease(gContext);
	SafeRelease(gDevice);

	for (const auto& [_, blend_state] : gBlendModes)
	{
		SafeRelease(blend_state);
	}
}

void BackendD3D11::resize(uint32_t width, uint32_t height)
{
	destroyMainRenderTarget();
	gSwapChain->ResizeBuffers(0, (UINT)width, (UINT)height, DXGI_FORMAT_R8G8B8A8_UNORM, 0);
	createMainRenderTarget(width, height);
	setRenderTarget(nullptr); // TODO: do it when nullptr was before

	if (!mViewport.has_value())
		mViewportDirty = true;
}

void BackendD3D11::setTopology(Topology topology)
{
	const static std::unordered_map<Topology, D3D11_PRIMITIVE_TOPOLOGY> TopologyMap = {
		{ Topology::PointList, D3D11_PRIMITIVE_TOPOLOGY_POINTLIST },
		{ Topology::LineList, D3D11_PRIMITIVE_TOPOLOGY_LINELIST },
		{ Topology::LineStrip, D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP },
		{ Topology::TriangleList, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST },
		{ Topology::TriangleStrip, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP }
	};

	gContext->IASetPrimitiveTopology(TopologyMap.at(topology));
}

void BackendD3D11::setViewport(std::optional<Viewport> viewport)
{
	if (mViewport != viewport)
		mViewportDirty = true;

	mViewport = viewport;
}

void BackendD3D11::setScissor(std::optional<Scissor> scissor)
{
	if (scissor.has_value())
	{
		auto value = scissor.value();

		gRasterizerState.scissor_enabled = true;

		D3D11_RECT rect;
		rect.left = static_cast<LONG>(value.position.x);
		rect.top = static_cast<LONG>(value.position.y);
		rect.right = static_cast<LONG>(value.position.x + value.size.x);
		rect.bottom = static_cast<LONG>(value.position.y + value.size.y);
		gContext->RSSetScissorRects(1, &rect);

		gRasterizerStateDirty = true;
	}
	else
	{
		gRasterizerState.scissor_enabled = false;
		gRasterizerStateDirty = true;
	}
}

void BackendD3D11::setTexture(uint32_t binding, TextureHandle* handle)
{
	auto texture = (TextureD3D11*)handle;
	texture->bind(binding);
	mCurrentTextures[binding] = handle;
}

void BackendD3D11::setRenderTarget(RenderTargetHandle* handle)
{
	auto render_target = (RenderTargetD3D11*)handle;

	ID3D11ShaderResourceView* prev_shader_resource_view;
	gContext->PSGetShaderResources(0, 1, &prev_shader_resource_view);

	if (prev_shader_resource_view == render_target->texture_data->shader_resource_view)
	{
		ID3D11ShaderResourceView* null[] = { nullptr };
		gContext->PSSetShaderResources(0, 1, null); // remove old shader view
	}

	SafeRelease(prev_shader_resource_view); // avoid memory leak

	gContext->OMSetRenderTargets(1, &render_target->render_target_view, render_target->depth_stencil_view);

	gCurrentRenderTarget = render_target;
	
	if (!mViewport.has_value())
		mViewportDirty = true;
}

void BackendD3D11::setRenderTarget(std::nullptr_t value)
{
	gContext->OMSetRenderTargets(1, &gMainRenderTarget.render_taget_view, gMainRenderTarget.depth_stencil_view);
	gCurrentRenderTarget = nullptr;

	if (!mViewport.has_value())
		mViewportDirty = true;
}

void BackendD3D11::setShader(ShaderHandle* handle)
{
	auto shader = (ShaderD3D11*)handle;
	shader->apply();
}

void BackendD3D11::setVertexBuffer(VertexBufferHandle* handle)
{
	auto buffer = (VertexBufferD3D11*)handle;

	auto stride = static_cast<UINT>(buffer->stride);
	auto offset = static_cast<UINT>(0);

	gContext->IASetVertexBuffers(0, 1, &buffer->buffer, &stride, &offset);
}

void BackendD3D11::setIndexBuffer(IndexBufferHandle* handle)
{
	auto buffer = (IndexBufferD3D11*)handle;

	auto stride = static_cast<UINT>(buffer->stride);

	gContext->IASetIndexBuffer(buffer->buffer, buffer->stride == 2 ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT, 0);
}

void BackendD3D11::setUniformBuffer(uint32_t binding, UniformBufferHandle* handle)
{
	auto buffer = (UniformBufferD3D11*)handle;

	gContext->VSSetConstantBuffers(binding, 1, &buffer->buffer);
	gContext->PSSetConstantBuffers(binding, 1, &buffer->buffer);
}

void BackendD3D11::setBlendMode(const BlendMode& value)
{
	if (gBlendModes.count(value) == 0)
	{
		const static std::unordered_map<Blend, D3D11_BLEND> BlendMap = {
			{ Blend::One, D3D11_BLEND_ONE },
			{ Blend::Zero, D3D11_BLEND_ZERO },
			{ Blend::SrcColor, D3D11_BLEND_SRC_COLOR },
			{ Blend::InvSrcColor, D3D11_BLEND_INV_SRC_COLOR },
			{ Blend::SrcAlpha, D3D11_BLEND_SRC_ALPHA },
			{ Blend::InvSrcAlpha, D3D11_BLEND_INV_SRC_ALPHA },
			{ Blend::DstColor, D3D11_BLEND_DEST_COLOR },
			{ Blend::InvDstColor, D3D11_BLEND_INV_DEST_COLOR },
			{ Blend::DstAlpha, D3D11_BLEND_DEST_ALPHA },
			{ Blend::InvDstAlpha, D3D11_BLEND_INV_DEST_ALPHA }
		};

		const static std::unordered_map<BlendFunction, D3D11_BLEND_OP> BlendOpMap = {
			{ BlendFunction::Add, D3D11_BLEND_OP_ADD },
			{ BlendFunction::Subtract, D3D11_BLEND_OP_SUBTRACT },
			{ BlendFunction::ReverseSubtract, D3D11_BLEND_OP_REV_SUBTRACT },
			{ BlendFunction::Min, D3D11_BLEND_OP_MIN },
			{ BlendFunction::Max, D3D11_BLEND_OP_MAX },
		};

		D3D11_BLEND_DESC desc = {};
		desc.AlphaToCoverageEnable = false;

		auto& blend = desc.RenderTarget[0];

		if (value.colorMask.red)
			blend.RenderTargetWriteMask |= D3D11_COLOR_WRITE_ENABLE_RED;

		if (value.colorMask.green)
			blend.RenderTargetWriteMask |= D3D11_COLOR_WRITE_ENABLE_GREEN;

		if (value.colorMask.blue)
			blend.RenderTargetWriteMask |= D3D11_COLOR_WRITE_ENABLE_BLUE;

		if (value.colorMask.alpha)
			blend.RenderTargetWriteMask |= D3D11_COLOR_WRITE_ENABLE_ALPHA;

		blend.BlendEnable = true;

		blend.SrcBlend = BlendMap.at(value.colorSrcBlend);
		blend.DestBlend = BlendMap.at(value.colorDstBlend);
		blend.BlendOp = BlendOpMap.at(value.colorBlendFunction);

		blend.SrcBlendAlpha = BlendMap.at(value.alphaSrcBlend);
		blend.DestBlendAlpha = BlendMap.at(value.alphaDstBlend);
		blend.BlendOpAlpha = BlendOpMap.at(value.alphaBlendFunction);

		gDevice->CreateBlendState(&desc, &gBlendModes[value]);
	}

	const float blend_factor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	gContext->OMSetBlendState(gBlendModes.at(value), blend_factor, 0xFFFFFFFF);
}

void BackendD3D11::setDepthMode(std::optional<DepthMode> depth_mode)
{
	gDepthStencilState.depth_mode = depth_mode;
	gDepthStencilStateDirty = true;
}

void BackendD3D11::setStencilMode(std::optional<StencilMode> stencil_mode)
{
	gDepthStencilState.stencil_mode = stencil_mode;
	gDepthStencilStateDirty = true;
}

void BackendD3D11::setCullMode(CullMode cull_mode)
{
	gRasterizerState.cull_mode = cull_mode;
	gRasterizerStateDirty = true;
}

void BackendD3D11::setSampler(Sampler value)
{
	gSamplerState.sampler = value;
	gSamplerStateDirty = true;
}

void BackendD3D11::setTextureAddress(TextureAddress value)
{
	gSamplerState.textureAddress = value;
	gSamplerStateDirty = true;
}

void BackendD3D11::clear(const std::optional<glm::vec4>& color, const std::optional<float>& depth,
	const std::optional<uint8_t>& stencil)
{
	auto rtv = gMainRenderTarget.render_taget_view;
	auto dsv = gMainRenderTarget.depth_stencil_view;

	if (gCurrentRenderTarget != nullptr)
	{
		rtv = gCurrentRenderTarget->render_target_view;
		dsv = gCurrentRenderTarget->depth_stencil_view;
	}

	if (color.has_value())
	{
		gContext->ClearRenderTargetView(rtv, (float*)&color.value());
	}

	if (depth.has_value() || stencil.has_value())
	{
		UINT flags = 0;

		if (depth.has_value())
			flags |= D3D11_CLEAR_DEPTH;

		if (stencil.has_value())
			flags |= D3D11_CLEAR_STENCIL;

		gContext->ClearDepthStencilView(dsv, flags, depth.value_or(1.0f), stencil.value_or(0));
	}
}

void BackendD3D11::draw(uint32_t vertex_count, uint32_t vertex_offset)
{
	prepareForDrawing();
	gContext->Draw((UINT)vertex_count, (UINT)vertex_offset);
}

void BackendD3D11::drawIndexed(uint32_t index_count, uint32_t index_offset)
{
	prepareForDrawing();
	gContext->DrawIndexed((UINT)index_count, (UINT)index_offset, 0);
}

void BackendD3D11::readPixels(const glm::ivec2& pos, const glm::ivec2& size, TextureHandle* dst_texture_handle)
{
	auto dst_texture = (TextureD3D11*)dst_texture_handle;

	assert(dst_texture->width == size.x);
	assert(dst_texture->height == size.y);

	if (size.x <= 0 || size.y <= 0)
		return;

	ID3D11Resource* resource = NULL;

	if (gCurrentRenderTarget)
		gCurrentRenderTarget->render_target_view->GetResource(&resource);
	else
		gMainRenderTarget.render_taget_view->GetResource(&resource);

	ID3D11Texture2D* texture = NULL;
	resource->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&texture);

	D3D11_TEXTURE2D_DESC desc = { 0 };
	texture->GetDesc(&desc);
	auto back_w = desc.Width;
	auto back_h = desc.Height;
	SafeRelease(texture);

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

	D3D11_BOX box;
	box.left = src_x;
	box.right = src_x + src_w;
	box.top = src_y;
	box.bottom = src_y + src_h;
	box.front = 0;
	box.back = 1;

	if (pos.y < (int)back_h && pos.x < (int)back_w)
	{
		gContext->CopySubresourceRegion(dst_texture->texture2d, 0, dst_x, dst_y, 0, resource, 0, &box);

		if (dst_texture->mipmap)
			gContext->GenerateMips(dst_texture->shader_resource_view);
	}

	SafeRelease(resource);
}

void BackendD3D11::present()
{
	bool vsync = false; // TODO: globalize this var
	gSwapChain->Present(vsync ? 1 : 0, 0);
}

TextureHandle* BackendD3D11::createTexture(uint32_t width, uint32_t height, uint32_t channels, void* memory, bool mipmap)
{
	auto texture = new TextureD3D11(width, height, channels, memory, mipmap);
	return (TextureHandle*)texture;
}

void BackendD3D11::destroyTexture(TextureHandle* handle)
{
	auto texture = (TextureD3D11*)handle;
	delete texture;
}

RenderTargetHandle* BackendD3D11::createRenderTarget(uint32_t width, uint32_t height, TextureHandle* texture_handle)
{
	auto texture = (TextureD3D11*)texture_handle;
	auto render_target = new RenderTargetD3D11(width, height, texture);
	return (RenderTargetHandle*)render_target;
}

void BackendD3D11::destroyRenderTarget(RenderTargetHandle* handle)
{
	auto render_target = (RenderTargetD3D11*)handle;
	delete render_target;
}

ShaderHandle* BackendD3D11::createShader(const Vertex::Layout& layout, const std::string& vertex_code, 
	const std::string& fragment_code, const std::vector<std::string>& defines)
{
	auto shader = new ShaderD3D11(layout, vertex_code, fragment_code, defines);
	return (ShaderHandle*)shader;
}

void BackendD3D11::destroyShader(ShaderHandle* handle)
{
	auto shader = (ShaderD3D11*)handle;
	delete shader;
}

VertexBufferHandle* BackendD3D11::createVertexBuffer(void* memory, size_t size, size_t stride)
{
	auto buffer = new VertexBufferD3D11(memory, size, stride);
	return (VertexBufferHandle*)buffer;
}

void BackendD3D11::destroyVertexBuffer(VertexBufferHandle* handle)
{
	auto buffer = (VertexBufferD3D11*)handle;
	delete buffer;
}

void BackendD3D11::writeVertexBufferMemory(VertexBufferHandle* handle, void* memory, size_t size, size_t stride)
{
	auto buffer = (VertexBufferD3D11*)handle;
	buffer->write(memory, size);
	buffer->stride = stride;
}

IndexBufferHandle* BackendD3D11::createIndexBuffer(void* memory, size_t size, size_t stride)
{
	auto buffer = new IndexBufferD3D11(memory, size, stride);
	return (IndexBufferHandle*)buffer;
}

void BackendD3D11::writeIndexBufferMemory(IndexBufferHandle* handle, void* memory, size_t size, size_t stride)
{
	auto buffer = (IndexBufferD3D11*)handle;
	buffer->write(memory, size);
	buffer->stride = stride;
}

void BackendD3D11::destroyIndexBuffer(IndexBufferHandle* handle)
{
	auto buffer = (IndexBufferD3D11*)handle;
	delete buffer;
}

UniformBufferHandle* BackendD3D11::createUniformBuffer(void* memory, size_t size)
{
	auto buffer = new UniformBufferD3D11(memory, size);
	return (UniformBufferHandle*)buffer;
}

void BackendD3D11::destroyUniformBuffer(UniformBufferHandle* handle)
{
	auto buffer = (UniformBufferD3D11*)handle;
	delete buffer;
}

void BackendD3D11::writeUniformBufferMemory(UniformBufferHandle* handle, void* memory, size_t size)
{
	auto buffer = (UniformBufferD3D11*)handle;
	buffer->write(memory, size);
}

void BackendD3D11::createMainRenderTarget(uint32_t width, uint32_t height)
{
	D3D11_TEXTURE2D_DESC desc = {};
	desc.Width = width;
	desc.Height = height;
	desc.MipLevels = 1;
	desc.ArraySize = 1;
	desc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
	desc.SampleDesc.Count = 1;
	desc.SampleDesc.Quality = 0;
	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
	desc.CPUAccessFlags = 0;
	desc.MiscFlags = 0;
	gDevice->CreateTexture2D(&desc, nullptr, &gMainRenderTarget.texture2d);

	D3D11_DEPTH_STENCIL_VIEW_DESC descDSV = {};
	descDSV.Format = desc.Format;
	descDSV.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;

	gDevice->CreateDepthStencilView(gMainRenderTarget.texture2d, &descDSV, &gMainRenderTarget.depth_stencil_view);

	ID3D11Texture2D* pBackBuffer;
	gSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (LPVOID*)&pBackBuffer);
	gDevice->CreateRenderTargetView(pBackBuffer, nullptr, &gMainRenderTarget.render_taget_view);
	SafeRelease(pBackBuffer);

	mBackbufferWidth = width;
	mBackbufferHeight = height;
}

void BackendD3D11::destroyMainRenderTarget()
{
	SafeRelease(gMainRenderTarget.render_taget_view);
	SafeRelease(gMainRenderTarget.depth_stencil_view);
	SafeRelease(gMainRenderTarget.texture2d);
}

void BackendD3D11::prepareForDrawing()
{
	// depthstencil state

	if (gDepthStencilStateDirty)
	{
		gDepthStencilStateDirty = false;

		const auto& depth_stencil_state = gDepthStencilState;

		auto depth_mode = depth_stencil_state.depth_mode.value_or(DepthMode());
		auto stencil_mode = depth_stencil_state.stencil_mode.value_or(StencilMode());

		if (gDepthStencilStates.count(depth_stencil_state) == 0)
		{
			const static std::unordered_map<ComparisonFunc, D3D11_COMPARISON_FUNC> ComparisonFuncMap = {
				{ ComparisonFunc::Always, D3D11_COMPARISON_ALWAYS },
				{ ComparisonFunc::Never, D3D11_COMPARISON_NEVER },
				{ ComparisonFunc::Less, D3D11_COMPARISON_LESS },
				{ ComparisonFunc::Equal, D3D11_COMPARISON_EQUAL },
				{ ComparisonFunc::NotEqual, D3D11_COMPARISON_NOT_EQUAL },
				{ ComparisonFunc::LessEqual, D3D11_COMPARISON_LESS_EQUAL },
				{ ComparisonFunc::Greater, D3D11_COMPARISON_GREATER },
				{ ComparisonFunc::GreaterEqual, D3D11_COMPARISON_GREATER_EQUAL }
			};

			const static std::unordered_map<StencilOp, D3D11_STENCIL_OP> StencilOpMap = {
				{ StencilOp::Keep, D3D11_STENCIL_OP_KEEP },
				{ StencilOp::Zero, D3D11_STENCIL_OP_ZERO },
				{ StencilOp::Replace, D3D11_STENCIL_OP_REPLACE },
				{ StencilOp::IncrementSaturation, D3D11_STENCIL_OP_INCR_SAT },
				{ StencilOp::DecrementSaturation, D3D11_STENCIL_OP_DECR_SAT },
				{ StencilOp::Invert, D3D11_STENCIL_OP_INVERT },
				{ StencilOp::Increment, D3D11_STENCIL_OP_INCR },
				{ StencilOp::Decrement, D3D11_STENCIL_OP_DECR },
			};

			D3D11_DEPTH_STENCIL_DESC desc = {};
			desc.DepthEnable = depth_stencil_state.depth_mode.has_value();
			desc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
			desc.DepthFunc = ComparisonFuncMap.at(depth_mode.func);

			desc.StencilEnable = depth_stencil_state.stencil_mode.has_value();
			desc.StencilReadMask = stencil_mode.read_mask;
			desc.StencilWriteMask = stencil_mode.write_mask;

			desc.FrontFace.StencilDepthFailOp = StencilOpMap.at(stencil_mode.depth_fail_op);
			desc.FrontFace.StencilFailOp = StencilOpMap.at(stencil_mode.fail_op);
			desc.FrontFace.StencilFunc = ComparisonFuncMap.at(stencil_mode.func);
			desc.FrontFace.StencilPassOp = StencilOpMap.at(stencil_mode.pass_op);

			desc.BackFace = desc.FrontFace;

			gDevice->CreateDepthStencilState(&desc, &gDepthStencilStates[depth_stencil_state]);
		}

		gContext->OMSetDepthStencilState(gDepthStencilStates.at(depth_stencil_state), stencil_mode.reference);
	}

	// rasterizer state

	if (gRasterizerStateDirty)
	{
		gRasterizerStateDirty = false;

		const auto& value = gRasterizerState;

		if (gRasterizerStates.count(value) == 0)
		{
			const static std::unordered_map<CullMode, D3D11_CULL_MODE> CullMap = {
				{ CullMode::None, D3D11_CULL_NONE },
				{ CullMode::Front, D3D11_CULL_FRONT },
				{ CullMode::Back, D3D11_CULL_BACK }
			};

			D3D11_RASTERIZER_DESC desc = {};
			desc.FillMode = D3D11_FILL_SOLID;
			desc.CullMode = CullMap.at(value.cull_mode);
			desc.ScissorEnable = value.scissor_enabled;
			desc.DepthClipEnable = true;
			gDevice->CreateRasterizerState(&desc, &gRasterizerStates[value]);
		}

		gContext->RSSetState(gRasterizerStates.at(value));
	}

	// sampler state

	if (gSamplerStateDirty)
	{
		gSamplerStateDirty = false;

		const auto& value = gSamplerState;

		if (gSamplerStates.count(value) == 0)
		{
			// TODO: see D3D11_ENCODE_BASIC_FILTER

			const static std::unordered_map<Sampler, D3D11_FILTER> SamplerMap = {
				{ Sampler::Linear, D3D11_FILTER_MIN_MAG_MIP_LINEAR  },
				{ Sampler::Nearest, D3D11_FILTER_MIN_MAG_MIP_POINT },
			};

			const static std::unordered_map<TextureAddress, D3D11_TEXTURE_ADDRESS_MODE> TextureAddressMap = {
				{ TextureAddress::Clamp, D3D11_TEXTURE_ADDRESS_CLAMP },
				{ TextureAddress::Wrap, D3D11_TEXTURE_ADDRESS_WRAP },
				{ TextureAddress::MirrorWrap, D3D11_TEXTURE_ADDRESS_MIRROR }
			};

			D3D11_SAMPLER_DESC desc = {};
			desc.Filter = SamplerMap.at(value.sampler);
			desc.AddressU = TextureAddressMap.at(value.textureAddress);
			desc.AddressV = TextureAddressMap.at(value.textureAddress);
			desc.AddressW = TextureAddressMap.at(value.textureAddress);
			desc.MaxAnisotropy = D3D11_MAX_MAXANISOTROPY;
			desc.MipLODBias = 0.0f;
			desc.ComparisonFunc = D3D11_COMPARISON_NEVER;
			desc.MinLOD = 0.0f;
			desc.MaxLOD = FLT_MAX;
			gDevice->CreateSamplerState(&desc, &gSamplerStates[value]);
		}

		for (auto [binding, texture_handle] : mCurrentTextures)
		{
			gContext->PSSetSamplers(binding, 1, &gSamplerStates.at(value));
		}
	}

	// viewport

	if (mViewportDirty)
	{
		float width;
		float height;

		if (gCurrentRenderTarget == nullptr)
		{
			width = static_cast<float>(mBackbufferWidth);
			height = static_cast<float>(mBackbufferHeight);
		}
		else
		{
			width = static_cast<float>(gCurrentRenderTarget->width);
			height = static_cast<float>(gCurrentRenderTarget->height);
		}

		auto viewport = mViewport.value_or(Viewport{ { 0.0f, 0.0f }, { width, height } });

		D3D11_VIEWPORT vp;
		vp.Width = viewport.size.x;
		vp.Height = viewport.size.y;
		vp.MinDepth = viewport.min_depth;
		vp.MaxDepth = viewport.max_depth;
		vp.TopLeftX = viewport.position.x;
		vp.TopLeftY = viewport.position.y;
		gContext->RSSetViewports(1, &vp);

		mViewportDirty = false;
	}
}

#endif