#pragma once

#ifdef SKYGFX_HAS_D3D11

#include "backend.h"

namespace skygfx
{
	class BackendD3D11 : public Backend
	{
	public:
		BackendD3D11(void* window, uint32_t width, uint32_t height);
		~BackendD3D11();

		void resize(uint32_t width, uint32_t height) override;

		void setTopology(Topology topology) override;
		void setViewport(std::optional<Viewport> viewport) override;
		void setScissor(std::optional<Scissor> scissor) override;
		void setTexture(TextureHandle* handle, uint32_t slot) override;
		void setRenderTarget(RenderTargetHandle* handle) override;
		void setRenderTarget(std::nullptr_t value) override;
		void setShader(ShaderHandle* handle) override;
		void setVertexBuffer(const Buffer& buffer) override;
		void setIndexBuffer(const Buffer& buffer) override;
		void setUniformBuffer(uint32_t slot, void* memory, size_t size) override;
		void setBlendMode(const BlendMode& value) override;
		void setDepthMode(std::optional<DepthMode> depth_mode) override;
		void setStencilMode(std::optional<StencilMode> stencil_mode) override;
		void setCullMode(CullMode cull_mode) override;
		void setSampler(const Sampler& value) override;
		void setTextureAddressMode(const TextureAddress& value) override;

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

	private:
		void createMainRenderTarget(uint32_t width, uint32_t height);
		void destroyMainRenderTarget();

		void prepareForDrawing();

	private:
		std::optional<Viewport> mViewport;
		bool mViewportDirty = true;
		uint32_t mBackbufferWidth = 0;
		uint32_t mBackbufferHeight = 0;
	};
}

#endif