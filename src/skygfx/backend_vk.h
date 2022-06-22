#pragma once

#include "backend.h"
#include <vulkan/vulkan_raii.hpp>

namespace skygfx
{
	class BackendVK : public Backend
	{
	public:
		BackendVK(void* window, uint32_t width, uint32_t height);
		~BackendVK();

		void resize(uint32_t width, uint32_t height) override;

		void setTopology(Topology topology) override;
		void setViewport(const Viewport& viewport) override;
		void setScissor(const Scissor& value) override;
		void setScissor(std::nullptr_t value) override;
		void setTexture(TextureHandle* handle) override;
		void setRenderTarget(RenderTargetHandle* handle) override;
		void setRenderTarget(std::nullptr_t value) override;
		void setShader(ShaderHandle* handle) override;
		void setVertexBuffer(const Buffer& buffer) override;
		void setIndexBuffer(const Buffer& buffer) override;
		void setUniformBuffer(int slot, void* memory, size_t size) override;
		void setBlendMode(const BlendMode& value) override;
		void setDepthMode(const DepthMode& value) override;
		void setStencilMode(const StencilMode& value) override;
		void setCullMode(const CullMode& value) override;
		void setSampler(const Sampler& value) override;
		void setTextureAddressMode(const TextureAddress& value) override;

		void clear(const std::optional<glm::vec4>& color, const std::optional<float>& depth,
			const std::optional<uint8_t>& stencil) override;
		void draw(size_t vertex_count, size_t vertex_offset) override;
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
		void createSwapchain(uint32_t width, uint32_t height);
		void begin();
		void end();
		void prepareForDrawing();

	public:
		static void setImageLayout(vk::raii::CommandBuffer const& commandBuffer, vk::Image image,
			vk::Format format, vk::ImageLayout oldImageLayout, vk::ImageLayout newImageLayout);

		template <typename Func>
		static void oneTimeSubmit(vk::raii::CommandBuffer const& commandBuffer, vk::raii::Queue const& queue, Func const& func)
		{
			commandBuffer.begin(vk::CommandBufferBeginInfo(vk::CommandBufferUsageFlagBits::eOneTimeSubmit));
			func(commandBuffer);
			commandBuffer.end();
			vk::SubmitInfo submitInfo(nullptr, nullptr, *commandBuffer);
			queue.submit(submitInfo, nullptr);
			queue.waitIdle();
		}

		template <typename Func>
		static void oneTimeSubmit(vk::raii::Device const& device, vk::raii::CommandPool const& commandPool, vk::raii::Queue const& queue, Func const& func)
		{
			vk::raii::CommandBuffers commandBuffers(device, { *commandPool, vk::CommandBufferLevel::ePrimary, 1 });
			oneTimeSubmit(commandBuffers.front(), queue, func);
		}
	};
}