#include "skygfx.h"
#include "backend.h"
#include "backend_d3d11.h"
#include "backend_gl44.h"

#include <cassert>

using namespace skygfx;

// device

static Backend* gBackend = nullptr;

Device::Device(BackendType type, void* window)
{
	assert(gBackend == nullptr);

	if (type == BackendType::D3D11)
		gBackend = new BackendD3D11(window);
	else if (type == BackendType::OpenGL44)
		gBackend = new BackendGL44(window);
}

Device::~Device()
{
	delete gBackend;
	gBackend = nullptr;
}

void Device::setTexture(Texture& texture)
{
	gBackend->setTexture(texture);
}

void Device::clear(float r, float g, float b, float a)
{
	gBackend->clear(r, g, b, a);
}

void Device::present()
{
	gBackend->present();
}

// texture

Texture::Texture()
{
	mTextureHandle = gBackend->createTexture();
}

Texture::~Texture()
{
	gBackend->destroyTexture(mTextureHandle);
}

