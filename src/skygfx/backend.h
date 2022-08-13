#pragma once

#include "skygfx.h"

namespace skygfx
{
	class Backend
	{
	public:
		virtual ~Backend() {}

		virtual void resize(uint32_t width, uint32_t height) = 0;

		virtual void setTopology(Topology topology) = 0;
		virtual void setViewport(std::optional<Viewport> viewport) = 0;
		virtual void setScissor(std::optional<Scissor> scissor) = 0;
		virtual void setTexture(uint32_t binding, TextureHandle* handle) = 0;
		virtual void setRenderTarget(RenderTargetHandle* handle) = 0;
		virtual void setRenderTarget(std::nullptr_t value) = 0;
		virtual void setShader(ShaderHandle* handle) = 0;
		virtual void setVertexBuffer(VertexBufferHandle* handle) = 0;
		virtual void setIndexBuffer(IndexBufferHandle* handle) = 0;
		virtual void setUniformBuffer(uint32_t binding, void* memory, size_t size) = 0;
		virtual void setBlendMode(const BlendMode& value) = 0;
		virtual void setDepthMode(std::optional<DepthMode> depth_mode) = 0;
		virtual void setStencilMode(std::optional<StencilMode> stencil_mode) = 0;
		virtual void setCullMode(CullMode cull_mode) = 0;
		virtual void setSampler(Sampler value) = 0;
		virtual void setTextureAddress(TextureAddress value) = 0;

		virtual void clear(const std::optional<glm::vec4>& color, const std::optional<float>& depth,
			const std::optional<uint8_t>& stencil) = 0;
		virtual void draw(uint32_t vertex_count, uint32_t vertex_offset) = 0;
		virtual void drawIndexed(uint32_t index_count, uint32_t index_offset) = 0;

		virtual void readPixels(const glm::ivec2& pos, const glm::ivec2& size, TextureHandle* dst_texture) = 0;

		virtual void present() = 0;

		virtual TextureHandle* createTexture(uint32_t width, uint32_t height, uint32_t channels, 
			void* memory, bool mipmap) = 0;
		virtual void destroyTexture(TextureHandle* handle) = 0;

		virtual RenderTargetHandle* createRenderTarget(uint32_t width, uint32_t height, TextureHandle* texture) = 0;
		virtual void destroyRenderTarget(RenderTargetHandle* handle) = 0;

		virtual ShaderHandle* createShader(const Vertex::Layout& layout, const std::string& vertex_code, 
			const std::string& fragment_code, const std::vector<std::string>& defines) = 0;
		virtual void destroyShader(ShaderHandle* handle) = 0;

		virtual VertexBufferHandle* createVertexBuffer(void* memory, size_t size, size_t stride) = 0;
		virtual void destroyVertexBuffer(VertexBufferHandle* handle) = 0;

		virtual IndexBufferHandle* createIndexBuffer(void* memory, size_t size, size_t stride) = 0;
		virtual void destroyIndexBuffer(IndexBufferHandle* handle) = 0;
	};
}