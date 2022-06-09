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
		virtual void setViewport(const Viewport& viewport) = 0;
		virtual void setScissor(const Scissor& value) = 0;
		virtual void setScissor(std::nullptr_t value) = 0;
		virtual void setTexture(TextureHandle* handle) = 0;
		virtual void setRenderTarget(RenderTargetHandle* handle) = 0;
		virtual void setRenderTarget(std::nullptr_t value) = 0;
		virtual void setShader(ShaderHandle* handle) = 0;
		virtual void setVertexBuffer(const Buffer& buffer) = 0;
		virtual void setIndexBuffer(const Buffer& buffer) = 0;
		virtual void setUniformBuffer(int slot, void* memory, size_t size) = 0;
		virtual void setBlendMode(const BlendMode& value) = 0;
		virtual void setDepthMode(const DepthMode& value) = 0;
		virtual void setStencilMode(const StencilMode& value) = 0;
		virtual void setCullMode(const CullMode& value) = 0;
		virtual void setSampler(const Sampler& value) = 0;
		virtual void setTextureAddressMode(const TextureAddress& value) = 0;

		virtual void clear(const std::optional<glm::vec4>& color, const std::optional<float>& depth,
			const std::optional<uint8_t>& stencil) = 0;
		virtual void draw(size_t vertex_count, size_t vertex_offset) = 0;
		virtual void drawIndexed(uint32_t index_count, uint32_t index_offset) = 0;
		virtual void present() = 0;

		virtual TextureHandle* createTexture(uint32_t width, uint32_t height, uint32_t channels, 
			void* memory, bool mipmap) = 0;
		virtual void destroyTexture(TextureHandle* handle) = 0;

		virtual RenderTargetHandle* createRenderTarget(uint32_t width, uint32_t height, TextureHandle* texture) = 0;
		virtual void destroyRenderTarget(RenderTargetHandle* handle) = 0;

		virtual ShaderHandle* createShader(const Vertex::Layout& layout, const std::string& vertex_code, 
			const std::string& fragment_code) = 0;
		virtual void destroyShader(ShaderHandle* handle) = 0;
	};
}