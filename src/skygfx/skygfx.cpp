#include "skygfx.h"
#include "backend.h"
#include "backend_d3d11.h"
#include "backend_d3d12.h"
#include "backend_gl.h"
#include "backend_vk.h"
#include "backend_mtl.h"

#include <stdexcept>
#include <cassert>

using namespace skygfx;

static Backend* gBackend = nullptr;

// texture

Texture::Texture(uint32_t width, uint32_t height, uint32_t channels, void* memory, bool mipmap) :
	mWidth(width),
	mHeight(height)
{
	mTextureHandle = gBackend->createTexture(width, height, channels, memory, mipmap);
}

Texture::~Texture()
{
	gBackend->destroyTexture(mTextureHandle);
}

RenderTarget::RenderTarget(uint32_t width, uint32_t height) : Texture(width, height, 4, nullptr)
{
	mRenderTargetHandle = gBackend->createRenderTarget(width, height, *this);
}

RenderTarget::~RenderTarget()
{
	gBackend->destroyRenderTarget(mRenderTargetHandle);
}

// shader

Shader::Shader(const Vertex::Layout& layout, const std::string& vertex_code, const std::string& fragment_code, const std::vector<std::string>& defines)
{
	mShaderHandle = gBackend->createShader(layout, vertex_code, fragment_code, defines);
}

Shader::~Shader()
{
	gBackend->destroyShader(mShaderHandle);
}

// buffer

Buffer::Buffer(size_t size) : mSize(size)
{
}

// vertex buffer

VertexBuffer::VertexBuffer(void* memory, size_t size, size_t stride) : Buffer(size)
{
	mVertexBufferHandle = gBackend->createVertexBuffer(size, stride);
	write(memory, size, stride);
}

VertexBuffer::~VertexBuffer()
{
	gBackend->destroyVertexBuffer(mVertexBufferHandle);
}

void VertexBuffer::write(void* memory, size_t size, size_t stride)
{
	gBackend->writeVertexBufferMemory(mVertexBufferHandle, memory, size, stride);
}

// index buffer

IndexBuffer::IndexBuffer(void* memory, size_t size, size_t stride) : Buffer(size)
{
	mIndexBufferHandle = gBackend->createIndexBuffer(size, stride);
	write(memory, size, stride);
}

IndexBuffer::~IndexBuffer()
{
	gBackend->destroyIndexBuffer(mIndexBufferHandle);
}

void IndexBuffer::write(void* memory, size_t size, size_t stride)
{
	gBackend->writeIndexBufferMemory(mIndexBufferHandle, memory, size, stride);
}

// uniform buffer

UniformBuffer::UniformBuffer(void* memory, size_t size) : Buffer(size)
{
	mUniformBufferHandle = gBackend->createUniformBuffer(size);
	write(memory, size);
}

UniformBuffer::~UniformBuffer()
{
	gBackend->destroyUniformBuffer(mUniformBufferHandle);
}

void UniformBuffer::write(void* memory, size_t size)
{
	gBackend->writeUniformBufferMemory(mUniformBufferHandle, memory, size);
}

// device

Device::Device(void* window, uint32_t width, uint32_t height, std::optional<BackendType> _type)
{
	assert(gBackend == nullptr);

	auto type = _type.value_or(GetDefaultBackend());

#ifdef SKYGFX_HAS_D3D11
	if (type == BackendType::D3D11)
		gBackend = new BackendD3D11(window, width, height);
#endif
#ifdef SKYGFX_HAS_D3D12
	if (type == BackendType::D3D12)
		gBackend = new BackendD3D12(window, width, height);
#endif
#ifdef SKYGFX_HAS_OPENGL
	if (type == BackendType::OpenGL)
		gBackend = new BackendGL(window, width, height);
#endif
#ifdef SKYGFX_HAS_VULKAN
	if (type == BackendType::Vulkan)
		gBackend = new BackendVK(window, width, height);
#endif
#ifdef SKYGFX_HAS_METAL
	if (type == BackendType::Metal)
		gBackend = new BackendMetal(window, width, height);
#endif

	if (gBackend == nullptr)
		throw std::runtime_error("backend not implemented");

	mBackbufferWidth = width;
	mBackbufferHeight = height;
}

Device::~Device()
{
	mDynamicIndexBuffer.reset();
	mDynamicVertexBuffer.reset();
	mDynamicUniformBuffers.clear();

	delete gBackend;
	gBackend = nullptr;
}

void Device::resize(uint32_t width, uint32_t height)
{
	gBackend->resize(width, height);
	mBackbufferWidth = width;
	mBackbufferHeight = height;
}

void Device::setTopology(Topology topology)
{
	gBackend->setTopology(topology);
}

void Device::setViewport(std::optional<Viewport> viewport)
{
	gBackend->setViewport(viewport);
}

void Device::setScissor(std::optional<Scissor> scissor)
{
	gBackend->setScissor(scissor);
}

void Device::setTexture(uint32_t binding, const Texture& texture)
{
	gBackend->setTexture(binding, const_cast<Texture&>(texture));
}

void Device::setRenderTarget(const RenderTarget& value)
{
	gBackend->setRenderTarget(const_cast<RenderTarget&>(value));
}

void Device::setRenderTarget(std::nullptr_t value)
{
	gBackend->setRenderTarget(value);
}

void Device::setShader(const Shader& shader)
{
	gBackend->setShader(const_cast<Shader&>(shader));
}

void Device::setVertexBuffer(const VertexBuffer& value)
{
	gBackend->setVertexBuffer(const_cast<VertexBuffer&>(value));
}

void Device::setIndexBuffer(const IndexBuffer& value)
{
	gBackend->setIndexBuffer(const_cast<IndexBuffer&>(value));
}

void Device::setUniformBuffer(uint32_t binding, const UniformBuffer& value)
{
	gBackend->setUniformBuffer(binding, const_cast<UniformBuffer&>(value));
}

void Device::setBlendMode(const BlendMode& value)
{
	gBackend->setBlendMode(value);
}

void Device::setDepthMode(std::optional<DepthMode> depth_mode)
{
	gBackend->setDepthMode(depth_mode);
}

void Device::setStencilMode(std::optional<StencilMode> stencil_mode)
{
	gBackend->setStencilMode(stencil_mode);
}

void Device::setCullMode(CullMode cull_mode)
{
	gBackend->setCullMode(cull_mode);
}

void Device::setSampler(Sampler value)
{
	gBackend->setSampler(value);
}

void Device::setTextureAddress(TextureAddress value)
{
	gBackend->setTextureAddress(value);
}

void Device::clear(const std::optional<glm::vec4>& color, const std::optional<float>& depth, 
	const std::optional<uint8_t>& stencil)
{
	gBackend->clear(color, depth, stencil);
}

void Device::draw(uint32_t vertex_count, uint32_t vertex_offset)
{
	gBackend->draw(vertex_count, vertex_offset);
}

void Device::drawIndexed(uint32_t index_count, uint32_t index_offset)
{
	gBackend->drawIndexed(index_count, index_offset);
}

void Device::readPixels(const glm::ivec2& pos, const glm::ivec2& size, Texture& dst_texture)
{
	gBackend->readPixels(pos, size, dst_texture);
}

void Device::present()
{
	gBackend->present();
}

void Device::setDynamicVertexBuffer(void* memory, size_t size, size_t stride)
{
	assert(size > 0);
	
	size_t vertex_buffer_size = 0;

	if (mDynamicVertexBuffer)
		vertex_buffer_size = mDynamicVertexBuffer->getSize();

	if (vertex_buffer_size < size)
		mDynamicVertexBuffer = std::make_shared<skygfx::VertexBuffer>(memory, size, stride);
	else
		mDynamicVertexBuffer->write(memory, size, stride);

	setVertexBuffer(*mDynamicVertexBuffer);
}

void Device::setDynamicIndexBuffer(void* memory, size_t size, size_t stride)
{
	assert(size > 0);

	size_t index_buffer_size = 0;

	if (mDynamicIndexBuffer)
		index_buffer_size = mDynamicIndexBuffer->getSize();

	if (index_buffer_size < size)
		mDynamicIndexBuffer = std::make_shared<skygfx::IndexBuffer>(memory, size, stride);
	else
		mDynamicIndexBuffer->write(memory, size, stride);

	setIndexBuffer(*mDynamicIndexBuffer);
}

void Device::setDynamicUniformBuffer(uint32_t binding, void* memory, size_t size)
{
	assert(size > 0);

	std::shared_ptr<skygfx::UniformBuffer> uniform_buffer = nullptr;

	if (mDynamicUniformBuffers.contains(binding))
		uniform_buffer = mDynamicUniformBuffers.at(binding);

	size_t uniform_buffer_size = 0;

	if (uniform_buffer)
		uniform_buffer_size = uniform_buffer->getSize();

	if (uniform_buffer_size < size)
	{
		uniform_buffer = std::make_shared<skygfx::UniformBuffer>(memory, size);
		mDynamicUniformBuffers[binding] = uniform_buffer;
	}
	else
	{
		uniform_buffer->write(memory, size);
	}

	setUniformBuffer(binding, *uniform_buffer);
}

uint32_t Device::getBackbufferWidth() const
{
	return mBackbufferWidth;
}

uint32_t Device::getBackbufferHeight() const
{
	return mBackbufferHeight;
}

std::vector<BackendType> Device::GetAvailableBackends()
{
	std::vector<BackendType> result;

#ifdef SKYGFX_HAS_D3D11
	result.push_back(BackendType::D3D11);
#endif
#ifdef SKYGFX_HAS_D3D12
	result.push_back(BackendType::D3D12);
#endif
#ifdef SKYGFX_HAS_OPENGL
	result.push_back(BackendType::OpenGL);
#endif
#ifdef SKYGFX_HAS_VULKAN
	result.push_back(BackendType::Vulkan);
#endif
#ifdef SKYGFX_HAS_METAL
	result.push_back(BackendType::Metal);
#endif

	return result;
}

BackendType Device::GetDefaultBackend()
{
	auto backends = GetAvailableBackends();
	
	if (backends.empty())
		throw std::runtime_error("no available backends");

	return backends.at(0);
}
