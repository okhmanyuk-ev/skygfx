#pragma once

#include "skygfx.h"

namespace skygfx
{
	class Backend
	{
	public:
		virtual ~Backend() {}

		virtual void resize(uint32_t width, uint32_t height) = 0;
		virtual void setVsync(bool value) = 0;

		virtual void setTopology(Topology topology) = 0;
		virtual void setViewport(std::optional<Viewport> viewport) = 0;
		virtual void setScissor(std::optional<Scissor> scissor) = 0;
		virtual void setTexture(uint32_t binding, TextureHandle* handle) = 0;
		virtual void setRenderTarget(const RenderTarget** render_target, size_t count) = 0;
		virtual void setShader(ShaderHandle* handle) = 0;
		virtual void setInputLayout(const std::vector<InputLayout>& value) = 0;
		virtual void setVertexBuffer(const VertexBuffer** vertex_buffer, size_t count) = 0;
		virtual void setIndexBuffer(IndexBufferHandle* handle) = 0;
		virtual void setUniformBuffer(uint32_t binding, UniformBufferHandle* handle) = 0;
		virtual void setBlendMode(const std::optional<BlendMode>& blend_mode) = 0;
		virtual void setDepthMode(const std::optional<DepthMode>& depth_mode) = 0;
		virtual void setStencilMode(const std::optional<StencilMode>& stencil_mode) = 0;
		virtual void setCullMode(CullMode cull_mode) = 0;
		virtual void setSampler(Sampler value) = 0;
		virtual void setAnisotropyLevel(AnisotropyLevel value) = 0;
		virtual void setTextureAddress(TextureAddress value) = 0;
		virtual void setFrontFace(FrontFace value) = 0;
		virtual void setDepthBias(const std::optional<DepthBias> depth_bias) = 0;

		virtual void clear(const std::optional<glm::vec4>& color, const std::optional<float>& depth,
			const std::optional<uint8_t>& stencil) = 0;
		virtual void draw(uint32_t vertex_count, uint32_t vertex_offset, uint32_t instance_count) = 0;
		virtual void drawIndexed(uint32_t index_count, uint32_t index_offset, uint32_t instance_count) = 0;

		virtual void copyBackbufferToTexture(const glm::i32vec2& src_pos, const glm::i32vec2& size, const glm::i32vec2& dst_pos,
			TextureHandle* dst_texture_handle) = 0;
		virtual void present() = 0;

		virtual TextureHandle* createTexture(uint32_t width, uint32_t height, PixelFormat format,
			uint32_t mip_count) = 0;
		virtual void writeTexturePixels(TextureHandle* handle, uint32_t width, uint32_t height, const void* memory,
			uint32_t mip_level, uint32_t offset_x, uint32_t offset_y) = 0;
		virtual std::vector<uint8_t> readTexturePixels(TextureHandle* handle, uint32_t mip_level) = 0;
		virtual void generateMips(TextureHandle* handle) = 0;
		virtual void destroyTexture(TextureHandle* handle) = 0;

		virtual RenderTargetHandle* createRenderTarget(uint32_t width, uint32_t height, TextureHandle* texture) = 0;
		virtual void destroyRenderTarget(RenderTargetHandle* handle) = 0;

		virtual ShaderHandle* createShader(const std::string& vertex_code, 
			const std::string& fragment_code, const std::vector<std::string>& defines) = 0;
		virtual void destroyShader(ShaderHandle* handle) = 0;

		virtual VertexBufferHandle* createVertexBuffer(size_t size, size_t stride) = 0;
		virtual void destroyVertexBuffer(VertexBufferHandle* handle) = 0;
		virtual void writeVertexBufferMemory(VertexBufferHandle* handle, const void* memory, size_t size, size_t stride) = 0;

		virtual IndexBufferHandle* createIndexBuffer(size_t size, size_t stride) = 0;
		virtual void destroyIndexBuffer(IndexBufferHandle* handle) = 0;
		virtual void writeIndexBufferMemory(IndexBufferHandle* handle, const void* memory, size_t size, size_t stride) = 0;

		virtual UniformBufferHandle* createUniformBuffer(size_t size) = 0;
		virtual void destroyUniformBuffer(UniformBufferHandle* handle) = 0;
		virtual void writeUniformBufferMemory(UniformBufferHandle* handle, const void* memory, size_t size) = 0;
	};

	class RaytracingBackend
	{
	public:
		virtual void setStorageBuffer(uint32_t binding, StorageBufferHandle* handle) = 0;
		virtual void setRaytracingShader(RaytracingShaderHandle* handle) = 0;
		virtual void setAccelerationStructure(uint32_t binding, TopLevelAccelerationStructureHandle* handle) = 0;

		virtual void dispatchRays(uint32_t width, uint32_t height, uint32_t depth) = 0;

		virtual RaytracingShaderHandle* createRaytracingShader(const std::string& raygen_code,
			const std::vector<std::string>& miss_code, const std::string& closesthit_code,
			const std::vector<std::string>& defines) = 0;
		virtual void destroyRaytracingShader(RaytracingShaderHandle* handle) = 0;

		virtual BottomLevelAccelerationStructureHandle* createBottomLevelAccelerationStructure(const void* vertex_memory,
			uint32_t vertex_count, uint32_t vertex_stride, const void* index_memory, uint32_t index_count,
			uint32_t index_stride, const glm::mat4& transform) = 0;
		virtual void destroyBottomLevelAccelerationStructure(BottomLevelAccelerationStructureHandle* handle) = 0;

		virtual TopLevelAccelerationStructureHandle* createTopLevelAccelerationStructure(
			const std::vector<std::tuple<uint32_t, BottomLevelAccelerationStructureHandle*>>& bottom_level_acceleration_structures) = 0;
		virtual void destroyTopLevelAccelerationStructure(TopLevelAccelerationStructureHandle* handle) = 0;

		virtual StorageBufferHandle* createStorageBuffer(size_t size) = 0;
		virtual void destroyStorageBuffer(StorageBufferHandle* handle) = 0;
		virtual void writeStorageBufferMemory(StorageBufferHandle* handle, const void* memory, size_t size) = 0;
	};
}
