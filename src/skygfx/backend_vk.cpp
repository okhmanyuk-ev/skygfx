#include "backend_vk.h"

#ifdef SKYGFX_HAS_VULKAN

#include <vulkan/vulkan_raii.hpp>

using namespace skygfx;

static vk::raii::Context gContext;
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
static vk::raii::Sampler gSampler = nullptr;
static bool gWorking = false;
static uint32_t gWidth = 0;
static uint32_t gHeight = 0;
static const uint32_t gMinImageCount = 2; // TODO: https://github.com/nvpro-samples/nvpro_core/blob/f2c05e161bba9ab9a8c96c0173bf0edf7c168dfa/nvvk/swapchain_vk.cpp#L143

struct FrameVK
{
	vk::raii::CommandBuffer command_buffer = nullptr;
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

static int gVertexBufferIndex = 0;
static int gIndexBufferIndex = 0;
static int gUniformBufferIndex = 0;

static uint32_t gSemaphoreIndex = 0;
static uint32_t gFrameIndex = 0;

struct DeviceBufferVK
{
	vk::raii::Buffer buffer = nullptr;
	vk::raii::DeviceMemory memory = nullptr;
	vk::DeviceSize size = 0;
};

static std::vector<DeviceBufferVK> gVertexBuffers;
static std::vector<DeviceBufferVK> gIndexBuffers;
static std::vector<DeviceBufferVK> gUniformBuffers;

static std::unordered_map<uint32_t, vk::ImageView> gTexturesPushQueue;
static std::unordered_map<uint32_t, vk::Buffer> gUniformBuffersPushQueue;

static std::optional<Scissor> gScissor;
static bool gScissorDirty = true;

static std::optional<Viewport> gViewport;
static bool gViewportDirty = true;

class ShaderDataVK;

static ShaderDataVK* gShader = nullptr;
static std::unordered_map<ShaderDataVK*, vk::raii::Pipeline> gPipelines;

static std::optional<DepthMode> gDepthMode = DepthMode();
static bool gDepthModeDirty = true;

static CullMode gCullMode = CullMode::None;
static bool gCullModeDirty = true;

static uint32_t GetMemoryType(vk::MemoryPropertyFlags properties, uint32_t type_bits)
{
	auto prop = gPhysicalDevice.getMemoryProperties();

	for (uint32_t i = 0; i < prop.memoryTypeCount; i++)
		if ((prop.memoryTypes[i].propertyFlags & properties) == properties && type_bits & (1 << i))
			return i;

	return 0xFFFFFFFF; // Unable to find memoryType
}

template <typename Func>
static void OneTimeSubmit(const vk::raii::CommandBuffer& cmd, const vk::raii::Queue& queue, const Func& func)
{
	cmd.begin(vk::CommandBufferBeginInfo(vk::CommandBufferUsageFlagBits::eOneTimeSubmit));
	func(cmd);
	cmd.end();
	vk::SubmitInfo submitInfo(nullptr, nullptr, *cmd);
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

	auto command_buffers = gDevice.allocateCommandBuffers(command_buffer_allocate_info);
	auto cmd = std::move(command_buffers.at(0));

	OneTimeSubmit(cmd, queue, func);
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

class ShaderDataVK
{
	friend class BackendVK;

private:
	vk::raii::DescriptorSetLayout descriptor_set_layout = nullptr;
	vk::raii::PipelineLayout pipeline_layout = nullptr;
	vk::raii::ShaderModule vertex_shader_module = nullptr;
	vk::raii::ShaderModule fragment_shader_module = nullptr;
	vk::VertexInputBindingDescription vertex_input_binding_description;
	std::vector<vk::VertexInputAttributeDescription> vertex_input_attribute_descriptions;

public:
	ShaderDataVK(const Vertex::Layout& layout, const std::string& vertex_code, const std::string& fragment_code,
		std::vector<std::string> defines)
	{
		AddShaderLocationDefines(layout, defines);

		auto vertex_shader_spirv = CompileGlslToSpirv(ShaderStage::Vertex, vertex_code, defines);
		auto fragment_shader_spirv = CompileGlslToSpirv(ShaderStage::Fragment, fragment_code, defines);

		auto vertex_shader_reflection = MakeSpirvReflection(vertex_shader_spirv);
		auto fragment_shader_reflection = MakeSpirvReflection(fragment_shader_spirv);

		static const std::unordered_map<ShaderStage, vk::ShaderStageFlagBits> StageMap = {
			{ ShaderStage::Vertex, vk::ShaderStageFlagBits::eVertex },
			{ ShaderStage::Fragment, vk::ShaderStageFlagBits::eFragment }
		};

		static const std::unordered_map<ShaderReflection::DescriptorSet::Type, vk::DescriptorType> TypeMap = {
			{ ShaderReflection::DescriptorSet::Type::CombinedImageSampler, vk::DescriptorType::eCombinedImageSampler },
			{ ShaderReflection::DescriptorSet::Type::UniformBuffer, vk::DescriptorType::eUniformBuffer }
		};
		
		std::vector<vk::DescriptorSetLayoutBinding> bindings;

		for (const auto& reflection : { vertex_shader_reflection, fragment_shader_reflection })
		{
			for (const auto& descriptor_set : reflection.descriptor_sets)
			{
				bool overwritten = false;

				for (auto& binding : bindings)
				{
					if (binding.binding == descriptor_set.binding)
					{
						binding.stageFlags |= StageMap.at(reflection.stage);
						overwritten = true;
						break;
					}
				}

				if (overwritten)
					continue;

				auto binding = vk::DescriptorSetLayoutBinding()
					.setDescriptorType(TypeMap.at(descriptor_set.type))
					.setDescriptorCount(1)
					.setBinding(descriptor_set.binding)
					.setStageFlags(StageMap.at(reflection.stage));

				bindings.push_back(binding);
			}
		}

		auto descriptor_set_layout_create_info = vk::DescriptorSetLayoutCreateInfo()
			.setFlags(vk::DescriptorSetLayoutCreateFlagBits::ePushDescriptorKHR)
			.setBindings(bindings);

		descriptor_set_layout = gDevice.createDescriptorSetLayout(descriptor_set_layout_create_info);

		auto pipeline_layout_create_info = vk::PipelineLayoutCreateInfo()
			.setSetLayoutCount(1)
			.setPSetLayouts(&*descriptor_set_layout);

		pipeline_layout = gDevice.createPipelineLayout(pipeline_layout_create_info);

		auto vertex_shader_module_create_info = vk::ShaderModuleCreateInfo()
			.setCode(vertex_shader_spirv);

		auto fragment_shader_module_create_info = vk::ShaderModuleCreateInfo()
			.setCode(fragment_shader_spirv);

		vertex_shader_module = gDevice.createShaderModule(vertex_shader_module_create_info);
		fragment_shader_module = gDevice.createShaderModule(fragment_shader_module_create_info);

		static const std::unordered_map<Vertex::Attribute::Format, vk::Format> Format = {
			{ Vertex::Attribute::Format::R32F, vk::Format::eR32Sfloat },
			{ Vertex::Attribute::Format::R32G32F, vk::Format::eR32G32Sfloat },
			{ Vertex::Attribute::Format::R32G32B32F, vk::Format::eR32G32B32Sfloat },
			{ Vertex::Attribute::Format::R32G32B32A32F, vk::Format::eR32G32B32A32Sfloat },
			{ Vertex::Attribute::Format::R8UN, vk::Format::eR8Unorm },
			{ Vertex::Attribute::Format::R8G8UN, vk::Format::eR8G8Unorm },
			{ Vertex::Attribute::Format::R8G8B8UN, vk::Format::eR8G8B8Unorm },
			{ Vertex::Attribute::Format::R8G8B8A8UN, vk::Format::eR8G8B8A8Unorm }
		};

		vertex_input_binding_description = vk::VertexInputBindingDescription()
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

			vertex_input_attribute_descriptions.push_back(vertex_input_attribute_description);
		}
	}
};

class TextureDataVK
{
	friend class BackendVK;

private:
	vk::raii::Image image = nullptr;
	vk::raii::ImageView image_view = nullptr;
	vk::raii::DeviceMemory memory = nullptr;

public:
	TextureDataVK(uint32_t width, uint32_t height, uint32_t channels, void* data, bool mipmap)
	{
		uint32_t mip_levels = 1;

		if (mipmap)
		{
			mip_levels = static_cast<uint32_t>(glm::floor(glm::log2(glm::max(width, height)))) + 1;
		}

		auto image_create_info = vk::ImageCreateInfo()
			.setImageType(vk::ImageType::e2D)
			.setFormat(vk::Format::eR8G8B8A8Unorm)
			.setExtent({ width, height, 1 })
			.setMipLevels(mip_levels)
			.setArrayLayers(1)
			.setSamples(vk::SampleCountFlagBits::e1)
			.setTiling(vk::ImageTiling::eOptimal)
			.setUsage(vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst 
				| vk::ImageUsageFlagBits::eTransferSrc)
			.setSharingMode(vk::SharingMode::eExclusive)
			.setInitialLayout(vk::ImageLayout::eUndefined);

		image = gDevice.createImage(image_create_info);

		auto memory_requirements = image.getMemoryRequirements();

		auto memory_allocate_info = vk::MemoryAllocateInfo()
			.setAllocationSize(memory_requirements.size)
			.setMemoryTypeIndex(GetMemoryType(vk::MemoryPropertyFlagBits::eDeviceLocal, 
				memory_requirements.memoryTypeBits));

		memory = gDevice.allocateMemory(memory_allocate_info);
	
		image.bindMemory(*memory, 0);

		auto image_subresource_range = vk::ImageSubresourceRange()
			.setAspectMask(vk::ImageAspectFlagBits::eColor)
			.setLevelCount(mip_levels)
			.setLayerCount(1);

		auto image_view_create_info = vk::ImageViewCreateInfo()
			.setImage(*image)
			.setViewType(vk::ImageViewType::e2D)
			.setFormat(vk::Format::eR8G8B8A8Unorm)
			.setSubresourceRange(image_subresource_range);

		image_view = gDevice.createImageView(image_view_create_info);

		if (data)
		{
			auto size = width * height * channels;

			auto buffer_create_info = vk::BufferCreateInfo()
				.setSize(size)
				.setUsage(vk::BufferUsageFlagBits::eTransferSrc)
				.setSharingMode(vk::SharingMode::eExclusive);

			auto upload_buffer = gDevice.createBuffer(buffer_create_info);

			auto req = upload_buffer.getMemoryRequirements();

			//bd->BufferMemoryAlignment = (bd->BufferMemoryAlignment > req.alignment) ? bd->BufferMemoryAlignment : req.alignment;

			auto memory_allocate_info = vk::MemoryAllocateInfo()
				.setAllocationSize(req.size)
				.setMemoryTypeIndex(GetMemoryType(vk::MemoryPropertyFlagBits::eHostVisible, req.memoryTypeBits));

			auto upload_buffer_memory = gDevice.allocateMemory(memory_allocate_info);

			upload_buffer.bindMemory(*upload_buffer_memory, 0);

			auto map = upload_buffer_memory.mapMemory(0, size);
			memcpy(map, data, size);
			upload_buffer_memory.unmapMemory();

			OneTimeSubmit(gDevice, gCommandPool, gQueue, [&](auto& cmd) {
				SetImageLayout(cmd, *image, vk::Format::eUndefined, vk::ImageLayout::eUndefined,
					vk::ImageLayout::eTransferDstOptimal);

				auto image_subresource_layers = vk::ImageSubresourceLayers()
					.setAspectMask(vk::ImageAspectFlagBits::eColor)
					.setLayerCount(1);

				auto region = vk::BufferImageCopy()
					.setImageSubresource(image_subresource_layers)
					.setImageExtent({ width, height, 1 });

				cmd.copyBufferToImage(*upload_buffer, *image, vk::ImageLayout::eTransferDstOptimal, { region });

				SetImageLayout(cmd, *image, vk::Format::eUndefined, vk::ImageLayout::eTransferDstOptimal,
					vk::ImageLayout::eTransferSrcOptimal);

				for (uint32_t i = 1; i < mip_levels; i++)
				{
					auto mip_subresource_range = vk::ImageSubresourceRange()
						.setAspectMask(vk::ImageAspectFlagBits::eColor)
						.setBaseMipLevel(i)
						.setLayerCount(1)
						.setLevelCount(1);

					SetImageLayout(cmd, *image, vk::Format::eUndefined, vk::ImageLayout::eUndefined,
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

					cmd.blitImage(*image, vk::ImageLayout::eTransferSrcOptimal, *image,
						vk::ImageLayout::eTransferDstOptimal, { mip_region }, vk::Filter::eLinear);

					SetImageLayout(cmd, *image, vk::Format::eUndefined, vk::ImageLayout::eTransferDstOptimal,
						vk::ImageLayout::eTransferSrcOptimal, mip_subresource_range);
				}

				auto subresource_range = vk::ImageSubresourceRange()
					.setAspectMask(vk::ImageAspectFlagBits::eColor)
					.setLayerCount(1)
					.setLevelCount(mip_levels);

				SetImageLayout(cmd, *image, vk::Format::eUndefined, vk::ImageLayout::eTransferSrcOptimal,
					vk::ImageLayout::eShaderReadOnlyOptimal, subresource_range);
			});
		}
	}
};

class RenderTargetDataVK
{
public:
	RenderTargetDataVK(uint32_t width, uint32_t height, TextureDataVK* _texture_data)
	{
	}

	~RenderTargetDataVK()
	{
	}
};

BackendVK::BackendVK(void* window, uint32_t width, uint32_t height)
{
	auto all_extensions = gContext.enumerateInstanceExtensionProperties();

	for (auto extension : all_extensions)
	{
		//	std::cout << extension.extensionName << std::endl;
	}

	auto all_layers = gContext.enumerateInstanceLayerProperties();

	for (auto layer : all_layers)
	{
		//	std::cout << layer.layerName << std::endl;
	}

	auto extensions = {
		VK_KHR_SURFACE_EXTENSION_NAME,
		VK_KHR_WIN32_SURFACE_EXTENSION_NAME
	};

	auto layers = {
		"VK_LAYER_KHRONOS_validation"
	};

	auto version = gContext.enumerateInstanceVersion();

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

	gInstance = gContext.createInstance(instance_info);

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
		VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME
	};

	auto queue_priority = { 1.0f };

	auto queue_info = vk::DeviceQueueCreateInfo()
		.setQueueFamilyIndex(gQueueFamilyIndex)
		.setQueuePriorities(queue_priority);

	auto device_features = gPhysicalDevice.getFeatures2<vk::PhysicalDeviceFeatures2,
		vk::PhysicalDeviceVulkan13Features>();

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

	auto surface_info = vk::Win32SurfaceCreateInfoKHR()
		.setHwnd((HWND)window);

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
		.setLevel(vk::CommandBufferLevel::eSecondary)
		.setCommandPool(*gCommandPool);

	auto command_buffers = gDevice.allocateCommandBuffers(command_buffer_allocate_info);
	gCommandBuffer = std::move(command_buffers.at(0));

	auto sampler_create_info = vk::SamplerCreateInfo()
		.setMagFilter(vk::Filter::eLinear)
		.setMinFilter(vk::Filter::eLinear)
		.setMipmapMode(vk::SamplerMipmapMode::eLinear)
		.setAddressModeU(vk::SamplerAddressMode::eRepeat)
		.setAddressModeV(vk::SamplerAddressMode::eRepeat)
		.setAddressModeW(vk::SamplerAddressMode::eRepeat)
		.setMinLod(-1000)
		.setMaxLod(1000)
		.setMaxAnisotropy(1.0f);

	gSampler = gDevice.createSampler(sampler_create_info);

	createSwapchain(width, height);

	begin();
}

BackendVK::~BackendVK()
{
	end();
}

void BackendVK::resize(uint32_t width, uint32_t height)
{
}

void BackendVK::setTopology(Topology topology)
{
	static const std::unordered_map<Topology, vk::PrimitiveTopology> TopologyMap = {
		{ Topology::PointList, vk::PrimitiveTopology::ePointList },
		{ Topology::LineList, vk::PrimitiveTopology::eLineList },
		{ Topology::LineStrip, vk::PrimitiveTopology::eLineStrip },
		{ Topology::TriangleList, vk::PrimitiveTopology::eTriangleList },
		{ Topology::TriangleStrip, vk::PrimitiveTopology::eTriangleStrip },
	};

	gCommandBuffer.setPrimitiveTopology(TopologyMap.at(topology));
}

void BackendVK::setViewport(std::optional<Viewport> viewport)
{
	gViewport = viewport;
	gViewportDirty = true;
}

void BackendVK::setScissor(std::optional<Scissor> scissor)
{
	gScissor = scissor;
	gScissorDirty = true;
}

void BackendVK::setTexture(TextureHandle* handle, uint32_t slot)
{
	auto texture = (TextureDataVK*)handle;
	gTexturesPushQueue[slot] = *texture->image_view;
}

void BackendVK::setRenderTarget(RenderTargetHandle* handle)
{
}

void BackendVK::setRenderTarget(std::nullptr_t value)
{
}

void BackendVK::setShader(ShaderHandle* handle)
{
	gShader = (ShaderDataVK*)handle;
}

void BackendVK::setVertexBuffer(const Buffer& value)
{
	assert(value.size > 0);

	auto create_buffer = [&] {
		DeviceBufferVK buffer;

		auto buffer_create_info = vk::BufferCreateInfo()
			.setSize(value.size)
			.setUsage(vk::BufferUsageFlagBits::eVertexBuffer)
			.setSharingMode(vk::SharingMode::eExclusive);

		buffer.buffer = gDevice.createBuffer(buffer_create_info);

		auto memory_requirements = buffer.buffer.getMemoryRequirements();

		auto memory_allocate_info = vk::MemoryAllocateInfo()
			.setAllocationSize(memory_requirements.size)
			.setMemoryTypeIndex(GetMemoryType(vk::MemoryPropertyFlagBits::eHostVisible, memory_requirements.memoryTypeBits));

		buffer.memory = gDevice.allocateMemory(memory_allocate_info);
		buffer.size = memory_requirements.size;

		buffer.buffer.bindMemory(*buffer.memory, 0);

		return buffer;
	};

	if (gVertexBuffers.size() < gVertexBufferIndex + 1)
	{
		assert(gVertexBuffers.size() == gVertexBufferIndex);
		gVertexBuffers.push_back(create_buffer());
	}

	auto& buffer = gVertexBuffers[gVertexBufferIndex];

	if (buffer.size < value.size)
	{
		buffer = create_buffer();
	}

	auto mem = buffer.memory.mapMemory(0, VK_WHOLE_SIZE);
	memcpy(mem, value.data, value.size);
	buffer.memory.unmapMemory();

	gCommandBuffer.bindVertexBuffers2(0, { *buffer.buffer }, { 0 }, nullptr, { value.stride });

	gVertexBufferIndex += 1;
}

void BackendVK::setIndexBuffer(const Buffer& value)
{
	assert(value.size > 0);

	auto create_buffer = [&] {
		DeviceBufferVK buffer;

		auto buffer_create_info = vk::BufferCreateInfo()
			.setSize(value.size)
			.setUsage(vk::BufferUsageFlagBits::eIndexBuffer)
			.setSharingMode(vk::SharingMode::eExclusive);

		buffer.buffer = gDevice.createBuffer(buffer_create_info);

		auto memory_requirements = buffer.buffer.getMemoryRequirements();

		auto memory_allocate_info = vk::MemoryAllocateInfo()
			.setAllocationSize(memory_requirements.size)
			.setMemoryTypeIndex(GetMemoryType(vk::MemoryPropertyFlagBits::eHostVisible, memory_requirements.memoryTypeBits));

		buffer.memory = gDevice.allocateMemory(memory_allocate_info);
		buffer.size = memory_requirements.size;

		buffer.buffer.bindMemory(*buffer.memory, 0);

		return buffer;
	};

	if (gIndexBuffers.size() < gIndexBufferIndex + 1)
	{
		assert(gIndexBuffers.size() == gIndexBufferIndex);
		gIndexBuffers.push_back(create_buffer());
	}

	auto& buffer = gIndexBuffers[gIndexBufferIndex];

	if (buffer.size < value.size)
	{
		buffer = create_buffer();
	}

	auto mem = buffer.memory.mapMemory(0, VK_WHOLE_SIZE);
	memcpy(mem, value.data, value.size);
	buffer.memory.unmapMemory();

	gCommandBuffer.bindIndexBuffer(*buffer.buffer, 0, value.stride == 2 ? vk::IndexType::eUint16 : vk::IndexType::eUint32);

	gIndexBufferIndex += 1;
}

void BackendVK::setUniformBuffer(uint32_t slot, void* memory, size_t size)
{
	assert(size > 0);

	auto create_buffer = [&] {
		DeviceBufferVK buffer;

		auto buffer_create_info = vk::BufferCreateInfo()
			.setSize(size)
			.setUsage(vk::BufferUsageFlagBits::eUniformBuffer)
			.setSharingMode(vk::SharingMode::eExclusive);

		buffer.buffer = gDevice.createBuffer(buffer_create_info);

		auto memory_requirements = buffer.buffer.getMemoryRequirements();

		auto memory_allocate_info = vk::MemoryAllocateInfo()
			.setAllocationSize(memory_requirements.size)
			.setMemoryTypeIndex(GetMemoryType(vk::MemoryPropertyFlagBits::eHostVisible, memory_requirements.memoryTypeBits));

		buffer.memory = gDevice.allocateMemory(memory_allocate_info);
		buffer.size = memory_requirements.size;

		buffer.buffer.bindMemory(*buffer.memory, 0);

		return buffer;
	};

	if (gUniformBuffers.size() < gUniformBufferIndex + 1)
	{
		assert(gUniformBuffers.size() == gUniformBufferIndex);
		gUniformBuffers.push_back(create_buffer());
	}

	auto& buffer = gUniformBuffers[gUniformBufferIndex];

	if (buffer.size < size)
	{
		buffer = create_buffer();
	}

	auto mem = buffer.memory.mapMemory(0, VK_WHOLE_SIZE);
	memcpy(mem, memory, size);
	buffer.memory.unmapMemory();

	gUniformBuffersPushQueue[slot] = *buffer.buffer;
	gUniformBufferIndex += 1;
}

void BackendVK::setBlendMode(const BlendMode& value)
{
}

void BackendVK::setDepthMode(std::optional<DepthMode> depth_mode)
{
	auto prev_depth_mode = gDepthMode;

	gDepthMode = depth_mode;

	if (prev_depth_mode != gDepthMode)
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

void BackendVK::setSampler(const Sampler& value)
{
}

void BackendVK::setTextureAddressMode(const TextureAddress& value)
{
}

void BackendVK::clear(const std::optional<glm::vec4>& color, const std::optional<float>& depth,
	const std::optional<uint8_t>& stencil)
{
		auto clear_rect = vk::ClearRect()
		.setBaseArrayLayer(0)
		.setLayerCount(1)
		.setRect({ { 0, 0 }, { gWidth, gHeight } });

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
	prepareForDrawing();
	gCommandBuffer.draw(vertex_count, 0, vertex_offset, 0);
}

void BackendVK::drawIndexed(uint32_t index_count, uint32_t index_offset)
{
	prepareForDrawing();
	gCommandBuffer.drawIndexed(index_count, 1, index_offset, 0, 0);
}

void BackendVK::readPixels(const glm::ivec2& pos, const glm::ivec2& size, TextureHandle* dst_texture_handle)
{
}

void BackendVK::present()
{
	end(); 

	const auto& image_acquired_semaphore = gFrames.at(gSemaphoreIndex).image_acquired_semaphore;

	auto [result, image_index] = gSwapchain.acquireNextImage(UINT64_MAX, *image_acquired_semaphore);

	gFrameIndex = image_index;

	const auto& frame = gFrames.at(gFrameIndex);

	auto wait_result = gDevice.waitForFences({ *frame.fence }, true, UINT64_MAX);

	gDevice.resetFences({ *frame.fence });

	auto begin_info = vk::CommandBufferBeginInfo()
		.setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit);

	const auto& cmd = frame.command_buffer;

	cmd.begin(begin_info);

	auto color_attachment = vk::RenderingAttachmentInfo()
		.setImageView(*frame.backbuffer_color_image_view)
		.setImageLayout(vk::ImageLayout::eAttachmentOptimal)
		.setLoadOp(vk::AttachmentLoadOp::eDontCare)
		.setStoreOp(vk::AttachmentStoreOp::eStore);

	auto depth_stencil_attachment = vk::RenderingAttachmentInfo()
		.setImageView(*gDepthStencil.view)
		.setImageLayout(vk::ImageLayout::eDepthAttachmentOptimal)
		.setLoadOp(vk::AttachmentLoadOp::eDontCare)
		.setStoreOp(vk::AttachmentStoreOp::eStore);

	auto rendering_info = vk::RenderingInfo()
		.setRenderArea({ { 0, 0 }, { gWidth, gHeight } })
		.setLayerCount(1)
		.setColorAttachmentCount(1)
		.setPColorAttachments(&color_attachment)
		.setPDepthAttachment(&depth_stencil_attachment)
		.setPStencilAttachment(&depth_stencil_attachment)
		.setFlags(vk::RenderingFlagBits::eContentsSecondaryCommandBuffers);

	cmd.beginRendering(rendering_info);
	cmd.executeCommands({ *gCommandBuffer });
	cmd.endRendering();
	cmd.end();

	const auto& render_complete_semaphore = gFrames.at(gSemaphoreIndex).render_complete_semaphore;

	vk::PipelineStageFlags wait_dst_stage_mask = vk::PipelineStageFlagBits::eColorAttachmentOutput;

	auto submit_info = vk::SubmitInfo()
		.setPWaitDstStageMask(&wait_dst_stage_mask)
		.setWaitSemaphoreCount(1)
		.setPWaitSemaphores(&*image_acquired_semaphore)
		.setCommandBufferCount(1)
		.setPCommandBuffers(&*frame.command_buffer)
		.setSignalSemaphoreCount(1)
		.setPSignalSemaphores(&*render_complete_semaphore);

	gQueue.submit({ submit_info }, *frame.fence); // TODO: can be called with no fence, check it out

	auto present_info = vk::PresentInfoKHR()
		.setWaitSemaphoreCount(1)
		.setPWaitSemaphores(&*render_complete_semaphore)
		.setSwapchainCount(1)
		.setPSwapchains(&*gSwapchain)
		.setPImageIndices(&gFrameIndex);

	auto present_result = gQueue.presentKHR(present_info);

	gQueue.waitIdle();

	gSemaphoreIndex = (gSemaphoreIndex + 1) % gFrames.size(); // TODO: maybe gFrameIndex can be used for both

	begin();
}

TextureHandle* BackendVK::createTexture(uint32_t width, uint32_t height, uint32_t channels, void* memory, bool mipmap)
{
	auto texture = new TextureDataVK(width, height, channels, memory, mipmap);
	return (TextureHandle*)texture;
}

void BackendVK::destroyTexture(TextureHandle* handle)
{
	auto texture = (TextureDataVK*)handle;
	delete texture;
}

RenderTargetHandle* BackendVK::createRenderTarget(uint32_t width, uint32_t height, TextureHandle* texture_handle)
{
	auto texture = (TextureDataVK*)texture_handle;
	auto render_target = new RenderTargetDataVK(width, height, texture);
	return (RenderTargetHandle*)render_target;
}

void BackendVK::destroyRenderTarget(RenderTargetHandle* handle)
{
	auto render_target = (RenderTargetDataVK*)handle;
	delete render_target;
}

ShaderHandle* BackendVK::createShader(const Vertex::Layout& layout, const std::string& vertex_code,
	const std::string& fragment_code, const std::vector<std::string>& defines)
{
	auto shader = new ShaderDataVK(layout, vertex_code, fragment_code, defines);
	return (ShaderHandle*)shader;
}

void BackendVK::destroyShader(ShaderHandle* handle)
{
	// TODO: remove pipeline connected with this shader

	auto shader = (ShaderDataVK*)handle;
	delete shader;
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

		auto buffer_allocate_info = vk::CommandBufferAllocateInfo()
			.setCommandBufferCount(1)
			.setLevel(vk::CommandBufferLevel::ePrimary)
			.setCommandPool(*gCommandPool);

		auto command_buffers = gDevice.allocateCommandBuffers(buffer_allocate_info);
		frame.command_buffer = std::move(command_buffers.at(0));

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

		OneTimeSubmit(gDevice, gCommandPool, gQueue, [&](auto& cmd) {
			SetImageLayout(cmd, backbuffer, gSurfaceFormat.format, vk::ImageLayout::eUndefined,
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

	OneTimeSubmit(gDevice, gCommandPool, gQueue, [&](auto& cmd) {
		SetImageLayout(cmd, *gDepthStencil.image, gDepthStencil.format, vk::ImageLayout::eUndefined,
			vk::ImageLayout::eDepthStencilAttachmentOptimal);
	});
}

void BackendVK::begin()
{
	assert(!gWorking);
	gWorking = true;

	gVertexBufferIndex = 0;
	gIndexBufferIndex = 0;
	gUniformBufferIndex = 0;

	gViewportDirty = true;
	gScissorDirty = true;

	auto inheritance_rendering_info = vk::CommandBufferInheritanceRenderingInfo()
		.setColorAttachmentCount(1)
		.setPColorAttachmentFormats(&gSurfaceFormat.format)
		.setDepthAttachmentFormat(gDepthStencil.format)
		.setStencilAttachmentFormat(gDepthStencil.format)
		.setRasterizationSamples(vk::SampleCountFlagBits::e1);

	auto inheritance_info = vk::CommandBufferInheritanceInfo()
		.setPNext(&inheritance_rendering_info);

	auto begin_info = vk::CommandBufferBeginInfo()
		.setFlags(vk::CommandBufferUsageFlagBits::eRenderPassContinue)
		.setPInheritanceInfo(&inheritance_info);

	gCommandBuffer.begin(begin_info);
}

void BackendVK::end()
{
	assert(gWorking);
	gWorking = false;

	gCommandBuffer.end();
}

void BackendVK::prepareForDrawing()
{
	assert(gShader);

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

	if (!gPipelines.contains(gShader))
	{
		auto pipeline_shader_stage_create_info = {
			vk::PipelineShaderStageCreateInfo()
				.setStage(vk::ShaderStageFlagBits::eVertex)
				.setModule(*gShader->vertex_shader_module)
				.setPName("main"),

			vk::PipelineShaderStageCreateInfo()
				.setStage(vk::ShaderStageFlagBits::eFragment)
				.setModule(*gShader->fragment_shader_module)
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

		auto pipeline_color_blent_attachment_state = vk::PipelineColorBlendAttachmentState()
			.setBlendEnable(true)
			.setSrcColorBlendFactor(vk::BlendFactor::eSrcAlpha)
			.setDstColorBlendFactor(vk::BlendFactor::eOneMinusSrcAlpha)
			.setColorBlendOp(vk::BlendOp::eAdd)
			.setSrcAlphaBlendFactor(vk::BlendFactor::eOne)
			.setDstAlphaBlendFactor(vk::BlendFactor::eOneMinusSrcAlpha)
			.setAlphaBlendOp(vk::BlendOp::eAdd)
			.setColorWriteMask(vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
				vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA);

		auto pipeline_color_blend_state_create_info = vk::PipelineColorBlendStateCreateInfo()
			.setAttachmentCount(1)
			.setPAttachments(&pipeline_color_blent_attachment_state);

		auto pipeline_vertex_input_state_create_info = vk::PipelineVertexInputStateCreateInfo()
			.setVertexBindingDescriptionCount(1)
			.setPVertexBindingDescriptions(&gShader->vertex_input_binding_description)
			.setVertexAttributeDescriptions(gShader->vertex_input_attribute_descriptions);

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

		auto pipeline_rendering_create_info = vk::PipelineRenderingCreateInfo()
			.setColorAttachmentCount(1)
			.setColorAttachmentFormats(gSurfaceFormat.format);

		auto graphics_pipeline_create_info = vk::GraphicsPipelineCreateInfo()
			.setLayout(*gShader->pipeline_layout)
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

		gPipelines.insert({ gShader, gDevice.createGraphicsPipeline(nullptr, graphics_pipeline_create_info) });
	}

	auto pipeline = *gPipelines.at(gShader);

	gCommandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline);

	auto pipeline_layout = *gShader->pipeline_layout;

	for (const auto& [slot, image_view] : gTexturesPushQueue)
	{
		auto descriptor_image_info = vk::DescriptorImageInfo()
			.setSampler(*gSampler)
			.setImageView(image_view)
			.setImageLayout(vk::ImageLayout::eShaderReadOnlyOptimal);

		auto write_descriptor_set = vk::WriteDescriptorSet()
			.setDescriptorCount(1)
			.setDstBinding(slot)
			.setDescriptorType(vk::DescriptorType::eCombinedImageSampler)
			.setPImageInfo(&descriptor_image_info);

		gCommandBuffer.pushDescriptorSetKHR(vk::PipelineBindPoint::eGraphics, pipeline_layout, 0, { write_descriptor_set });
	}

	gTexturesPushQueue.clear();

	for (const auto& [slot, buffer] : gUniformBuffersPushQueue)
	{
		auto descriptor_buffer_info = vk::DescriptorBufferInfo()
			.setBuffer(buffer)
			.setRange(VK_WHOLE_SIZE);

		auto write_descriptor_set = vk::WriteDescriptorSet()
			.setDescriptorCount(1)
			.setDstBinding(slot)
			.setDescriptorType(vk::DescriptorType::eUniformBuffer)
			.setPBufferInfo(&descriptor_buffer_info);

		gCommandBuffer.pushDescriptorSetKHR(vk::PipelineBindPoint::eGraphics, pipeline_layout, 0, { write_descriptor_set });
	}

	gUniformBuffersPushQueue.clear();

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
		// NOTE: bug with vkCmdSetCullMode have been fixed in NVIDIA Vulkan 1.3.212 developer driver 473.50 

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

#endif