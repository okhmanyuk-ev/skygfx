#include "skygfx.h"
#include "backend.h"
#include "backend_d3d11.h"
#include "backend_d3d12.h"
#include "backend_gl.h"
#include "backend_vk.h"
#include "backend_mtl.h"

using namespace skygfx;

static Backend* gBackend = nullptr;
static glm::u32vec2 gSize = { 0, 0 };
static std::optional<glm::u32vec2> gRenderTargetSize;
static BackendType gBackendType = BackendType::OpenGL;
static std::optional<VertexBuffer> gVertexBuffer;
static std::optional<IndexBuffer> gIndexBuffer;
static std::unordered_map<uint32_t, UniformBuffer> gUniformBuffers;

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

// raytracing shader

RaytracingShader::RaytracingShader(const std::string& raygen_code, const std::string& miss_code,
	const std::string& closesthit_code, const std::vector<std::string>& defines)
{
	mRaytracingShaderHandle = gBackend->createRaytracingShader(raygen_code, miss_code, closesthit_code, defines);
}

RaytracingShader::~RaytracingShader()
{
	if (gBackend)
		gBackend->destroyRaytracingShader(mRaytracingShaderHandle);
}

// buffer

Buffer::Buffer(size_t size) : mSize(size)
{
	assert(size > 0);
}

Buffer::Buffer(Buffer&& other) noexcept
{
	if (this == &other)
		return;

	mSize = other.mSize;
	other.mSize = 0;
}

Buffer& Buffer::operator=(Buffer&& other) noexcept
{
	if (this == &other)
		return *this;

	mSize = other.mSize;
	other.mSize = 0;
	return *this;
}

// vertex buffer

VertexBuffer::VertexBuffer(size_t size, size_t stride) : Buffer(size)
{
	mVertexBufferHandle = gBackend->createVertexBuffer(size, stride);
}

VertexBuffer::VertexBuffer(void* memory, size_t size, size_t stride) : VertexBuffer(size, stride)
{
	write(memory, size, stride);
}

VertexBuffer::VertexBuffer(VertexBuffer&& other) noexcept : Buffer(std::move(other))
{
	if (this == &other)
		return;

	mVertexBufferHandle = other.mVertexBufferHandle;
	other.mVertexBufferHandle = nullptr;
}

VertexBuffer::~VertexBuffer()
{
	if (gBackend && mVertexBufferHandle)
		gBackend->destroyVertexBuffer(mVertexBufferHandle);
}

VertexBuffer& VertexBuffer::operator=(VertexBuffer&& other) noexcept
{
	Buffer::operator=(std::move(other));

	if (this == &other)
		return *this;

	mVertexBufferHandle = other.mVertexBufferHandle;
	other.mVertexBufferHandle = nullptr;
	return *this;
}

void VertexBuffer::write(void* memory, size_t size, size_t stride)
{
	gBackend->writeVertexBufferMemory(mVertexBufferHandle, memory, size, stride);
}

// index buffer

IndexBuffer::IndexBuffer(size_t size, size_t stride) : Buffer(size)
{
	mIndexBufferHandle = gBackend->createIndexBuffer(size, stride);
}

IndexBuffer::IndexBuffer(void* memory, size_t size, size_t stride) : IndexBuffer(size, stride)
{
	write(memory, size, stride);
}

IndexBuffer::IndexBuffer(IndexBuffer&& other) noexcept : Buffer(std::move(other))
{
	if (this == &other)
		return;

	mIndexBufferHandle = other.mIndexBufferHandle;
	other.mIndexBufferHandle = nullptr;
}

IndexBuffer::~IndexBuffer()
{
	if (gBackend && mIndexBufferHandle)
		gBackend->destroyIndexBuffer(mIndexBufferHandle);
}

IndexBuffer& IndexBuffer::operator=(IndexBuffer&& other) noexcept
{
	Buffer::operator=(std::move(other));

	if (this == &other)
		return *this;

	mIndexBufferHandle = other.mIndexBufferHandle;
	other.mIndexBufferHandle = nullptr;
	return *this;
}

void IndexBuffer::write(void* memory, size_t size, size_t stride)
{
	gBackend->writeIndexBufferMemory(mIndexBufferHandle, memory, size, stride);
}

// uniform buffer

UniformBuffer::UniformBuffer(size_t size) : Buffer(size)
{
	mUniformBufferHandle = gBackend->createUniformBuffer(size);
}

UniformBuffer::UniformBuffer(void* memory, size_t size) : UniformBuffer(size)
{
	write(memory, size);
}

UniformBuffer::UniformBuffer(UniformBuffer&& other) noexcept : Buffer(std::move(other))
{
	if (this == &other)
		return;

	mUniformBufferHandle = other.mUniformBufferHandle;
	other.mUniformBufferHandle = nullptr;
}

UniformBuffer::~UniformBuffer()
{
	if (gBackend && mUniformBufferHandle)
		gBackend->destroyUniformBuffer(mUniformBufferHandle);
}

UniformBuffer& UniformBuffer::operator=(UniformBuffer&& other) noexcept
{
	Buffer::operator=(std::move(other));

	if (this == &other)
		return *this;

	mUniformBufferHandle = other.mUniformBufferHandle;
	other.mUniformBufferHandle = nullptr;
	return *this;
}

void UniformBuffer::write(void* memory, size_t size)
{
	gBackend->writeUniformBufferMemory(mUniformBufferHandle, memory, size);
}

// acceleration structure

AccelerationStructure::AccelerationStructure(const std::vector<glm::vec3>& vertices,
	const std::vector<uint32_t>& indices)
{
	mAccelerationStructureHandle = gBackend->createAccelerationStructure(vertices, indices);
}

AccelerationStructure::~AccelerationStructure()
{
	if (gBackend && mAccelerationStructureHandle)
		gBackend->destroyAccelerationStructure(mAccelerationStructureHandle);
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

	gSize = { width, height };
	gBackendType = type;
}

void skygfx::Finalize()
{
	gIndexBuffer.reset();
	gVertexBuffer.reset();
	gUniformBuffers.clear();

	delete gBackend;
	gBackend = nullptr;
}

void skygfx::Resize(uint32_t width, uint32_t height)
{
	gBackend->resize(width, height);
	gSize = { width, height };
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
	gRenderTargetSize = { value.getWidth(), value.getHeight() };
}

void skygfx::SetRenderTarget(std::nullopt_t value)
{
	gBackend->setRenderTarget(value);
	gRenderTargetSize.reset();
}

void skygfx::SetShader(const Shader& shader)
{
	gBackend->setShader(const_cast<Shader&>(shader));
}

void skygfx::SetShader(const RaytracingShader& shader)
{
	gBackend->setRaytracingShader(const_cast<RaytracingShader&>(shader));
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

void skygfx::SetAccelerationStructure(uint32_t binding, const AccelerationStructure& value)
{
	gBackend->setAccelerationStructure(binding, const_cast<AccelerationStructure&>(value));
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

void skygfx::ReadPixels(const glm::i32vec2& pos, const glm::i32vec2& size, Texture& dst_texture)
{
	gBackend->readPixels(pos, size, dst_texture);
}

void skygfx::DispatchRays(uint32_t width, uint32_t height, uint32_t depth)
{
	gBackend->dispatchRays(width, height, depth);
}

void skygfx::Present()
{
	gBackend->present();
}

void skygfx::SetVertexBuffer(void* memory, size_t size, size_t stride)
{
	assert(size > 0);
	
	size_t vertex_buffer_size = 0;

	if (gVertexBuffer.has_value())
		vertex_buffer_size = gVertexBuffer->getSize();

	if (vertex_buffer_size < size)
		gVertexBuffer.emplace(memory, size, stride);
	else
		gVertexBuffer.value().write(memory, size, stride);

	SetVertexBuffer(gVertexBuffer.value());
}

void skygfx::SetIndexBuffer(void* memory, size_t size, size_t stride)
{
	assert(size > 0);

	size_t index_buffer_size = 0;

	if (gIndexBuffer.has_value())
		index_buffer_size = gIndexBuffer->getSize();

	if (index_buffer_size < size)
		gIndexBuffer.emplace(memory, size, stride);
	else
		gIndexBuffer.value().write(memory, size, stride);

	SetIndexBuffer(gIndexBuffer.value());
}

void skygfx::SetUniformBuffer(uint32_t binding, void* memory, size_t size)
{
	assert(size > 0);
	
	if (!gUniformBuffers.contains(binding))
		gUniformBuffers.emplace(binding, size);

	auto& buffer = gUniformBuffers.at(binding);

	if (buffer.getSize() < size)
		buffer = UniformBuffer(size);

	buffer.write(memory, size);

	SetUniformBuffer(binding, buffer);
}

uint32_t skygfx::GetWidth()
{
	return gSize.x;
}

uint32_t skygfx::GetHeight()
{
	return gSize.y;
}

uint32_t skygfx::GetBackbufferWidth()
{
	return gRenderTargetSize.value_or(gSize).x;
}

uint32_t skygfx::GetBackbufferHeight()
{
	return gRenderTargetSize.value_or(gSize).y;
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
