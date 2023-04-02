#include "backend_vk.h"

#ifdef SKYGFX_HAS_VULKAN

#include <vulkan/vulkan_raii.hpp>

using namespace skygfx;

class ShaderVK;
class RaytracingShaderVK;
class UniformBufferVK;
class AccelerationStructureVK;
class TextureVK;
class RenderTargetVK;
class VertexBufferVK;
class IndexBufferVK;

struct PipelineStateVK
{
	ShaderVK* shader = nullptr;
	BlendMode blend_mode = BlendStates::AlphaBlend;
	RenderTargetVK* render_target = nullptr; // TODO: see how we do it in Metal, we can remove this field from here!

	bool operator==(const PipelineStateVK& value) const
	{
		return 
			shader == value.shader &&
			blend_mode == value.blend_mode &&
			render_target == value.render_target;
	}
};

SKYGFX_MAKE_HASHABLE(PipelineStateVK,
	t.shader,
	t.blend_mode,
	t.render_target);

struct RaytracingPipelineStateVK
{
	RaytracingShaderVK* shader = nullptr;

	bool operator==(const RaytracingPipelineStateVK& value) const
	{
		return
			shader == value.shader;
	}
};

SKYGFX_MAKE_HASHABLE(RaytracingPipelineStateVK,
	t.shader);

struct SamplerStateVK
{
	Sampler sampler = Sampler::Linear;
	TextureAddress texture_address = TextureAddress::Clamp;

	bool operator==(const SamplerStateVK& value) const
	{
		return
			sampler == value.sampler &&
			texture_address == value.texture_address;
	}
};

SKYGFX_MAKE_HASHABLE(SamplerStateVK,
	t.sampler,
	t.texture_address);

static vk::raii::Context* gContext = nullptr;
static vk::raii::Instance gInstance = nullptr;
static vk::raii::PhysicalDevice gPhysicalDevice = nullptr;
static vk::raii::Queue gQueue = nullptr;
static vk::raii::Device gDevice = nullptr;
static uint32_t gQueueFamilyIndex = -1;
static vk::SurfaceFormatKHR gSurfaceFormat;
static vk::raii::SurfaceKHR gSurface = nullptr;
static vk::raii::SwapchainKHR gSwapchain = nullptr;
static vk::raii::CommandPool gCommandPool = nullptr;
static vk::raii::CommandBuffer gCommandBuffer = nullptr;
static bool gWorking = false;
static uint32_t gWidth = 0;
static uint32_t gHeight = 0;
static const uint32_t gMinImageCount = 2; // TODO: https://github.com/nvpro-samples/nvpro_core/blob/f2c05e161bba9ab9a8c96c0173bf0edf7c168dfa/nvvk/swapchain_vk.cpp#L143
static ExecuteList gExecuteAfterPresent;

using StagingObjectVK = std::variant<
	vk::raii::Buffer,
	vk::raii::DeviceMemory
>;

static std::vector<StagingObjectVK> gStagingObjects;

struct FrameVK
{
	vk::raii::Fence fence = nullptr;
	vk::raii::ImageView backbuffer_color_image_view = nullptr;
	vk::raii::Semaphore image_acquired_semaphore = nullptr;
	vk::raii::Semaphore render_complete_semaphore = nullptr;
};

static struct
{
	const vk::Format format = vk::Format::eD32SfloatS8Uint;

	vk::raii::Image image = nullptr;
	vk::raii::ImageView view = nullptr;
	vk::raii::DeviceMemory memory = nullptr;
} gDepthStencil;

static std::vector<FrameVK> gFrames;

static uint32_t gSemaphoreIndex = 0;
static uint32_t gFrameIndex = 0;

static std::unordered_map<uint32_t, TextureVK*> gTextures;
static std::unordered_map<uint32_t, UniformBufferVK*> gUniformBuffers;
static std::unordered_map<uint32_t, AccelerationStructureVK*> gAccelerationStructures;

static std::optional<Scissor> gScissor;
static bool gScissorDirty = true;

static std::optional<Viewport> gViewport;
static bool gViewportDirty = true;

static PipelineStateVK gPipelineState;
static std::unordered_map<PipelineStateVK, vk::raii::Pipeline> gPipelineStates;

static RaytracingPipelineStateVK gRaytracingPipelineState;
static std::unordered_map<RaytracingPipelineStateVK, vk::raii::Pipeline> gRaytracingPipelineStates;

static SamplerStateVK gSamplerState;
static std::unordered_map<SamplerStateVK, vk::raii::Sampler> gSamplers;

static std::optional<DepthMode> gDepthMode = DepthMode();
static bool gDepthModeDirty = true;

static CullMode gCullMode = CullMode::None;
static bool gCullModeDirty = true;

static Topology gTopology = Topology::TriangleList;
static bool gTopologyDirty = true;

static VertexBufferVK* gVertexBuffer = nullptr;
static bool gVertexBufferDirty = true;

static IndexBufferVK* gIndexBuffer = nullptr;
static bool gIndexBufferDirty = true;

static RenderTargetVK* gRenderTarget = nullptr;

static uint32_t GetMemoryType(vk::MemoryPropertyFlags properties, uint32_t type_bits)
{
	auto prop = gPhysicalDevice.getMemoryProperties();

	for (uint32_t i = 0; i < prop.memoryTypeCount; i++)
		if ((prop.memoryTypes[i].propertyFlags & properties) == properties && type_bits & (1 << i))
			return i;

	return 0xFFFFFFFF; // Unable to find memoryType
}

template <typename Func>
static void OneTimeSubmit(const vk::raii::CommandBuffer& cmdbuf, const vk::raii::Queue& queue, const Func& func)
{
	cmdbuf.begin(vk::CommandBufferBeginInfo(vk::CommandBufferUsageFlagBits::eOneTimeSubmit));
	func(cmdbuf);
	cmdbuf.end();
	vk::SubmitInfo submitInfo(nullptr, nullptr, *cmdbuf);
	queue.submit(submitInfo, nullptr);
	queue.waitIdle();
}

template <typename Func>
static void OneTimeSubmit(const vk::raii::Device& device, const vk::raii::CommandPool& command_pool, const vk::raii::Queue& queue, const Func& func)
{
	auto command_buffer_allocate_info = vk::CommandBufferAllocateInfo()
		.setCommandBufferCount(1)
		.setCommandPool(*command_pool)
		.setLevel(vk::CommandBufferLevel::ePrimary);

	auto command_buffers = device.allocateCommandBuffers(command_buffer_allocate_info);
	auto cmdbuf = std::move(command_buffers.at(0));

	OneTimeSubmit(cmdbuf, queue, func);
}

static void SetImageLayout(const vk::raii::CommandBuffer& cmd, vk::Image image,
	vk::Format format, vk::ImageLayout old_image_layout, vk::ImageLayout new_image_layout, std::optional<vk::ImageSubresourceRange> subresource_range = std::nullopt)
{
	vk::AccessFlags src_access_mask;
	switch (old_image_layout)
	{
	case vk::ImageLayout::eTransferSrcOptimal: src_access_mask = vk::AccessFlagBits::eTransferRead; break;
	case vk::ImageLayout::eTransferDstOptimal: src_access_mask = vk::AccessFlagBits::eTransferWrite; break;
	case vk::ImageLayout::ePreinitialized: src_access_mask = vk::AccessFlagBits::eHostWrite; break;
	case vk::ImageLayout::eGeneral:  // src_access_mask is empty
	case vk::ImageLayout::eUndefined: break;
	default: assert(false); break;
	}

	vk::PipelineStageFlags src_stage;
	switch (old_image_layout)
	{
	case vk::ImageLayout::eGeneral:
	case vk::ImageLayout::ePreinitialized: src_stage = vk::PipelineStageFlagBits::eHost; break;
	case vk::ImageLayout::eTransferSrcOptimal:
	case vk::ImageLayout::eTransferDstOptimal: src_stage = vk::PipelineStageFlagBits::eTransfer; break;
	case vk::ImageLayout::eUndefined: src_stage = vk::PipelineStageFlagBits::eTopOfPipe; break;
	default: assert(false); break;
	}

	vk::AccessFlags dst_access_mask;
	switch (new_image_layout)
	{
	case vk::ImageLayout::eColorAttachmentOptimal: dst_access_mask = vk::AccessFlagBits::eColorAttachmentWrite; break;
	case vk::ImageLayout::eDepthStencilAttachmentOptimal:
		dst_access_mask = vk::AccessFlagBits::eDepthStencilAttachmentRead | vk::AccessFlagBits::eDepthStencilAttachmentWrite;
		break;
	case vk::ImageLayout::eGeneral:  // empty dst_access_mask
	case vk::ImageLayout::ePresentSrcKHR: break;
	case vk::ImageLayout::eShaderReadOnlyOptimal: dst_access_mask = vk::AccessFlagBits::eShaderRead; break;
	case vk::ImageLayout::eTransferSrcOptimal: dst_access_mask = vk::AccessFlagBits::eTransferRead; break;
	case vk::ImageLayout::eTransferDstOptimal: dst_access_mask = vk::AccessFlagBits::eTransferWrite; break;
	default: assert(false); break;
	}

	vk::PipelineStageFlags dst_stage;
	switch (new_image_layout)
	{
	case vk::ImageLayout::eColorAttachmentOptimal: dst_stage = vk::PipelineStageFlagBits::eColorAttachmentOutput; break;
	case vk::ImageLayout::eDepthStencilAttachmentOptimal: dst_stage = vk::PipelineStageFlagBits::eEarlyFragmentTests; break;
	case vk::ImageLayout::eGeneral: dst_stage = vk::PipelineStageFlagBits::eHost; break;
	case vk::ImageLayout::ePresentSrcKHR: dst_stage = vk::PipelineStageFlagBits::eBottomOfPipe; break;
	case vk::ImageLayout::eShaderReadOnlyOptimal: dst_stage = vk::PipelineStageFlagBits::eFragmentShader; break;
	case vk::ImageLayout::eTransferDstOptimal:
	case vk::ImageLayout::eTransferSrcOptimal: dst_stage = vk::PipelineStageFlagBits::eTransfer; break;
	default: assert(false); break;
	}

	vk::ImageAspectFlags aspect_mask;
	if (new_image_layout == vk::ImageLayout::eDepthStencilAttachmentOptimal)
	{
		aspect_mask = vk::ImageAspectFlagBits::eDepth;
		if (format == vk::Format::eD32SfloatS8Uint || format == vk::Format::eD24UnormS8Uint)
		{
			aspect_mask |= vk::ImageAspectFlagBits::eStencil;
		}
	}
	else
	{
		aspect_mask = vk::ImageAspectFlagBits::eColor;
	}

	auto default_image_subresource_range = vk::ImageSubresourceRange()
		.setAspectMask(aspect_mask)
		.setLayerCount(1)
		.setLevelCount(1);

	auto image_memory_barrier = vk::ImageMemoryBarrier()
		.setSrcAccessMask(src_access_mask)
		.setDstAccessMask(dst_access_mask)
		.setOldLayout(old_image_layout)
		.setNewLayout(new_image_layout)
		.setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
		.setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
		.setImage(image)
		.setSubresourceRange(subresource_range.value_or(default_image_subresource_range));

	return cmd.pipelineBarrier(src_stage, dst_stage, {}, nullptr, nullptr, image_memory_barrier);
}

static std::tuple<vk::raii::Buffer, vk::raii::DeviceMemory> CreateBuffer(size_t size, vk::BufferUsageFlags usage = {})
{
	auto buffer_create_info = vk::BufferCreateInfo()
		.setSize(size)
		.setUsage(vk::BufferUsageFlagBits::eTransferSrc | vk::BufferUsageFlagBits::eTransferDst | usage)
		.setSharingMode(vk::SharingMode::eExclusive);

	auto buffer = gDevice.createBuffer(buffer_create_info);

	auto memory_requirements = buffer.getMemoryRequirements();

	auto memory_allocate_info = vk::MemoryAllocateInfo()
		.setAllocationSize(memory_requirements.size)
		.setMemoryTypeIndex(GetMemoryType(vk::MemoryPropertyFlagBits::eHostVisible, memory_requirements.memoryTypeBits));

	auto device_memory = gDevice.allocateMemory(memory_allocate_info);

	buffer.bindMemory(*device_memory, 0);

	return { std::move(buffer), std::move(device_memory) };
}

static vk::DeviceAddress GetBufferDeviceAddress(vk::Buffer buffer)
{
	auto info = vk::BufferDeviceAddressInfo()
		.setBuffer(buffer);

	return gDevice.getBufferAddress(info);
};

static void WriteToBuffer(vk::raii::DeviceMemory& memory, const void* data, size_t size) {
	auto ptr = memory.mapMemory(0, size);
	memcpy(ptr, data, size);
	memory.unmapMemory();
};

static void BeginRenderPass();
static void EndRenderPass();
static void EnsureRenderPassActivated();
static void EnsureRenderPassDeactivated();

const static std::unordered_map<ComparisonFunc, vk::CompareOp> CompareOpMap = {
	{ ComparisonFunc::Always, vk::CompareOp::eAlways },
	{ ComparisonFunc::Never, vk::CompareOp::eNever },
	{ ComparisonFunc::Less, vk::CompareOp::eLess },
	{ ComparisonFunc::Equal, vk::CompareOp::eEqual },
	{ ComparisonFunc::NotEqual, vk::CompareOp::eNotEqual },
	{ ComparisonFunc::LessEqual, vk::CompareOp::eLessOrEqual },
	{ ComparisonFunc::Greater, vk::CompareOp::eGreater },
	{ ComparisonFunc::GreaterEqual, vk::CompareOp::eGreaterOrEqual }
};

const static std::unordered_map<ShaderStage, vk::ShaderStageFlagBits> ShaderStageMap = {
	{ ShaderStage::Vertex, vk::ShaderStageFlagBits::eVertex },
	{ ShaderStage::Fragment, vk::ShaderStageFlagBits::eFragment }
};

const static std::unordered_map<ShaderReflection::Descriptor::Type, vk::DescriptorType> ShaderTypeMap = {
	{ ShaderReflection::Descriptor::Type::CombinedImageSampler, vk::DescriptorType::eCombinedImageSampler },
	{ ShaderReflection::Descriptor::Type::UniformBuffer, vk::DescriptorType::eUniformBuffer }
};

class ShaderVK
{
public:
	const auto& getPipelineLayout() const { return mPipelineLayout; }
	const auto& getVertexShaderModule() const { return mVertexShaderModule; }
	const auto& getFragmentShaderModule() const { return mFragmentShaderModule; }
	const auto& getVertexInputBindingDescription() const { return mVertexInputBindingDescription; }
	const auto& getVertexInputAttributeDescriptions() const { return mVertexInputAttributeDescriptions; }
	const auto& getRequiredDescriptorBindings() const { return mRequiredDescriptorBindings; }
	
private:
	vk::raii::DescriptorSetLayout mDescriptorSetLayout = nullptr;
	vk::raii::PipelineLayout mPipelineLayout = nullptr;
	vk::raii::ShaderModule mVertexShaderModule = nullptr;
	vk::raii::ShaderModule mFragmentShaderModule = nullptr;
	vk::VertexInputBindingDescription mVertexInputBindingDescription;
	std::vector<vk::VertexInputAttributeDescription> mVertexInputAttributeDescriptions;
	std::vector<vk::DescriptorSetLayoutBinding> mRequiredDescriptorBindings;

public:
	ShaderVK(const Vertex::Layout& layout, const std::string& vertex_code, const std::string& fragment_code,
		std::vector<std::string> defines)
	{
		AddShaderLocationDefines(layout, defines);

		auto vertex_shader_spirv = CompileGlslToSpirv(ShaderStage::Vertex, vertex_code, defines);
		auto fragment_shader_spirv = CompileGlslToSpirv(ShaderStage::Fragment, fragment_code, defines);

		for (const auto& spirv : { vertex_shader_spirv, fragment_shader_spirv })
		{
			auto reflection = MakeSpirvReflection(spirv);

			for (const auto& [binding, descriptor] : reflection.descriptor_bindings)
			{
				bool overwritten = false;

				for (auto& _binding : mRequiredDescriptorBindings)
				{
					if (_binding.binding != binding)
						continue;

					_binding.stageFlags |= ShaderStageMap.at(reflection.stage);
					overwritten = true;
					break;
				}

				if (overwritten)
					continue;

				auto descriptor_set_layout_binding = vk::DescriptorSetLayoutBinding()
					.setDescriptorType(ShaderTypeMap.at(descriptor.type))
					.setDescriptorCount(1)
					.setBinding(binding)
					.setStageFlags(ShaderStageMap.at(reflection.stage));

				mRequiredDescriptorBindings.push_back(descriptor_set_layout_binding);
			}
		}

		auto descriptor_set_layout_create_info = vk::DescriptorSetLayoutCreateInfo()
			.setFlags(vk::DescriptorSetLayoutCreateFlagBits::ePushDescriptorKHR)
			.setBindings(mRequiredDescriptorBindings);

		mDescriptorSetLayout = gDevice.createDescriptorSetLayout(descriptor_set_layout_create_info);

		auto pipeline_layout_create_info = vk::PipelineLayoutCreateInfo()
			.setSetLayoutCount(1)
			.setPSetLayouts(&*mDescriptorSetLayout);

		mPipelineLayout = gDevice.createPipelineLayout(pipeline_layout_create_info);

		auto vertex_shader_module_create_info = vk::ShaderModuleCreateInfo()
			.setCode(vertex_shader_spirv);

		auto fragment_shader_module_create_info = vk::ShaderModuleCreateInfo()
			.setCode(fragment_shader_spirv);

		mVertexShaderModule = gDevice.createShaderModule(vertex_shader_module_create_info);
		mFragmentShaderModule = gDevice.createShaderModule(fragment_shader_module_create_info);

		static const std::unordered_map<Vertex::Attribute::Format, vk::Format> Format = {
			{ Vertex::Attribute::Format::Float1, vk::Format::eR32Sfloat },
			{ Vertex::Attribute::Format::Float2, vk::Format::eR32G32Sfloat },
			{ Vertex::Attribute::Format::Float3, vk::Format::eR32G32B32Sfloat },
			{ Vertex::Attribute::Format::Float4, vk::Format::eR32G32B32A32Sfloat },
			{ Vertex::Attribute::Format::Byte1, vk::Format::eR8Unorm },
			{ Vertex::Attribute::Format::Byte2, vk::Format::eR8G8Unorm },
			{ Vertex::Attribute::Format::Byte3, vk::Format::eR8G8B8Unorm },
			{ Vertex::Attribute::Format::Byte4, vk::Format::eR8G8B8A8Unorm }
		};

		mVertexInputBindingDescription = vk::VertexInputBindingDescription()
			.setStride(static_cast<uint32_t>(layout.stride))
			.setInputRate(vk::VertexInputRate::eVertex)
			.setBinding(0);

		for (int i = 0; i < layout.attributes.size(); i++)
		{
			const auto& attrib = layout.attributes.at(i);

			auto vertex_input_attribute_description = vk::VertexInputAttributeDescription()
				.setBinding(0)
				.setLocation(i)
				.setFormat(Format.at(attrib.format))
				.setOffset(static_cast<uint32_t>(attrib.offset));

			mVertexInputAttributeDescriptions.push_back(vertex_input_attribute_description);
		}
	}
};

class RaytracingShaderVK
{
public:
	const auto& getRaygenShaderModule() const { return mRaygenShaderModule; }
	const auto& getMissShaderModule() const { return mMissShaderModule; }
	const auto& getClosestHitShaderModule() const { return mClosestHitShaderModule; }

private:
	vk::raii::ShaderModule mRaygenShaderModule = nullptr;
	vk::raii::ShaderModule mMissShaderModule = nullptr;
	vk::raii::ShaderModule mClosestHitShaderModule = nullptr;

public:
	RaytracingShaderVK(const std::string& raygen_code, const std::string& miss_code,
		const std::string& closesthit_code, std::vector<std::string> defines)
	{
		auto raygen_shader_spirv = CompileGlslToSpirv(ShaderStage::Raygen, raygen_code);
		auto miss_shader_spirv = CompileGlslToSpirv(ShaderStage::Miss, miss_code);
		auto closesthit_shader_spirv = CompileGlslToSpirv(ShaderStage::ClosestHit, closesthit_code);

		auto raygen_shader_module_create_info = vk::ShaderModuleCreateInfo()
			.setCode(raygen_shader_spirv);

		auto miss_shader_module_create_info = vk::ShaderModuleCreateInfo()
			.setCode(miss_shader_spirv);

		auto closesthit_shader_module_create_info = vk::ShaderModuleCreateInfo()
			.setCode(closesthit_shader_spirv);

		mRaygenShaderModule = gDevice.createShaderModule(raygen_shader_module_create_info);
		mMissShaderModule = gDevice.createShaderModule(miss_shader_module_create_info);
		mClosestHitShaderModule = gDevice.createShaderModule(closesthit_shader_module_create_info);
	}
};

class TextureVK
{
public:
	const auto& getImage() const { return mImage; }
	const auto& getImageView() const { return mImageView; }
	const auto& getDeviceMemory() const { return mDeviceMemory; }
	auto getFormat() const { return vk::Format::eR8G8B8A8Unorm; }
	auto getWidth() const { return mWidth; }
	auto getHeight() const { return mHeight; }

private:
	vk::raii::Image mImage = nullptr;
	vk::raii::ImageView mImageView = nullptr;
	vk::raii::DeviceMemory mDeviceMemory = nullptr;
	uint32_t mWidth = 0;
	uint32_t mHeight = 0;

public:
	TextureVK(uint32_t width, uint32_t height, uint32_t channels, void* memory, bool mipmap) :
		mWidth(width),
		mHeight(height)
	{
		uint32_t mip_levels = 1;

		if (mipmap)
		{
			mip_levels = static_cast<uint32_t>(glm::floor(glm::log2(glm::max(width, height)))) + 1;
		}

		auto image_create_info = vk::ImageCreateInfo()
			.setImageType(vk::ImageType::e2D)
			.setFormat(getFormat())
			.setExtent({ width, height, 1 })
			.setMipLevels(mip_levels)
			.setArrayLayers(1)
			.setSamples(vk::SampleCountFlagBits::e1)
			.setTiling(vk::ImageTiling::eOptimal)
			.setUsage(vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst 
				| vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eColorAttachment
				| vk::ImageUsageFlagBits::eStorage)
			.setSharingMode(vk::SharingMode::eExclusive)
			.setInitialLayout(vk::ImageLayout::eUndefined);

		mImage = gDevice.createImage(image_create_info);

		auto memory_requirements = mImage.getMemoryRequirements();

		auto memory_allocate_info = vk::MemoryAllocateInfo()
			.setAllocationSize(memory_requirements.size)
			.setMemoryTypeIndex(GetMemoryType(vk::MemoryPropertyFlagBits::eDeviceLocal, 
				memory_requirements.memoryTypeBits));

		mDeviceMemory = gDevice.allocateMemory(memory_allocate_info);
	
		mImage.bindMemory(*mDeviceMemory, 0);

		auto image_subresource_range = vk::ImageSubresourceRange()
			.setAspectMask(vk::ImageAspectFlagBits::eColor)
			.setLevelCount(mip_levels)
			.setLayerCount(1);

		auto image_view_create_info = vk::ImageViewCreateInfo()
			.setImage(*mImage)
			.setViewType(vk::ImageViewType::e2D)
			.setFormat(vk::Format::eR8G8B8A8Unorm)
			.setSubresourceRange(image_subresource_range);

		mImageView = gDevice.createImageView(image_view_create_info);

		if (memory)
		{
			auto size = width * height * channels;

			auto buffer_create_info = vk::BufferCreateInfo()
				.setSize(size)
				.setUsage(vk::BufferUsageFlagBits::eTransferSrc)
				.setSharingMode(vk::SharingMode::eExclusive);

			auto upload_buffer = gDevice.createBuffer(buffer_create_info); // TODO: use CreateBuffer() func

			auto req = upload_buffer.getMemoryRequirements();

			auto memory_allocate_info = vk::MemoryAllocateInfo()
				.setAllocationSize(req.size)
				.setMemoryTypeIndex(GetMemoryType(vk::MemoryPropertyFlagBits::eHostVisible, req.memoryTypeBits));

			auto upload_buffer_memory = gDevice.allocateMemory(memory_allocate_info);

			upload_buffer.bindMemory(*upload_buffer_memory, 0);

			WriteToBuffer(upload_buffer_memory, memory, size);

			OneTimeSubmit(gDevice, gCommandPool, gQueue, [&](auto& cmdbuf) {
				SetImageLayout(cmdbuf, *mImage, vk::Format::eUndefined, vk::ImageLayout::eUndefined,
					vk::ImageLayout::eTransferDstOptimal);

				auto image_subresource_layers = vk::ImageSubresourceLayers()
					.setAspectMask(vk::ImageAspectFlagBits::eColor)
					.setLayerCount(1);

				auto region = vk::BufferImageCopy()
					.setImageSubresource(image_subresource_layers)
					.setImageExtent({ width, height, 1 });

				cmdbuf.copyBufferToImage(*upload_buffer, *mImage, vk::ImageLayout::eTransferDstOptimal, { region });

				SetImageLayout(cmdbuf, *mImage, vk::Format::eUndefined, vk::ImageLayout::eTransferDstOptimal,
					vk::ImageLayout::eTransferSrcOptimal);

				for (uint32_t i = 1; i < mip_levels; i++)
				{
					auto mip_subresource_range = vk::ImageSubresourceRange()
						.setAspectMask(vk::ImageAspectFlagBits::eColor)
						.setBaseMipLevel(i)
						.setLayerCount(1)
						.setLevelCount(1);

					SetImageLayout(cmdbuf, *mImage, vk::Format::eUndefined, vk::ImageLayout::eUndefined,
						vk::ImageLayout::eTransferDstOptimal, mip_subresource_range);

					auto src_subresource = vk::ImageSubresourceLayers()
						.setAspectMask(vk::ImageAspectFlagBits::eColor)
						.setMipLevel(i - 1)
						.setLayerCount(1);

					auto dst_subresource = vk::ImageSubresourceLayers()
						.setAspectMask(vk::ImageAspectFlagBits::eColor)
						.setMipLevel(i)
						.setLayerCount(1);

					auto mip_region = vk::ImageBlit()
						.setSrcSubresource(src_subresource)
						.setDstSubresource(dst_subresource)
						.setSrcOffsets({ vk::Offset3D{ 0, 0, 0 }, vk::Offset3D{ int32_t(width >> (i - 1)), int32_t(height >> (i - 1)), 1 } })
						.setDstOffsets({ vk::Offset3D{ 0, 0, 0 }, vk::Offset3D{ int32_t(width >> i), int32_t(height >> i), 1 } });

					cmdbuf.blitImage(*mImage, vk::ImageLayout::eTransferSrcOptimal, *mImage,
						vk::ImageLayout::eTransferDstOptimal, { mip_region }, vk::Filter::eLinear);

					SetImageLayout(cmdbuf, *mImage, vk::Format::eUndefined, vk::ImageLayout::eTransferDstOptimal,
						vk::ImageLayout::eTransferSrcOptimal, mip_subresource_range);
				}

				auto subresource_range = vk::ImageSubresourceRange()
					.setAspectMask(vk::ImageAspectFlagBits::eColor)
					.setLayerCount(1)
					.setLevelCount(mip_levels);

				SetImageLayout(cmdbuf, *mImage, vk::Format::eUndefined, vk::ImageLayout::eTransferSrcOptimal,
					vk::ImageLayout::eShaderReadOnlyOptimal, subresource_range);
			});
		}
	}
};

class RenderTargetVK
{
public:
	auto getTexture() const { return mTexture; }
	auto getDepthStencilFormat() const { return mDepthStencilFormat; }
	const auto& getDepthStencilImage() const { return mDepthStencilImage; }
	const auto& getDepthStencilView() const { return mDepthStencilView; }
	const auto& getDepthStencilMemory() const { return mDepthStencilMemory; }

private:
	TextureVK* mTexture;
	vk::Format mDepthStencilFormat = vk::Format::eD32SfloatS8Uint;
	vk::raii::Image mDepthStencilImage = nullptr;
	vk::raii::ImageView mDepthStencilView = nullptr;
	vk::raii::DeviceMemory mDepthStencilMemory = nullptr;

public:
	RenderTargetVK(uint32_t width, uint32_t height, TextureVK* _texture) : mTexture(_texture)
	{
		OneTimeSubmit(gDevice, gCommandPool, gQueue, [&](auto& cmdbuf) {
			SetImageLayout(cmdbuf, *getTexture()->getImage(), vk::Format::eUndefined, vk::ImageLayout::eUndefined,
				vk::ImageLayout::eGeneral);
		});

		auto depth_stencil_image_create_info = vk::ImageCreateInfo()
			.setImageType(vk::ImageType::e2D)
			.setFormat(mDepthStencilFormat)
			.setExtent({ width, height, 1 })
			.setMipLevels(1)
			.setArrayLayers(1)
			.setSamples(vk::SampleCountFlagBits::e1)
			.setTiling(vk::ImageTiling::eOptimal)
			.setUsage(vk::ImageUsageFlagBits::eDepthStencilAttachment);

		mDepthStencilImage = gDevice.createImage(depth_stencil_image_create_info);

		auto depth_stencil_mem_req = mDepthStencilImage.getMemoryRequirements();

		auto depth_stencil_memory_allocate_info = vk::MemoryAllocateInfo()
			.setAllocationSize(depth_stencil_mem_req.size)
			.setMemoryTypeIndex(GetMemoryType(vk::MemoryPropertyFlagBits::eDeviceLocal, depth_stencil_mem_req.memoryTypeBits));

		mDepthStencilMemory = gDevice.allocateMemory(depth_stencil_memory_allocate_info);

		mDepthStencilImage.bindMemory(*mDepthStencilMemory, 0);

		auto depth_stencil_view_subresource_range = vk::ImageSubresourceRange()
			.setLevelCount(1)
			.setLayerCount(1)
			.setAspectMask(vk::ImageAspectFlagBits::eDepth | vk::ImageAspectFlagBits::eStencil);

		auto depth_stencil_view_create_info = vk::ImageViewCreateInfo()
			.setViewType(vk::ImageViewType::e2D)
			.setImage(*mDepthStencilImage)
			.setFormat(mDepthStencilFormat)
			.setSubresourceRange(depth_stencil_view_subresource_range);

		mDepthStencilView = gDevice.createImageView(depth_stencil_view_create_info);

		OneTimeSubmit(gDevice, gCommandPool, gQueue, [&](auto& cmdbuf) {
			SetImageLayout(cmdbuf, *mDepthStencilImage, mDepthStencilFormat, vk::ImageLayout::eUndefined,
				vk::ImageLayout::eDepthStencilAttachmentOptimal);
		});
	}

	~RenderTargetVK()
	{
	}
};

class BufferVK
{
public:
	const auto& getBuffer() const { return mBuffer; }
	const auto& getDeviceMemory() const { return mDeviceMemory; }

private:
	vk::raii::Buffer mBuffer = nullptr;
	vk::raii::DeviceMemory mDeviceMemory = nullptr;

public:
	BufferVK(size_t size, vk::BufferUsageFlags usage)
	{
		std::tie(mBuffer, mDeviceMemory) = CreateBuffer(size, usage);
	}

	void write(void* memory, size_t size)
	{
		auto [staging_buffer, staging_buffer_memory] = CreateBuffer(size);

		WriteToBuffer(staging_buffer_memory, memory, size);

		auto region = vk::BufferCopy()
			.setSize(size);

		EnsureRenderPassDeactivated();

		gCommandBuffer.copyBuffer(*staging_buffer, *mBuffer, { region });

		gStagingObjects.push_back(std::move(staging_buffer));
		gStagingObjects.push_back(std::move(staging_buffer_memory));
	}
};

class VertexBufferVK : public BufferVK
{
public:
	auto getStride() const { return mStride; }
	void setStride(size_t value) { mStride = value; }

private:
	size_t mStride = 0;

public:
	VertexBufferVK(size_t size, size_t stride) :
		BufferVK(size, vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR),
		mStride(stride)
	{
	}
};

class IndexBufferVK : public BufferVK
{
public:
	auto getStride() const { return mStride; }
	void setStride(size_t value) { mStride = value; }

private:
	size_t mStride = 0;

public:
	IndexBufferVK(size_t size, size_t stride) :
		BufferVK(size, vk::BufferUsageFlagBits::eIndexBuffer | vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR),
		mStride(stride)
	{
	}
};

class UniformBufferVK : public BufferVK
{
public:
	UniformBufferVK(size_t size) :
		BufferVK(size, vk::BufferUsageFlagBits::eUniformBuffer)
	{
	}
};

static vk::IndexType GetIndexTypeFromStride(size_t stride)
{
	return stride == 2 ? vk::IndexType::eUint16 : vk::IndexType::eUint32;
}

static std::tuple<vk::raii::AccelerationStructureKHR, vk::DeviceAddress> CreateBottomLevelAccelerationStrucutre(const std::vector<glm::vec3>& vertices, const std::vector<uint32_t>& indices, const vk::TransformMatrixKHR& transform)
{
	using vertex_t = glm::vec3;
	using index_t = uint32_t;

	auto [vertex_buffer, vertex_buffer_memory] = CreateBuffer(vertices.size() * sizeof(vertex_t),
		vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR | vk::BufferUsageFlagBits::eShaderDeviceAddress);

	auto [index_buffer, index_buffer_memory] = CreateBuffer(indices.size() * sizeof(index_t),
		vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR | vk::BufferUsageFlagBits::eShaderDeviceAddress);

	auto [transform_buffer, transform_buffer_memory] = CreateBuffer(sizeof(transform), 
		vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR | vk::BufferUsageFlagBits::eShaderDeviceAddress);

	WriteToBuffer(vertex_buffer_memory, vertices.data(), vertices.size() * sizeof(vertex_t));
	WriteToBuffer(index_buffer_memory, indices.data(), indices.size() * sizeof(index_t));
	WriteToBuffer(transform_buffer_memory, &transform, sizeof(transform));

	auto vertex_buffer_device_address = GetBufferDeviceAddress(*vertex_buffer);
	auto index_buffer_device_address = GetBufferDeviceAddress(*index_buffer);
	auto transform_buffer_device_address = GetBufferDeviceAddress(*transform_buffer);

	auto blas_triangles = vk::AccelerationStructureGeometryTrianglesDataKHR()
		.setVertexFormat(vk::Format::eR32G32B32Sfloat)
		.setVertexData(vertex_buffer_device_address)
		.setMaxVertex(static_cast<uint32_t>(vertices.size()))
		.setVertexStride(sizeof(vertex_t))
		.setIndexType(GetIndexTypeFromStride(sizeof(index_t)))
		.setIndexData(index_buffer_device_address)
		.setTransformData(transform_buffer_device_address);

	auto blas_geometry_data = vk::AccelerationStructureGeometryDataKHR()
		.setTriangles(blas_triangles);

	auto blas_geometry = vk::AccelerationStructureGeometryKHR()
		.setGeometryType(vk::GeometryTypeKHR::eTriangles)
		.setGeometry(blas_geometry_data)
		.setFlags(vk::GeometryFlagBitsKHR::eOpaque);

	auto blas_build_geometry_info = vk::AccelerationStructureBuildGeometryInfoKHR()
		.setType(vk::AccelerationStructureTypeKHR::eBottomLevel)
		.setFlags(vk::BuildAccelerationStructureFlagBitsKHR::ePreferFastTrace)
		.setGeometryCount(1)
		.setPGeometries(&blas_geometry);

	auto blas_build_sizes = gDevice.getAccelerationStructureBuildSizesKHR(
		vk::AccelerationStructureBuildTypeKHR::eDevice, blas_build_geometry_info, { 1 });
		
	static auto [blas_buffer, blas_memory] = CreateBuffer(blas_build_sizes.accelerationStructureSize,
		vk::BufferUsageFlagBits::eAccelerationStructureStorageKHR);

	auto blas_create_info = vk::AccelerationStructureCreateInfoKHR()
		.setBuffer(*blas_buffer)
		.setType(vk::AccelerationStructureTypeKHR::eBottomLevel)
		.setSize(blas_build_sizes.accelerationStructureSize);

	auto blas = gDevice.createAccelerationStructureKHR(blas_create_info);

	auto [blas_scratch_buffer, blas_scratch_memory] = CreateBuffer(blas_build_sizes.buildScratchSize,
		vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eShaderDeviceAddress);

	auto blas_scratch_buffer_addr = GetBufferDeviceAddress(*blas_scratch_buffer);

	blas_build_geometry_info
		.setMode(vk::BuildAccelerationStructureModeKHR::eBuild)
		.setDstAccelerationStructure(*blas)
		.setScratchData(blas_scratch_buffer_addr);

	auto blas_build_range_info = vk::AccelerationStructureBuildRangeInfoKHR()
		.setPrimitiveCount(1);

	auto blas_build_geometry_infos = { blas_build_geometry_info };
	std::vector blas_build_range_infos = { &blas_build_range_info };
		
	OneTimeSubmit(gDevice, gCommandPool, gQueue, [&](auto& cmdbuf) {
		cmdbuf.buildAccelerationStructuresKHR(blas_build_geometry_infos, blas_build_range_infos);
	});

	auto blas_device_address_info = vk::AccelerationStructureDeviceAddressInfoKHR()
		.setAccelerationStructure(*blas);

	auto blas_device_address = gDevice.getAccelerationStructureAddressKHR(blas_device_address_info);

	return { std::move(blas), blas_device_address };
}

static vk::raii::AccelerationStructureKHR CreateTopLevelAccelerationStructure(const vk::TransformMatrixKHR& transform, const vk::DeviceAddress& bottom_level_acceleration_structure_device_address)
{
	auto tlas_instance = vk::AccelerationStructureInstanceKHR()
		.setTransform(transform)
		.setMask(0xFF)
		.setFlags(vk::GeometryInstanceFlagBitsKHR::eTriangleFacingCullDisable)
		.setAccelerationStructureReference(bottom_level_acceleration_structure_device_address);

	auto [instance_buffer, instance_buffer_memory] = CreateBuffer(sizeof(tlas_instance),
		vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR | vk::BufferUsageFlagBits::eShaderDeviceAddress);

	WriteToBuffer(instance_buffer_memory, &tlas_instance, sizeof(tlas_instance));

	auto instance_buffer_addr = GetBufferDeviceAddress(*instance_buffer);

	auto tlas_instances = vk::AccelerationStructureGeometryInstancesDataKHR()
		.setArrayOfPointers(false)
		.setData(instance_buffer_addr);

	auto tlas_geometry_data = vk::AccelerationStructureGeometryDataKHR()
		.setInstances(tlas_instances);

	auto tlas_geometry = vk::AccelerationStructureGeometryKHR()
		.setGeometryType(vk::GeometryTypeKHR::eInstances)
		.setGeometry(tlas_geometry_data)
		.setFlags(vk::GeometryFlagBitsKHR::eOpaque);

	auto tlas_build_geometry_info = vk::AccelerationStructureBuildGeometryInfoKHR()
		.setType(vk::AccelerationStructureTypeKHR::eTopLevel)
		.setFlags(vk::BuildAccelerationStructureFlagBitsKHR::ePreferFastTrace)
		.setGeometryCount(1)
		.setPGeometries(&tlas_geometry);

	auto tlas_build_sizes = gDevice.getAccelerationStructureBuildSizesKHR(
		vk::AccelerationStructureBuildTypeKHR::eDevice, tlas_build_geometry_info, { 1 });

	static auto [tlas_buffer, tlas_memory] = CreateBuffer(tlas_build_sizes.accelerationStructureSize,
		vk::BufferUsageFlagBits::eAccelerationStructureStorageKHR);

	auto tlas_create_info = vk::AccelerationStructureCreateInfoKHR()
		.setBuffer(*tlas_buffer)
		.setType(vk::AccelerationStructureTypeKHR::eTopLevel)
		.setSize(tlas_build_sizes.accelerationStructureSize);

	auto tlas = gDevice.createAccelerationStructureKHR(tlas_create_info);

	auto [tlas_scratch_buffer, tlas_scratch_memory] = CreateBuffer(tlas_build_sizes.buildScratchSize,
		vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eShaderDeviceAddress);

	auto tlas_scratch_buffer_addr = GetBufferDeviceAddress(*tlas_scratch_buffer);

	tlas_build_geometry_info
		.setMode(vk::BuildAccelerationStructureModeKHR::eBuild)
		.setDstAccelerationStructure(*tlas)
		.setScratchData(tlas_scratch_buffer_addr);

	auto tlas_build_range_info = vk::AccelerationStructureBuildRangeInfoKHR()
		.setPrimitiveCount(1);

	auto tlas_build_geometry_infos = { tlas_build_geometry_info };
	std::vector tlas_build_range_infos = { &tlas_build_range_info };

	OneTimeSubmit(gDevice, gCommandPool, gQueue, [&](auto& cmdbuf) {
		cmdbuf.buildAccelerationStructuresKHR(tlas_build_geometry_infos, tlas_build_range_infos);
	});

	return std::move(tlas);
}

class AccelerationStructureVK
{
public:
	const auto& getTopLevelAccelerationStructure() const { return mTopLevelAccelerationStructure; }

private:
	vk::raii::AccelerationStructureKHR mTopLevelAccelerationStructure = nullptr;
	vk::raii::AccelerationStructureKHR mBottomLevelAccelerationStructure = nullptr;
	vk::DeviceAddress mBottomLevelAccelerationStructureDeviceAddress;

public:
	AccelerationStructureVK(const std::vector<glm::vec3>& vertices, const std::vector<uint32_t>& indices)
	{
		auto transform = vk::TransformMatrixKHR()
			.setMatrix({
				1.0f, 0.0f, 0.0f, 0.0f,
				0.0f, 1.0f, 0.0f, 0.0f,
				0.0f, 0.0f, 1.0f, 0.0f
			});

		std::tie(mBottomLevelAccelerationStructure, mBottomLevelAccelerationStructureDeviceAddress) = 
			CreateBottomLevelAccelerationStrucutre(vertices, indices, transform);
		mTopLevelAccelerationStructure = CreateTopLevelAccelerationStructure(transform,
			mBottomLevelAccelerationStructureDeviceAddress);
	}
};

static bool gRenderPassActive = false;

static void BeginRenderPass()
{
	assert(!gRenderPassActive);
	gRenderPassActive = true;

	auto color_texture = gRenderTarget ?
		*gRenderTarget->getTexture()->getImageView() :
		*gFrames.at(gFrameIndex).backbuffer_color_image_view;

	auto depth_stencil_texture = gRenderTarget ?
		*gRenderTarget->getDepthStencilView() :
		*gDepthStencil.view;

	auto color_attachment = vk::RenderingAttachmentInfo()
		.setImageView(color_texture)
		.setImageLayout(vk::ImageLayout::eAttachmentOptimal)
		.setLoadOp(vk::AttachmentLoadOp::eLoad)
		.setStoreOp(vk::AttachmentStoreOp::eStore);

	auto depth_stencil_attachment = vk::RenderingAttachmentInfo()
		.setImageView(depth_stencil_texture)
		.setImageLayout(vk::ImageLayout::eDepthAttachmentOptimal)
		.setLoadOp(vk::AttachmentLoadOp::eLoad)
		.setStoreOp(vk::AttachmentStoreOp::eStore);

	auto width = gRenderTarget ? gRenderTarget->getTexture()->getWidth() : gWidth;
	auto height = gRenderTarget ? gRenderTarget->getTexture()->getHeight() : gHeight;

	auto rendering_info = vk::RenderingInfo()
		.setRenderArea({ { 0, 0 }, { width, height } })
		.setLayerCount(1)
		.setColorAttachmentCount(1)
		.setPColorAttachments(&color_attachment)
		.setPDepthAttachment(&depth_stencil_attachment)
		.setPStencilAttachment(&depth_stencil_attachment);

	gCommandBuffer.beginRendering(rendering_info);
}

static void EndRenderPass()
{
	assert(gRenderPassActive);
	gRenderPassActive = false;

	gCommandBuffer.endRendering();
}

static void EnsureRenderPassActivated()
{
	if (gRenderPassActive)
		return;

	BeginRenderPass();
}

static void EnsureRenderPassDeactivated()
{
	if (!gRenderPassActive)
		return;

	EndRenderPass();
}

static void PrepareForDrawing()
{
	assert(gVertexBuffer);
	
	if (gVertexBufferDirty)
	{
		gCommandBuffer.bindVertexBuffers2(0, { *gVertexBuffer->getBuffer() }, { 0 }, nullptr, { gVertexBuffer->getStride() });
		gVertexBufferDirty = false;
	}

	if (gIndexBufferDirty)
	{
		gCommandBuffer.bindIndexBuffer(*gIndexBuffer->getBuffer(), 0, GetIndexTypeFromStride(gIndexBuffer->getStride()));
		gIndexBufferDirty = false;
	}

	if (gTopologyDirty)
	{
		static const std::unordered_map<Topology, vk::PrimitiveTopology> TopologyMap = {
			{ Topology::PointList, vk::PrimitiveTopology::ePointList },
			{ Topology::LineList, vk::PrimitiveTopology::eLineList },
			{ Topology::LineStrip, vk::PrimitiveTopology::eLineStrip },
			{ Topology::TriangleList, vk::PrimitiveTopology::eTriangleList },
			{ Topology::TriangleStrip, vk::PrimitiveTopology::eTriangleStrip },
		};

		gCommandBuffer.setPrimitiveTopology(TopologyMap.at(gTopology));
		gTopologyDirty = false;
	}

	if (gDepthModeDirty)
	{
		// TODO: this depth options should work only when dynamic state enabled, 
		// but now it working only when dynamic state turned off. wtf is going on?
		// WTR: try to uncomment dynamic state depth values, try to move this block to end of this function

		if (gDepthMode.has_value())
		{
			gCommandBuffer.setDepthTestEnable(true);
			gCommandBuffer.setDepthWriteEnable(true);
			gCommandBuffer.setDepthCompareOp(CompareOpMap.at(gDepthMode.value().func));
		}
		else
		{
			gCommandBuffer.setDepthTestEnable(false);
			gCommandBuffer.setDepthWriteEnable(false);
		}

		gDepthModeDirty = false;
	}

	auto shader = gPipelineState.shader;
	const auto& blend_mode = gPipelineState.blend_mode;
	auto render_target = gPipelineState.render_target;

	assert(shader);

	if (!gPipelineStates.contains(gPipelineState))
	{
		auto pipeline_shader_stage_create_info = {
			vk::PipelineShaderStageCreateInfo()
				.setStage(vk::ShaderStageFlagBits::eVertex)
				.setModule(*shader->getVertexShaderModule())
				.setPName("main"),

			vk::PipelineShaderStageCreateInfo()
				.setStage(vk::ShaderStageFlagBits::eFragment)
				.setModule(*shader->getFragmentShaderModule())
				.setPName("main")
		};

		auto pipeline_input_assembly_state_create_info = vk::PipelineInputAssemblyStateCreateInfo()
			.setTopology(vk::PrimitiveTopology::eTriangleList);

		auto pipeline_viewport_state_create_info = vk::PipelineViewportStateCreateInfo()
			.setViewportCount(1)
			.setScissorCount(1);

		auto pipeline_rasterization_state_create_info = vk::PipelineRasterizationStateCreateInfo()
			.setPolygonMode(vk::PolygonMode::eFill);

		auto pipeline_multisample_state_create_info = vk::PipelineMultisampleStateCreateInfo()
			.setRasterizationSamples(vk::SampleCountFlagBits::e1);

		auto pipeline_depth_stencil_state_create_info = vk::PipelineDepthStencilStateCreateInfo();

		static const std::unordered_map<Blend, vk::BlendFactor> BlendFactorMap = {
			{ Blend::One, vk::BlendFactor::eOne },
			{ Blend::Zero, vk::BlendFactor::eZero },
			{ Blend::SrcColor, vk::BlendFactor::eSrcColor },
			{ Blend::InvSrcColor, vk::BlendFactor::eOneMinusSrcColor },
			{ Blend::SrcAlpha, vk::BlendFactor::eSrcAlpha },
			{ Blend::InvSrcAlpha, vk::BlendFactor::eOneMinusSrcAlpha },
			{ Blend::DstColor, vk::BlendFactor::eDstColor },
			{ Blend::InvDstColor, vk::BlendFactor::eOneMinusDstColor },
			{ Blend::DstAlpha, vk::BlendFactor::eDstAlpha },
			{ Blend::InvDstAlpha, vk::BlendFactor::eOneMinusDstAlpha }
		};

		static const std::unordered_map<BlendFunction, vk::BlendOp> BlendFuncMap = {
			{ BlendFunction::Add, vk::BlendOp::eAdd },
			{ BlendFunction::Subtract, vk::BlendOp::eSubtract },
			{ BlendFunction::ReverseSubtract, vk::BlendOp::eReverseSubtract },
			{ BlendFunction::Min, vk::BlendOp::eMin },
			{ BlendFunction::Max, vk::BlendOp::eMax },
		};

		auto color_mask = vk::ColorComponentFlags();

		if (blend_mode.color_mask.red)
			color_mask |= vk::ColorComponentFlagBits::eR;

		if (blend_mode.color_mask.green)
			color_mask |= vk::ColorComponentFlagBits::eG;

		if (blend_mode.color_mask.blue)
			color_mask |= vk::ColorComponentFlagBits::eB;

		if (blend_mode.color_mask.alpha)
			color_mask |= vk::ColorComponentFlagBits::eA;

		auto pipeline_color_blend_attachment_state = vk::PipelineColorBlendAttachmentState()
			.setBlendEnable(true)
			.setSrcColorBlendFactor(BlendFactorMap.at(blend_mode.color_src_blend))
			.setDstColorBlendFactor(BlendFactorMap.at(blend_mode.color_dst_blend))
			.setColorBlendOp(BlendFuncMap.at(blend_mode.color_blend_func))
			.setSrcAlphaBlendFactor(BlendFactorMap.at(blend_mode.alpha_src_blend))
			.setDstAlphaBlendFactor(BlendFactorMap.at(blend_mode.alpha_dst_blend))
			.setAlphaBlendOp(BlendFuncMap.at(blend_mode.alpha_blend_func))
			.setColorWriteMask(color_mask);

		auto pipeline_color_blend_state_create_info = vk::PipelineColorBlendStateCreateInfo()
			.setAttachmentCount(1)
			.setPAttachments(&pipeline_color_blend_attachment_state);

		auto pipeline_vertex_input_state_create_info = vk::PipelineVertexInputStateCreateInfo()
			.setVertexBindingDescriptionCount(1)
			.setPVertexBindingDescriptions(&shader->getVertexInputBindingDescription())
			.setVertexAttributeDescriptions(shader->getVertexInputAttributeDescriptions());

		auto dynamic_states = {
			vk::DynamicState::eViewport,
			vk::DynamicState::eScissor,
			vk::DynamicState::ePrimitiveTopology,
			vk::DynamicState::eLineWidth,
			vk::DynamicState::eCullMode,
			vk::DynamicState::eFrontFace,
			vk::DynamicState::eVertexInputBindingStride,
		//	vk::DynamicState::eDepthTestEnable, // TODO: this depth values should be uncommented
		//	vk::DynamicState::eDepthCompareOp,
		//	vk::DynamicState::eDepthWriteEnable
		};

		auto pipeline_dynamic_state_create_info = vk::PipelineDynamicStateCreateInfo()
			.setDynamicStates(dynamic_states);

		auto color_attachment_formats = {
			render_target ? render_target->getTexture()->getFormat() : gSurfaceFormat.format
		};

		auto depth_stencil_format = render_target ? render_target->getDepthStencilFormat() : gDepthStencil.format;

		auto pipeline_rendering_create_info = vk::PipelineRenderingCreateInfo()
			.setColorAttachmentFormats(color_attachment_formats)
			.setDepthAttachmentFormat(depth_stencil_format)
			.setStencilAttachmentFormat(depth_stencil_format);

		auto graphics_pipeline_create_info = vk::GraphicsPipelineCreateInfo()
			.setLayout(*shader->getPipelineLayout())
			.setFlags(vk::PipelineCreateFlagBits())
			.setStages(pipeline_shader_stage_create_info)
			.setPVertexInputState(&pipeline_vertex_input_state_create_info)
			.setPInputAssemblyState(&pipeline_input_assembly_state_create_info)
			.setPViewportState(&pipeline_viewport_state_create_info)
			.setPRasterizationState(&pipeline_rasterization_state_create_info)
			.setPMultisampleState(&pipeline_multisample_state_create_info)
			.setPDepthStencilState(&pipeline_depth_stencil_state_create_info)
			.setPColorBlendState(&pipeline_color_blend_state_create_info)
			.setPDynamicState(&pipeline_dynamic_state_create_info)
			.setRenderPass(nullptr)
			.setPNext(&pipeline_rendering_create_info);

		auto pipeline = gDevice.createGraphicsPipeline(nullptr, graphics_pipeline_create_info);

		gPipelineStates.insert({ gPipelineState, std::move(pipeline) });
	}

	const auto& pipeline = gPipelineStates.at(gPipelineState);

	gCommandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, *pipeline);

	if (!gSamplers.contains(gSamplerState))
	{
		static const std::unordered_map<Sampler, vk::Filter> FilterMap = {
			{ Sampler::Linear, vk::Filter::eLinear },
			{ Sampler::Nearest, vk::Filter::eNearest },
		};

		static const std::unordered_map<TextureAddress, vk::SamplerAddressMode> AddressModeMap = {
			{ TextureAddress::Clamp, vk::SamplerAddressMode::eClampToEdge },
			{ TextureAddress::Wrap, vk::SamplerAddressMode::eRepeat },
			{ TextureAddress::MirrorWrap, vk::SamplerAddressMode::eMirrorClampToEdge },
		};

		auto sampler_create_info = vk::SamplerCreateInfo()
			.setMagFilter(FilterMap.at(gSamplerState.sampler))
			.setMinFilter(FilterMap.at(gSamplerState.sampler))
			.setMipmapMode(vk::SamplerMipmapMode::eLinear)
			.setAddressModeU(AddressModeMap.at(gSamplerState.texture_address))
			.setAddressModeV(AddressModeMap.at(gSamplerState.texture_address))
			.setAddressModeW(AddressModeMap.at(gSamplerState.texture_address))
			.setMinLod(-1000)
			.setMaxLod(1000)
			.setMaxAnisotropy(1.0f);

		gSamplers.insert({ gSamplerState, gDevice.createSampler(sampler_create_info) });
	}

	const auto& sampler = gSamplers.at(gSamplerState);

	auto pipeline_layout = *shader->getPipelineLayout();

	for (const auto& required_descriptor_binding : shader->getRequiredDescriptorBindings())
	{
		auto binding = required_descriptor_binding.binding;

		auto write_descriptor_set = vk::WriteDescriptorSet()
			.setDescriptorCount(1)
			.setDstBinding(binding)
			//.setDstSet() // TODO: it seems we need iterate through required_descriptor_sets, not .._bindings
			.setDescriptorType(required_descriptor_binding.descriptorType);

		if (required_descriptor_binding.descriptorType == vk::DescriptorType::eCombinedImageSampler)
		{
			auto texture = gTextures.at(binding);

			auto descriptor_image_info = vk::DescriptorImageInfo()
				.setSampler(*sampler)
				.setImageView(*texture->getImageView())
				.setImageLayout(vk::ImageLayout::eGeneral);

			write_descriptor_set.setPImageInfo(&descriptor_image_info);
		}
		else if (required_descriptor_binding.descriptorType == vk::DescriptorType::eUniformBuffer)
		{
			auto buffer = gUniformBuffers.at(binding);

			auto descriptor_buffer_info = vk::DescriptorBufferInfo()
				.setBuffer(*buffer->getBuffer())
				.setRange(VK_WHOLE_SIZE);

			write_descriptor_set.setPBufferInfo(&descriptor_buffer_info);
		}
		else
		{
			assert(false);
		}

		gCommandBuffer.pushDescriptorSetKHR(vk::PipelineBindPoint::eGraphics, pipeline_layout, 0, { write_descriptor_set });
	}

	if (gViewportDirty)
	{
		auto value = gViewport.value_or(Viewport{ { 0.0f, 0.0f }, { static_cast<float>(gWidth), static_cast<float>(gHeight) } });

		auto viewport = vk::Viewport()
			.setX(value.position.x)
			.setY(value.size.y - value.position.y)
			.setWidth(value.size.x)
			.setHeight(-value.size.y)
			.setMinDepth(value.min_depth)
			.setMaxDepth(value.max_depth);

		gCommandBuffer.setViewport(0, { viewport });
		gViewportDirty = false;
	}

	if (gScissorDirty)
	{
		auto value = gScissor.value_or(Scissor{ { 0.0f, 0.0f }, { static_cast<float>(gWidth), static_cast<float>(gHeight) } });

		auto rect = vk::Rect2D()
			.setOffset({ static_cast<int32_t>(value.position.x), static_cast<int32_t>(value.position.y) })
			.setExtent({ static_cast<uint32_t>(value.size.x), static_cast<uint32_t>(value.size.y) });

		gCommandBuffer.setScissor(0, { rect });
		gScissorDirty = false;
	}

	if (gCullModeDirty)
	{
		const static std::unordered_map<CullMode, vk::CullModeFlags> CullModeMap = {
			{ CullMode::None, vk::CullModeFlagBits::eNone },
			{ CullMode::Front, vk::CullModeFlagBits::eFront },
			{ CullMode::Back, vk::CullModeFlagBits::eBack },
		};

		gCommandBuffer.setFrontFace(vk::FrontFace::eClockwise);
		gCommandBuffer.setCullMode(CullModeMap.at(gCullMode));

		gCullModeDirty = false;
	}
}

BackendVK::BackendVK(void* window, uint32_t width, uint32_t height)
{
#if defined(SKYGFX_PLATFORM_WINDOWS)
	gContext = new vk::raii::Context();
#elif defined(SKYGFX_PLATFORM_MACOS) | defined(SKYGFX_PLATFORM_IOS)
	gContext = new vk::raii::Context(vkGetInstanceProcAddr);
#endif

	auto all_extensions = gContext->enumerateInstanceExtensionProperties();

	for (auto extension : all_extensions)
	{
		//	std::cout << extension.extensionName << std::endl;
	}

	auto all_layers = gContext->enumerateInstanceLayerProperties();

	for (auto layer : all_layers)
	{
		//	std::cout << layer.layerName << std::endl;
	}

	auto extensions = {
		VK_KHR_SURFACE_EXTENSION_NAME,
#if defined(SKYGFX_PLATFORM_WINDOWS)
		VK_KHR_WIN32_SURFACE_EXTENSION_NAME,
#elif defined(SKYGFX_PLATFORM_IOS)
		VK_MVK_IOS_SURFACE_EXTENSION_NAME,
#elif defined(SKYGFX_PLATFORM_MACOS)
		VK_MVK_MACOS_SURFACE_EXTENSION_NAME,
#endif
	};
	
	auto layers = {
		"VK_LAYER_KHRONOS_validation"
	};

	auto version = gContext->enumerateInstanceVersion();

	auto major_version = VK_API_VERSION_MAJOR(version);
	auto minor_version = VK_API_VERSION_MINOR(version);
	auto patch_version = VK_API_VERSION_PATCH(version);

	//std::cout << "available vulkan version: " << major_version << "." << minor_version << std::endl;

	auto application_info = vk::ApplicationInfo()
		.setApiVersion(VK_API_VERSION_1_3);

	auto instance_info = vk::InstanceCreateInfo()
		.setPEnabledExtensionNames(extensions)
		.setPEnabledLayerNames(layers)
		.setPApplicationInfo(&application_info);

	gInstance = gContext->createInstance(instance_info);

	auto devices = gInstance.enumeratePhysicalDevices();
	size_t device_index = 0;
	for (size_t i = 0; i < devices.size(); i++)
	{
		auto properties = devices.at(i).getProperties();
		if (properties.deviceType == vk::PhysicalDeviceType::eDiscreteGpu)
		{
			device_index = i;
			break;
		}
	}

	gPhysicalDevice = std::move(devices.at(device_index));

	auto properties = gPhysicalDevice.getQueueFamilyProperties();

	for (size_t i = 0; i < properties.size(); i++)
	{
		if (properties[i].queueFlags & vk::QueueFlagBits::eGraphics)
		{
			gQueueFamilyIndex = static_cast<uint32_t>(i);
			break;
		}
	}

	auto all_device_extensions = gPhysicalDevice.enumerateDeviceExtensionProperties();

	for (auto device_extension : all_device_extensions)
	{
		//	std::cout << device_extension.extensionName << std::endl;
	}

	auto device_extensions = {
		VK_KHR_SWAPCHAIN_EXTENSION_NAME,
		VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME,

		// raytracing
		VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME,
		VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,
		VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME
	};

	auto queue_priority = { 1.0f };

	auto queue_info = vk::DeviceQueueCreateInfo()
		.setQueueFamilyIndex(gQueueFamilyIndex)
		.setQueuePriorities(queue_priority);

	auto device_features = gPhysicalDevice.getFeatures2<vk::PhysicalDeviceFeatures2,
		vk::PhysicalDeviceVulkan13Features, 
		vk::PhysicalDeviceRayTracingPipelineFeaturesKHR,
		vk::PhysicalDeviceAccelerationStructureFeaturesKHR,
		vk::PhysicalDeviceBufferAddressFeaturesEXT>();

	//auto device_properties = gPhysicalDevice.getProperties2<vk::PhysicalDeviceProperties2, 
	//	vk::PhysicalDeviceVulkan13Properties>(); // TODO: unused

	auto device_info = vk::DeviceCreateInfo()
		.setQueueCreateInfoCount(1)
		.setPQueueCreateInfos(&queue_info)
		.setPEnabledExtensionNames(device_extensions)
		.setPEnabledFeatures(nullptr)
		.setPNext(&device_features.get<vk::PhysicalDeviceFeatures2>());

	gDevice = gPhysicalDevice.createDevice(device_info);

	gQueue = gDevice.getQueue(gQueueFamilyIndex, 0);

#if defined(SKYGFX_PLATFORM_WINDOWS)
	auto surface_info = vk::Win32SurfaceCreateInfoKHR()
		.setHwnd((HWND)window);
#elif defined(SKYGFX_PLATFORM_MACOS)
	auto surface_info = vk::MacOSSurfaceCreateInfoMVK()
		.setPView(window);
#elif defined(SKYGFX_PLATFORM_IOS)
	auto surface_info = vk::IOSSurfaceCreateInfoMVK()
		.setPView(window);
#endif

	gSurface = vk::raii::SurfaceKHR(gInstance, surface_info);

	auto formats = gPhysicalDevice.getSurfaceFormatsKHR(*gSurface);

	if ((formats.size() == 1) && (formats.at(0).format == vk::Format::eUndefined))
	{
		gSurfaceFormat = {
			vk::Format::eB8G8R8A8Unorm,
			formats.at(0).colorSpace
		};
	}
	else
	{
		bool found = false;
		for (const auto& format : formats)
		{
			if (format.format == vk::Format::eB8G8R8A8Unorm)
			{
				gSurfaceFormat = format;
				found = true;
				break;
			}
		}
		if (!found)
		{
			gSurfaceFormat = formats.at(0);
		}
	}

	auto command_pool_info = vk::CommandPoolCreateInfo()
		.setFlags(vk::CommandPoolCreateFlagBits::eResetCommandBuffer)
		.setQueueFamilyIndex(gQueueFamilyIndex);

	gCommandPool = gDevice.createCommandPool(command_pool_info);

	auto command_buffer_allocate_info = vk::CommandBufferAllocateInfo()
		.setCommandBufferCount(1)
		.setLevel(vk::CommandBufferLevel::ePrimary)
		.setCommandPool(*gCommandPool);

	auto command_buffers = gDevice.allocateCommandBuffers(command_buffer_allocate_info);
	gCommandBuffer = std::move(command_buffers.at(0));

	createSwapchain(width, height);

	begin();
}

BackendVK::~BackendVK()
{
	end();
	gExecuteAfterPresent.flush();
	gStagingObjects.clear();
	delete gContext;
}

void BackendVK::resize(uint32_t width, uint32_t height)
{
	end();
	createSwapchain(width, height);
	begin();
}

void BackendVK::setTopology(Topology topology)
{
	if (gTopology == topology)
		return;

	gTopology = topology;
	gTopologyDirty = true;
}

void BackendVK::setViewport(std::optional<Viewport> viewport)
{
	if (gViewport == viewport)
		return;

	gViewport = viewport;
	gViewportDirty = true;
}

void BackendVK::setScissor(std::optional<Scissor> scissor)
{
	if (gScissor == scissor)
		return;

	gScissor = scissor;
	gScissorDirty = true;
}

void BackendVK::setTexture(uint32_t binding, TextureHandle* handle)
{
	auto texture = (TextureVK*)handle;
	gTextures[binding] = texture;
}

void BackendVK::setRenderTarget(RenderTargetHandle* handle)
{
	auto render_target = (RenderTargetVK*)handle;

	if (gRenderTarget == render_target)
		return;

	gPipelineState.render_target = render_target; // TODO: see how we do it in metal, we dont need this field!
	gRenderTarget = render_target;
	EnsureRenderPassDeactivated();
}

void BackendVK::setRenderTarget(std::nullopt_t value)
{
	if (gRenderTarget == nullptr)
		return;

	gPipelineState.render_target = nullptr; // TODO: see how we do it in metal, we dont need this field!
	gRenderTarget = nullptr;
	EnsureRenderPassDeactivated();
}

void BackendVK::setShader(ShaderHandle* handle)
{
	auto shader = (ShaderVK*)handle;
	gPipelineState.shader = shader;
}

void BackendVK::setRaytracingShader(RaytracingShaderHandle* handle)
{
	auto shader = (RaytracingShaderVK*)handle;
	gRaytracingPipelineState.shader = shader;
}

void BackendVK::setVertexBuffer(VertexBufferHandle* handle)
{
	auto buffer = (VertexBufferVK*)handle;

	if (buffer == gVertexBuffer)
		return;

	gVertexBuffer = buffer;
	gVertexBufferDirty = true;
}

void BackendVK::setIndexBuffer(IndexBufferHandle* handle)
{
	auto buffer = (IndexBufferVK*)handle;

	if (buffer == gIndexBuffer)
		return;

	gIndexBuffer = buffer;
	gIndexBufferDirty = true;
}

void BackendVK::setUniformBuffer(uint32_t binding, UniformBufferHandle* handle)
{
	auto buffer = (UniformBufferVK*)handle;
	gUniformBuffers[binding] = buffer;
}

void BackendVK::setAccelerationStructure(uint32_t binding, AccelerationStructureHandle* handle)
{
	auto acceleration_structure = (AccelerationStructureVK*)handle;
	gAccelerationStructures[binding] = acceleration_structure;
}

void BackendVK::setBlendMode(const BlendMode& value)
{
	gPipelineState.blend_mode = value;
}

void BackendVK::setDepthMode(std::optional<DepthMode> depth_mode)
{
	if (gDepthMode != depth_mode)
		return;

	gDepthMode = depth_mode;
	gDepthModeDirty = true;
}

void BackendVK::setStencilMode(std::optional<StencilMode> stencil_mode)
{
	gCommandBuffer.setStencilTestEnable(stencil_mode.has_value());
}

void BackendVK::setCullMode(CullMode cull_mode)
{	
	gCullMode = cull_mode;
	gCullModeDirty = true;
}

void BackendVK::setSampler(Sampler value)
{
	gSamplerState.sampler = value;
}

void BackendVK::setTextureAddress(TextureAddress value)
{
	gSamplerState.texture_address = value;
}

void BackendVK::clear(const std::optional<glm::vec4>& color, const std::optional<float>& depth,
	const std::optional<uint8_t>& stencil)
{
	EnsureRenderPassActivated();

	auto width = gRenderTarget ? gRenderTarget->getTexture()->getWidth() : gWidth;
	auto height = gRenderTarget ? gRenderTarget->getTexture()->getHeight() : gHeight;

	auto clear_rect = vk::ClearRect()
		.setBaseArrayLayer(0)
		.setLayerCount(1)
		.setRect({ { 0, 0 }, { width, height } });

	if (color.has_value())
	{
		auto value = color.value();

		auto clear_color_value = vk::ClearColorValue()
			.setFloat32({ value.r, value.g, value.b, value.a });

		auto clear_value = vk::ClearValue()
			.setColor(clear_color_value);
		
		auto attachment = vk::ClearAttachment()
			.setAspectMask(vk::ImageAspectFlagBits::eColor)
			.setColorAttachment(0)
			.setClearValue(clear_value);

		gCommandBuffer.clearAttachments({ attachment }, { clear_rect });
	}

	if (depth.has_value() || stencil.has_value())
	{
		auto clear_depth_stencil_value = vk::ClearDepthStencilValue()
			.setDepth(depth.value_or(1.0f))
			.setStencil((uint32_t)stencil.value_or(0)); // TODO: maybe we should change argument uint8_t -> uint32_t

		auto clear_value = vk::ClearValue()
			.setDepthStencil(clear_depth_stencil_value);

		auto aspect_mask = vk::ImageAspectFlags();

		if (depth.has_value())
			aspect_mask |= vk::ImageAspectFlagBits::eDepth;

		if (stencil.has_value())
			aspect_mask |= vk::ImageAspectFlagBits::eStencil;

		auto attachment = vk::ClearAttachment()
			.setAspectMask(aspect_mask)
			.setColorAttachment(0)
			.setClearValue(clear_value);

		gCommandBuffer.clearAttachments({ attachment }, { clear_rect });
	}
}

void BackendVK::draw(uint32_t vertex_count, uint32_t vertex_offset)
{
	PrepareForDrawing();
	EnsureRenderPassActivated();
	gCommandBuffer.draw(vertex_count, 1, vertex_offset, 0);
}

void BackendVK::drawIndexed(uint32_t index_count, uint32_t index_offset)
{
	PrepareForDrawing();
	EnsureRenderPassActivated();
	gCommandBuffer.drawIndexed(index_count, 1, index_offset, 0, 0);
}

void BackendVK::readPixels(const glm::i32vec2& pos, const glm::i32vec2& size, TextureHandle* dst_texture_handle)
{
}

template<typename T>
inline T AlignUp(T size, size_t alignment) noexcept
{
	if (alignment > 0)
	{
		assert(((alignment - 1) & alignment) == 0);
		auto mask = static_cast<T>(alignment - 1);
		return (size + mask) & ~mask;
	}
	return size;
}

void BackendVK::dispatchRays(uint32_t width, uint32_t height, uint32_t depth)
{
	assert(gRenderTarget != nullptr);

	EnsureRenderPassDeactivated();

	static std::optional<vk::raii::PipelineLayout> pipeline_layout;

	auto shader = gRaytracingPipelineState.shader;
	assert(shader);

	if (!gRaytracingPipelineStates.contains(gRaytracingPipelineState))
	{
		auto pipeline_shader_stage_create_info = {
			vk::PipelineShaderStageCreateInfo()
				.setStage(vk::ShaderStageFlagBits::eRaygenKHR)
				.setModule(*shader->getRaygenShaderModule())
				.setPName("main"),

			vk::PipelineShaderStageCreateInfo()
				.setStage(vk::ShaderStageFlagBits::eMissKHR)
				.setModule(*shader->getMissShaderModule())
				.setPName("main"),

			vk::PipelineShaderStageCreateInfo()
				.setStage(vk::ShaderStageFlagBits::eClosestHitKHR)
				.setModule(*shader->getClosestHitShaderModule())
				.setPName("main")
		};

		auto required_descriptor_bindings = {
			vk::DescriptorSetLayoutBinding()
				.setDescriptorType(vk::DescriptorType::eAccelerationStructureKHR)
				.setDescriptorCount(1)
				.setBinding(0)
				.setStageFlags(vk::ShaderStageFlagBits::eRaygenKHR),

			vk::DescriptorSetLayoutBinding()
				.setDescriptorType(vk::DescriptorType::eStorageImage)
				.setDescriptorCount(1)
				.setBinding(1)
				.setStageFlags(vk::ShaderStageFlagBits::eRaygenKHR)
		};

		auto descriptor_set_layout_create_info = vk::DescriptorSetLayoutCreateInfo()
			.setFlags(vk::DescriptorSetLayoutCreateFlagBits::ePushDescriptorKHR)
			.setBindings(required_descriptor_bindings);

		static auto descriptor_set_layout = gDevice.createDescriptorSetLayout(descriptor_set_layout_create_info);

		auto pipeline_layout_create_info = vk::PipelineLayoutCreateInfo()
			.setSetLayoutCount(1)
			.setPSetLayouts(&*descriptor_set_layout);

		pipeline_layout = gDevice.createPipelineLayout(pipeline_layout_create_info);

		auto raytracing_shader_groups = { 
			vk::RayTracingShaderGroupCreateInfoKHR() // raygen
				.setType(vk::RayTracingShaderGroupTypeKHR::eGeneral)
				.setGeneralShader(0)
				.setClosestHitShader(VK_SHADER_UNUSED_KHR)
				.setAnyHitShader(VK_SHADER_UNUSED_KHR)
				.setIntersectionShader(VK_SHADER_UNUSED_KHR),

			vk::RayTracingShaderGroupCreateInfoKHR() // miss
				.setType(vk::RayTracingShaderGroupTypeKHR::eGeneral)
				.setGeneralShader(1)
				.setClosestHitShader(VK_SHADER_UNUSED_KHR)
				.setAnyHitShader(VK_SHADER_UNUSED_KHR)
				.setIntersectionShader(VK_SHADER_UNUSED_KHR),

			vk::RayTracingShaderGroupCreateInfoKHR() // closesthit
				.setType(vk::RayTracingShaderGroupTypeKHR::eTrianglesHitGroup)
				.setGeneralShader(VK_SHADER_UNUSED_KHR)
				.setClosestHitShader(2)
				.setAnyHitShader(VK_SHADER_UNUSED_KHR)
				.setIntersectionShader(VK_SHADER_UNUSED_KHR)
		};

		auto raytracing_pipeline_create_info = vk::RayTracingPipelineCreateInfoKHR()
			.setLayout(*pipeline_layout.value())
			.setStages(pipeline_shader_stage_create_info)
			.setGroups(raytracing_shader_groups)
			.setMaxPipelineRayRecursionDepth(1);

		auto pipeline = gDevice.createRayTracingPipelineKHR(nullptr, nullptr, raytracing_pipeline_create_info);

		gRaytracingPipelineStates.insert({ gRaytracingPipelineState, std::move(pipeline) });
	}

	const auto& pipeline = gRaytracingPipelineStates.at(gRaytracingPipelineState);

	gCommandBuffer.bindPipeline(vk::PipelineBindPoint::eRayTracingKHR, *pipeline);
	
	auto write_descriptor_set_acceleration_structure = vk::WriteDescriptorSetAccelerationStructureKHR()
		.setAccelerationStructureCount(1)
		.setPAccelerationStructures(&*gAccelerationStructures.at(0)->getTopLevelAccelerationStructure());

	gCommandBuffer.pushDescriptorSetKHR(vk::PipelineBindPoint::eRayTracingKHR, *pipeline_layout.value(), 0, {
		vk::WriteDescriptorSet()
			.setDstBinding(0)
			.setDescriptorCount(1)
			.setDescriptorType(vk::DescriptorType::eAccelerationStructureKHR)
			.setPNext(&write_descriptor_set_acceleration_structure),
	});

	auto render_target_image_view = *gRenderTarget->getTexture()->getImageView();

	auto descriptor_image_info = vk::DescriptorImageInfo()
		.setImageLayout(vk::ImageLayout::eGeneral)
		.setImageView(render_target_image_view);

	gCommandBuffer.pushDescriptorSetKHR(vk::PipelineBindPoint::eRayTracingKHR, *pipeline_layout.value(), 0, {
		vk::WriteDescriptorSet()
			.setDstBinding(1)
			.setDescriptorCount(1)
			.setDescriptorType(vk::DescriptorType::eStorageImage)
			.setPImageInfo(&descriptor_image_info)
	});

	static std::optional<vk::StridedDeviceAddressRegionKHR> raygen_shader_binding_table;
	static std::optional<vk::StridedDeviceAddressRegionKHR> miss_shader_binding_table;
	static std::optional<vk::StridedDeviceAddressRegionKHR> hit_shader_binding_table;
	auto callable_shader_binding_table = vk::StridedDeviceAddressRegionKHR();

	if (!raygen_shader_binding_table.has_value())
	{
		VkPhysicalDeviceRayTracingPipelinePropertiesKHR ray_tracing_pipeline_properties{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR };

		VkPhysicalDeviceProperties2 prop2{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2 };
		prop2.pNext = &ray_tracing_pipeline_properties;
		vkGetPhysicalDeviceProperties2(*gPhysicalDevice, &prop2);

		auto handle_size = ray_tracing_pipeline_properties.shaderGroupHandleSize;
		auto handle_size_aligned = AlignUp(handle_size, ray_tracing_pipeline_properties.shaderGroupHandleAlignment);

		static auto [raygen_binding_table_buffer, raygen_binding_table_memory] = CreateBuffer(handle_size_aligned,
			vk::BufferUsageFlagBits::eShaderBindingTableKHR | vk::BufferUsageFlagBits::eShaderDeviceAddress);

		static auto [miss_binding_table_buffer, miss_binding_table_memory] = CreateBuffer(handle_size_aligned, 
			vk::BufferUsageFlagBits::eShaderBindingTableKHR | vk::BufferUsageFlagBits::eShaderDeviceAddress);

		static auto [closesthit_binding_table_buffer, closesthit_binding_table_memory] = CreateBuffer(handle_size_aligned,
			vk::BufferUsageFlagBits::eShaderBindingTableKHR | vk::BufferUsageFlagBits::eShaderDeviceAddress);

		auto group_count = 3; // 3 shader groups
		auto sbt_size = group_count * handle_size_aligned;

		auto shader_handle_storage = pipeline.getRayTracingShaderGroupHandlesKHR<uint8_t>(0, group_count, sbt_size);

		WriteToBuffer(raygen_binding_table_memory, shader_handle_storage.data(), handle_size);
		WriteToBuffer(miss_binding_table_memory, shader_handle_storage.data() + handle_size_aligned, handle_size);
		WriteToBuffer(closesthit_binding_table_memory, shader_handle_storage.data() + (handle_size_aligned * 2), handle_size);

		raygen_shader_binding_table = vk::StridedDeviceAddressRegionKHR()
			.setStride(handle_size_aligned)
			.setSize(handle_size_aligned)
			.setDeviceAddress(GetBufferDeviceAddress(*raygen_binding_table_buffer));

		miss_shader_binding_table = vk::StridedDeviceAddressRegionKHR()
			.setStride(handle_size_aligned)
			.setSize(handle_size_aligned)
			.setDeviceAddress(GetBufferDeviceAddress(*miss_binding_table_buffer));

		hit_shader_binding_table = vk::StridedDeviceAddressRegionKHR()
			.setStride(handle_size_aligned)
			.setSize(handle_size_aligned)
			.setDeviceAddress(GetBufferDeviceAddress(*closesthit_binding_table_buffer));
	}

	gCommandBuffer.traceRaysKHR(raygen_shader_binding_table.value(), miss_shader_binding_table.value(), hit_shader_binding_table.value(),
		callable_shader_binding_table, width, height, depth);
}

void BackendVK::present()
{
	end(); 

	const auto& render_complete_semaphore = gFrames.at(gSemaphoreIndex).render_complete_semaphore;

	auto present_info = vk::PresentInfoKHR()
		.setWaitSemaphoreCount(1)
		.setPWaitSemaphores(&*render_complete_semaphore)
		.setSwapchainCount(1)
		.setPSwapchains(&*gSwapchain)
		.setPImageIndices(&gFrameIndex);

	auto present_result = gQueue.presentKHR(present_info);

	gExecuteAfterPresent.flush();
	gStagingObjects.clear();

	gSemaphoreIndex = (gSemaphoreIndex + 1) % gFrames.size();

	begin();
}

void BackendVK::begin()
{
	const auto& image_acquired_semaphore = gFrames.at(gSemaphoreIndex).image_acquired_semaphore;

	auto [result, image_index] = gSwapchain.acquireNextImage(UINT64_MAX, *image_acquired_semaphore);

	gFrameIndex = image_index;

	assert(!gWorking);
	gWorking = true;

	gTopologyDirty = true;
	gViewportDirty = true;
	gScissorDirty = true;
	gCullModeDirty = true;
	gVertexBufferDirty = true;
	gIndexBufferDirty = true;

	auto begin_info = vk::CommandBufferBeginInfo()
		.setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit);

	gCommandBuffer.begin(begin_info);
}

void BackendVK::end()
{
	assert(gWorking);
	gWorking = false;

	EnsureRenderPassDeactivated();
	gCommandBuffer.end();

	const auto& frame = gFrames.at(gFrameIndex);

	auto wait_result = gDevice.waitForFences({ *frame.fence }, true, UINT64_MAX);

	gDevice.resetFences({ *frame.fence });

	const auto& render_complete_semaphore = gFrames.at(gSemaphoreIndex).render_complete_semaphore;
	const auto& image_acquired_semaphore = gFrames.at(gSemaphoreIndex).image_acquired_semaphore;

	auto wait_dst_stage_mask = vk::PipelineStageFlags{
		vk::PipelineStageFlagBits::eColorAttachmentOutput
	};

	auto submit_info = vk::SubmitInfo()
		.setPWaitDstStageMask(&wait_dst_stage_mask)
		.setWaitSemaphoreCount(1)
		.setPWaitSemaphores(&*image_acquired_semaphore)
		.setCommandBufferCount(1)
		.setPCommandBuffers(&*gCommandBuffer)
		.setSignalSemaphoreCount(1)
		.setPSignalSemaphores(&*render_complete_semaphore);

	gQueue.submit({ submit_info }, *frame.fence);
	gQueue.waitIdle();
}

TextureHandle* BackendVK::createTexture(uint32_t width, uint32_t height, uint32_t channels, void* memory, bool mipmap)
{
	auto texture = new TextureVK(width, height, channels, memory, mipmap);
	return (TextureHandle*)texture;
}

void BackendVK::destroyTexture(TextureHandle* handle)
{
	gExecuteAfterPresent.add([handle] {
		auto texture = (TextureVK*)handle;

		auto remove_from_global = [&] {
			for (const auto& [binding, _texture] : gTextures)
			{
				if (texture == _texture)
				{
					gTextures.erase(binding);
					return false;
				}
			}

			return true;
		};

		while (!remove_from_global()) {}

		delete texture;
	});
}

RenderTargetHandle* BackendVK::createRenderTarget(uint32_t width, uint32_t height, TextureHandle* texture_handle)
{
	auto texture = (TextureVK*)texture_handle;
	auto render_target = new RenderTargetVK(width, height, texture);
	return (RenderTargetHandle*)render_target;
}

void BackendVK::destroyRenderTarget(RenderTargetHandle* handle)
{
	auto render_target = (RenderTargetVK*)handle;
	delete render_target;
}

ShaderHandle* BackendVK::createShader(const Vertex::Layout& layout, const std::string& vertex_code,
	const std::string& fragment_code, const std::vector<std::string>& defines)
{
	auto shader = new ShaderVK(layout, vertex_code, fragment_code, defines);
	return (ShaderHandle*)shader;
}

void BackendVK::destroyShader(ShaderHandle* handle)
{
	gExecuteAfterPresent.add([handle] {
		auto shader = (ShaderVK*)handle;

		for (const auto& [state, pipeline] : gPipelineStates)
		{
			if (state.shader != shader)
				continue;

			gPipelineStates.erase(state);
		}

		delete shader;
	});
}

RaytracingShaderHandle* BackendVK::createRaytracingShader(const std::string& raygen_code, const std::string& miss_code,
	const std::string& closesthit_code, const std::vector<std::string>& defines)
{
	auto shader = new RaytracingShaderVK(raygen_code, miss_code, closesthit_code, defines);
	return (RaytracingShaderHandle*)shader;
}

void BackendVK::destroyRaytracingShader(RaytracingShaderHandle* handle)
{
	gExecuteAfterPresent.add([handle] {
		auto shader = (RaytracingShaderVK*)handle;

		for (const auto& [state, pipeline] : gRaytracingPipelineStates)
		{
			if (state.shader != shader)
				continue;

			gRaytracingPipelineStates.erase(state);
		}

		delete shader;
	});
}

VertexBufferHandle* BackendVK::createVertexBuffer(size_t size, size_t stride)
{
	auto buffer = new VertexBufferVK(size, stride);
	return (VertexBufferHandle*)buffer;
}

void BackendVK::destroyVertexBuffer(VertexBufferHandle* handle)
{
	gExecuteAfterPresent.add([handle] {
		auto buffer = (VertexBufferVK*)handle;
		delete buffer;
	});
}

void BackendVK::writeVertexBufferMemory(VertexBufferHandle* handle, void* memory, size_t size, size_t stride)
{
	auto buffer = (VertexBufferVK*)handle;
	buffer->write(memory, size);
	buffer->setStride(stride);
}

IndexBufferHandle* BackendVK::createIndexBuffer(size_t size, size_t stride)
{
	auto buffer = new IndexBufferVK(size, stride);
	return (IndexBufferHandle*)buffer;
}

void BackendVK::destroyIndexBuffer(IndexBufferHandle* handle)
{
	gExecuteAfterPresent.add([handle] {
		auto buffer = (IndexBufferVK*)handle;
		delete buffer;
	});
}

void BackendVK::writeIndexBufferMemory(IndexBufferHandle* handle, void* memory, size_t size, size_t stride)
{
	auto buffer = (IndexBufferVK*)handle;
	buffer->write(memory, size);
	buffer->setStride(stride);
}

UniformBufferHandle* BackendVK::createUniformBuffer(size_t size)
{
	auto buffer = new UniformBufferVK(size);
	return (UniformBufferHandle*)buffer;
}

void BackendVK::destroyUniformBuffer(UniformBufferHandle* handle)
{
	gExecuteAfterPresent.add([handle] {
		auto buffer = (UniformBufferVK*)handle;

		auto remove_from_global = [&] {
			for (const auto& [binding, _buffer] : gUniformBuffers)
			{
				if (buffer == _buffer)
				{
					gUniformBuffers.erase(binding);
					return false;
				}
			}

			return true;
		};

		while (!remove_from_global()) {}

		delete buffer;
	});
}

void BackendVK::writeUniformBufferMemory(UniformBufferHandle* handle, void* memory, size_t size)
{
	auto buffer = (UniformBufferVK*)handle;
	buffer->write(memory, size);
}

AccelerationStructureHandle* BackendVK::createAccelerationStructure(const std::vector<glm::vec3>& vertices,
	const std::vector<uint32_t>& indices)
{
	auto acceleration_structure = new AccelerationStructureVK(vertices, indices);
	return (AccelerationStructureHandle*)acceleration_structure;
}

void BackendVK::destroyAccelerationStructure(AccelerationStructureHandle* handle)
{
	gExecuteAfterPresent.add([handle] {
		auto acceleration_structure = (AccelerationStructureVK*)handle;

		auto remove_from_global = [&] {
			for (const auto& [binding, _acceleration_structure] : gAccelerationStructures)
			{
				if (acceleration_structure == _acceleration_structure)
				{
					gAccelerationStructures.erase(binding);
					return false;
				}
			}

			return true;
		};

		while (!remove_from_global()) {}

		delete acceleration_structure;
	});
}

void BackendVK::createSwapchain(uint32_t width, uint32_t height)
{
	gWidth = width;
	gHeight = height;

	auto swapchain_info = vk::SwapchainCreateInfoKHR()
		.setSurface(*gSurface)
		.setMinImageCount(gMinImageCount)
		.setImageFormat(gSurfaceFormat.format)
		.setImageColorSpace(gSurfaceFormat.colorSpace)
		.setImageExtent({ width, height })
		.setImageUsage(vk::ImageUsageFlagBits::eColorAttachment)
		.setPreTransform(vk::SurfaceTransformFlagBitsKHR::eIdentity)
		.setImageArrayLayers(1)
		.setImageSharingMode(vk::SharingMode::eExclusive)
		.setQueueFamilyIndexCount(1)
		.setPQueueFamilyIndices(&gQueueFamilyIndex)
		.setPresentMode(vk::PresentModeKHR::eFifo)
		.setClipped(true)
		.setCompositeAlpha(vk::CompositeAlphaFlagBitsKHR::eOpaque)
		.setOldSwapchain(*gSwapchain);

	gSwapchain = gDevice.createSwapchainKHR(swapchain_info);

	auto backbuffers = gSwapchain.getImages();

	gFrames.clear();

	for (auto& backbuffer : backbuffers)
	{
		auto frame = FrameVK();

		auto fence_info = vk::FenceCreateInfo()
			.setFlags(vk::FenceCreateFlagBits::eSignaled);

		frame.fence = gDevice.createFence(fence_info);

		frame.image_acquired_semaphore = gDevice.createSemaphore({});
		frame.render_complete_semaphore = gDevice.createSemaphore({});

		auto image_view_info = vk::ImageViewCreateInfo()
			.setViewType(vk::ImageViewType::e2D)
			.setFormat(gSurfaceFormat.format)
			.setComponents(vk::ComponentMapping()
				.setR(vk::ComponentSwizzle::eR)
				.setG(vk::ComponentSwizzle::eG)
				.setB(vk::ComponentSwizzle::eB)
				.setA(vk::ComponentSwizzle::eA)
			)
			.setSubresourceRange(vk::ImageSubresourceRange()
				.setAspectMask(vk::ImageAspectFlagBits::eColor)
				.setBaseMipLevel(0)
				.setLevelCount(1)
				.setBaseArrayLayer(0)
				.setLayerCount(1)
			)
			.setImage(backbuffer);

		frame.backbuffer_color_image_view = gDevice.createImageView(image_view_info);

		OneTimeSubmit(gDevice, gCommandPool, gQueue, [&](auto& cmdbuf) {
			SetImageLayout(cmdbuf, backbuffer, gSurfaceFormat.format, vk::ImageLayout::eUndefined,
				vk::ImageLayout::ePresentSrcKHR);
		});

		gFrames.push_back(std::move(frame));
	}

	// depth stencil

	auto depth_stencil_image_create_info = vk::ImageCreateInfo()
		.setImageType(vk::ImageType::e2D)
		.setFormat(gDepthStencil.format)
		.setExtent({ width, height, 1 })
		.setMipLevels(1)
		.setArrayLayers(1)
		.setSamples(vk::SampleCountFlagBits::e1)
		.setTiling(vk::ImageTiling::eOptimal)
		.setUsage(vk::ImageUsageFlagBits::eDepthStencilAttachment);

	gDepthStencil.image = gDevice.createImage(depth_stencil_image_create_info);

	auto depth_stencil_mem_req = gDepthStencil.image.getMemoryRequirements();

	auto depth_stencil_memory_allocate_info = vk::MemoryAllocateInfo()
		.setAllocationSize(depth_stencil_mem_req.size)
		.setMemoryTypeIndex(GetMemoryType(vk::MemoryPropertyFlagBits::eDeviceLocal, depth_stencil_mem_req.memoryTypeBits));

	gDepthStencil.memory = gDevice.allocateMemory(depth_stencil_memory_allocate_info);

	gDepthStencil.image.bindMemory(*gDepthStencil.memory, 0);

	auto depth_stencil_view_subresource_range = vk::ImageSubresourceRange()
		.setLevelCount(1)
		.setLayerCount(1)
		.setAspectMask(vk::ImageAspectFlagBits::eDepth | vk::ImageAspectFlagBits::eStencil);

	auto depth_stencil_view_create_info = vk::ImageViewCreateInfo()
		.setViewType(vk::ImageViewType::e2D)
		.setImage(*gDepthStencil.image)
		.setFormat(gDepthStencil.format)
		.setSubresourceRange(depth_stencil_view_subresource_range);

	gDepthStencil.view = gDevice.createImageView(depth_stencil_view_create_info);

	OneTimeSubmit(gDevice, gCommandPool, gQueue, [&](auto& cmdbuf) {
		SetImageLayout(cmdbuf, *gDepthStencil.image, gDepthStencil.format, vk::ImageLayout::eUndefined,
			vk::ImageLayout::eDepthStencilAttachmentOptimal);
	});
}

#endif
