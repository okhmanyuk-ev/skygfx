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
	mVertexBufferHandle = gBackend->createVertexBuffer(memory, size, stride);
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
	mIndexBufferHandle = gBackend->createIndexBuffer(memory, size, stride);
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
	mUniformBufferHandle = gBackend->createUniformBuffer(memory, size);
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

Device::Device(BackendType type, void* window, uint32_t width, uint32_t height)
{
	assert(gBackend == nullptr);

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
}

Device::~Device()
{
	delete gBackend;
	gBackend = nullptr;
}

void Device::resize(uint32_t width, uint32_t height)
{
	gBackend->resize(width, height);
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

// stack device

StackDevice::StackDevice(std::shared_ptr<Device> device) :
	mDevice(device)
{
	push(State());
}

StackDevice::~StackDevice()
{
	assert(mStates.size() == 1);
}

void StackDevice::resize(uint32_t width, uint32_t height)
{
	mDevice->resize(width, height);
	mAppliedState.reset();
}

void StackDevice::applyState(bool clearing)
{
	const auto& state = mStates.top();
	
	if (mAppliedState.has_value() && mAppliedState.value() == state)
		return;

	bool topology_changed = true;
	bool viewport_changed = true;
	bool scissor_changed = true;
	bool textures_changed = true;
	bool render_target_changed = true;
	bool shader_changed = true;
	bool vertex_buffer_changed = true;
	bool index_buffer_changed = true;
	bool uniform_buffers_changed = true;
	bool blend_mode_changed = true;
	bool depth_mode_changed = true;
	bool stencil_mode_changed = true;
	bool cull_mode_changed = true;
	bool sampler_changed = true;
	bool texture_address_changed = true;

	if (mAppliedState.has_value())
	{
		const auto& applied_state = mAppliedState.value();

		topology_changed = state.topology != applied_state.topology;
		viewport_changed = state.viewport != applied_state.viewport;
		scissor_changed = state.scissor != applied_state.scissor;
		textures_changed = state.textures != applied_state.textures;
		render_target_changed = state.render_target != applied_state.render_target;
		shader_changed = state.shader != applied_state.shader;
		vertex_buffer_changed = state.vertex_buffer != applied_state.vertex_buffer;
		index_buffer_changed = state.index_buffer != applied_state.index_buffer;
		uniform_buffers_changed = state.uniform_buffers != applied_state.uniform_buffers;
		blend_mode_changed = state.blend_mode != applied_state.blend_mode;
		depth_mode_changed = state.depth_mode != applied_state.depth_mode;
		stencil_mode_changed = state.stencil_mode != applied_state.stencil_mode;
		cull_mode_changed = state.cull_mode != applied_state.cull_mode;
		sampler_changed = state.sampler != applied_state.sampler;
		texture_address_changed = state.texture_address != applied_state.texture_address;
	}

	if (topology_changed)
		mDevice->setTopology(state.topology);

	if (viewport_changed)
		mDevice->setViewport(state.viewport);

	if (scissor_changed)
		mDevice->setScissor(state.scissor);

	if (textures_changed)
	{
		for (const auto& [binding, texture] : state.textures)
		{
			mDevice->setTexture(binding, *texture);
		}
	}

	if (render_target_changed)
	{
		if (state.render_target == nullptr)
			mDevice->setRenderTarget(nullptr);
		else
			mDevice->setRenderTarget(*state.render_target);
	}

	if (shader_changed && !clearing)
		mDevice->setShader(*state.shader);

	if (vertex_buffer_changed && !clearing)
		mDevice->setVertexBuffer(*state.vertex_buffer);

	if (index_buffer_changed && !clearing)
		mDevice->setIndexBuffer(*state.index_buffer);
	
	if (uniform_buffers_changed)
	{
		for (const auto& [binding, uniform_buffer] : state.uniform_buffers)
		{
			mDevice->setUniformBuffer(binding, *uniform_buffer);
		}
	}

	if (blend_mode_changed)
		mDevice->setBlendMode(state.blend_mode);

	if (depth_mode_changed)
		mDevice->setDepthMode(state.depth_mode);

	if (stencil_mode_changed)
		mDevice->setStencilMode(state.stencil_mode);

	if (cull_mode_changed)
		mDevice->setCullMode(state.cull_mode);

	if (sampler_changed)
		mDevice->setSampler(state.sampler);

	if (texture_address_changed)
		mDevice->setTextureAddress(state.texture_address);

	mAppliedState = state;
}

void StackDevice::push(const State& state)
{
	mStates.push(state);
}

void StackDevice::pop(size_t count)
{
	assert(mStates.size() - 1 >= count);

	for (size_t i = 0; i < count; i++)
	{
		mStates.pop();
	}
}

void StackDevice::pushTopology(Topology topology)
{
	auto state = mStates.top();
	state.topology = topology;
	push(state);
}

void StackDevice::pushViewport(std::optional<Viewport> viewport)
{
	auto state = mStates.top();
	state.viewport = viewport;
	push(state);
}

void StackDevice::pushScissor(std::optional<Scissor> scissor)
{
	auto state = mStates.top();
	state.scissor = scissor;
	push(state);
}

void StackDevice::pushTexture(uint32_t binding, std::shared_ptr<Texture> texture)
{
	auto state = mStates.top();
	state.textures[binding] = texture;
	push(state);
}

void StackDevice::pushRenderTarget(std::shared_ptr<RenderTarget> value)
{
	auto state = mStates.top();
	state.render_target = value;
	push(state);
}

void StackDevice::pushShader(std::shared_ptr<Shader> shader)
{
	auto state = mStates.top();
	state.shader = shader;
	push(state);
}

void StackDevice::pushVertexBuffer(std::shared_ptr<VertexBuffer> value)
{
	auto state = mStates.top();
	state.vertex_buffer = value;
	push(state);
}

void StackDevice::pushIndexBuffer(std::shared_ptr<IndexBuffer> value)
{
	auto state = mStates.top();
	state.index_buffer = value;
	push(state);
}

void StackDevice::pushUniformBuffer(uint32_t binding, std::shared_ptr<UniformBuffer> value)
{
	auto state = mStates.top();
	state.uniform_buffers[binding] = value;
	push(state);
}

void StackDevice::pushBlendMode(const BlendMode& value)
{
	auto state = mStates.top();
	state.blend_mode = value;
	push(state);
}

void StackDevice::pushDepthMode(std::optional<DepthMode> depth_mode)
{
	auto state = mStates.top();
	state.depth_mode = depth_mode;
	push(state);
}

void StackDevice::pushStencilMode(std::optional<StencilMode> stencil_mode)
{
	auto state = mStates.top();
	state.stencil_mode = stencil_mode;
	push(state);
}

void StackDevice::pushCullMode(CullMode cull_mode)
{
	auto state = mStates.top();
	state.cull_mode = cull_mode;
	push(state);
}

void StackDevice::pushSampler(Sampler value)
{
	auto state = mStates.top();
	state.sampler = value;
	push(state);
}

void StackDevice::pushTextureAddress(TextureAddress value)
{
	auto state = mStates.top();
	state.texture_address = value;
	push(state);
}

void StackDevice::clear(const std::optional<glm::vec4>& color, const std::optional<float>& depth, 
	const std::optional<uint8_t>& stencil)
{
	applyState(true);
	mDevice->clear(color);
}

void StackDevice::draw(uint32_t vertex_count, uint32_t vertex_offset)
{
	applyState();
	mDevice->draw(vertex_count, vertex_offset);
}

void StackDevice::drawIndexed(uint32_t index_count, uint32_t index_offset)
{
	applyState();
	mDevice->drawIndexed(index_count, index_offset);
}

void StackDevice::readPixels(const glm::ivec2& pos, const glm::ivec2& size, Texture& dst_texture)
{
	applyState();
	mDevice->readPixels(pos, size, dst_texture);
}

void StackDevice::present()
{
	assert(mStates.size() == 1);
	mDevice->present();
}
