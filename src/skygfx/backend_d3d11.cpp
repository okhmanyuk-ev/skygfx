#include "backend_d3d11.h"

#ifdef SKYGFX_HAS_D3D11

#include "shader_compiler.h"
#include <stdexcept>
#include <vector>
#include <unordered_map>

#include <d3dcompiler.h>
#include <d3d11.h>

#pragma comment(lib, "d3d11")
#pragma comment(lib, "d3dcompiler")

#include <wrl.h>

using Microsoft::WRL::ComPtr;
using namespace skygfx;

struct DepthStencilStateD3D11
{
	std::optional<DepthMode> depth_mode;
	std::optional<StencilMode> stencil_mode;

	bool operator==(const DepthStencilStateD3D11& value) const
	{
		return 
			depth_mode == value.depth_mode && 
			stencil_mode == value.stencil_mode;
	}
};

SKYGFX_MAKE_HASHABLE(DepthStencilStateD3D11,
	t.depth_mode,
	t.stencil_mode);

struct RasterizerStateD3D11
{
	bool scissor_enabled = false;
	CullMode cull_mode = CullMode::None;

	bool operator==(const RasterizerStateD3D11& value) const
	{
		return 
			scissor_enabled == value.scissor_enabled && 
			cull_mode == value.cull_mode;
	}
};

SKYGFX_MAKE_HASHABLE(RasterizerStateD3D11,
	t.cull_mode,
	t.scissor_enabled);

struct SamplerStateD3D11
{
	Sampler sampler = Sampler::Linear;
	TextureAddress textureAddress = TextureAddress::Clamp;

	bool operator==(const SamplerStateD3D11& value) const
	{
		return
			sampler == value.sampler &&
			textureAddress == value.textureAddress;
	}
};

SKYGFX_MAKE_HASHABLE(SamplerStateD3D11,
	t.sampler,
	t.textureAddress);

class TextureD3D11;
class RenderTargetD3D11;

static ComPtr<IDXGISwapChain> gSwapChain;
static ComPtr<ID3D11Device> gDevice;
static ComPtr<ID3D11DeviceContext> gContext;
static std::unordered_map<BlendMode, ComPtr<ID3D11BlendState>> gBlendModes;
static DepthStencilStateD3D11 gDepthStencilState;
static bool gDepthStencilStateDirty = true;
static std::unordered_map<DepthStencilStateD3D11, ComPtr<ID3D11DepthStencilState>> gDepthStencilStates;
static std::unordered_map<RasterizerStateD3D11, ComPtr<ID3D11RasterizerState>> gRasterizerStates;
static RasterizerStateD3D11 gRasterizerState;
static bool gRasterizerStateDirty = true;
static std::unordered_map<SamplerStateD3D11, ComPtr<ID3D11SamplerState>> gSamplerStates;
static SamplerStateD3D11 gSamplerState;
static bool gSamplerStateDirty = true;
static RenderTargetD3D11* gRenderTarget = nullptr;
static std::optional<Viewport> gViewport;
static bool gViewportDirty = true;
static uint32_t gBackbufferWidth = 0;
static uint32_t gBackbufferHeight = 0;
static std::unordered_map<uint32_t, TextureD3D11*> gTextures;

static struct
{
	ComPtr<ID3D11Texture2D> depth_stencil_texture;
	ComPtr<ID3D11RenderTargetView> render_target_view;
	ComPtr<ID3D11DepthStencilView> depth_stencil_view;
} gMainRenderTarget;

static const std::unordered_map<Format, DXGI_FORMAT> FormatMap = {
	{ Format::Float1, DXGI_FORMAT_R32_FLOAT },
	{ Format::Float2, DXGI_FORMAT_R32G32_FLOAT },
	{ Format::Float3, DXGI_FORMAT_R32G32B32_FLOAT },
	{ Format::Float4, DXGI_FORMAT_R32G32B32A32_FLOAT },
	{ Format::Byte1, DXGI_FORMAT_R8_UNORM },
	{ Format::Byte2, DXGI_FORMAT_R8G8_UNORM },
	//	{ Format::Byte3, DXGI_FORMAT_R8G8B8_UNORM }, // TODO: fix
	{ Format::Byte4, DXGI_FORMAT_R8G8B8A8_UNORM }
};

class ShaderD3D11
{
private:
	ComPtr<ID3D11VertexShader> vertex_shader;
	ComPtr<ID3D11PixelShader> pixel_shader;
	ComPtr<ID3D11InputLayout> input_layout;

public:
	ShaderD3D11(const VertexLayout& vertex_layout, const std::string& vertex_code, const std::string& fragment_code,
		std::vector<std::string> defines)
	{
		ComPtr<ID3DBlob> vertex_shader_blob;
		ComPtr<ID3DBlob> pixel_shader_blob;

		ComPtr<ID3DBlob> vertex_shader_error;
		ComPtr<ID3DBlob> pixel_shader_error;
		
		AddShaderLocationDefines(vertex_layout, defines);

		auto vertex_shader_spirv = CompileGlslToSpirv(ShaderStage::Vertex, vertex_code, defines);
		auto fragment_shader_spirv = CompileGlslToSpirv(ShaderStage::Fragment, fragment_code, defines);

		auto hlsl_vert = CompileSpirvToHlsl(vertex_shader_spirv, 40);
		auto hlsl_frag = CompileSpirvToHlsl(fragment_shader_spirv, 40);

		D3DCompile(hlsl_vert.c_str(), hlsl_vert.size(), NULL, NULL, NULL, "main", "vs_4_0", 0, 0, 
			vertex_shader_blob.GetAddressOf(), vertex_shader_error.GetAddressOf());
		
		D3DCompile(hlsl_frag.c_str(), hlsl_frag.size(), NULL, NULL, NULL, "main", "ps_4_0", 0, 0, 
			pixel_shader_blob.GetAddressOf(), pixel_shader_error.GetAddressOf());

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

		gDevice->CreateVertexShader(vertex_shader_blob->GetBufferPointer(), vertex_shader_blob->GetBufferSize(), 
			NULL, vertex_shader.GetAddressOf());
		
		gDevice->CreatePixelShader(pixel_shader_blob->GetBufferPointer(), pixel_shader_blob->GetBufferSize(), 
			NULL, pixel_shader.GetAddressOf());

		std::vector<D3D11_INPUT_ELEMENT_DESC> input;

		UINT i = 0;

		for (auto& attrib : vertex_layout.attributes)
		{
			input.push_back({ "TEXCOORD", i, FormatMap.at(attrib.format), 0,
				static_cast<UINT>(attrib.offset), D3D11_INPUT_PER_VERTEX_DATA, 0 });
			i++;
		}

		gDevice->CreateInputLayout(input.data(), static_cast<UINT>(input.size()), 
			vertex_shader_blob->GetBufferPointer(), vertex_shader_blob->GetBufferSize(), input_layout.GetAddressOf());
	}

	void apply()
	{
		gContext->IASetInputLayout(input_layout.Get());
		gContext->VSSetShader(vertex_shader.Get(), NULL, 0);
		gContext->PSSetShader(pixel_shader.Get(), NULL, 0);
	}
};

class TextureD3D11
{
public:
	const auto& getD3D11Texture2D() const { return mTexture2D; }
	const auto& getD3D11ShaderResourceView() const { return mShaderResourceView; }
	auto getWidth() const { return mWidth; }
	auto getHeight() const { return mHeight; }
	bool isMipmap() const { return mMipmap; }
	auto getFormat() const { return mFormat; }

private:
	ComPtr<ID3D11ShaderResourceView> mShaderResourceView;
	ComPtr<ID3D11Texture2D> mTexture2D;
	uint32_t mWidth = 0;
	uint32_t mHeight = 0;
	bool mMipmap = false;
	Format mFormat;

public:
	TextureD3D11(uint32_t width, uint32_t height, Format format, void* memory, bool mipmap) :
		mWidth(width),
		mHeight(height),
		mMipmap(mipmap),
		mFormat(format)
	{
		auto tex_desc = CD3D11_TEXTURE2D_DESC(FormatMap.at(format), width, height);
		tex_desc.MipLevels = mipmap ? 0 : 1;
		tex_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
		tex_desc.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS; // TODO: only in mapmap mode ?
		gDevice->CreateTexture2D(&tex_desc, NULL, mTexture2D.GetAddressOf());

		auto srv_desc = CD3D11_SHADER_RESOURCE_VIEW_DESC(mTexture2D.Get(), D3D11_SRV_DIMENSION_TEXTURE2D);
		gDevice->CreateShaderResourceView(mTexture2D.Get(), &srv_desc, mShaderResourceView.GetAddressOf());

		if (memory)
		{
			auto channels = GetFormatChannelsCount(format);
			auto channel_size = GetFormatChannelSize(format);
			auto memPitch = width * channels * channel_size;
			auto memSlicePitch = width * height * channels * channel_size;
			gContext->UpdateSubresource(mTexture2D.Get(), 0, NULL, memory, memPitch, memSlicePitch);

			if (mipmap)
				gContext->GenerateMips(mShaderResourceView.Get());
		}
	}
};

class RenderTargetD3D11
{
public:
	const auto& getD3D11RenderTargetView() const { return mRenderTargetView; }
	const auto& getD3D11DepthStencilView() const { return mDepthStencilView; }
	auto getTexture() const { return mTexture; }

private:
	ComPtr<ID3D11Texture2D> mDepthStencilTexture;
	ComPtr<ID3D11RenderTargetView> mRenderTargetView;
	ComPtr<ID3D11DepthStencilView> mDepthStencilView;
	TextureD3D11* mTexture = nullptr;

public:
	RenderTargetD3D11(uint32_t width, uint32_t height, TextureD3D11* texture) :
		mTexture(texture)
	{
		auto format = FormatMap.at(texture->getFormat());
		auto rtv_desc = CD3D11_RENDER_TARGET_VIEW_DESC(D3D11_RTV_DIMENSION_TEXTURE2D, format);

		gDevice->CreateRenderTargetView(texture->getD3D11Texture2D().Get(), &rtv_desc, mRenderTargetView.GetAddressOf());

		auto tex_desc = CD3D11_TEXTURE2D_DESC(DXGI_FORMAT_D24_UNORM_S8_UINT, width, height);
		tex_desc.MipLevels = 1;
		tex_desc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
		gDevice->CreateTexture2D(&tex_desc, NULL, mDepthStencilTexture.GetAddressOf());

		auto dsv_desc = CD3D11_DEPTH_STENCIL_VIEW_DESC(D3D11_DSV_DIMENSION_TEXTURE2D, tex_desc.Format);
		gDevice->CreateDepthStencilView(mDepthStencilTexture.Get(), &dsv_desc, mDepthStencilView.GetAddressOf());
	}
};

class BufferD3D11
{
public:
	const auto& getD3D11Buffer() const { return mBuffer; }
	auto getSize() const { return mSize; }

private:
	ComPtr<ID3D11Buffer> mBuffer;
	size_t mSize = 0;

public:
	BufferD3D11(size_t size, D3D11_BIND_FLAG bind_flags) : mSize(size)
	{
		auto desc = CD3D11_BUFFER_DESC((UINT)size, bind_flags, D3D11_USAGE_DYNAMIC, 
			D3D11_CPU_ACCESS_WRITE);
		gDevice->CreateBuffer(&desc, NULL, mBuffer.GetAddressOf());
	}

	void write(void* memory, size_t size)
	{
		assert(size <= mSize);
		D3D11_MAPPED_SUBRESOURCE resource;
		gContext->Map(mBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &resource);
		memcpy(resource.pData, memory, size);
		gContext->Unmap(mBuffer.Get(), 0);
	}
};

class VertexBufferD3D11 : public BufferD3D11
{
public:
	auto getStride() const { return mStride; }
	void setStride(size_t value) { mStride = value; }

private:
	size_t mStride = 0;

public:
	VertexBufferD3D11(size_t size, size_t stride) :
		BufferD3D11(size, D3D11_BIND_VERTEX_BUFFER),
		mStride(stride)
	{
	}
};

class IndexBufferD3D11 : public BufferD3D11
{
public:
	auto getStride() const { return mStride; }
	void setStride(size_t value) { mStride = value; }

private:
	size_t mStride = 0;

public:
	IndexBufferD3D11(size_t size, size_t stride) :
		BufferD3D11(size, D3D11_BIND_INDEX_BUFFER),
		mStride(stride)
	{
	}
};

class UniformBufferD3D11 : public BufferD3D11
{
public:
	UniformBufferD3D11(size_t size) :
		BufferD3D11(size, D3D11_BIND_CONSTANT_BUFFER)
	{
		assert(size % 16 == 0);
	}
};

BackendD3D11::BackendD3D11(void* window, uint32_t width, uint32_t height)
{
	DXGI_SWAP_CHAIN_DESC sd = {};
	sd.BufferCount = 2;
	sd.BufferDesc.Width = width;
	sd.BufferDesc.Height = height;
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

	D3D11CreateDeviceAndSwapChain(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, flags, features.data(),
		static_cast<UINT>(features.size()), D3D11_SDK_VERSION, &sd, gSwapChain.GetAddressOf(), 
		gDevice.GetAddressOf(), NULL, gContext.GetAddressOf());

	createMainRenderTarget(width, height);
	setRenderTarget(std::nullopt);
}

BackendD3D11::~BackendD3D11()
{
	destroyMainRenderTarget();
	gBlendModes.clear();
	gSwapChain.Reset();
	gDevice.Reset();
	gContext.Reset();
}

void BackendD3D11::resize(uint32_t width, uint32_t height)
{
	destroyMainRenderTarget();
	gSwapChain->ResizeBuffers(0, (UINT)width, (UINT)height, DXGI_FORMAT_R8G8B8A8_UNORM, 0);
	createMainRenderTarget(width, height);
	setRenderTarget(std::nullopt); // TODO: do it when nullptr was before

	if (!gViewport.has_value())
		gViewportDirty = true;
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
	if (gViewport != viewport)
		gViewportDirty = true;

	gViewport = viewport;
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
	gContext->PSSetShaderResources((UINT)binding, 1, texture->getD3D11ShaderResourceView().GetAddressOf());
	gTextures[binding] = texture;
}

void BackendD3D11::setRenderTarget(RenderTargetHandle* handle)
{
	auto render_target = (RenderTargetD3D11*)handle;

	ComPtr<ID3D11ShaderResourceView> prev_shader_resource_view;
	gContext->PSGetShaderResources(0, 1, prev_shader_resource_view.GetAddressOf());

	if (prev_shader_resource_view.Get() == render_target->getTexture()->getD3D11ShaderResourceView().Get())
	{
		ID3D11ShaderResourceView* null[] = { NULL };
		gContext->PSSetShaderResources(0, 1, null); // remove old shader view
		// TODO: here we removing only binding 0, 
		// we should remove every binding with this texture
	}

	gContext->OMSetRenderTargets(1, render_target->getD3D11RenderTargetView().GetAddressOf(),
		render_target->getD3D11DepthStencilView().Get());

	gRenderTarget = render_target;
	
	if (!gViewport.has_value())
		gViewportDirty = true;
}

void BackendD3D11::setRenderTarget(std::nullopt_t value)
{
	gContext->OMSetRenderTargets(1, gMainRenderTarget.render_target_view.GetAddressOf(),
		gMainRenderTarget.depth_stencil_view.Get());
	
	gRenderTarget = nullptr;

	if (!gViewport.has_value())
		gViewportDirty = true;
}

void BackendD3D11::setShader(ShaderHandle* handle)
{
	auto shader = (ShaderD3D11*)handle;
	shader->apply();
}

void BackendD3D11::setVertexBuffer(VertexBufferHandle* handle)
{
	auto buffer = (VertexBufferD3D11*)handle;

	auto stride = (UINT)buffer->getStride();
	auto offset = (UINT)0;

	gContext->IASetVertexBuffers(0, 1, buffer->getD3D11Buffer().GetAddressOf(), &stride, &offset);
}

void BackendD3D11::setIndexBuffer(IndexBufferHandle* handle)
{
	auto buffer = (IndexBufferD3D11*)handle;

	auto stride = (UINT)buffer->getStride();

	gContext->IASetIndexBuffer(buffer->getD3D11Buffer().Get(), buffer->getStride() == 2 ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT, 0);
}

void BackendD3D11::setUniformBuffer(uint32_t binding, UniformBufferHandle* handle)
{
	auto buffer = (UniformBufferD3D11*)handle;

	gContext->VSSetConstantBuffers(binding, 1, buffer->getD3D11Buffer().GetAddressOf());
	gContext->PSSetConstantBuffers(binding, 1, buffer->getD3D11Buffer().GetAddressOf());
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

		auto desc = CD3D11_BLEND_DESC(D3D11_DEFAULT);
		desc.AlphaToCoverageEnable = false;

		for (int i = 0; i < D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT; i++)
		{
			auto& blend = desc.RenderTarget[i];

			if (value.color_mask.red)
				blend.RenderTargetWriteMask |= D3D11_COLOR_WRITE_ENABLE_RED;

			if (value.color_mask.green)
				blend.RenderTargetWriteMask |= D3D11_COLOR_WRITE_ENABLE_GREEN;

			if (value.color_mask.blue)
				blend.RenderTargetWriteMask |= D3D11_COLOR_WRITE_ENABLE_BLUE;

			if (value.color_mask.alpha)
				blend.RenderTargetWriteMask |= D3D11_COLOR_WRITE_ENABLE_ALPHA;

			blend.BlendEnable = true;

			blend.SrcBlend = BlendMap.at(value.color_src_blend);
			blend.DestBlend = BlendMap.at(value.color_dst_blend);
			blend.BlendOp = BlendOpMap.at(value.color_blend_func);

			blend.SrcBlendAlpha = BlendMap.at(value.alpha_src_blend);
			blend.DestBlendAlpha = BlendMap.at(value.alpha_dst_blend);
			blend.BlendOpAlpha = BlendOpMap.at(value.alpha_blend_func);
		}

		gDevice->CreateBlendState(&desc, gBlendModes[value].GetAddressOf());
	}

	const float blend_factor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	gContext->OMSetBlendState(gBlendModes.at(value).Get(), blend_factor, 0xFFFFFFFF);
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
	auto rtv = gRenderTarget != nullptr ? gRenderTarget->getD3D11RenderTargetView() : gMainRenderTarget.render_target_view;
	auto dsv = gRenderTarget != nullptr ? gRenderTarget->getD3D11DepthStencilView() : gMainRenderTarget.depth_stencil_view;

	if (color.has_value())
	{
		gContext->ClearRenderTargetView(rtv.Get(), (float*)&color.value());
	}

	if (depth.has_value() || stencil.has_value())
	{
		UINT flags = 0;

		if (depth.has_value())
			flags |= D3D11_CLEAR_DEPTH;

		if (stencil.has_value())
			flags |= D3D11_CLEAR_STENCIL;

		gContext->ClearDepthStencilView(dsv.Get(), flags, depth.value_or(1.0f), stencil.value_or(0));
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

void BackendD3D11::readPixels(const glm::i32vec2& pos, const glm::i32vec2& size, TextureHandle* dst_texture_handle)
{
	auto dst_texture = (TextureD3D11*)dst_texture_handle;

	assert(dst_texture->getWidth() == size.x);
	assert(dst_texture->getHeight() == size.y);

	if (size.x <= 0 || size.y <= 0)
		return;

	auto rtv = gRenderTarget ? 
		gRenderTarget->getD3D11RenderTargetView() : 
		gMainRenderTarget.render_target_view;

	ComPtr<ID3D11Resource> rtv_resource;
	rtv->GetResource(rtv_resource.GetAddressOf());

	ComPtr<ID3D11Texture2D> rtv_texture;
	rtv_resource.As(&rtv_texture);
	
	D3D11_TEXTURE2D_DESC desc = { 0 };
	rtv_texture->GetDesc(&desc);
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

	D3D11_BOX box;
	box.left = src_x;
	box.right = src_x + src_w;
	box.top = src_y;
	box.bottom = src_y + src_h;
	box.front = 0;
	box.back = 1;

	if (pos.y < (int)back_h && pos.x < (int)back_w)
	{
		gContext->CopySubresourceRegion(dst_texture->getD3D11Texture2D().Get(), 0, dst_x, dst_y, 0, rtv_resource.Get(), 0, &box);

		if (dst_texture->isMipmap())
			gContext->GenerateMips(dst_texture->getD3D11ShaderResourceView().Get());
	}
}

std::vector<uint8_t> BackendD3D11::getPixels()
{
	auto width = gRenderTarget ? gRenderTarget->getTexture()->getWidth() : gBackbufferWidth;
	auto height = gRenderTarget ? gRenderTarget->getTexture()->getHeight() : gBackbufferHeight;
	auto format = gRenderTarget ? gRenderTarget->getTexture()->getFormat() : Format::Byte4;
	auto channels_count = GetFormatChannelsCount(format);
	auto channel_size = GetFormatChannelSize(format);

	std::vector<uint8_t> result(width * height * channels_count * channel_size);

	auto texture = TextureD3D11(width, height, format, nullptr, false);

	readPixels({ 0, 0 }, { width, height }, (TextureHandle*)&texture);	

	CD3D11_TEXTURE2D_DESC desc(FormatMap.at(format), width, height, 1, 1, 0, D3D11_USAGE_STAGING,
		D3D11_CPU_ACCESS_READ);

	ComPtr<ID3D11Texture2D> dstTexture = nullptr;
	gDevice->CreateTexture2D(&desc, nullptr, dstTexture.GetAddressOf());

	gContext->CopyResource(dstTexture.Get(), texture.getD3D11Texture2D().Get());

	D3D11_MAPPED_SUBRESOURCE resource;
	ZeroMemory(&resource, sizeof(D3D11_MAPPED_SUBRESOURCE));

	gContext->Map(dstTexture.Get(), 0, D3D11_MAP_READ, 0, &resource);

	auto src = (uint8_t*)resource.pData;
	auto dst = result.data();
	for (int i = 0; i < height; i++)
	{
		memcpy(dst, src, width * channels_count * channel_size);
		src += resource.RowPitch;
		dst += width * channels_count * channel_size;
	}

	gContext->Unmap(texture.getD3D11Texture2D().Get(), 0);

	return result;
}

void BackendD3D11::present()
{
	bool vsync = false; // TODO: globalize this var
	gSwapChain->Present(vsync ? 1 : 0, 0);
}

TextureHandle* BackendD3D11::createTexture(uint32_t width, uint32_t height, Format format,
	void* memory, bool mipmap)
{
	auto texture = new TextureD3D11(width, height, format, memory, mipmap);
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

ShaderHandle* BackendD3D11::createShader(const VertexLayout& vertex_layout, const std::string& vertex_code, 
	const std::string& fragment_code, const std::vector<std::string>& defines)
{
	auto shader = new ShaderD3D11(vertex_layout, vertex_code, fragment_code, defines);
	return (ShaderHandle*)shader;
}

void BackendD3D11::destroyShader(ShaderHandle* handle)
{
	auto shader = (ShaderD3D11*)handle;
	delete shader;
}

VertexBufferHandle* BackendD3D11::createVertexBuffer(size_t size, size_t stride)
{
	auto buffer = new VertexBufferD3D11(size, stride);
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
	buffer->setStride(stride);
}

IndexBufferHandle* BackendD3D11::createIndexBuffer(size_t size, size_t stride)
{
	auto buffer = new IndexBufferD3D11(size, stride);
	return (IndexBufferHandle*)buffer;
}

void BackendD3D11::writeIndexBufferMemory(IndexBufferHandle* handle, void* memory, size_t size, size_t stride)
{
	auto buffer = (IndexBufferD3D11*)handle;
	buffer->write(memory, size);
	buffer->setStride(stride);
}

void BackendD3D11::destroyIndexBuffer(IndexBufferHandle* handle)
{
	auto buffer = (IndexBufferD3D11*)handle;
	delete buffer;
}

UniformBufferHandle* BackendD3D11::createUniformBuffer(size_t size)
{
	auto buffer = new UniformBufferD3D11(size);
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
	ComPtr<ID3D11Texture2D> backbuffer;
	gSwapChain->GetBuffer(0, IID_PPV_ARGS(backbuffer.GetAddressOf()));
	gDevice->CreateRenderTargetView(backbuffer.Get(), NULL, gMainRenderTarget.render_target_view.GetAddressOf());

	auto tex_desc = CD3D11_TEXTURE2D_DESC(DXGI_FORMAT_D24_UNORM_S8_UINT, width, height);
	tex_desc.MipLevels = 1;
	tex_desc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
	gDevice->CreateTexture2D(&tex_desc, NULL, gMainRenderTarget.depth_stencil_texture.GetAddressOf());

	auto dsv_desc = CD3D11_DEPTH_STENCIL_VIEW_DESC(D3D11_DSV_DIMENSION_TEXTURE2D, 
		tex_desc.Format);
	gDevice->CreateDepthStencilView(gMainRenderTarget.depth_stencil_texture.Get(), &dsv_desc, gMainRenderTarget.depth_stencil_view.GetAddressOf());
	
	gBackbufferWidth = width;
	gBackbufferHeight = height;
}

void BackendD3D11::destroyMainRenderTarget()
{
	gMainRenderTarget.depth_stencil_texture.Reset();
	gMainRenderTarget.depth_stencil_view.Reset();
	gMainRenderTarget.render_target_view.Reset();
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

			auto desc = CD3D11_DEPTH_STENCIL_DESC(D3D11_DEFAULT);
			desc.DepthEnable = depth_stencil_state.depth_mode.has_value();
			desc.DepthFunc = ComparisonFuncMap.at(depth_mode.func);

			desc.StencilEnable = depth_stencil_state.stencil_mode.has_value();
			desc.StencilReadMask = stencil_mode.read_mask;
			desc.StencilWriteMask = stencil_mode.write_mask;

			desc.FrontFace.StencilDepthFailOp = StencilOpMap.at(stencil_mode.depth_fail_op);
			desc.FrontFace.StencilFailOp = StencilOpMap.at(stencil_mode.fail_op);
			desc.FrontFace.StencilFunc = ComparisonFuncMap.at(stencil_mode.func);
			desc.FrontFace.StencilPassOp = StencilOpMap.at(stencil_mode.pass_op);

			desc.BackFace = desc.FrontFace;

			gDevice->CreateDepthStencilState(&desc, gDepthStencilStates[depth_stencil_state].GetAddressOf());
		}

		gContext->OMSetDepthStencilState(gDepthStencilStates.at(depth_stencil_state).Get(), stencil_mode.reference);
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

			auto desc = CD3D11_RASTERIZER_DESC(D3D11_DEFAULT);
			desc.CullMode = CullMap.at(value.cull_mode);
			desc.ScissorEnable = value.scissor_enabled;
			gDevice->CreateRasterizerState(&desc, gRasterizerStates[value].GetAddressOf());
		}

		gContext->RSSetState(gRasterizerStates.at(value).Get());
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

			auto desc = CD3D11_SAMPLER_DESC(D3D11_DEFAULT);
			desc.Filter = SamplerMap.at(value.sampler);
			desc.AddressU = TextureAddressMap.at(value.textureAddress);
			desc.AddressV = TextureAddressMap.at(value.textureAddress);
			desc.AddressW = TextureAddressMap.at(value.textureAddress);
			gDevice->CreateSamplerState(&desc, gSamplerStates[value].GetAddressOf());
		}

		for (auto [binding, _] : gTextures)
		{
			gContext->PSSetSamplers(binding, 1, gSamplerStates.at(value).GetAddressOf());
		}
	}

	// viewport

	if (gViewportDirty)
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
			width = static_cast<float>(gRenderTarget->getTexture()->getWidth());
			height = static_cast<float>(gRenderTarget->getTexture()->getHeight());
		}

		auto viewport = gViewport.value_or(Viewport{ { 0.0f, 0.0f }, { width, height } });

		D3D11_VIEWPORT vp;
		vp.Width = viewport.size.x;
		vp.Height = viewport.size.y;
		vp.MinDepth = viewport.min_depth;
		vp.MaxDepth = viewport.max_depth;
		vp.TopLeftX = viewport.position.x;
		vp.TopLeftY = viewport.position.y;
		gContext->RSSetViewports(1, &vp);

		gViewportDirty = false;
	}
}

#endif