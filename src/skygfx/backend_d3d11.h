#pragma once

#include "backend.h"

namespace skygfx
{
	class BackendD3D11 : public Backend
	{
	public:
		BackendD3D11(void* window, uint32_t width, uint32_t height);
		~BackendD3D11();

		void setTopology(Topology topology) override;
		void setViewport(const Viewport& viewport) override;
		void setTexture(TextureHandle* handle) override;
		void setShader(ShaderHandle* handle) override;
		void setVertexBuffer(const Buffer& buffer) override;
		void setIndexBuffer(const Buffer& buffer) override;
		void setUniformBuffer(int slot, void* memory, size_t size) override;
		void setBlendMode(const BlendMode& value) override;
		void clear(const std::optional<glm::vec4>& color, const std::optional<float>& depth,
			const std::optional<uint8_t>& stencil) override;
		void drawIndexed(uint32_t index_count, uint32_t index_offset) override;
		void present() override;

		TextureHandle* createTexture() override;
		void destroyTexture(TextureHandle* handle) override;

		ShaderHandle* createShader(const Vertex::Layout& layout, const std::string& vertex_code, 
			const std::string& fragment_code) override;
		void destroyShader(ShaderHandle* handle) override;

	private:
		void createMainRenderTarget(uint32_t width, uint32_t height);
		void destroyMainRenderTarget();
	};
}