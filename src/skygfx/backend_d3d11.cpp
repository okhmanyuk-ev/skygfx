#include "backend_d3d11.h"

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
static ID3D11Buffer* D3D11ConstantBuffer = nullptr;

template <class T> inline void combine(size_t& seed, const T& v)
{
	std::hash<T> hasher;
	seed ^= hasher(v) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
}

using namespace skygfx;

struct BlendModeHasher
{
	size_t operator()(const BlendMode& k) const
	{
		size_t seed = 0;
		combine(seed, k.alphaBlendFunction);
		combine(seed, k.alphaDstBlend);
		combine(seed, k.alphaSrcBlend);
		combine(seed, k.colorBlendFunction);
		combine(seed, k.colorDstBlend);
		combine(seed, k.colorSrcBlend);
		return seed;
	}
};

static std::unordered_map<BlendMode, ID3D11BlendState*, BlendModeHasher> D3D11BlendModes;

struct DepthStencilState
{
	DepthMode depthMode;
	StencilMode stencilMode;

	bool operator==(const DepthStencilState& value) const
	{
		return depthMode == value.depthMode && stencilMode == value.stencilMode;
	}

	bool operator!=(const DepthStencilState& value) const
	{
		return !(value == *this);
	}

	struct Hasher
	{
		size_t operator()(const DepthStencilState& k) const
		{
			size_t seed = 0;
			combine(seed, k.depthMode.enabled);
			combine(seed, k.depthMode.func);
			combine(seed, k.stencilMode.enabled);
			combine(seed, k.stencilMode.readMask);
			combine(seed, k.stencilMode.writeMask);
			combine(seed, k.stencilMode.depthFailOp);
			combine(seed, k.stencilMode.failOp);
			combine(seed, k.stencilMode.func);
			combine(seed, k.stencilMode.passOp);
			return seed;
		}
	};

	struct Comparer
	{
		bool operator()(const DepthStencilState& left, const DepthStencilState& right) const
		{
			return left == right;
		}
	};
};

static DepthStencilState D3D11DepthStencilState;
static bool D3D11DepthStencilStateDirty = true;
static std::unordered_map<DepthStencilState, ID3D11DepthStencilState*, DepthStencilState::Hasher, DepthStencilState::Comparer> D3D11DepthStencilStates;

struct RasterizerState
{
	bool scissorEnabled = false;
	CullMode cullMode = CullMode::None;

	bool operator==(const RasterizerState& value) const
	{
		return scissorEnabled == value.scissorEnabled && cullMode == value.cullMode;
	}

	bool operator!=(const RasterizerState& value) const
	{
		return !(value == *this);
	}

	struct Hasher
	{
		size_t operator()(const RasterizerState& k) const
		{
			size_t seed = 0;
			combine(seed, k.cullMode);
			combine(seed, k.scissorEnabled);
			return seed;
		}
	};

	struct Comparer
	{
		bool operator()(const RasterizerState& left, const RasterizerState& right) const
		{
			return left == right;
		}
	};
};

static std::unordered_map<RasterizerState, ID3D11RasterizerState*, RasterizerState::Hasher, RasterizerState::Comparer> D3D11RasterizerStates;
static RasterizerState D3D11RasterizerState;
static bool D3D11RasterizerStateDirty = true;

struct SamplerState
{
	Sampler sampler = Sampler::Linear;
	TextureAddress textureAddress = TextureAddress::Clamp;

	struct Hasher
	{
		size_t operator()(const SamplerState& k) const
		{
			size_t seed = 0;
			combine(seed, k.sampler);
			combine(seed, k.textureAddress);
			return seed;
		}
	};

	struct Comparer
	{
		bool operator()(const SamplerState& left, const SamplerState& right) const
		{
			return
				left.sampler == right.sampler &&
				left.textureAddress == right.textureAddress;
		}
	};
};

static std::unordered_map<SamplerState, ID3D11SamplerState*, SamplerState::Hasher, SamplerState::Comparer> D3D11SamplerStates;
static SamplerState D3D11SamplerState;
static bool D3D11SamplerStateDirty = true;

class ShaderDataD3D11
{
private:
	ID3D11VertexShader* vertex_shader = nullptr;
	ID3D11PixelShader* pixel_shader = nullptr;
	ID3D11InputLayout* input_layout = nullptr;

public:
	ShaderDataD3D11(const Vertex::Layout& layout, const std::string& vertex_code, const std::string& fragment_code)
	{
		ID3DBlob* vertexShaderBlob;
		ID3DBlob* pixelShaderBlob;

		ID3DBlob* vertex_shader_error;
		ID3DBlob* pixel_shader_error;
		
		std::vector<std::string> defines;
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

public:
	TextureDataD3D11(uint32_t width, uint32_t height, uint32_t channels, void* memory, bool mipmap)
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

public:
	RenderTargetDataD3D11(uint32_t width, uint32_t height, TextureDataD3D11* _texture_data) : texture_data(_texture_data)
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

	for (const auto& [_, blend_state] : D3D11BlendModes)
	{
		blend_state->Release();
	}
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

void BackendD3D11::setViewport(const Viewport& viewport)
{
	D3D11_VIEWPORT vp;
	vp.Width = viewport.size.x;
	vp.Height = viewport.size.y;
	vp.MinDepth = viewport.min_depth;
	vp.MaxDepth = viewport.max_depth;
	vp.TopLeftX = viewport.position.x;
	vp.TopLeftY = viewport.position.y;
	D3D11Context->RSSetViewports(1, &vp);
}

void BackendD3D11::setScissor(const Scissor& value)
{
	D3D11RasterizerState.scissorEnabled = true;

	D3D11_RECT rect;
	rect.left = static_cast<LONG>(value.position.x);
	rect.top = static_cast<LONG>(value.position.y);
	rect.right = static_cast<LONG>(value.position.x + value.size.x);
	rect.bottom = static_cast<LONG>(value.position.y + value.size.y);
	D3D11Context->RSSetScissorRects(1, &rect);

	D3D11RasterizerStateDirty = true;
}

void BackendD3D11::setScissor(std::nullptr_t value)
{
	D3D11RasterizerState.scissorEnabled = false;
	D3D11RasterizerStateDirty = true;
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
}

void BackendD3D11::setRenderTarget(std::nullptr_t value)
{
	D3D11Context->OMSetRenderTargets(1, &MainRenderTarget.render_taget_view, MainRenderTarget.depth_stencil_view);
	D3D11CurrentRenderTarget = nullptr;
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

	if (D3D11ConstantBuffer)
		D3D11ConstantBuffer->GetDesc(&desc);

	if (desc.ByteWidth < size)
	{
		if (D3D11ConstantBuffer)
			D3D11ConstantBuffer->Release();

		desc.ByteWidth = static_cast<UINT>(size);
		desc.Usage = D3D11_USAGE_DYNAMIC;
		desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		D3D11Device->CreateBuffer(&desc, nullptr, &D3D11ConstantBuffer);
	}

	D3D11_MAPPED_SUBRESOURCE resource;
	D3D11Context->Map(D3D11ConstantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &resource);
	memcpy(resource.pData, memory, size);
	D3D11Context->Unmap(D3D11ConstantBuffer, 0);

	D3D11Context->VSSetConstantBuffers(slot, 1, &D3D11ConstantBuffer);
	D3D11Context->PSSetConstantBuffers(slot, 1, &D3D11ConstantBuffer);
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

void BackendD3D11::setDepthMode(const DepthMode& value)
{
	D3D11DepthStencilState.depthMode = value;
	D3D11DepthStencilStateDirty = true;
}

void BackendD3D11::setStencilMode(const StencilMode& value)
{
	D3D11DepthStencilState.stencilMode = value;
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

void BackendD3D11::draw(size_t vertex_count, size_t vertex_offset)
{
	prepareForDrawing();
	D3D11Context->Draw((UINT)vertex_count, (UINT)vertex_offset);
}

void BackendD3D11::drawIndexed(uint32_t index_count, uint32_t index_offset)
{
	prepareForDrawing();
	D3D11Context->DrawIndexed((UINT)index_count, (UINT)index_offset, 0);
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

ShaderHandle* BackendD3D11::createShader(const Vertex::Layout& layout, const std::string& vertex_code, const std::string& fragment_code)
{
	auto shader = new ShaderDataD3D11(layout, vertex_code, fragment_code);
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

		const auto& value = D3D11DepthStencilState;

		if (D3D11DepthStencilStates.count(value) == 0)
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
			desc.DepthEnable = value.depthMode.enabled;
			desc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
			desc.DepthFunc = ComparisonFuncMap.at(value.depthMode.func);

			desc.StencilEnable = value.stencilMode.enabled;
			desc.StencilReadMask = value.stencilMode.readMask;
			desc.StencilWriteMask = value.stencilMode.writeMask;

			desc.FrontFace.StencilDepthFailOp = StencilOpMap.at(value.stencilMode.depthFailOp);
			desc.FrontFace.StencilFailOp = StencilOpMap.at(value.stencilMode.failOp);
			desc.FrontFace.StencilFunc = ComparisonFuncMap.at(value.stencilMode.func);
			desc.FrontFace.StencilPassOp = StencilOpMap.at(value.stencilMode.passOp);

			desc.BackFace = desc.FrontFace;

			D3D11Device->CreateDepthStencilState(&desc, &D3D11DepthStencilStates[value]);
		}

		D3D11Context->OMSetDepthStencilState(D3D11DepthStencilStates.at(value), value.stencilMode.reference);
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
}