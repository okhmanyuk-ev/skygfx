#include "skygfx.h"
#include "backend.h"
#include "backend_d3d11.h"
#include "backend_gl44.h"

#include <cassert>

using namespace skygfx;

static Backend* gBackend = nullptr;

// texture

Texture::Texture(uint32_t width, uint32_t height, uint32_t channels, void* memory)
{
	mTextureHandle = gBackend->createTexture(width, height, channels, memory);
}

Texture::~Texture()
{
	gBackend->destroyTexture(mTextureHandle);
}

// shader

Shader::Shader(const Vertex::Layout& layout, const std::string& vertex_code, const std::string& fragment_code)
{
	mShaderHandle = gBackend->createShader(layout, vertex_code, fragment_code);
}

Shader::~Shader()
{
	gBackend->destroyShader(mShaderHandle);
}

// device

Device::Device(BackendType type, void* window, uint32_t width, uint32_t height)
{
	assert(gBackend == nullptr);

	if (type == BackendType::D3D11)
		gBackend = new BackendD3D11(window, width, height);
	else if (type == BackendType::OpenGL44)
		gBackend = new BackendGL44(window);
}

Device::~Device()
{
	delete gBackend;
	gBackend = nullptr;
}

void Device::setTopology(Topology topology)
{
	gBackend->setTopology(topology);
}

void Device::setViewport(const Viewport& viewport)
{
	gBackend->setViewport(viewport);
}

void Device::setTexture(const Texture& texture)
{
	gBackend->setTexture(const_cast<Texture&>(texture));
}

void Device::setShader(const Shader& shader)
{
	gBackend->setShader(const_cast<Shader&>(shader));
}

void Device::setVertexBuffer(const Buffer& buffer)
{
	gBackend->setVertexBuffer(buffer);
}

void Device::setIndexBuffer(const Buffer& buffer)
{
	gBackend->setIndexBuffer(buffer);
}

void Device::setUniformBuffer(int slot, void* memory, size_t size)
{
	gBackend->setUniformBuffer(slot, memory, size);
}

void Device::setBlendMode(const BlendMode& value)
{
	gBackend->setBlendMode(value);
}

void Device::clear(const std::optional<glm::vec4>& color, const std::optional<float>& depth, 
	const std::optional<uint8_t>& stencil)
{
	gBackend->clear(color, depth, stencil);
}

void Device::drawIndexed(uint32_t index_count, uint32_t index_offset)
{
	gBackend->drawIndexed(index_count, index_offset);
}

void Device::present()
{
	gBackend->present();
}
