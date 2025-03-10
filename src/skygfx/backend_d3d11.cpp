#include "backend_d3d11.h"

#ifdef SKYGFX_HAS_D3D11

//#define SKYGFX_D3D11_VALIDATION_ENABLED

#include "shader_compiler.h"
#include <stdexcept>
#include <vector>
#include <unordered_map>

#include <d3dcompiler.h>
#include <d3d11.h>
#include <dxgi1_6.h>

#pragma comment(lib, "d3d11")
#pragma comment(lib, "d3dcompiler")
#pragma comment(lib, "dxgi")
#pragma comment(lib, "dxguid.lib")

#include <wrl.h>

using Microsoft::WRL::ComPtr;
using namespace skygfx;

struct DepthStencilStateD3D11
{
	std::optional<DepthMode> depth_mode;
	std::optional<StencilMode> stencil_mode;

	bool operator==(const DepthStencilStateD3D11& other) const = default;
};

SKYGFX_MAKE_HASHABLE(DepthStencilStateD3D11,
	t.depth_mode,
	t.stencil_mode
);

struct RasterizerStateD3D11
{
	bool scissor_enabled = false;
	CullMode cull_mode = CullMode::None;
	FrontFace front_face = FrontFace::Clockwise;
	std::optional<DepthBias> depth_bias;

	bool operator==(const RasterizerStateD3D11& other) const = default;
};

SKYGFX_MAKE_HASHABLE(RasterizerStateD3D11,
	t.cull_mode,
	t.scissor_enabled,
	t.front_face,
	t.depth_bias
);

struct SamplerStateD3D11
{
	Sampler sampler = Sampler::Linear;
	AnisotropyLevel anisotropy_level = AnisotropyLevel::None;
	TextureAddress texture_address = TextureAddress::Clamp;

	bool operator==(const SamplerStateD3D11& other) const = default;
};

SKYGFX_MAKE_HASHABLE(SamplerStateD3D11,
	t.sampler,
	t.anisotropy_level,
	t.texture_address
);

class ShaderD3D11;
class TextureD3D11;
class RenderTargetD3D11;

struct ContextD3D11
{
	ComPtr<IDXGISwapChain> swapchain;
	ComPtr<ID3D11Device> device;
	ComPtr<ID3D11DeviceContext> context;
	TextureD3D11* backbuffer_texture = nullptr;
	RenderTargetD3D11* main_render_target = nullptr;
	std::vector<RenderTargetD3D11*> render_targets;
	ShaderD3D11* shader = nullptr;
	std::vector<InputLayout> input_layouts;
	
	std::unordered_map<DepthStencilStateD3D11, ComPtr<ID3D11DepthStencilState>> depth_stencil_states;
	DepthStencilStateD3D11 depth_stencil_state;
	
	std::unordered_map<RasterizerStateD3D11, ComPtr<ID3D11RasterizerState>> rasterizer_states;
	RasterizerStateD3D11 rasterizer_state;
	
	std::unordered_map<SamplerStateD3D11, ComPtr<ID3D11SamplerState>> sampler_states;
	SamplerStateD3D11 sampler_state;

	std::unordered_map<std::optional<BlendMode>, ComPtr<ID3D11BlendState>> blend_modes;
	std::optional<BlendMode> blend_mode;

	std::optional<Viewport> viewport;

	bool shader_dirty = true;
	bool input_layouts_dirty = true;
	bool depth_stencil_state_dirty = true;
	bool rasterizer_state_dirty = true;
	bool sampler_state_dirty = true;
	bool blend_mode_dirty = true;
	bool viewport_dirty = true;

	bool vsync = false;
	std::unordered_map<uint32_t, TextureD3D11*> textures;

	uint32_t getBackbufferWidth();
	uint32_t getBackbufferHeight();
	PixelFormat getBackbufferFormat();
};

static ContextD3D11* gContext = nullptr;

static const std::unordered_map<VertexFormat, DXGI_FORMAT> VertexFormatMap = {
	{ VertexFormat::Float1, DXGI_FORMAT_R32_FLOAT },
	{ VertexFormat::Float2, DXGI_FORMAT_R32G32_FLOAT },
	{ VertexFormat::Float3, DXGI_FORMAT_R32G32B32_FLOAT },
	{ VertexFormat::Float4, DXGI_FORMAT_R32G32B32A32_FLOAT },
	{ VertexFormat::UChar1Normalized, DXGI_FORMAT_R8_UNORM },
	{ VertexFormat::UChar2Normalized, DXGI_FORMAT_R8G8_UNORM },
	{ VertexFormat::UChar4Normalized, DXGI_FORMAT_R8G8B8A8_UNORM },
	{ VertexFormat::UChar1, DXGI_FORMAT_R8_UINT },
	{ VertexFormat::UChar2, DXGI_FORMAT_R8G8_UINT },
	{ VertexFormat::UChar4, DXGI_FORMAT_R8G8B8A8_UINT },
};

static const std::unordered_map<PixelFormat, DXGI_FORMAT> PixelFormatMap = {
	{ PixelFormat::R32Float, DXGI_FORMAT_R32_FLOAT },
	{ PixelFormat::RG32Float, DXGI_FORMAT_R32G32_FLOAT },
	{ PixelFormat::RGB32Float, DXGI_FORMAT_R32G32B32_FLOAT },
	{ PixelFormat::RGBA32Float, DXGI_FORMAT_R32G32B32A32_FLOAT },
	{ PixelFormat::R8UNorm, DXGI_FORMAT_R8_UNORM },
	{ PixelFormat::RG8UNorm, DXGI_FORMAT_R8G8_UNORM },
	{ PixelFormat::RGBA8UNorm, DXGI_FORMAT_R8G8B8A8_UNORM }
};

SKYGFX_MAKE_HASHABLE(std::vector<InputLayout>,
	t
);

class ShaderD3D11
{
public:
	const auto& getD3D11VertexShader() const { return mVertexShader; }
	const auto& getD3D11PixelShader() const { return mPixelShader; }
	auto& getInputLayoutCache() { return mInputLayoutCache; }
	const auto& getVertexShaderBlob() const { return mVertexShaderBlob; }

private:
	ComPtr<ID3D11VertexShader> mVertexShader;
	ComPtr<ID3D11PixelShader> mPixelShader;
	std::unordered_map<std::vector<InputLayout>, ComPtr<ID3D11InputLayout>> mInputLayoutCache;
	ComPtr<ID3DBlob> mVertexShaderBlob; // for input layout

public:
	ShaderD3D11(const std::string& vertex_code, const std::string& fragment_code,
		std::vector<std::string> defines)
	{
		ComPtr<ID3DBlob> pixel_shader_blob;

		ComPtr<ID3DBlob> vertex_shader_error;
		ComPtr<ID3DBlob> pixel_shader_error;
		
		auto vertex_shader_spirv = CompileGlslToSpirv(ShaderStage::Vertex, vertex_code, defines);
		auto fragment_shader_spirv = CompileGlslToSpirv(ShaderStage::Fragment, fragment_code, defines);

		auto hlsl_vert = CompileSpirvToHlsl(vertex_shader_spirv, 40);
		auto hlsl_frag = CompileSpirvToHlsl(fragment_shader_spirv, 40);

		D3DCompile(hlsl_vert.c_str(), hlsl_vert.size(), NULL, NULL, NULL, "main", "vs_4_0", 0, 0, 
			mVertexShaderBlob.GetAddressOf(), vertex_shader_error.GetAddressOf());
		
		D3DCompile(hlsl_frag.c_str(), hlsl_frag.size(), NULL, NULL, NULL, "main", "ps_4_0", 0, 0, 
			pixel_shader_blob.GetAddressOf(), pixel_shader_error.GetAddressOf());

		std::string vertex_shader_error_string = "";
		std::string pixel_shader_error_string = "";

		if (vertex_shader_error != NULL)
			vertex_shader_error_string = std::string((char*)vertex_shader_error->GetBufferPointer(), vertex_shader_error->GetBufferSize());

		if (pixel_shader_error != NULL)
			pixel_shader_error_string = std::string((char*)pixel_shader_error->GetBufferPointer(), pixel_shader_error->GetBufferSize());

		if (mVertexShaderBlob == NULL)
			throw std::runtime_error(vertex_shader_error_string);

		if (pixel_shader_blob == NULL)
			throw std::runtime_error(pixel_shader_error_string);

		gContext->device->CreateVertexShader(mVertexShaderBlob->GetBufferPointer(), mVertexShaderBlob->GetBufferSize(),
			NULL, mVertexShader.GetAddressOf());
		
		gContext->device->CreatePixelShader(pixel_shader_blob->GetBufferPointer(), pixel_shader_blob->GetBufferSize(),
			NULL, mPixelShader.GetAddressOf());
	}
};

class TextureD3D11
{
public:
	const auto& getD3D11Texture2D() const { return mTexture2D; }
	const auto& getD3D11ShaderResourceView() const { return mShaderResourceView; }
	auto getWidth() const { return mWidth; }
	auto getHeight() const { return mHeight; }
	auto getFormat() const { return mFormat; }

private:
	ComPtr<ID3D11ShaderResourceView> mShaderResourceView;
	ComPtr<ID3D11Texture2D> mTexture2D;
	uint32_t mWidth = 0;
	uint32_t mHeight = 0;
	uint32_t mMipCount = 0;
	PixelFormat mFormat;

public:
	TextureD3D11(uint32_t width, uint32_t height, PixelFormat format, uint32_t mip_count) :
		mWidth(width),
		mHeight(height),
		mFormat(format),
		mMipCount(mip_count)
	{
		auto tex_desc = CD3D11_TEXTURE2D_DESC(PixelFormatMap.at(format), width, height);
		tex_desc.MipLevels = mip_count;
		tex_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
		tex_desc.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;
		gContext->device->CreateTexture2D(&tex_desc, NULL, mTexture2D.GetAddressOf());

		auto srv_desc = CD3D11_SHADER_RESOURCE_VIEW_DESC(mTexture2D.Get(), D3D11_SRV_DIMENSION_TEXTURE2D);
		gContext->device->CreateShaderResourceView(mTexture2D.Get(), &srv_desc, mShaderResourceView.GetAddressOf());
	}

	TextureD3D11(uint32_t width, uint32_t height, PixelFormat format, ComPtr<ID3D11Texture2D> texture) :
		mWidth(width),
		mHeight(height),
		mFormat(format),
		mTexture2D(texture)
	{
	}

	void write(uint32_t width, uint32_t height, const void* memory, uint32_t mip_level, uint32_t offset_x, uint32_t offset_y)
	{
		auto channels = GetFormatChannelsCount(mFormat);
		auto channel_size = GetFormatChannelSize(mFormat);
		auto mem_pitch = width * channels * channel_size;
		auto mem_slice_pitch = width * height * channels * channel_size;
		auto dst_box = CD3D11_BOX(offset_x, offset_y, 0, offset_x + width, offset_y + height, 1);
		gContext->context->UpdateSubresource(mTexture2D.Get(), mip_level, &dst_box, memory, mem_pitch,
			mem_slice_pitch);
	}

	std::vector<uint8_t> read(uint32_t mip_level) const
	{
		auto mip_width = GetMipWidth(mWidth, mip_level);
		auto mip_height = GetMipHeight(mHeight, mip_level);

		CD3D11_TEXTURE2D_DESC staging_desc(PixelFormatMap.at(mFormat), mip_width, mip_height, 1, 1, 0, D3D11_USAGE_STAGING,
			D3D11_CPU_ACCESS_READ);

		ComPtr<ID3D11Texture2D> staging_texture;
		gContext->device->CreateTexture2D(&staging_desc, nullptr, &staging_texture);

		gContext->context->CopySubresourceRegion(staging_texture.Get(), 0, 0, 0, 0, mTexture2D.Get(), mip_level, nullptr);

		D3D11_MAPPED_SUBRESOURCE mapped_resource;
		gContext->context->Map(staging_texture.Get(), 0, D3D11_MAP_READ, 0, &mapped_resource);

		auto channels_count = GetFormatChannelsCount(mFormat);
		auto channel_size = GetFormatChannelSize(mFormat);
		size_t row_size = mip_width * channels_count * channel_size;
		std::vector<uint8_t> result(mip_height * row_size);

		uint32_t row_pitch = mapped_resource.RowPitch;

		for (uint32_t y = 0; y < mip_height; ++y)
		{
			auto src_row = static_cast<const uint8_t*>(mapped_resource.pData) + y * row_pitch;
			auto dst_row = result.data() + y * row_size;
			memcpy(dst_row, src_row, row_size);
		}

		gContext->context->Unmap(staging_texture.Get(), 0);

		return result;
	}

	void generateMips()
	{
		gContext->context->GenerateMips(mShaderResourceView.Get());
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
		auto format = PixelFormatMap.at(texture->getFormat());
		auto rtv_desc = CD3D11_RENDER_TARGET_VIEW_DESC(D3D11_RTV_DIMENSION_TEXTURE2D, format);
		gContext->device->CreateRenderTargetView(texture->getD3D11Texture2D().Get(), &rtv_desc, mRenderTargetView.GetAddressOf());

		auto tex_desc = CD3D11_TEXTURE2D_DESC(DXGI_FORMAT_D24_UNORM_S8_UINT, width, height, 1, 1, D3D11_BIND_DEPTH_STENCIL);
		gContext->device->CreateTexture2D(&tex_desc, NULL, mDepthStencilTexture.GetAddressOf());

		auto dsv_desc = CD3D11_DEPTH_STENCIL_VIEW_DESC(D3D11_DSV_DIMENSION_TEXTURE2D, tex_desc.Format);
		gContext->device->CreateDepthStencilView(mDepthStencilTexture.Get(), &dsv_desc, mDepthStencilView.GetAddressOf());
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
		auto desc = CD3D11_BUFFER_DESC((UINT)size, bind_flags, D3D11_USAGE_DYNAMIC,  D3D11_CPU_ACCESS_WRITE);
		gContext->device->CreateBuffer(&desc, NULL, mBuffer.GetAddressOf());
	}

	void write(const void* memory, size_t size)
	{
		assert(size <= mSize);
		D3D11_MAPPED_SUBRESOURCE resource;
		gContext->context->Map(mBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &resource);
		memcpy(resource.pData, memory, size);
		gContext->context->Unmap(mBuffer.Get(), 0);
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

uint32_t ContextD3D11::getBackbufferWidth()
{
	return render_targets.at(0)->getTexture()->getWidth();
}

uint32_t ContextD3D11::getBackbufferHeight()
{
	return render_targets.at(0)->getTexture()->getHeight();
}

PixelFormat ContextD3D11::getBackbufferFormat() // TODO: wtf when mrt
{
	return render_targets.at(0)->getTexture()->getFormat();
}

static void CreateMainRenderTarget(uint32_t width, uint32_t height)
{
	ComPtr<ID3D11Texture2D> backbuffer;
	gContext->swapchain->GetBuffer(0, IID_PPV_ARGS(backbuffer.GetAddressOf()));

	gContext->backbuffer_texture = new TextureD3D11(width, height, skygfx::PixelFormat::RGBA8UNorm, backbuffer);
	gContext->main_render_target = new RenderTargetD3D11(width, height, gContext->backbuffer_texture);
}

static void DestroyMainRenderTarget()
{
	delete gContext->backbuffer_texture;
	delete gContext->main_render_target;
	gContext->backbuffer_texture = nullptr;
	gContext->main_render_target = nullptr;
}

static void EnsureShader()
{
	if (!gContext->shader_dirty)
		return;

	gContext->shader_dirty = false;

	gContext->context->VSSetShader(gContext->shader->getD3D11VertexShader().Get(), NULL, 0);
	gContext->context->PSSetShader(gContext->shader->getD3D11PixelShader().Get(), NULL, 0);
}

static void EnsureInputLayout()
{
	if (!gContext->input_layouts_dirty)
		return;

	gContext->input_layouts_dirty = false;

	auto& cache = gContext->shader->getInputLayoutCache();

	if (!cache.contains(gContext->input_layouts))
	{
		std::vector<D3D11_INPUT_ELEMENT_DESC> input_elements;

		for (size_t i = 0; i < gContext->input_layouts.size(); i++)
		{
			const auto& input_layout = gContext->input_layouts.at(i);

			for (const auto& [location, attribute] : input_layout.attributes)
			{
				static const std::unordered_map<InputLayout::Rate, D3D11_INPUT_CLASSIFICATION> InputRateMap = {
					{ InputLayout::Rate::Vertex, D3D11_INPUT_PER_VERTEX_DATA },
					{ InputLayout::Rate::Instance, D3D11_INPUT_PER_INSTANCE_DATA },
				};

				input_elements.push_back(D3D11_INPUT_ELEMENT_DESC{
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

		gContext->device->CreateInputLayout(input_elements.data(), (UINT)input_elements.size(),
			gContext->shader->getVertexShaderBlob()->GetBufferPointer(),
			gContext->shader->getVertexShaderBlob()->GetBufferSize(), cache[gContext->input_layouts].GetAddressOf());
	}

	gContext->context->IASetInputLayout(cache.at(gContext->input_layouts).Get());
}

static void EnsureDepthStencilState()
{
	if (!gContext->depth_stencil_state_dirty)
		return;

	gContext->depth_stencil_state_dirty = false;

	const auto& depth_stencil_state = gContext->depth_stencil_state;

	auto depth_mode = depth_stencil_state.depth_mode.value_or(DepthMode());
	auto stencil_mode = depth_stencil_state.stencil_mode.value_or(StencilMode());

	if (!gContext->depth_stencil_states.contains(depth_stencil_state))
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
		desc.DepthWriteMask = depth_mode.write_mask ? D3D11_DEPTH_WRITE_MASK_ALL : D3D11_DEPTH_WRITE_MASK_ZERO;

		desc.StencilEnable = depth_stencil_state.stencil_mode.has_value();
		desc.StencilReadMask = stencil_mode.read_mask;
		desc.StencilWriteMask = stencil_mode.write_mask;

		desc.FrontFace.StencilDepthFailOp = StencilOpMap.at(stencil_mode.depth_fail_op);
		desc.FrontFace.StencilFailOp = StencilOpMap.at(stencil_mode.fail_op);
		desc.FrontFace.StencilFunc = ComparisonFuncMap.at(stencil_mode.func);
		desc.FrontFace.StencilPassOp = StencilOpMap.at(stencil_mode.pass_op);

		desc.BackFace = desc.FrontFace;

		gContext->device->CreateDepthStencilState(&desc, gContext->depth_stencil_states[depth_stencil_state].GetAddressOf());
	}

	gContext->context->OMSetDepthStencilState(gContext->depth_stencil_states.at(depth_stencil_state).Get(), stencil_mode.reference);
}

static void EnsureRasterizerState()
{
	if (!gContext->rasterizer_state_dirty)
		return;

	gContext->rasterizer_state_dirty = false;

	const auto& value = gContext->rasterizer_state;

	if (!gContext->rasterizer_states.contains(value))
	{
		const static std::unordered_map<CullMode, D3D11_CULL_MODE> CullMap = {
			{ CullMode::None, D3D11_CULL_NONE },
			{ CullMode::Front, D3D11_CULL_FRONT },
			{ CullMode::Back, D3D11_CULL_BACK }
		};

		auto desc = CD3D11_RASTERIZER_DESC(D3D11_DEFAULT);
		desc.CullMode = CullMap.at(value.cull_mode);
		desc.ScissorEnable = value.scissor_enabled;
		desc.FrontCounterClockwise = value.front_face == FrontFace::CounterClockwise;
		if (value.depth_bias.has_value())
		{
			desc.SlopeScaledDepthBias = value.depth_bias->factor;
			desc.DepthBias = (INT)value.depth_bias->units;
		}
		else
		{
			desc.DepthBias = D3D11_DEFAULT_DEPTH_BIAS;
			desc.SlopeScaledDepthBias = D3D11_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
		}
		gContext->device->CreateRasterizerState(&desc, gContext->rasterizer_states[value].GetAddressOf());
	}

	gContext->context->RSSetState(gContext->rasterizer_states.at(value).Get());
}

static void EnsureSamplerState()
{
	if (!gContext->sampler_state_dirty)
		return;

	gContext->sampler_state_dirty = false;

	const auto& value = gContext->sampler_state;

	if (!gContext->sampler_states.contains(value))
	{

		const static std::unordered_map<Sampler, D3D11_FILTER> SamplerMap = {
			{ Sampler::Linear, D3D11_FILTER_MIN_MAG_MIP_LINEAR },
			{ Sampler::Nearest, D3D11_FILTER_MIN_MAG_MIP_POINT },
		};

		const static std::unordered_map<TextureAddress, D3D11_TEXTURE_ADDRESS_MODE> TextureAddressMap = {
			{ TextureAddress::Clamp, D3D11_TEXTURE_ADDRESS_CLAMP },
			{ TextureAddress::Wrap, D3D11_TEXTURE_ADDRESS_WRAP },
			{ TextureAddress::MirrorWrap, D3D11_TEXTURE_ADDRESS_MIRROR }
		};

		static const std::unordered_map<AnisotropyLevel, UINT> AnisotropyLevelMap = {
			{ AnisotropyLevel::None, 0 },
			{ AnisotropyLevel::X2, 2 },
			{ AnisotropyLevel::X4, 4 },
			{ AnisotropyLevel::X8, 8 },
			{ AnisotropyLevel::X16, 16 },
		};

		auto desc = CD3D11_SAMPLER_DESC(D3D11_DEFAULT);
		desc.Filter = value.anisotropy_level == AnisotropyLevel::None ? SamplerMap.at(value.sampler) : D3D11_FILTER_ANISOTROPIC;
		desc.MaxAnisotropy = AnisotropyLevelMap.at(value.anisotropy_level);
		desc.AddressU = TextureAddressMap.at(value.texture_address);
		desc.AddressV = TextureAddressMap.at(value.texture_address);
		desc.AddressW = TextureAddressMap.at(value.texture_address);
		gContext->device->CreateSamplerState(&desc, gContext->sampler_states[value].GetAddressOf());
	}

	for (auto [binding, _] : gContext->textures)
	{
		gContext->context->PSSetSamplers(binding, 1, gContext->sampler_states.at(value).GetAddressOf());
	}	
}

static void EnsureBlendMode()
{
	if (!gContext->blend_mode_dirty)
		return;

	gContext->blend_mode_dirty = false;

	const auto& blend_mode = gContext->blend_mode;

	if (!gContext->blend_modes.contains(blend_mode))
	{
		const static std::unordered_map<Blend, D3D11_BLEND> ColorBlendMap = {
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

		const static std::unordered_map<Blend, D3D11_BLEND> AlphaBlendMap = {
			{ Blend::One, D3D11_BLEND_ONE },
			{ Blend::Zero, D3D11_BLEND_ZERO },
			{ Blend::SrcColor, D3D11_BLEND_SRC_ALPHA },
			{ Blend::InvSrcColor, D3D11_BLEND_INV_SRC_ALPHA },
			{ Blend::SrcAlpha, D3D11_BLEND_SRC_ALPHA },
			{ Blend::InvSrcAlpha, D3D11_BLEND_INV_SRC_ALPHA },
			{ Blend::DstColor, D3D11_BLEND_DEST_ALPHA },
			{ Blend::InvDstColor, D3D11_BLEND_INV_DEST_ALPHA },
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

		for (int i = 0; i < D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT; i++)
		{
			auto& blend = desc.RenderTarget[i];

			blend.BlendEnable = blend_mode.has_value();

			if (!blend.BlendEnable)
				continue;

			const auto& blend_mode_nn = blend_mode.value();

			if (blend_mode_nn.color_mask.red)
				blend.RenderTargetWriteMask |= D3D11_COLOR_WRITE_ENABLE_RED;

			if (blend_mode_nn.color_mask.green)
				blend.RenderTargetWriteMask |= D3D11_COLOR_WRITE_ENABLE_GREEN;

			if (blend_mode_nn.color_mask.blue)
				blend.RenderTargetWriteMask |= D3D11_COLOR_WRITE_ENABLE_BLUE;

			if (blend_mode_nn.color_mask.alpha)
				blend.RenderTargetWriteMask |= D3D11_COLOR_WRITE_ENABLE_ALPHA;

			blend.SrcBlend = ColorBlendMap.at(blend_mode_nn.color_src);
			blend.DestBlend = ColorBlendMap.at(blend_mode_nn.color_dst);
			blend.BlendOp = BlendOpMap.at(blend_mode_nn.color_func);

			blend.SrcBlendAlpha = AlphaBlendMap.at(blend_mode_nn.alpha_src);
			blend.DestBlendAlpha = AlphaBlendMap.at(blend_mode_nn.alpha_dst);
			blend.BlendOpAlpha = BlendOpMap.at(blend_mode_nn.alpha_func);
		}

		gContext->device->CreateBlendState(&desc, gContext->blend_modes[blend_mode].GetAddressOf());
	}

	gContext->context->OMSetBlendState(gContext->blend_modes.at(blend_mode).Get(), nullptr, 0xFFFFFFFF);
}

static void EnsureViewport()
{
	if (!gContext->viewport_dirty)
		return;

	gContext->viewport_dirty = false;

	auto width = static_cast<float>(gContext->getBackbufferWidth());
	auto height = static_cast<float>(gContext->getBackbufferHeight());

	auto viewport = gContext->viewport.value_or(Viewport{ { 0.0f, 0.0f }, { width, height } });

	D3D11_VIEWPORT vp;
	vp.Width = viewport.size.x;
	vp.Height = viewport.size.y;
	vp.MinDepth = viewport.min_depth;
	vp.MaxDepth = viewport.max_depth;
	vp.TopLeftX = viewport.position.x;
	vp.TopLeftY = viewport.position.y;
	gContext->context->RSSetViewports(1, &vp);
}

static void EnsureGraphicsState(bool draw_indexed)
{
	EnsureShader();
	EnsureInputLayout();
	EnsureDepthStencilState();
	EnsureRasterizerState();
	EnsureSamplerState();
	EnsureBlendMode();
	EnsureViewport();
}

BackendD3D11::BackendD3D11(void* window, uint32_t width, uint32_t height, Adapter _adapter)
{
	gContext = new ContextD3D11();

	ComPtr<IDXGIFactory6> dxgi_factory;
	CreateDXGIFactory1(IID_PPV_ARGS(dxgi_factory.GetAddressOf()));

	IDXGIAdapter1* adapter;
	auto gpu_preference = _adapter == Adapter::HighPerformance ? DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE : DXGI_GPU_PREFERENCE_MINIMUM_POWER;
	dxgi_factory->EnumAdapterByGpuPreference(0, gpu_preference, IID_PPV_ARGS(&adapter));

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
	sd.Windowed = TRUE;
	sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

#ifdef SKYGFX_D3D11_VALIDATION_ENABLED
	UINT flags = D3D11_CREATE_DEVICE_DEBUG;
#else
	UINT flags = 0;
#endif

	D3D11CreateDeviceAndSwapChain(adapter, D3D_DRIVER_TYPE_UNKNOWN, NULL, flags, NULL, 0,
		D3D11_SDK_VERSION, &sd, gContext->swapchain.GetAddressOf(), gContext->device.GetAddressOf(),
		NULL, gContext->context.GetAddressOf());

#ifdef SKYGFX_D3D11_VALIDATION_ENABLED
	ComPtr<ID3D11InfoQueue> info_queue;
	gContext->device->QueryInterface(IID_PPV_ARGS(info_queue.GetAddressOf()));

	info_queue->SetBreakOnSeverity(D3D11_MESSAGE_SEVERITY_CORRUPTION, true);
	info_queue->SetBreakOnSeverity(D3D11_MESSAGE_SEVERITY_ERROR, true);
	info_queue->SetBreakOnSeverity(D3D11_MESSAGE_SEVERITY_INFO, true);
	info_queue->SetBreakOnSeverity(D3D11_MESSAGE_SEVERITY_MESSAGE, true);
	info_queue->SetBreakOnSeverity(D3D11_MESSAGE_SEVERITY_WARNING, true);
#endif

	CreateMainRenderTarget(width, height);
	setRenderTarget(nullptr, 0);
}

BackendD3D11::~BackendD3D11()
{
	DestroyMainRenderTarget();
	delete gContext;
}

void BackendD3D11::resize(uint32_t width, uint32_t height)
{
	DestroyMainRenderTarget();
	gContext->swapchain->ResizeBuffers(0, (UINT)width, (UINT)height, DXGI_FORMAT_R8G8B8A8_UNORM, 0);
	CreateMainRenderTarget(width, height);
	setRenderTarget(nullptr, 0); // TODO: do it when nullptr was before

	if (!gContext->viewport.has_value())
		gContext->viewport_dirty = true;
}

void BackendD3D11::setVsync(bool value)
{
	gContext->vsync = value;
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

	gContext->context->IASetPrimitiveTopology(TopologyMap.at(topology));
}

void BackendD3D11::setViewport(std::optional<Viewport> viewport)
{
	gContext->viewport = viewport;
	gContext->viewport_dirty = true;
}

void BackendD3D11::setScissor(std::optional<Scissor> scissor)
{
	if (scissor.has_value())
	{
		auto value = scissor.value();

		D3D11_RECT rect;
		rect.left = static_cast<LONG>(value.position.x);
		rect.top = static_cast<LONG>(value.position.y);
		rect.right = static_cast<LONG>(value.position.x + value.size.x);
		rect.bottom = static_cast<LONG>(value.position.y + value.size.y);
		gContext->context->RSSetScissorRects(1, &rect);
	}

	gContext->rasterizer_state.scissor_enabled = scissor.has_value();
	gContext->rasterizer_state_dirty = true;
}

void BackendD3D11::setTexture(uint32_t binding, TextureHandle* handle)
{
	auto texture = (TextureD3D11*)handle;
	gContext->context->PSSetShaderResources((UINT)binding, 1, texture->getD3D11ShaderResourceView().GetAddressOf());
	gContext->textures[binding] = texture;
}

void BackendD3D11::setInputLayout(const std::vector<InputLayout>& value)
{
	gContext->input_layouts = value;
	gContext->input_layouts_dirty = true;
}

void BackendD3D11::setRenderTarget(const RenderTarget** render_target, size_t count)
{
	if (count == 0)
	{
		gContext->context->OMSetRenderTargets(1, gContext->main_render_target->getD3D11RenderTargetView().GetAddressOf(),
			gContext->main_render_target->getD3D11DepthStencilView().Get());

		gContext->render_targets = { gContext->main_render_target };

		if (!gContext->viewport.has_value())
			gContext->viewport_dirty = true;

		return;
	}

	ComPtr<ID3D11ShaderResourceView> prev_shader_resource_view;
	gContext->context->PSGetShaderResources(0, 1, prev_shader_resource_view.GetAddressOf());

	std::vector<ID3D11RenderTargetView*> render_target_views;
	std::optional<ID3D11DepthStencilView*> depth_stencil_view;

	gContext->render_targets.clear();

	for (size_t i = 0; i < count; i++)
	{
		auto target = (RenderTargetD3D11*)(RenderTargetHandle*)*(RenderTarget*)render_target[i];

		if (prev_shader_resource_view.Get() == target->getTexture()->getD3D11ShaderResourceView().Get())
		{
			ID3D11ShaderResourceView* null[] = { NULL };
			gContext->context->PSSetShaderResources(0, 1, null); // remove old shader view
			// TODO: here we removing only binding 0, 
			// we should remove every binding with this texture
		}

		render_target_views.push_back(target->getD3D11RenderTargetView().Get());

		if (!depth_stencil_view.has_value())
			depth_stencil_view = target->getD3D11DepthStencilView().Get();

		gContext->render_targets.push_back(target);
	}

	gContext->context->OMSetRenderTargets((UINT)render_target_views.size(),
		render_target_views.data(), depth_stencil_view.value_or(nullptr));
	
	if (!gContext->viewport.has_value())
		gContext->viewport_dirty = true;
}

void BackendD3D11::setShader(ShaderHandle* handle)
{
	gContext->shader = (ShaderD3D11*)handle;
	gContext->shader_dirty = true;
	gContext->input_layouts_dirty = true;
}

void BackendD3D11::setVertexBuffer(const VertexBuffer** vertex_buffer, size_t count)
{
	std::vector<ID3D11Buffer*> buffers;
	std::vector<UINT> strides;
	std::vector<UINT> offsets;

	for (size_t i = 0; i < count; i++)
	{
		auto buffer = (VertexBufferD3D11*)(VertexBufferHandle*)*(VertexBuffer*)vertex_buffer[i];
		buffers.push_back(buffer->getD3D11Buffer().Get());
		strides.push_back((UINT)buffer->getStride());
		offsets.push_back(0);
	}

	gContext->context->IASetVertexBuffers(0, (UINT)buffers.size(), buffers.data(), strides.data(), offsets.data());
}

void BackendD3D11::setIndexBuffer(IndexBufferHandle* handle)
{
	auto buffer = (IndexBufferD3D11*)handle;
	auto stride = (UINT)buffer->getStride();
	gContext->context->IASetIndexBuffer(buffer->getD3D11Buffer().Get(), buffer->getStride() == 2 ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT, 0);
}

void BackendD3D11::setUniformBuffer(uint32_t binding, UniformBufferHandle* handle)
{
	auto buffer = (UniformBufferD3D11*)handle;
	gContext->context->VSSetConstantBuffers(binding, 1, buffer->getD3D11Buffer().GetAddressOf());
	gContext->context->PSSetConstantBuffers(binding, 1, buffer->getD3D11Buffer().GetAddressOf());
}

void BackendD3D11::setBlendMode(const std::optional<BlendMode>& blend_mode)
{
	gContext->blend_mode = blend_mode;
	gContext->blend_mode_dirty = true;
}

void BackendD3D11::setDepthMode(const std::optional<DepthMode>& depth_mode)
{
	gContext->depth_stencil_state.depth_mode = depth_mode;
	gContext->depth_stencil_state_dirty = true;
}

void BackendD3D11::setStencilMode(const std::optional<StencilMode>& stencil_mode)
{
	gContext->depth_stencil_state.stencil_mode = stencil_mode;
	gContext->depth_stencil_state_dirty = true;
}

void BackendD3D11::setCullMode(CullMode cull_mode)
{
	gContext->rasterizer_state.cull_mode = cull_mode;
	gContext->rasterizer_state_dirty = true;
}

void BackendD3D11::setSampler(Sampler value)
{
	gContext->sampler_state.sampler = value;
	gContext->sampler_state_dirty = true;
}

void BackendD3D11::setAnisotropyLevel(AnisotropyLevel value)
{
	gContext->sampler_state.anisotropy_level = value;
	gContext->sampler_state_dirty = true;
}

void BackendD3D11::setTextureAddress(TextureAddress value)
{
	gContext->sampler_state.texture_address = value;
	gContext->sampler_state_dirty = true;
}

void BackendD3D11::setFrontFace(FrontFace value)
{
	gContext->rasterizer_state.front_face = value;
	gContext->rasterizer_state_dirty = true;
}

void BackendD3D11::setDepthBias(const std::optional<DepthBias> depth_bias)
{
	gContext->rasterizer_state.depth_bias = depth_bias;
	gContext->rasterizer_state_dirty = true;
}

void BackendD3D11::clear(const std::optional<glm::vec4>& color, const std::optional<float>& depth,
	const std::optional<uint8_t>& stencil)
{
	for (auto target : gContext->render_targets)
	{
		if (color.has_value())
		{
			gContext->context->ClearRenderTargetView(target->getD3D11RenderTargetView().Get(), (float*)&color.value());
		}

		if (depth.has_value() || stencil.has_value())
		{
			UINT flags = 0;

			if (depth.has_value())
				flags |= D3D11_CLEAR_DEPTH;

			if (stencil.has_value())
				flags |= D3D11_CLEAR_STENCIL;

			gContext->context->ClearDepthStencilView(target->getD3D11DepthStencilView().Get(), flags,
				depth.value_or(1.0f), stencil.value_or(0));
		}
	}
}

void BackendD3D11::draw(uint32_t vertex_count, uint32_t vertex_offset, uint32_t instance_count)
{
	EnsureGraphicsState(false);
	gContext->context->DrawInstanced((UINT)vertex_count, (UINT)instance_count, (UINT)vertex_offset, 0);
}

void BackendD3D11::drawIndexed(uint32_t index_count, uint32_t index_offset, uint32_t instance_count)
{
	EnsureGraphicsState(true);
	gContext->context->DrawIndexedInstanced((UINT)index_count, (UINT)instance_count, (UINT)index_offset, 0, 0);
}

void BackendD3D11::copyBackbufferToTexture(const glm::i32vec2& src_pos, const glm::i32vec2& size, const glm::i32vec2& dst_pos,
	TextureHandle* dst_texture_handle)
{
	if (size.x <= 0 || size.y <= 0)
		return;

	auto dst_texture = (TextureD3D11*)dst_texture_handle;
	auto format = gContext->getBackbufferFormat();

	assert(dst_texture->getWidth() >= static_cast<uint32_t>(dst_pos.x + size.x));
	assert(dst_texture->getHeight() >= static_cast<uint32_t>(dst_pos.y + size.y));
	assert(dst_texture->getFormat() == format);

	auto target = gContext->render_targets.at(0);

	ComPtr<ID3D11Resource> rtv_resource;
	target->getD3D11RenderTargetView()->GetResource(rtv_resource.GetAddressOf());

	ComPtr<ID3D11Texture2D> rtv_texture;
	rtv_resource.As(&rtv_texture);
	
	D3D11_TEXTURE2D_DESC desc;
	rtv_texture->GetDesc(&desc);

	if (src_pos.x >= (int)desc.Width || src_pos.y >= (int)desc.Height)
		return;
	
	auto src_x = src_pos.x;
	auto src_y = src_pos.y;
	auto src_w = size.x;
	auto src_h = size.y;
	auto dst_x = dst_pos.x;
	auto dst_y = dst_pos.y;

	if (src_x < 0)
	{
		src_w = std::max(src_w + src_x, 0);
		dst_x -= src_x;
		src_x = 0;
	}

	if (src_y < 0)
	{
		src_h = std::max(src_h + src_y, 0);
		dst_y -= src_y;
		src_y = 0;
	}

	auto src_box = CD3D11_BOX(src_x, src_y, 0, src_x + src_w, src_y + src_h, 1);

	gContext->context->CopySubresourceRegion(dst_texture->getD3D11Texture2D().Get(), 0, dst_x, dst_y, 0,
		rtv_resource.Get(), 0, &src_box);
}

void BackendD3D11::present()
{
	gContext->swapchain->Present(gContext->vsync ? 1 : 0, 0);
}

TextureHandle* BackendD3D11::createTexture(uint32_t width, uint32_t height, PixelFormat format,
	uint32_t mip_count)
{
	auto texture = new TextureD3D11(width, height, format, mip_count);
	return (TextureHandle*)texture;
}

void BackendD3D11::writeTexturePixels(TextureHandle* handle, uint32_t width, uint32_t height, const void* memory,
	uint32_t mip_level, uint32_t offset_x, uint32_t offset_y)
{
	auto texture = (TextureD3D11*)handle;
	texture->write(width, height, memory, mip_level, offset_x, offset_y);
}

std::vector<uint8_t> BackendD3D11::readTexturePixels(TextureHandle* handle, uint32_t mip_level)
{
	auto texture = (TextureD3D11*)handle;
	return texture->read(mip_level);
}

void BackendD3D11::generateMips(TextureHandle* handle)
{
	auto texture = (TextureD3D11*)handle;
	texture->generateMips();
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

ShaderHandle* BackendD3D11::createShader(const std::string& vertex_code, 
	const std::string& fragment_code, const std::vector<std::string>& defines)
{
	auto shader = new ShaderD3D11(vertex_code, fragment_code, defines);
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

void BackendD3D11::writeVertexBufferMemory(VertexBufferHandle* handle, const void* memory, size_t size, size_t stride)
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

void BackendD3D11::writeIndexBufferMemory(IndexBufferHandle* handle, const void* memory, size_t size, size_t stride)
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

void BackendD3D11::writeUniformBufferMemory(UniformBufferHandle* handle, const void* memory, size_t size)
{
	auto buffer = (UniformBufferD3D11*)handle;
	buffer->write(memory, size);
}

#endif
