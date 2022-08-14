#include "backend_d3d12.h"

#ifdef SKYGFX_HAS_D3D12

#include <d3dcompiler.h>
#include <d3d12.h>

#pragma comment(lib, "d3d12")
#pragma comment(lib, "d3dcompiler")

using namespace skygfx;

BackendD3D12::BackendD3D12(void* window, uint32_t width, uint32_t height)
{
}

BackendD3D12::~BackendD3D12()
{
}

void BackendD3D12::resize(uint32_t width, uint32_t height)
{
}

void BackendD3D12::setTopology(Topology topology)
{
}

void BackendD3D12::setViewport(std::optional<Viewport> viewport)
{
}

void BackendD3D12::setScissor(std::optional<Scissor> scissor)
{
}

void BackendD3D12::setTexture(uint32_t binding, TextureHandle* handle)
{
}

void BackendD3D12::setRenderTarget(RenderTargetHandle* handle)
{
}

void BackendD3D12::setRenderTarget(std::nullptr_t value)
{
}

void BackendD3D12::setShader(ShaderHandle* handle)
{
}

void BackendD3D12::setVertexBuffer(VertexBufferHandle* handle)
{
}

void BackendD3D12::setIndexBuffer(IndexBufferHandle* handle)
{
}

void BackendD3D12::setUniformBuffer(uint32_t binding, UniformBufferHandle* handle)
{
}

void BackendD3D12::setBlendMode(const BlendMode& value)
{
}

void BackendD3D12::setDepthMode(std::optional<DepthMode> depth_mode)
{
}

void BackendD3D12::setStencilMode(std::optional<StencilMode> stencil_mode)
{
}

void BackendD3D12::setCullMode(CullMode cull_mode)
{
}

void BackendD3D12::setSampler(Sampler value)
{
}

void BackendD3D12::setTextureAddress(TextureAddress value)
{
}

void BackendD3D12::clear(const std::optional<glm::vec4>& color, const std::optional<float>& depth,
	const std::optional<uint8_t>& stencil)
{
}

void BackendD3D12::draw(uint32_t vertex_count, uint32_t vertex_offset)
{
}

void BackendD3D12::drawIndexed(uint32_t index_count, uint32_t index_offset)
{
}

void BackendD3D12::readPixels(const glm::ivec2& pos, const glm::ivec2& size, TextureHandle* dst_texture_handle)
{
}

void BackendD3D12::present()
{
}

TextureHandle* BackendD3D12::createTexture(uint32_t width, uint32_t height, uint32_t channels, void* memory, bool mipmap)
{
	return nullptr;
}

void BackendD3D12::destroyTexture(TextureHandle* handle)
{
}

RenderTargetHandle* BackendD3D12::createRenderTarget(uint32_t width, uint32_t height, TextureHandle* texture_handle)
{
	return nullptr;
}

void BackendD3D12::destroyRenderTarget(RenderTargetHandle* handle)
{
}

ShaderHandle* BackendD3D12::createShader(const Vertex::Layout& layout, const std::string& vertex_code,
	const std::string& fragment_code, const std::vector<std::string>& defines)
{
	return nullptr;
}

void BackendD3D12::destroyShader(ShaderHandle* handle)
{
}

VertexBufferHandle* BackendD3D12::createVertexBuffer(void* memory, size_t size, size_t stride)
{
	return nullptr;
}

void BackendD3D12::destroyVertexBuffer(VertexBufferHandle* handle)
{
}

void BackendD3D12::writeVertexBufferMemory(VertexBufferHandle* handle, void* memory, size_t size, size_t stride)
{
}

IndexBufferHandle* BackendD3D12::createIndexBuffer(void* memory, size_t size, size_t stride)
{
	return nullptr;
}

void BackendD3D12::writeIndexBufferMemory(IndexBufferHandle* handle, void* memory, size_t size, size_t stride)
{
}

void BackendD3D12::destroyIndexBuffer(IndexBufferHandle* handle)
{
}

UniformBufferHandle* BackendD3D12::createUniformBuffer(void* memory, size_t size)
{
	return nullptr;
}

void BackendD3D12::destroyUniformBuffer(UniformBufferHandle* handle)
{
}

void BackendD3D12::writeUniformBufferMemory(UniformBufferHandle* handle, void* memory, size_t size)
{
}

#endif