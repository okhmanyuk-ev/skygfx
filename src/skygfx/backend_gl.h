#pragma once

#ifdef SKYGFX_HAS_OPENGL

#include "backend.h"
#include <unordered_map>

namespace skygfx
{
	class BackendGL : public Backend
	{
	public:
		BackendGL(void* window, uint32_t width, uint32_t height);
		~BackendGL();

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
		void prepareForDrawing();
		void refreshTexParameters();

	private:
		bool mTexParametersDirty = true;
		bool mViewportDirty = true;
		Sampler mSampler = Sampler::Linear;
		TextureAddress mTextureAddress = TextureAddress::Wrap;
		std::unordered_map<uint32_t, TextureHandle*> mCurrentTextures;
		std::optional<Viewport> mViewport;
		uint32_t mBackbufferWidth = 0;
		uint32_t mBackbufferHeight = 0;
	};
}

#endif