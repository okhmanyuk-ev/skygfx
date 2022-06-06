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

using namespace skygfx;

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
private:
	ID3D11Texture2D* texture2d;
	ID3D11ShaderResourceView* shader_resource_view;

public:
	TextureDataD3D11(uint32_t width, uint32_t height, uint32_t channels, void* memory)
	{
		D3D11_TEXTURE2D_DESC texture2d_desc = { };
		texture2d_desc.Width = width;
		texture2d_desc.Height = height;
		texture2d_desc.MipLevels = 1;
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

		auto memPitch = width * channels;
		auto memSlicePitch = width * height * channels;
		D3D11Context->UpdateSubresource(texture2d, 0, nullptr, memory, memPitch, memSlicePitch);
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

	D3D11Context->OMSetRenderTargets(1, &MainRenderTarget.render_taget_view, MainRenderTarget.depth_stencil_view);
}

BackendD3D11::~BackendD3D11()
{
	destroyMainRenderTarget();

	D3D11SwapChain->Release();
	D3D11Context->Release();
	D3D11Device->Release();
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

void BackendD3D11::setTexture(TextureHandle* handle)
{
	int slot = 0;
	auto texture = (TextureDataD3D11*)handle;
	texture->bind(slot);
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
	//
}

void BackendD3D11::clear(const std::optional<glm::vec4>& color, const std::optional<float>& depth,
	const std::optional<uint8_t>& stencil)
{
	auto rtv = MainRenderTarget.render_taget_view;
	auto dsv = MainRenderTarget.depth_stencil_view;

	//if (currentRenderTarget != nullptr)
	//{
	//	rtv = currentRenderTarget->mRenderTargetImpl->render_target_view;
	//	dsv = currentRenderTarget->mRenderTargetImpl->depth_stencil_view;
	//}

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

void BackendD3D11::drawIndexed(uint32_t index_count, uint32_t index_offset)
{
	D3D11Context->DrawIndexed((UINT)index_count, (UINT)index_offset, 0);
}

void BackendD3D11::present()
{
	bool vsync = false; // TODO: globalize this var
	D3D11SwapChain->Present(vsync ? 1 : 0, 0);
}

TextureHandle* BackendD3D11::createTexture(uint32_t width, uint32_t height, uint32_t channels, void* memory)
{
	auto texture = new TextureDataD3D11(width, height, channels, memory);
	return (TextureHandle*)texture;
}

void BackendD3D11::destroyTexture(TextureHandle* handle)
{
	auto texture = (TextureDataD3D11*)handle;
	delete texture;
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
