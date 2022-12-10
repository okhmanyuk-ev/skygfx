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
static uint32_t gBackbufferWidth = 0;
static uint32_t gBackbufferHeight = 0;
static BackendType gBackendType = BackendType::OpenGL;
static std::shared_ptr<VertexBuffer> gDynamicVertexBuffer;
static std::shared_ptr<IndexBuffer> gDynamicIndexBuffer;
static std::unordered_map<uint32_t, std::shared_ptr<UniformBuffer>> gDynamicUniformBuffers;

// texture

Texture::Texture(uint32_t width, uint32_t height, uint32_t channels, void* memory, bool mipmap) :
	mWidth(width),
	mHeight(height)
{
	mTextureHandle = gBackend->createTexture(width, height, channels, memory, mipmap);
}

Texture::~Texture()
{
	if (gBackend)
		gBackend->destroyTexture(mTextureHandle);
}

RenderTarget::RenderTarget(uint32_t width, uint32_t height) : Texture(width, height, 4, nullptr)
{
	mRenderTargetHandle = gBackend->createRenderTarget(width, height, *this);
}

RenderTarget::~RenderTarget()
{
	if (gBackend)
		gBackend->destroyRenderTarget(mRenderTargetHandle);
}

// shader

Shader::Shader(const Vertex::Layout& layout, const std::string& vertex_code, const std::string& fragment_code, const std::vector<std::string>& defines)
{
	mShaderHandle = gBackend->createShader(layout, vertex_code, fragment_code, defines);
}

Shader::~Shader()
{
	if (gBackend)
		gBackend->destroyShader(mShaderHandle);
}

// buffer

Buffer::Buffer(size_t size) : mSize(size)
{
	assert(size > 0);
}

// vertex buffer

VertexBuffer::VertexBuffer(void* memory, size_t size, size_t stride) : Buffer(size)
{
	mVertexBufferHandle = gBackend->createVertexBuffer(size, stride);
	write(memory, size, stride);
}

VertexBuffer::~VertexBuffer()
{
	if (gBackend)
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
	if (gBackend)
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
	if (gBackend)
		gBackend->destroyUniformBuffer(mUniformBufferHandle);
}

void UniformBuffer::write(void* memory, size_t size)
{
	gBackend->writeUniformBufferMemory(mUniformBufferHandle, memory, size);
}

// device

void skygfx::Initialize(void* window, uint32_t width, uint32_t height, std::optional<BackendType> _type)
{
	assert(gBackend == nullptr);

	auto type = _type.value_or(GetDefaultBackend().value());

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

	gBackbufferWidth = width;
	gBackbufferHeight = height;
	gBackendType = type;
}

void skygfx::Finalize()
{
	gDynamicIndexBuffer.reset();
	gDynamicVertexBuffer.reset();
	gDynamicUniformBuffers.clear();

	delete gBackend;
	gBackend = nullptr;
}

void skygfx::Resize(uint32_t width, uint32_t height)
{
	gBackend->resize(width, height);
	gBackbufferWidth = width;
	gBackbufferHeight = height;
}

void skygfx::SetTopology(Topology topology)
{
	gBackend->setTopology(topology);
}

void skygfx::SetViewport(const std::optional<Viewport>& viewport)
{
	gBackend->setViewport(viewport);
}

void skygfx::SetScissor(const std::optional<Scissor>& scissor)
{
	gBackend->setScissor(scissor);
}

void skygfx::SetTexture(uint32_t binding, const Texture& texture)
{
	gBackend->setTexture(binding, const_cast<Texture&>(texture));
}

void skygfx::SetRenderTarget(const RenderTarget& value)
{
	gBackend->setRenderTarget(const_cast<RenderTarget&>(value));
}

void skygfx::SetRenderTarget(std::nullptr_t value)
{
	gBackend->setRenderTarget(value);
}

void skygfx::SetShader(const Shader& shader)
{
	gBackend->setShader(const_cast<Shader&>(shader));
}

void skygfx::SetVertexBuffer(const VertexBuffer& value)
{
	gBackend->setVertexBuffer(const_cast<VertexBuffer&>(value));
}

void skygfx::SetIndexBuffer(const IndexBuffer& value)
{
	gBackend->setIndexBuffer(const_cast<IndexBuffer&>(value));
}

void skygfx::SetUniformBuffer(uint32_t binding, const UniformBuffer& value)
{
	gBackend->setUniformBuffer(binding, const_cast<UniformBuffer&>(value));
}

void skygfx::SetBlendMode(const BlendMode& value)
{
	gBackend->setBlendMode(value);
}

void skygfx::SetDepthMode(const std::optional<DepthMode>& depth_mode)
{
	gBackend->setDepthMode(depth_mode);
}

void skygfx::SetStencilMode(const std::optional<StencilMode>& stencil_mode)
{
	gBackend->setStencilMode(stencil_mode);
}

void skygfx::SetCullMode(CullMode cull_mode)
{
	gBackend->setCullMode(cull_mode);
}

void skygfx::SetSampler(Sampler value)
{
	gBackend->setSampler(value);
}

void skygfx::SetTextureAddress(TextureAddress value)
{
	gBackend->setTextureAddress(value);
}

void skygfx::Clear(const std::optional<glm::vec4>& color, const std::optional<float>& depth,
	const std::optional<uint8_t>& stencil)
{
	gBackend->clear(color, depth, stencil);
}

void skygfx::Draw(uint32_t vertex_count, uint32_t vertex_offset)
{
	gBackend->draw(vertex_count, vertex_offset);
}

void skygfx::DrawIndexed(uint32_t index_count, uint32_t index_offset)
{
	gBackend->drawIndexed(index_count, index_offset);
}

void skygfx::ReadPixels(const glm::ivec2& pos, const glm::ivec2& size, Texture& dst_texture)
{
	gBackend->readPixels(pos, size, dst_texture);
}

void skygfx::Present()
{
	gBackend->present();
}

void skygfx::SetDynamicVertexBuffer(void* memory, size_t size, size_t stride)
{
	assert(size > 0);
	
	size_t vertex_buffer_size = 0;

	if (gDynamicVertexBuffer)
		vertex_buffer_size = gDynamicVertexBuffer->getSize();

	if (vertex_buffer_size < size)
		gDynamicVertexBuffer = std::make_shared<skygfx::VertexBuffer>(memory, size, stride);
	else
		gDynamicVertexBuffer->write(memory, size, stride);

	SetVertexBuffer(*gDynamicVertexBuffer);
}

void skygfx::SetDynamicIndexBuffer(void* memory, size_t size, size_t stride)
{
	assert(size > 0);

	size_t index_buffer_size = 0;

	if (gDynamicIndexBuffer)
		index_buffer_size = gDynamicIndexBuffer->getSize();

	if (index_buffer_size < size)
		gDynamicIndexBuffer = std::make_shared<skygfx::IndexBuffer>(memory, size, stride);
	else
		gDynamicIndexBuffer->write(memory, size, stride);

	SetIndexBuffer(*gDynamicIndexBuffer);
}

void skygfx::SetDynamicUniformBuffer(uint32_t binding, void* memory, size_t size)
{
	assert(size > 0);

	std::shared_ptr<skygfx::UniformBuffer> uniform_buffer = nullptr;

	if (gDynamicUniformBuffers.contains(binding))
		uniform_buffer = gDynamicUniformBuffers.at(binding);

	size_t uniform_buffer_size = 0;

	if (uniform_buffer)
		uniform_buffer_size = uniform_buffer->getSize();

	if (uniform_buffer_size < size)
	{
		uniform_buffer = std::make_shared<skygfx::UniformBuffer>(memory, size);
		gDynamicUniformBuffers[binding] = uniform_buffer;
	}
	else
	{
		uniform_buffer->write(memory, size);
	}

	SetUniformBuffer(binding, *uniform_buffer);
}

uint32_t skygfx::GetBackbufferWidth()
{
	return gBackbufferWidth;
}

uint32_t skygfx::GetBackbufferHeight()
{
	return gBackbufferHeight;
}

BackendType skygfx::GetBackendType()
{
	return gBackendType;
}

std::unordered_set<BackendType> skygfx::GetAvailableBackends()
{
	std::unordered_set<BackendType> result;

#ifdef SKYGFX_HAS_D3D11
	result.insert(BackendType::D3D11);
#endif
#ifdef SKYGFX_HAS_D3D12
	result.insert(BackendType::D3D12);
#endif
#ifdef SKYGFX_HAS_OPENGL
	result.insert(BackendType::OpenGL);
#endif
#ifdef SKYGFX_HAS_VULKAN
	result.insert(BackendType::Vulkan);
#endif
#ifdef SKYGFX_HAS_METAL
	result.insert(BackendType::Metal);
#endif

	return result;
}

std::optional<BackendType> skygfx::GetDefaultBackend()
{
	static const auto backends_by_priority = {
		BackendType::D3D11,
		BackendType::OpenGL,
		BackendType::Metal,
		BackendType::D3D12,
		BackendType::Vulkan
	};

	auto backends = GetAvailableBackends();
	
	for (auto backend : backends_by_priority)
	{
		if (!backends.contains(backend))
			continue;
			
		return backend;
	}

	return std::nullopt;
}
