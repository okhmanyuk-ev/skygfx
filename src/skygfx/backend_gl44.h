#pragma once

#ifdef SKYGFX_HAS_OPENGL

#include "backend.h"

namespace skygfx
{
	class BackendGL44 : public Backend
	{
	public:
		BackendGL44(void* window, uint32_t width, uint32_t height);
		~BackendGL44();

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
		void drawIndexed(uint32_t index_count, uint32_t index_offset = 0) override;
		
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
		void prepareForDrawing();

		void setInternalVertexBuffer(const Buffer& value);
		void setInternalIndexBuffer(const Buffer& value);
		void refreshTexParameters();

	private:
		bool mVertexBufferDirty = false;
		bool mIndexBufferDirty = false;
		bool mTexParametersDirty = true;
		bool mViewportDirty = true;
		Buffer mVertexBuffer;
		Buffer mIndexBuffer;
		Sampler mSampler = Sampler::Linear;
		TextureAddress mTextureAddress = TextureAddress::Wrap;
		std::unordered_map<uint32_t, TextureHandle*> mCurrentTextures;
		std::optional<Viewport> mViewport;
		uint32_t mBackbufferWidth = 0;
		uint32_t mBackbufferHeight = 0;
	};
}

#endif