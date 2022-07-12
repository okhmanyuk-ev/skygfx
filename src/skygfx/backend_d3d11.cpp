#include "backend_d3d11.h"

#ifdef SKYGFX_HAS_D3D11

#include <stdexcept>
#include <vector>
#include <unordered_map>

#include <d3dcompiler.h>
#include <d3d11.h>

#pragma comment(lib, "d3d11")
#pragma comment(lib, "d3dcompiler")

static IDXGISwapChain* D3D11SwapChain = nullptr;
static ID3D11Device* D3D11Device = nullptr;
static ID3D11DeviceContext* D3D11Context = nullptr;

static struct
{
	ID3D11Texture2D* texture2d;
	ID3D11RenderTargetView* render_taget_view;
	ID3D11DepthStencilView* depth_stencil_view;
} MainRenderTarget;

static ID3D11Buffer* D3D11VertexBuffer = nullptr;
static ID3D11Buffer* D3D11IndexBuffer = nullptr;
static std::unordered_map<int, ID3D11Buffer*> D3D11ConstantBuffers;

using namespace skygfx;

static std::unordered_map<BlendMode, ID3D11BlendState*> D3D11BlendModes;

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

static DepthStencilState D3D11DepthStencilState;
static bool D3D11DepthStencilStateDirty = true;
static std::unordered_map<DepthStencilState, ID3D11DepthStencilState*> D3D11DepthStencilStates;

struct RasterizerState
{
	bool scissorEnabled = false;
	CullMode cullMode = CullMode::None;

	bool operator==(const RasterizerState& value) const
	{
		return scissorEnabled == value.scissorEnabled && cullMode == value.cullMode;
	}
};

SKYGFX_MAKE_HASHABLE(RasterizerState,
	t.cullMode,
	t.scissorEnabled);

static std::unordered_map<RasterizerState, ID3D11RasterizerState*> D3D11RasterizerStates;
static RasterizerState D3D11RasterizerState;
static bool D3D11RasterizerStateDirty = true;

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

static std::unordered_map<SamplerState, ID3D11SamplerState*> D3D11SamplerStates;
static SamplerState D3D11SamplerState;
static bool D3D11SamplerStateDirty = true;

class ShaderDataD3D11
{
private:
	ID3D11VertexShader* vertex_shader = nullptr;
	ID3D11PixelShader* pixel_shader = nullptr;
	ID3D11InputLayout* input_layout = nullptr;

public:
	ShaderDataD3D11(const Vertex::Layout& layout, const std::string& vertex_code, const std::string& fragment_code, 
		std::vector<std::string> defines)
	{
		ID3DBlob* vertexShaderBlob;
		ID3DBlob* pixelShaderBlob;

		ID3DBlob* vertex_shader_error;
		ID3DBlob* pixel_shader_error;
		
		AddShaderLocationDefines(layout, defines);

		auto vertex_shader_spirv = CompileGlslToSpirv(ShaderStage::Vertex, vertex_code, defines);
		auto fragment_shader_spirv = CompileGlslToSpirv(ShaderStage::Fragment, fragment_code, defines);

		auto hlsl_vert = CompileSpirvToHlsl(vertex_shader_spirv);
		auto hlsl_frag = CompileSpirvToHlsl(fragment_shader_spirv);

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

		D3D11Device->CreateVertexShader(vertexShaderBlob->GetBufferPointer(), vertexShaderBlob->GetBufferSize(), nullptr, &vertex_shader);
		D3D11Device->CreatePixelShader(pixelShaderBlob->GetBufferPointer(), pixelShaderBlob->GetBufferSize(), nullptr, &pixel_shader);

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

		D3D11Device->CreateInputLayout(input.data(), static_cast<UINT>(input.size()), vertexShaderBlob->GetBufferPointer(), vertexShaderBlob->GetBufferSize(), &input_layout);
	}

	~ShaderDataD3D11()
	{
		vertex_shader->Release();
		pixel_shader->Release();
		input_layout->Release();
	}

	void apply()
	{
		D3D11Context->IASetInputLayout(input_layout);
		D3D11Context->VSSetShader(vertex_shader, nullptr, 0);
		D3D11Context->PSSetShader(pixel_shader, nullptr, 0);
	}
};

class TextureDataD3D11
{
	friend class RenderTargetDataD3D11;
	friend class BackendD3D11;

private:
	ID3D11Texture2D* texture2d;
	ID3D11ShaderResourceView* shader_resource_view;
	uint32_t width;
	uint32_t height;
	bool mipmap;

public:
	TextureDataD3D11(uint32_t _width, uint32_t _height, uint32_t channels, void* memory, bool _mipmap) :
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
		D3D11Device->CreateTexture2D(&texture2d_desc, nullptr, &texture2d);

		D3D11_SHADER_RESOURCE_VIEW_DESC shader_resource_view_desc = { };
		shader_resource_view_desc.Format = texture2d_desc.Format;
		shader_resource_view_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		shader_resource_view_desc.Texture2D.MipLevels = -1;
		shader_resource_view_desc.Texture2D.MostDetailedMip = 0;
		D3D11Device->CreateShaderResourceView(texture2d, &shader_resource_view_desc, &shader_resource_view);

		if (memory)
		{
			auto memPitch = width * channels;
			auto memSlicePitch = width * height * channels;
			D3D11Context->UpdateSubresource(texture2d, 0, nullptr, memory, memPitch, memSlicePitch);

			if (mipmap)
				D3D11Context->GenerateMips(shader_resource_view);
		}
	}

	~TextureDataD3D11()
	{
		shader_resource_view->Release();
		texture2d->Release();
	}

	void bind(int slot)
	{
		D3D11Context->PSSetShaderResources((UINT)slot, 1, &shader_resource_view);
	}
};

class RenderTargetDataD3D11
{
	friend class BackendD3D11;

private:
	ID3D11RenderTargetView* render_target_view;
	ID3D11Texture2D* depth_stencil_texture;
	ID3D11DepthStencilView* depth_stencil_view;
	TextureDataD3D11* texture_data;
	uint32_t width;
	uint32_t height;

public:
	RenderTargetDataD3D11(uint32_t _width, uint32_t _height, TextureDataD3D11* _texture_data) : 
		texture_data(_texture_data), width(_width), height(_height)
	{
		D3D11_RENDER_TARGET_VIEW_DESC render_target_view_desc = { };
		render_target_view_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		render_target_view_desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
		render_target_view_desc.Texture2D.MipSlice = 0;
		D3D11Device->CreateRenderTargetView(texture_data->texture2d, &render_target_view_desc, &render_target_view);

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
		D3D11Device->CreateTexture2D(&texture2d_desc, nullptr, &depth_stencil_texture);

		D3D11_DEPTH_STENCIL_VIEW_DESC depth_stencil_view_desc = {};
		depth_stencil_view_desc.Format = texture2d_desc.Format;
		depth_stencil_view_desc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
		D3D11Device->CreateDepthStencilView(depth_stencil_texture, &depth_stencil_view_desc, &depth_stencil_view);
	}

	~RenderTargetDataD3D11()
	{
		render_target_view->Release();
		depth_stencil_texture->Release();
		depth_stencil_view->Release();
	}
};

static RenderTargetDataD3D11* D3D11CurrentRenderTarget = nullptr;

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
		static_cast<UINT>(features.size()), D3D11_SDK_VERSION, &sd, &D3D11SwapChain, &D3D11Device,
		nullptr, &D3D11Context);

	createMainRenderTarget(width, height);
	setRenderTarget(nullptr);
}

BackendD3D11::~BackendD3D11()
{
	destroyMainRenderTarget();

	D3D11SwapChain->Release();
	D3D11Context->Release();
	D3D11Device->Release();

	D3D11VertexBuffer->Release();
	D3D11IndexBuffer->Release();
	
	for (auto [slot, buffer] : D3D11ConstantBuffers)
	{
		buffer->Release();
	}

	for (const auto& [_, blend_state] : D3D11BlendModes)
	{
		blend_state->Release();
	}
}

void BackendD3D11::resize(uint32_t width, uint32_t height)
{
	destroyMainRenderTarget();
	D3D11SwapChain->ResizeBuffers(0, (UINT)width, (UINT)height, DXGI_FORMAT_R8G8B8A8_UNORM, 0);
	createMainRenderTarget(width, height);
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

	D3D11Context->IASetPrimitiveTopology(TopologyMap.at(topology));
}

void BackendD3D11::setViewport(std::optional<Viewport> viewport)
{
	mViewport = viewport;
	mViewportDirty = true;
}

void BackendD3D11::setScissor(std::optional<Scissor> scissor)
{
	if (scissor.has_value())
	{
		auto value = scissor.value();

		D3D11RasterizerState.scissorEnabled = true;

		D3D11_RECT rect;
		rect.left = static_cast<LONG>(value.position.x);
		rect.top = static_cast<LONG>(value.position.y);
		rect.right = static_cast<LONG>(value.position.x + value.size.x);
		rect.bottom = static_cast<LONG>(value.position.y + value.size.y);
		D3D11Context->RSSetScissorRects(1, &rect);

		D3D11RasterizerStateDirty = true;
	}
	else
	{
		D3D11RasterizerState.scissorEnabled = false;
		D3D11RasterizerStateDirty = true;
	}
}

void BackendD3D11::setTexture(TextureHandle* handle)
{
	int slot = 0;
	auto texture = (TextureDataD3D11*)handle;
	texture->bind(slot);
}

void BackendD3D11::setRenderTarget(RenderTargetHandle* handle)
{
	auto render_target = (RenderTargetDataD3D11*)handle;

	ID3D11ShaderResourceView* prev_shader_resource_view;
	D3D11Context->PSGetShaderResources(0, 1, &prev_shader_resource_view);

	if (prev_shader_resource_view == render_target->texture_data->shader_resource_view)
	{
		ID3D11ShaderResourceView* null[] = { nullptr };
		D3D11Context->PSSetShaderResources(0, 1, null); // remove old shader view
	}

	if (prev_shader_resource_view)
		prev_shader_resource_view->Release(); // avoid memory leak

	D3D11Context->OMSetRenderTargets(1, &render_target->render_target_view, render_target->depth_stencil_view);

	D3D11CurrentRenderTarget = render_target;
	
	if (!mViewport.has_value())
		mViewportDirty = true;
}

void BackendD3D11::setRenderTarget(std::nullptr_t value)
{
	D3D11Context->OMSetRenderTargets(1, &MainRenderTarget.render_taget_view, MainRenderTarget.depth_stencil_view);
	D3D11CurrentRenderTarget = nullptr;

	if (!mViewport.has_value())
		mViewportDirty = true;
}

void BackendD3D11::setShader(ShaderHandle* handle)
{
	auto shader = (ShaderDataD3D11*)handle;
	shader->apply();
}

void BackendD3D11::setVertexBuffer(const Buffer& buffer)
{
	D3D11_BUFFER_DESC desc = {};

	if (D3D11VertexBuffer)
		D3D11VertexBuffer->GetDesc(&desc);

	if (desc.ByteWidth < buffer.size)
	{
		if (D3D11VertexBuffer)
			D3D11VertexBuffer->Release();

		desc.ByteWidth = static_cast<UINT>(buffer.size);
		desc.Usage = D3D11_USAGE_DYNAMIC;
		desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		D3D11Device->CreateBuffer(&desc, nullptr, &D3D11VertexBuffer);
	}

	D3D11_MAPPED_SUBRESOURCE resource;
	D3D11Context->Map(D3D11VertexBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &resource);
	memcpy(resource.pData, buffer.data, buffer.size);
	D3D11Context->Unmap(D3D11VertexBuffer, 0);

	auto stride = static_cast<UINT>(buffer.stride);
	auto offset = static_cast<UINT>(0);

	D3D11Context->IASetVertexBuffers(0, 1, &D3D11VertexBuffer, &stride, &offset);
}

void BackendD3D11::setIndexBuffer(const Buffer& buffer)
{
	D3D11_BUFFER_DESC desc = {};

	if (D3D11IndexBuffer)
		D3D11IndexBuffer->GetDesc(&desc);

	if (desc.ByteWidth < buffer.size)
	{
		if (D3D11IndexBuffer)
			D3D11IndexBuffer->Release();

		desc.ByteWidth = static_cast<UINT>(buffer.size);
		desc.Usage = D3D11_USAGE_DYNAMIC;
		desc.BindFlags = D3D11_BIND_INDEX_BUFFER;
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		D3D11Device->CreateBuffer(&desc, nullptr, &D3D11IndexBuffer);
	}

	D3D11_MAPPED_SUBRESOURCE resource;
	D3D11Context->Map(D3D11IndexBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &resource);
	memcpy(resource.pData, buffer.data, buffer.size);
	D3D11Context->Unmap(D3D11IndexBuffer, 0);

	D3D11Context->IASetIndexBuffer(D3D11IndexBuffer, buffer.stride == 2 ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT, 0);
}

void BackendD3D11::setUniformBuffer(int slot, void* memory, size_t size)
{
	assert(size % 16 == 0);

	D3D11_BUFFER_DESC desc = {};

	if (D3D11ConstantBuffers.contains(slot))
		D3D11ConstantBuffers.at(slot)->GetDesc(&desc);

	if (desc.ByteWidth < size)
	{
		if (D3D11ConstantBuffers.contains(slot))
			D3D11ConstantBuffers.at(slot)->Release();

		desc.ByteWidth = static_cast<UINT>(size);
		desc.Usage = D3D11_USAGE_DYNAMIC;
		desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		D3D11Device->CreateBuffer(&desc, nullptr, &D3D11ConstantBuffers[slot]);
	}

	auto constant_buffer = D3D11ConstantBuffers.at(slot);

	D3D11_MAPPED_SUBRESOURCE resource;
	D3D11Context->Map(constant_buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &resource);
	memcpy(resource.pData, memory, size);
	D3D11Context->Unmap(constant_buffer, 0);

	D3D11Context->VSSetConstantBuffers(slot, 1, &constant_buffer);
	D3D11Context->PSSetConstantBuffers(slot, 1, &constant_buffer);
}

void BackendD3D11::setBlendMode(const BlendMode& value)
{
	if (D3D11BlendModes.count(value) == 0)
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

		D3D11Device->CreateBlendState(&desc, &D3D11BlendModes[value]);
	}

	const float blend_factor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	D3D11Context->OMSetBlendState(D3D11BlendModes.at(value), blend_factor, 0xFFFFFFFF);
}

void BackendD3D11::setDepthMode(std::optional<DepthMode> depth_mode)
{
	D3D11DepthStencilState.depth_mode = depth_mode;
	D3D11DepthStencilStateDirty = true;
}

void BackendD3D11::setStencilMode(std::optional<StencilMode> stencil_mode)
{
	D3D11DepthStencilState.stencil_mode = stencil_mode;
	D3D11DepthStencilStateDirty = true;
}

void BackendD3D11::setCullMode(const CullMode& value)
{
	D3D11RasterizerState.cullMode = value;
	D3D11RasterizerStateDirty = true;
}

void BackendD3D11::setSampler(const Sampler& value)
{
	D3D11SamplerState.sampler = value;
	D3D11SamplerStateDirty = true;
}

void BackendD3D11::setTextureAddressMode(const TextureAddress& value)
{
	D3D11SamplerState.textureAddress = value;
	D3D11SamplerStateDirty = true;
}

void BackendD3D11::clear(const std::optional<glm::vec4>& color, const std::optional<float>& depth,
	const std::optional<uint8_t>& stencil)
{
	auto rtv = MainRenderTarget.render_taget_view;
	auto dsv = MainRenderTarget.depth_stencil_view;

	if (D3D11CurrentRenderTarget != nullptr)
	{
		rtv = D3D11CurrentRenderTarget->render_target_view;
		dsv = D3D11CurrentRenderTarget->depth_stencil_view;
	}

	if (color.has_value())
	{
		D3D11Context->ClearRenderTargetView(rtv, (float*)&color.value());
	}

	if (depth.has_value() || stencil.has_value())
	{
		UINT flags = 0;

		if (depth.has_value())
			flags |= D3D11_CLEAR_DEPTH;

		if (stencil.has_value())
			flags |= D3D11_CLEAR_STENCIL;

		D3D11Context->ClearDepthStencilView(dsv, flags, depth.value_or(1.0f), stencil.value_or(0));
	}
}

void BackendD3D11::draw(uint32_t vertex_count, uint32_t vertex_offset)
{
	prepareForDrawing();
	D3D11Context->Draw((UINT)vertex_count, (UINT)vertex_offset);
}

void BackendD3D11::drawIndexed(uint32_t index_count, uint32_t index_offset)
{
	prepareForDrawing();
	D3D11Context->DrawIndexed((UINT)index_count, (UINT)index_offset, 0);
}

void BackendD3D11::readPixels(const glm::ivec2& pos, const glm::ivec2& size, TextureHandle* dst_texture_handle)
{
	auto dst_texture = (TextureDataD3D11*)dst_texture_handle;

	assert(dst_texture->width == size.x);
	assert(dst_texture->height == size.y);

	if (size.x <= 0 || size.y <= 0)
		return;

	ID3D11Resource* resource = NULL;

	if (D3D11CurrentRenderTarget)
		D3D11CurrentRenderTarget->render_target_view->GetResource(&resource);
	else
		MainRenderTarget.render_taget_view->GetResource(&resource);

	ID3D11Texture2D* texture = NULL;
	resource->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&texture);

	D3D11_TEXTURE2D_DESC desc = { 0 };
	texture->GetDesc(&desc);
	auto back_w = desc.Width;
	auto back_h = desc.Height;
	texture->Release();

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
		D3D11Context->CopySubresourceRegion(dst_texture->texture2d, 0, dst_x, dst_y, 0, resource, 0, &box);

		if (dst_texture->mipmap)
			D3D11Context->GenerateMips(dst_texture->shader_resource_view);
	}

	resource->Release();
}

void BackendD3D11::present()
{
	bool vsync = false; // TODO: globalize this var
	D3D11SwapChain->Present(vsync ? 1 : 0, 0);
}

TextureHandle* BackendD3D11::createTexture(uint32_t width, uint32_t height, uint32_t channels, void* memory, bool mipmap)
{
	auto texture = new TextureDataD3D11(width, height, channels, memory, mipmap);
	return (TextureHandle*)texture;
}

void BackendD3D11::destroyTexture(TextureHandle* handle)
{
	auto texture = (TextureDataD3D11*)handle;
	delete texture;
}

RenderTargetHandle* BackendD3D11::createRenderTarget(uint32_t width, uint32_t height, TextureHandle* texture_handle)
{
	auto texture = (TextureDataD3D11*)texture_handle;
	auto render_target = new RenderTargetDataD3D11(width, height, texture);
	return (RenderTargetHandle*)render_target;
}

void BackendD3D11::destroyRenderTarget(RenderTargetHandle* handle)
{
	auto render_target = (RenderTargetDataD3D11*)handle;
	delete render_target;
}

ShaderHandle* BackendD3D11::createShader(const Vertex::Layout& layout, const std::string& vertex_code, 
	const std::string& fragment_code, const std::vector<std::string>& defines)
{
	auto shader = new ShaderDataD3D11(layout, vertex_code, fragment_code, defines);
	return (ShaderHandle*)shader;
}

void BackendD3D11::destroyShader(ShaderHandle* handle)
{
	auto shader = (ShaderDataD3D11*)handle;
	delete shader;
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
	D3D11Device->CreateTexture2D(&desc, nullptr, &MainRenderTarget.texture2d);

	D3D11_DEPTH_STENCIL_VIEW_DESC descDSV = {};
	descDSV.Format = desc.Format;
	descDSV.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;

	D3D11Device->CreateDepthStencilView(MainRenderTarget.texture2d, &descDSV, &MainRenderTarget.depth_stencil_view);

	ID3D11Texture2D* pBackBuffer;
	D3D11SwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (LPVOID*)&pBackBuffer);
	D3D11Device->CreateRenderTargetView(pBackBuffer, nullptr, &MainRenderTarget.render_taget_view);
	pBackBuffer->Release();

	mBackbufferWidth = width;
	mBackbufferHeight = height;
}

void BackendD3D11::destroyMainRenderTarget()
{
	MainRenderTarget.render_taget_view->Release();
	MainRenderTarget.depth_stencil_view->Release();
	MainRenderTarget.texture2d->Release();
}

void BackendD3D11::prepareForDrawing()
{
	// depthstencil state

	if (D3D11DepthStencilStateDirty)
	{
		D3D11DepthStencilStateDirty = false;

		const auto& depth_stencil_state = D3D11DepthStencilState;

		auto depth_mode = depth_stencil_state.depth_mode.value_or(DepthMode());
		auto stencil_mode = depth_stencil_state.stencil_mode.value_or(StencilMode());

		if (D3D11DepthStencilStates.count(depth_stencil_state) == 0)
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

			D3D11Device->CreateDepthStencilState(&desc, &D3D11DepthStencilStates[depth_stencil_state]);
		}

		D3D11Context->OMSetDepthStencilState(D3D11DepthStencilStates.at(depth_stencil_state), stencil_mode.reference);
	}

	// rasterizer state

	if (D3D11RasterizerStateDirty)
	{
		D3D11RasterizerStateDirty = false;

		const auto& value = D3D11RasterizerState;

		if (D3D11RasterizerStates.count(value) == 0)
		{
			const static std::unordered_map<CullMode, D3D11_CULL_MODE> CullMap = {
				{ CullMode::None, D3D11_CULL_NONE },
				{ CullMode::Front, D3D11_CULL_FRONT },
				{ CullMode::Back, D3D11_CULL_BACK }
			};

			D3D11_RASTERIZER_DESC desc = {};
			desc.FillMode = D3D11_FILL_SOLID;
			desc.CullMode = CullMap.at(value.cullMode);
			desc.ScissorEnable = value.scissorEnabled;
			desc.DepthClipEnable = true;
			D3D11Device->CreateRasterizerState(&desc, &D3D11RasterizerStates[value]);
		}

		D3D11Context->RSSetState(D3D11RasterizerStates.at(value));
	}

	// sampler state

	if (D3D11SamplerStateDirty)
	{
		D3D11SamplerStateDirty = false;

		const auto& value = D3D11SamplerState;

		if (D3D11SamplerStates.count(value) == 0)
		{
			// TODO: see D3D11_ENCODE_BASIC_FILTER

			const static std::unordered_map<Sampler, D3D11_FILTER> SamplerMap = {
				{ Sampler::Linear, D3D11_FILTER_MIN_MAG_MIP_LINEAR  },
				{ Sampler::Nearest, D3D11_FILTER_MIN_MAG_MIP_POINT },
				{ Sampler::LinearMipmapLinear, D3D11_FILTER_MIN_MAG_MIP_LINEAR }
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
			D3D11Device->CreateSamplerState(&desc, &D3D11SamplerStates[value]);
		}

		D3D11Context->PSSetSamplers(0, 1, &D3D11SamplerStates.at(value));
	}

	// viewport

	if (mViewportDirty)
	{
		float width;
		float height;

		if (D3D11CurrentRenderTarget == nullptr)
		{
			width = static_cast<float>(mBackbufferWidth);
			height = static_cast<float>(mBackbufferHeight);
		}
		else
		{
			width = static_cast<float>(D3D11CurrentRenderTarget->width);
			height = static_cast<float>(D3D11CurrentRenderTarget->height);
		}

		auto viewport = mViewport.value_or(Viewport{ { 0.0f, 0.0f }, { width, height } });

		D3D11_VIEWPORT vp;
		vp.Width = viewport.size.x;
		vp.Height = viewport.size.y;
		vp.MinDepth = viewport.min_depth;
		vp.MaxDepth = viewport.max_depth;
		vp.TopLeftX = viewport.position.x;
		vp.TopLeftY = viewport.position.y;
		D3D11Context->RSSetViewports(1, &vp);

		mViewportDirty = false;
	}
}

#endif