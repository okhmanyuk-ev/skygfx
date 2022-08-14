#pragma once

#ifdef SKYGFX_HAS_D3D12

#include "backend.h"

namespace skygfx
{
	class BackendD3D12 : public Backend
	{
	public:
		BackendD3D12(void* window, uint32_t width, uint32_t height);
		~BackendD3D12();

		void resize(uint32_t width, uint32_t height) override;

		void setTopology(Topology topology) override;
		void setViewport(std::optional<Viewport> viewport) override;
		void setScissor(std::optional<Scissor> scissor) override;
		void setTexture(uint32_t binding, TextureHandle* handle) override;
		void setRenderTarget(RenderTargetHandle* handle) override;
		void setRenderTarget(std::nullptr_t value) override;
		void setShader(ShaderHandle* handle) override;
		void setVertexBuffer(VertexBufferHandle* handle) override;
		void setIndexBuffer(IndexBufferHandle* handle) override;
		void setUniformBuffer(uint32_t binding, UniformBufferHandle* handle) override;
		void setBlendMode(const BlendMode& value) override;
		void setDepthMode(std::optional<DepthMode> depth_mode) override;
		void setStencilMode(std::optional<StencilMode> stencil_mode) override;
		void setCullMode(CullMode cull_mode) override;
		void setSampler(Sampler value) override;
		void setTextureAddress(TextureAddress value) override;

		void clear(const std::optional<glm::vec4>& color, const std::optional<float>& depth,
			const std::optional<uint8_t>& stencil) override;
		void draw(uint32_t vertex_count, uint32_t vertex_offset) override;
		void drawIndexed(uint32_t index_count, uint32_t index_offset) override;

		void readPixels(const glm::ivec2& pos, const glm::ivec2& size, TextureHandle* dst_texture) override;

		void present() override;

		TextureHandle* createTexture(uint32_t width, uint32_t height, uint32_t channels, 
			void* memory, bool mipmap) override;
		void destroyTexture(TextureHandle* handle) override;

		RenderTargetHandle* createRenderTarget(uint32_t width, uint32_t height, TextureHandle* texture) override;
		void destroyRenderTarget(RenderTargetHandle* handle) override;

		ShaderHandle* createShader(const Vertex::Layout& layout, const std::string& vertex_code, 
			const std::string& fragment_code, const std::vector<std::string>& defines) override;
		void destroyShader(ShaderHandle* handle) override;

		VertexBufferHandle* createVertexBuffer(void* memory, size_t size, size_t stride) override;
		void destroyVertexBuffer(VertexBufferHandle* handle) override;
		void writeVertexBufferMemory(VertexBufferHandle* handle, void* memory, size_t size, size_t stride) override;

		IndexBufferHandle* createIndexBuffer(void* memory, size_t size, size_t stride) override;
		void destroyIndexBuffer(IndexBufferHandle* handle) override;
		void writeIndexBufferMemory(IndexBufferHandle* handle, void* memory, size_t size, size_t stride) override;

		UniformBufferHandle* createUniformBuffer(void* memory, size_t size) override;
		void destroyUniformBuffer(UniformBufferHandle* handle) override;
		void writeUniformBufferMemory(UniformBufferHandle* handle, void* memory, size_t size) override;

	private:
		void begin();
		void end();

		void createRenderTarget();
		void cleanupRenderTarget();
	};
}

#endif