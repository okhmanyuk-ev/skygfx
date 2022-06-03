#include "backend_d3d11.h"

#include <d3dcompiler.h>
#include <d3d11.h>
#pragma comment(lib, "d3d11")
#pragma comment(lib, "d3dcompiler")

#include <vector>

// TODO: rename D3D11Name -> gName

static IDXGISwapChain* D3D11SwapChain = nullptr;
static ID3D11Device* D3D11Device = nullptr;
static ID3D11DeviceContext* D3D11Context = nullptr;

static struct
{
	ID3D11Texture2D* texture2d;
	ID3D11RenderTargetView* render_taget_view;
	ID3D11DepthStencilView* depth_stencil_view;
} MainRenderTarget;

using namespace skygfx;

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
}

BackendD3D11::~BackendD3D11()
{
	destroyMainRenderTarget();

	D3D11SwapChain->Release();
	D3D11Context->Release();
	D3D11Device->Release();
}

void BackendD3D11::clear(float r, float g, float b, float a)
{
	std::vector<float> color = {
		r, g, b, a
	};
	D3D11Context->ClearRenderTargetView(MainRenderTarget.render_taget_view, (float*)color.data());
}

void BackendD3D11::present()
{
	bool vsync = false; // TODO: globalize this var
	D3D11SwapChain->Present(vsync ? 1 : 0, 0);
}

TextureHandle* BackendD3D11::createTexture()
{
	return nullptr;
}

void BackendD3D11::destroyTexture(TextureHandle* handle)
{
	//
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
