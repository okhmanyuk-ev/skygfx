#include "backend_vk.h"

#ifdef SKYGFX_HAS_VULKAN

#include "shader_compiler.h"
#include <vulkan/vulkan_raii.hpp>
#include <iostream>

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
	vk::Format color_attachment_format;
	vk::Format depth_stencil_format;

	bool operator==(const PipelineStateVK& value) const
	{
		return 
			shader == value.shader &&
			color_attachment_format == value.color_attachment_format &&
			depth_stencil_format == value.depth_stencil_format;
	}
};

SKYGFX_MAKE_HASHABLE(PipelineStateVK,
	t.shader,
	t.color_attachment_format,
	t.depth_stencil_format);

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

struct ContextVK
{
	ContextVK() :
#if defined(SKYGFX_PLATFORM_WINDOWS)
		context(vk::raii::Context())
#elif defined(SKYGFX_PLATFORM_MACOS) | defined(SKYGFX_PLATFORM_IOS)
		context(vk::raii::Context(vkGetInstanceProcAddr));
#endif
	{
	}

	~ContextVK()
	{
		sampler_states.clear();
		raytracing_pipeline_states.clear();
		pipeline_states.clear();

		acceleration_structures.clear();
		uniform_buffers.clear();
		textures.clear();

		frames.clear();

		depth_stencil.image.release();
		depth_stencil.view.release();
		depth_stencil.memory.release();

		staging_objects.clear();

		execute_after_present.flush();

		command_buffer.release();
		command_pool.release();
		swapchain.release();
		surface.release();
		device.release();
		queue.release();
		physical_device.release();
		debug_utils_messenger.release();
		instance.release();
	}

	vk::raii::Context context;
	vk::raii::Instance instance = nullptr;
	vk::raii::DebugUtilsMessengerEXT debug_utils_messenger = nullptr;
	vk::raii::PhysicalDevice physical_device = nullptr;
	vk::raii::Queue queue = nullptr;
	vk::raii::Device device = nullptr;
	uint32_t queue_family_index = -1;
	vk::SurfaceFormatKHR surface_format;
	vk::raii::SurfaceKHR surface = nullptr;
	vk::raii::SwapchainKHR swapchain = nullptr;
	vk::raii::CommandPool command_pool = nullptr;
	vk::raii::CommandBuffer command_buffer = nullptr;

	bool working = false;

	uint32_t width = 0;
	uint32_t height = 0;

	ExecuteList execute_after_present;

	using StagingObject = std::variant<
		vk::raii::Buffer,
		vk::raii::DeviceMemory
	>;

	std::vector<StagingObject> staging_objects;

	struct Frame
	{
		vk::raii::Fence fence = nullptr;
		vk::raii::ImageView backbuffer_color_image_view = nullptr;
		vk::raii::Semaphore image_acquired_semaphore = nullptr;
		vk::raii::Semaphore render_complete_semaphore = nullptr;
	};

	struct
	{
		const vk::Format format = vk::Format::eD32SfloatS8Uint;

		vk::raii::Image image = nullptr;
		vk::raii::ImageView view = nullptr;
		vk::raii::DeviceMemory memory = nullptr;
	} depth_stencil;

	std::vector<Frame> frames;

	uint32_t semaphore_index = 0;
	uint32_t frame_index = 0;

	std::unordered_map<uint32_t, TextureVK*> textures;
	std::unordered_map<uint32_t, UniformBufferVK*> uniform_buffers;
	std::unordered_map<uint32_t, AccelerationStructureVK*> acceleration_structures;

	PipelineStateVK pipeline_state;
	std::unordered_map<PipelineStateVK, vk::raii::Pipeline> pipeline_states;

	RaytracingPipelineStateVK raytracing_pipeline_state;
	std::unordered_map<RaytracingPipelineStateVK, vk::raii::Pipeline> raytracing_pipeline_states;

	SamplerStateVK sampler_state;
	std::unordered_map<SamplerStateVK, vk::raii::Sampler> sampler_states;

	RenderTargetVK* render_target = nullptr;

	std::optional<Scissor> scissor;
	std::optional<Viewport> viewport;
	std::optional<DepthMode> depth_mode = DepthMode();
	CullMode cull_mode = CullMode::None;
	Topology topology = Topology::TriangleList;
	VertexBufferVK* vertex_buffer = nullptr;
	IndexBufferVK* index_buffer = nullptr;
	BlendMode blend_mode = BlendStates::AlphaBlend;

	bool scissor_dirty = true;
	bool viewport_dirty = true;
	bool depth_mode_dirty = true;
	bool cull_mode_dirty = true;
	bool topology_dirty = true;
	bool vertex_buffer_dirty = true;
	bool index_buffer_dirty = true;
	bool blend_mode_dirty = true;

	bool buffers_synchronized = false;
};

static ContextVK* gContext = nullptr;

static uint32_t GetMemoryType(vk::MemoryPropertyFlags properties, uint32_t type_bits)
{
	auto prop = gContext->physical_device.getMemoryProperties();

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
		.setUsage(
			vk::BufferUsageFlagBits::eTransferSrc |
			vk::BufferUsageFlagBits::eTransferDst |
			usage)
		.setSharingMode(vk::SharingMode::eExclusive);

	auto buffer = gContext->device.createBuffer(buffer_create_info);

	auto memory_requirements = buffer.getMemoryRequirements();

	auto memory_allocate_info = vk::MemoryAllocateInfo()
		.setAllocationSize(memory_requirements.size)
		.setMemoryTypeIndex(GetMemoryType(vk::MemoryPropertyFlagBits::eHostVisible, memory_requirements.memoryTypeBits));

	auto device_memory = gContext->device.allocateMemory(memory_allocate_info);

	buffer.bindMemory(*device_memory, 0);

	return { std::move(buffer), std::move(device_memory) };
}

static vk::DeviceAddress GetBufferDeviceAddress(vk::Buffer buffer)
{
	auto info = vk::BufferDeviceAddressInfo()
		.setBuffer(buffer);

	return gContext->device.getBufferAddress(info);
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

static const std::unordered_map<Format, vk::Format> FormatMap = {
	{ Format::Float1, vk::Format::eR32Sfloat },
	{ Format::Float2, vk::Format::eR32G32Sfloat },
	{ Format::Float3, vk::Format::eR32G32B32Sfloat },
	{ Format::Float4, vk::Format::eR32G32B32A32Sfloat },
	{ Format::Byte1, vk::Format::eR8Unorm },
	{ Format::Byte2, vk::Format::eR8G8Unorm },
	{ Format::Byte3, vk::Format::eR8G8B8Unorm },
	{ Format::Byte4, vk::Format::eR8G8B8A8Unorm }
};

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
	{ ShaderStage::Fragment, vk::ShaderStageFlagBits::eFragment },
	{ ShaderStage::Raygen, vk::ShaderStageFlagBits::eRaygenKHR },
	{ ShaderStage::Miss, vk::ShaderStageFlagBits::eMissKHR },
	{ ShaderStage::ClosestHit, vk::ShaderStageFlagBits::eClosestHitKHR }
};

const static std::unordered_map<ShaderReflection::Descriptor::Type, vk::DescriptorType> ShaderTypeMap = {
	{ ShaderReflection::Descriptor::Type::CombinedImageSampler, vk::DescriptorType::eCombinedImageSampler },
	{ ShaderReflection::Descriptor::Type::UniformBuffer, vk::DescriptorType::eUniformBuffer },
	{ ShaderReflection::Descriptor::Type::StorageImage, vk::DescriptorType::eStorageImage },
	{ ShaderReflection::Descriptor::Type::AccelerationStructure, vk::DescriptorType::eAccelerationStructureKHR }
};

std::tuple<vk::raii::PipelineLayout, vk::raii::DescriptorSetLayout, std::vector<vk::DescriptorSetLayoutBinding>> CreatePipelineLayout(
	const std::vector<std::vector<uint32_t>>& spirvs)
{
	std::vector<vk::DescriptorSetLayoutBinding> required_descriptor_bindings;

	for (const auto& spirv : spirvs)
	{
		auto reflection = MakeSpirvReflection(spirv);

		for (const auto& [binding, descriptor] : reflection.descriptor_bindings)
		{
			bool overwritten = false;

			for (auto& _binding : required_descriptor_bindings)
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

			required_descriptor_bindings.push_back(descriptor_set_layout_binding);
		}
	}

	auto descriptor_set_layout_create_info = vk::DescriptorSetLayoutCreateInfo()
		.setFlags(vk::DescriptorSetLayoutCreateFlagBits::ePushDescriptorKHR)
		.setBindings(required_descriptor_bindings);

	auto descriptor_set_layout = gContext->device.createDescriptorSetLayout(descriptor_set_layout_create_info);

	auto pipeline_layout_create_info = vk::PipelineLayoutCreateInfo()
		.setSetLayoutCount(1)
		.setPSetLayouts(&*descriptor_set_layout);

	auto pipeline_layout = gContext->device.createPipelineLayout(pipeline_layout_create_info);

	return { std::move(pipeline_layout), std::move(descriptor_set_layout), required_descriptor_bindings };
}

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
	ShaderVK(const VertexLayout& vertex_layout, const std::string& vertex_code, const std::string& fragment_code,
		std::vector<std::string> defines)
	{
		AddShaderLocationDefines(vertex_layout, defines);

		auto vertex_shader_spirv = CompileGlslToSpirv(ShaderStage::Vertex, vertex_code, defines);
		auto fragment_shader_spirv = CompileGlslToSpirv(ShaderStage::Fragment, fragment_code, defines);

		std::tie(mPipelineLayout, mDescriptorSetLayout, mRequiredDescriptorBindings) = CreatePipelineLayout({ 
			vertex_shader_spirv, fragment_shader_spirv });

		auto vertex_shader_module_create_info = vk::ShaderModuleCreateInfo()
			.setCode(vertex_shader_spirv);

		auto fragment_shader_module_create_info = vk::ShaderModuleCreateInfo()
			.setCode(fragment_shader_spirv);

		mVertexShaderModule = gContext->device.createShaderModule(vertex_shader_module_create_info);
		mFragmentShaderModule = gContext->device.createShaderModule(fragment_shader_module_create_info);

		mVertexInputBindingDescription = vk::VertexInputBindingDescription()
			.setStride(static_cast<uint32_t>(vertex_layout.stride))
			.setInputRate(vk::VertexInputRate::eVertex)
			.setBinding(0);

		for (int i = 0; i < vertex_layout.attributes.size(); i++)
		{
			const auto& attrib = vertex_layout.attributes.at(i);

			auto vertex_input_attribute_description = vk::VertexInputAttributeDescription()
				.setBinding(0)
				.setLocation(i)
				.setFormat(FormatMap.at(attrib.format))
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
	const auto& getPipelineLayout() const { return mPipelineLayout; }
	const auto& getRequiredDescriptorBindings() const { return mRequiredDescriptorBindings; }

private:
	vk::raii::ShaderModule mRaygenShaderModule = nullptr;
	vk::raii::ShaderModule mMissShaderModule = nullptr;
	vk::raii::ShaderModule mClosestHitShaderModule = nullptr;
	vk::raii::DescriptorSetLayout mDescriptorSetLayout = nullptr;
	vk::raii::PipelineLayout mPipelineLayout = nullptr;
	std::vector<vk::DescriptorSetLayoutBinding> mRequiredDescriptorBindings;

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

		mRaygenShaderModule = gContext->device.createShaderModule(raygen_shader_module_create_info);
		mMissShaderModule = gContext->device.createShaderModule(miss_shader_module_create_info);
		mClosestHitShaderModule = gContext->device.createShaderModule(closesthit_shader_module_create_info);

		std::tie(mPipelineLayout, mDescriptorSetLayout, mRequiredDescriptorBindings) = CreatePipelineLayout({
			raygen_shader_spirv, miss_shader_spirv, closesthit_shader_spirv });
	}
};

class TextureVK
{
public:
	const auto& getImage() const { return mImage; }
	const auto& getImageView() const { return mImageView; }
	const auto& getDeviceMemory() const { return mDeviceMemory; }
	auto getFormat() const { return mFormat; }
	auto getWidth() const { return mWidth; }
	auto getHeight() const { return mHeight; }

private:
	vk::raii::Image mImage = nullptr;
	vk::raii::ImageView mImageView = nullptr;
	vk::raii::DeviceMemory mDeviceMemory = nullptr;
	uint32_t mWidth = 0;
	uint32_t mHeight = 0;
	vk::Format mFormat;

public:
	TextureVK(uint32_t width, uint32_t height, Format format, void* memory, bool mipmap) :
		mWidth(width),
		mHeight(height),
		mFormat(FormatMap.at(format))
	{
		uint32_t mip_levels = 1;

		if (mipmap && memory)
		{
			mip_levels = static_cast<uint32_t>(glm::floor(glm::log2(glm::max(width, height)))) + 1;
		}

		auto image_create_info = vk::ImageCreateInfo()
			.setImageType(vk::ImageType::e2D)
			.setFormat(mFormat)
			.setExtent({ width, height, 1 })
			.setMipLevels(mip_levels)
			.setArrayLayers(1)
			.setSamples(vk::SampleCountFlagBits::e1)
			.setTiling(vk::ImageTiling::eOptimal)
			.setUsage(
				vk::ImageUsageFlagBits::eSampled |
				vk::ImageUsageFlagBits::eTransferDst |
				vk::ImageUsageFlagBits::eTransferSrc |
				vk::ImageUsageFlagBits::eColorAttachment |
				vk::ImageUsageFlagBits::eStorage
			)
			.setSharingMode(vk::SharingMode::eExclusive)
			.setInitialLayout(vk::ImageLayout::eUndefined);

		mImage = gContext->device.createImage(image_create_info);

		auto memory_requirements = mImage.getMemoryRequirements();

		auto memory_allocate_info = vk::MemoryAllocateInfo()
			.setAllocationSize(memory_requirements.size)
			.setMemoryTypeIndex(GetMemoryType(vk::MemoryPropertyFlagBits::eDeviceLocal, 
				memory_requirements.memoryTypeBits));

		mDeviceMemory = gContext->device.allocateMemory(memory_allocate_info);
	
		mImage.bindMemory(*mDeviceMemory, 0);

		auto image_subresource_range = vk::ImageSubresourceRange()
			.setAspectMask(vk::ImageAspectFlagBits::eColor)
			.setLevelCount(mip_levels)
			.setLayerCount(1);

		auto image_view_create_info = vk::ImageViewCreateInfo()
			.setImage(*mImage)
			.setViewType(vk::ImageViewType::e2D)
			.setFormat(mFormat)
			.setSubresourceRange(image_subresource_range);

		mImageView = gContext->device.createImageView(image_view_create_info);

		OneTimeSubmit(gContext->device, gContext->command_pool, gContext->queue, [&](auto& cmdbuf) {
			SetImageLayout(cmdbuf, *mImage, vk::Format::eUndefined, vk::ImageLayout::eUndefined,
				vk::ImageLayout::eGeneral);
		});

		if (memory)
		{
			auto channels = GetFormatChannelsCount(format);
			auto channel_size = GetFormatChannelSize(format);

			auto size = width * height * channels * channel_size;

			auto [upload_buffer, upload_buffer_memory] = CreateBuffer(size);

			WriteToBuffer(upload_buffer_memory, memory, size);

			OneTimeSubmit(gContext->device, gContext->command_pool, gContext->queue, [&](auto& cmdbuf) {
				SetImageLayout(cmdbuf, *mImage, vk::Format::eUndefined, vk::ImageLayout::eGeneral,
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
					vk::ImageLayout::eGeneral, subresource_range);
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
		auto depth_stencil_image_create_info = vk::ImageCreateInfo()
			.setImageType(vk::ImageType::e2D)
			.setFormat(mDepthStencilFormat)
			.setExtent({ width, height, 1 })
			.setMipLevels(1)
			.setArrayLayers(1)
			.setSamples(vk::SampleCountFlagBits::e1)
			.setTiling(vk::ImageTiling::eOptimal)
			.setUsage(vk::ImageUsageFlagBits::eDepthStencilAttachment);

		mDepthStencilImage = gContext->device.createImage(depth_stencil_image_create_info);

		auto depth_stencil_mem_req = mDepthStencilImage.getMemoryRequirements();

		auto depth_stencil_memory_allocate_info = vk::MemoryAllocateInfo()
			.setAllocationSize(depth_stencil_mem_req.size)
			.setMemoryTypeIndex(GetMemoryType(vk::MemoryPropertyFlagBits::eDeviceLocal, depth_stencil_mem_req.memoryTypeBits));

		mDepthStencilMemory = gContext->device.allocateMemory(depth_stencil_memory_allocate_info);

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

		mDepthStencilView = gContext->device.createImageView(depth_stencil_view_create_info);

		OneTimeSubmit(gContext->device, gContext->command_pool, gContext->queue, [&](auto& cmdbuf) {
			SetImageLayout(cmdbuf, *mDepthStencilImage, mDepthStencilFormat, vk::ImageLayout::eUndefined,
				vk::ImageLayout::eDepthStencilAttachmentOptimal);
		});
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

		if (gContext->buffers_synchronized)
		{
			auto memory_barrier = vk::MemoryBarrier2()
				.setSrcStageMask(vk::PipelineStageFlagBits2::eAllGraphics)
				.setSrcAccessMask(vk::AccessFlagBits2::eMemoryRead | vk::AccessFlagBits2::eMemoryWrite)
				.setDstStageMask(vk::PipelineStageFlagBits2::eTransfer)
				.setDstAccessMask(vk::AccessFlagBits2::eMemoryWrite);

			auto dependency_info = vk::DependencyInfo()
				.setMemoryBarrierCount(1)
				.setPMemoryBarriers(&memory_barrier);

			gContext->command_buffer.pipelineBarrier2(dependency_info);

			gContext->buffers_synchronized = false;
		}

		gContext->command_buffer.copyBuffer(*staging_buffer, *mBuffer, { region });

		gContext->staging_objects.push_back(std::move(staging_buffer));
		gContext->staging_objects.push_back(std::move(staging_buffer_memory));
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
	VertexBufferVK(size_t size, size_t stride) : BufferVK(size, vk::BufferUsageFlagBits::eVertexBuffer),
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
	IndexBufferVK(size_t size, size_t stride) : BufferVK(size, vk::BufferUsageFlagBits::eIndexBuffer),
		mStride(stride)
	{
	}
};

class UniformBufferVK : public BufferVK
{
public:
	UniformBufferVK(size_t size) : BufferVK(size, vk::BufferUsageFlagBits::eUniformBuffer)
	{
	}
};

static vk::IndexType GetIndexTypeFromStride(size_t stride)
{
	return stride == 2 ? vk::IndexType::eUint16 : vk::IndexType::eUint32;
}

static std::tuple<vk::raii::AccelerationStructureKHR, vk::DeviceAddress, vk::raii::Buffer, vk::raii::DeviceMemory> CreateBottomLevelAccelerationStrucutre(
	const std::vector<glm::vec3>& vertices, const std::vector<uint32_t>& indices, const glm::mat4& transform)
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

	auto blas_build_sizes = gContext->device.getAccelerationStructureBuildSizesKHR(
		vk::AccelerationStructureBuildTypeKHR::eDevice, blas_build_geometry_info, { 1 });
		
	auto [blas_buffer, blas_memory] = CreateBuffer(blas_build_sizes.accelerationStructureSize,
		vk::BufferUsageFlagBits::eAccelerationStructureStorageKHR);

	auto blas_create_info = vk::AccelerationStructureCreateInfoKHR()
		.setBuffer(*blas_buffer)
		.setType(vk::AccelerationStructureTypeKHR::eBottomLevel)
		.setSize(blas_build_sizes.accelerationStructureSize);

	auto blas = gContext->device.createAccelerationStructureKHR(blas_create_info);

	auto [blas_scratch_buffer, blas_scratch_memory] = CreateBuffer(blas_build_sizes.buildScratchSize,
		vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eShaderDeviceAddress);

	auto blas_scratch_buffer_addr = GetBufferDeviceAddress(*blas_scratch_buffer);

	blas_build_geometry_info
		.setMode(vk::BuildAccelerationStructureModeKHR::eBuild)
		.setDstAccelerationStructure(*blas)
		.setScratchData(blas_scratch_buffer_addr);

	auto blas_build_range_info = vk::AccelerationStructureBuildRangeInfoKHR()
		.setPrimitiveCount(static_cast<uint32_t>(indices.size() / 3));

	auto blas_build_geometry_infos = { blas_build_geometry_info };
	std::vector blas_build_range_infos = { &blas_build_range_info };
		
	OneTimeSubmit(gContext->device, gContext->command_pool, gContext->queue, [&](auto& cmdbuf) {
		cmdbuf.buildAccelerationStructuresKHR(blas_build_geometry_infos, blas_build_range_infos);
	});

	auto blas_device_address_info = vk::AccelerationStructureDeviceAddressInfoKHR()
		.setAccelerationStructure(*blas);

	auto blas_device_address = gContext->device.getAccelerationStructureAddressKHR(blas_device_address_info);

	return { std::move(blas), blas_device_address, std::move(blas_buffer), std::move(blas_memory) };
}

static std::tuple<vk::raii::AccelerationStructureKHR, vk::raii::Buffer, vk::raii::DeviceMemory> CreateTopLevelAccelerationStructure(
	const glm::mat4& transform, const vk::DeviceAddress& bottom_level_acceleration_structure_device_address)
{
	auto tlas_instance = vk::AccelerationStructureInstanceKHR()
		.setTransform(*(vk::TransformMatrixKHR*)&transform)
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

	auto tlas_build_sizes = gContext->device.getAccelerationStructureBuildSizesKHR(
		vk::AccelerationStructureBuildTypeKHR::eDevice, tlas_build_geometry_info, { 1 });

	auto [tlas_buffer, tlas_memory] = CreateBuffer(tlas_build_sizes.accelerationStructureSize,
		vk::BufferUsageFlagBits::eAccelerationStructureStorageKHR);

	auto tlas_create_info = vk::AccelerationStructureCreateInfoKHR()
		.setBuffer(*tlas_buffer)
		.setType(vk::AccelerationStructureTypeKHR::eTopLevel)
		.setSize(tlas_build_sizes.accelerationStructureSize);

	auto tlas = gContext->device.createAccelerationStructureKHR(tlas_create_info);

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

	OneTimeSubmit(gContext->device, gContext->command_pool, gContext->queue, [&](auto& cmdbuf) {
		cmdbuf.buildAccelerationStructuresKHR(tlas_build_geometry_infos, tlas_build_range_infos);
	});

	return { std::move(tlas), std::move(tlas_buffer), std::move(tlas_memory) };
}

class AccelerationStructureVK
{
public:
	const auto& getTlas() const { return mTlas; }

private:
	vk::raii::AccelerationStructureKHR mTlas = nullptr;
	vk::raii::Buffer mTlasBuffer = nullptr;
	vk::raii::DeviceMemory mTlasMemory = nullptr;
	vk::raii::AccelerationStructureKHR mBlas = nullptr;
	vk::DeviceAddress mBlasDeviceAddress;
	vk::raii::Buffer mBlasBuffer = nullptr;
	vk::raii::DeviceMemory mBlasMemory = nullptr;

public:
	AccelerationStructureVK(const std::vector<glm::vec3>& vertices, const std::vector<uint32_t>& indices,
		const glm::mat4& transform)
	{
		std::tie(mBlas, mBlasDeviceAddress, mBlasBuffer, mBlasMemory) = 
			CreateBottomLevelAccelerationStrucutre(vertices, indices, transform);
		std::tie(mTlas, mTlasBuffer, mTlasMemory) = CreateTopLevelAccelerationStructure(
			glm::mat4(1.0f), mBlasDeviceAddress);
	}
};

static bool gRenderPassActive = false;

static void BeginRenderPass()
{
	assert(!gRenderPassActive);
	gRenderPassActive = true;

	auto color_texture = gContext->render_target ?
		*gContext->render_target->getTexture()->getImageView() :
		*gContext->frames.at(gContext->frame_index).backbuffer_color_image_view;

	auto depth_stencil_texture = gContext->render_target ?
		*gContext->render_target->getDepthStencilView() :
		*gContext->depth_stencil.view;

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

	auto width = gContext->render_target ? gContext->render_target->getTexture()->getWidth() : gContext->width;
	auto height = gContext->render_target ? gContext->render_target->getTexture()->getHeight() : gContext->height;

	auto rendering_info = vk::RenderingInfo()
		.setRenderArea({ { 0, 0 }, { width, height } })
		.setLayerCount(1)
		.setColorAttachmentCount(1)
		.setPColorAttachments(&color_attachment)
		.setPDepthAttachment(&depth_stencil_attachment)
		.setPStencilAttachment(&depth_stencil_attachment);

	gContext->command_buffer.beginRendering(rendering_info);
}

static void EndRenderPass()
{
	assert(gRenderPassActive);
	gRenderPassActive = false;

	gContext->command_buffer.endRendering();
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
	assert(gContext->vertex_buffer);
	
	if (gContext->vertex_buffer_dirty)
	{
		gContext->command_buffer.bindVertexBuffers2(0, { *gContext->vertex_buffer->getBuffer() }, { 0 }, nullptr,
			{ gContext->vertex_buffer->getStride() });
		gContext->vertex_buffer_dirty = false;
	}

	if (gContext->index_buffer_dirty)
	{
		gContext->command_buffer.bindIndexBuffer(*gContext->index_buffer->getBuffer(), 0,
			GetIndexTypeFromStride(gContext->index_buffer->getStride()));
		gContext->index_buffer_dirty = false;
	}

	if (gContext->topology_dirty)
	{
		static const std::unordered_map<Topology, vk::PrimitiveTopology> TopologyMap = {
			{ Topology::PointList, vk::PrimitiveTopology::ePointList },
			{ Topology::LineList, vk::PrimitiveTopology::eLineList },
			{ Topology::LineStrip, vk::PrimitiveTopology::eLineStrip },
			{ Topology::TriangleList, vk::PrimitiveTopology::eTriangleList },
			{ Topology::TriangleStrip, vk::PrimitiveTopology::eTriangleStrip },
		};

		gContext->command_buffer.setPrimitiveTopology(TopologyMap.at(gContext->topology));
		gContext->topology_dirty = false;
	}

	auto shader = gContext->pipeline_state.shader;
	assert(shader);

	if (!gContext->pipeline_states.contains(gContext->pipeline_state))
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

		auto pipeline_color_blend_state_create_info = vk::PipelineColorBlendStateCreateInfo();

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
			vk::DynamicState::eDepthTestEnable,
			vk::DynamicState::eDepthCompareOp,
			vk::DynamicState::eDepthWriteEnable,
			vk::DynamicState::eColorWriteMaskEXT,
			vk::DynamicState::eColorBlendEquationEXT,
			vk::DynamicState::eColorBlendEnableEXT
		};

		auto pipeline_dynamic_state_create_info = vk::PipelineDynamicStateCreateInfo()
			.setDynamicStates(dynamic_states);

		auto color_attachment_formats = {
			gContext->pipeline_state.color_attachment_format
		};

		auto depth_stencil_format = gContext->pipeline_state.depth_stencil_format;

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

		auto pipeline = gContext->device.createGraphicsPipeline(nullptr, graphics_pipeline_create_info);

		gContext->pipeline_states.insert({ gContext->pipeline_state, std::move(pipeline) });
	}

	const auto& pipeline = gContext->pipeline_states.at(gContext->pipeline_state);

	gContext->command_buffer.bindPipeline(vk::PipelineBindPoint::eGraphics, *pipeline);

	if (!gContext->sampler_states.contains(gContext->sampler_state))
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
			.setMagFilter(FilterMap.at(gContext->sampler_state.sampler))
			.setMinFilter(FilterMap.at(gContext->sampler_state.sampler))
			.setMipmapMode(vk::SamplerMipmapMode::eLinear)
			.setAddressModeU(AddressModeMap.at(gContext->sampler_state.texture_address))
			.setAddressModeV(AddressModeMap.at(gContext->sampler_state.texture_address))
			.setAddressModeW(AddressModeMap.at(gContext->sampler_state.texture_address))
			.setMinLod(-1000)
			.setMaxLod(1000)
			.setMaxAnisotropy(1.0f);

		gContext->sampler_states.insert({ gContext->sampler_state, gContext->device.createSampler(sampler_create_info) });
	}

	const auto& sampler = gContext->sampler_states.at(gContext->sampler_state);

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
			auto texture = gContext->textures.at(binding);

			auto descriptor_image_info = vk::DescriptorImageInfo()
				.setSampler(*sampler)
				.setImageView(*texture->getImageView())
				.setImageLayout(vk::ImageLayout::eGeneral);

			write_descriptor_set.setPImageInfo(&descriptor_image_info);
		}
		else if (required_descriptor_binding.descriptorType == vk::DescriptorType::eUniformBuffer)
		{
			auto buffer = gContext->uniform_buffers.at(binding);

			auto descriptor_buffer_info = vk::DescriptorBufferInfo()
				.setBuffer(*buffer->getBuffer())
				.setRange(VK_WHOLE_SIZE);

			write_descriptor_set.setPBufferInfo(&descriptor_buffer_info);
		}
		else
		{
			assert(false);
		}

		gContext->command_buffer.pushDescriptorSetKHR(vk::PipelineBindPoint::eGraphics, pipeline_layout, 0, { write_descriptor_set });
	}

	auto width = gContext->render_target ? gContext->render_target->getTexture()->getWidth() : gContext->width;
	auto height = gContext->render_target ? gContext->render_target->getTexture()->getHeight() : gContext->height;

	if (gContext->viewport_dirty)
	{
		auto value = gContext->viewport.value_or(Viewport{ { 0.0f, 0.0f }, { static_cast<float>(width), static_cast<float>(height) } });

		auto viewport = vk::Viewport()
			.setX(value.position.x)
			.setY(value.size.y - value.position.y)
			.setWidth(value.size.x)
			.setHeight(-value.size.y)
			.setMinDepth(value.min_depth)
			.setMaxDepth(value.max_depth);

		gContext->command_buffer.setViewport(0, { viewport });
		gContext->viewport_dirty = false;
	}

	if (gContext->scissor_dirty)
	{
		auto value = gContext->scissor.value_or(Scissor{ { 0.0f, 0.0f }, { static_cast<float>(width), static_cast<float>(height) } });

		auto rect = vk::Rect2D()
			.setOffset({ static_cast<int32_t>(value.position.x), static_cast<int32_t>(value.position.y) })
			.setExtent({ static_cast<uint32_t>(value.size.x), static_cast<uint32_t>(value.size.y) });

		if (rect.offset.x < 0)
		{
			rect.extent.width -= rect.offset.x;
			rect.offset.x = 0;
		}

		if (rect.offset.y < 0)
		{
			rect.extent.height -= rect.offset.y;
			rect.offset.y = 0;
		}

		if (rect.extent.width < 0)
			rect.extent.width = 0;

		if (rect.extent.height < 0)
			rect.extent.height = 0;

		gContext->command_buffer.setScissor(0, { rect });
		gContext->scissor_dirty = false;
	}

	if (gContext->cull_mode_dirty)
	{
		const static std::unordered_map<CullMode, vk::CullModeFlags> CullModeMap = {
			{ CullMode::None, vk::CullModeFlagBits::eNone },
			{ CullMode::Front, vk::CullModeFlagBits::eFront },
			{ CullMode::Back, vk::CullModeFlagBits::eBack },
		};

		gContext->command_buffer.setFrontFace(vk::FrontFace::eClockwise);
		gContext->command_buffer.setCullMode(CullModeMap.at(gContext->cull_mode));

		gContext->cull_mode_dirty = false;
	}

	if (gContext->blend_mode_dirty)
	{
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

		if (gContext->blend_mode.color_mask.red)
			color_mask |= vk::ColorComponentFlagBits::eR;

		if (gContext->blend_mode.color_mask.green)
			color_mask |= vk::ColorComponentFlagBits::eG;

		if (gContext->blend_mode.color_mask.blue)
			color_mask |= vk::ColorComponentFlagBits::eB;

		if (gContext->blend_mode.color_mask.alpha)
			color_mask |= vk::ColorComponentFlagBits::eA;

		auto color_blend_equation = vk::ColorBlendEquationEXT()
			.setSrcColorBlendFactor(BlendFactorMap.at(gContext->blend_mode.color_src_blend))
			.setDstColorBlendFactor(BlendFactorMap.at(gContext->blend_mode.color_dst_blend))
			.setColorBlendOp(BlendFuncMap.at(gContext->blend_mode.color_blend_func))
			.setSrcAlphaBlendFactor(BlendFactorMap.at(gContext->blend_mode.alpha_src_blend))
			.setDstAlphaBlendFactor(BlendFactorMap.at(gContext->blend_mode.alpha_dst_blend))
			.setAlphaBlendOp(BlendFuncMap.at(gContext->blend_mode.alpha_blend_func));

		gContext->command_buffer.setColorBlendEnableEXT(0, { true });
		gContext->command_buffer.setColorBlendEquationEXT(0, { color_blend_equation });
		gContext->command_buffer.setColorWriteMaskEXT(0, { color_mask });

		gContext->blend_mode_dirty = false;
	}

	if (gContext->depth_mode_dirty)
	{
		if (gContext->depth_mode.has_value())
		{
			gContext->command_buffer.setDepthTestEnable(true);
			gContext->command_buffer.setDepthWriteEnable(true);
			gContext->command_buffer.setDepthCompareOp(CompareOpMap.at(gContext->depth_mode.value().func));
		}
		else
		{
			gContext->command_buffer.setDepthTestEnable(false);
			gContext->command_buffer.setDepthWriteEnable(false);
		}

		gContext->depth_mode_dirty = false;
	}

	if (!gContext->buffers_synchronized)
	{
		auto memory_barrier = vk::MemoryBarrier2()
			.setSrcStageMask(vk::PipelineStageFlagBits2::eTransfer)
			.setSrcAccessMask(vk::AccessFlagBits2::eMemoryWrite)
			.setDstStageMask(vk::PipelineStageFlagBits2::eAllGraphics)
			.setDstAccessMask(vk::AccessFlagBits2::eMemoryRead | vk::AccessFlagBits2::eMemoryWrite);

		auto dependency_info = vk::DependencyInfo()
			.setMemoryBarrierCount(1)
			.setPMemoryBarriers(&memory_barrier);

		gContext->command_buffer.pipelineBarrier2(dependency_info);

		gContext->buffers_synchronized = true;
	}
}

VKAPI_ATTR VkBool32 VKAPI_CALL DebugUtilsMessengerCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
	VkDebugUtilsMessageTypeFlagsEXT messageTypes, VkDebugUtilsMessengerCallbackDataEXT const* pCallbackData,
	void* /*pUserData*/)
{
#if !defined( NDEBUG )
	if (pCallbackData->messageIdNumber == 648835635)
	{
		// UNASSIGNED-khronos-Validation-debug-build-warning-message
		return VK_FALSE;
	}
	if (pCallbackData->messageIdNumber == 767975156)
	{
		// UNASSIGNED-BestPractices-vkCreateInstance-specialuse-extension
		return VK_FALSE;
	}
#endif

	std::cerr << vk::to_string(static_cast<vk::DebugUtilsMessageSeverityFlagBitsEXT>(messageSeverity)) << ": "
		<< vk::to_string(static_cast<vk::DebugUtilsMessageTypeFlagsEXT>(messageTypes)) << ":\n";
	std::cerr << std::string("\t") << "messageIDName   = <" << pCallbackData->pMessageIdName << ">\n";
	std::cerr << std::string("\t") << "messageIdNumber = " << pCallbackData->messageIdNumber << "\n";
	std::cerr << std::string("\t") << "message         = <" << pCallbackData->pMessage << ">\n";
	if (0 < pCallbackData->queueLabelCount)
	{
		std::cerr << std::string("\t") << "Queue Labels:\n";
		for (uint32_t i = 0; i < pCallbackData->queueLabelCount; i++)
		{
			std::cerr << std::string("\t\t") << "labelName = <" << pCallbackData->pQueueLabels[i].pLabelName << ">\n";
		}
	}
	if (0 < pCallbackData->cmdBufLabelCount)
	{
		std::cerr << std::string("\t") << "CommandBuffer Labels:\n";
		for (uint32_t i = 0; i < pCallbackData->cmdBufLabelCount; i++)
		{
			std::cerr << std::string("\t\t") << "labelName = <" << pCallbackData->pCmdBufLabels[i].pLabelName << ">\n";
		}
	}
	if (0 < pCallbackData->objectCount)
	{
		std::cerr << std::string("\t") << "Objects:\n";
		for (uint32_t i = 0; i < pCallbackData->objectCount; i++)
		{
			std::cerr << std::string("\t\t") << "Object " << i << "\n";
			std::cerr << std::string("\t\t\t") << "objectType   = " << vk::to_string(static_cast<vk::ObjectType>(pCallbackData->pObjects[i].objectType))
				<< "\n";
			std::cerr << std::string("\t\t\t") << "objectHandle = " << pCallbackData->pObjects[i].objectHandle << "\n";
			if (pCallbackData->pObjects[i].pObjectName)
			{
				std::cerr << std::string("\t\t\t") << "objectName   = <" << pCallbackData->pObjects[i].pObjectName << ">\n";
			}
		}
	}
	return VK_TRUE;
}

BackendVK::BackendVK(void* window, uint32_t width, uint32_t height)
{
	gContext = new ContextVK;

	auto all_extensions = gContext->context.enumerateInstanceExtensionProperties();

	for (auto extension : all_extensions)
	{
	//	std::cout << extension.extensionName << std::endl;
	}

	auto all_layers = gContext->context.enumerateInstanceLayerProperties();

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
		VK_EXT_DEBUG_UTILS_EXTENSION_NAME,
	};
	
	auto layers = {
		"VK_LAYER_KHRONOS_validation"
	};

	auto version = gContext->context.enumerateInstanceVersion();

	auto major_version = VK_API_VERSION_MAJOR(version);
	auto minor_version = VK_API_VERSION_MINOR(version);
	auto patch_version = VK_API_VERSION_PATCH(version);

	//std::cout << "available vulkan version: " << major_version << "." << minor_version << std::endl;

	auto application_info = vk::ApplicationInfo()
		.setApiVersion(VK_API_VERSION_1_3);

	auto instance_create_info = vk::InstanceCreateInfo()
		.setPEnabledExtensionNames(extensions)
		.setPEnabledLayerNames(layers)
		.setPApplicationInfo(&application_info);

	auto debug_utils_messenger_create_info = vk::DebugUtilsMessengerCreateInfoEXT()
		.setMessageSeverity(
			vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning |
			vk::DebugUtilsMessageSeverityFlagBitsEXT::eError
		)
		.setMessageType(
			vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral |
			vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance |
			vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation
		)
		.setPfnUserCallback(&DebugUtilsMessengerCallback);

	auto enabled_validation_features = {
	//	vk::ValidationFeatureEnableEXT::eBestPractices,
		vk::ValidationFeatureEnableEXT::eDebugPrintf,
	//	vk::ValidationFeatureEnableEXT::eGpuAssisted,
	//	vk::ValidationFeatureEnableEXT::eGpuAssistedReserveBindingSlot,
		vk::ValidationFeatureEnableEXT::eSynchronizationValidation
	};

	auto validation_features = vk::ValidationFeaturesEXT()
		.setEnabledValidationFeatures(enabled_validation_features);

	auto instance_create_info_chain = vk::StructureChain<
		vk::InstanceCreateInfo,
		vk::DebugUtilsMessengerCreateInfoEXT,
		vk::ValidationFeaturesEXT
	>(
		instance_create_info,
		debug_utils_messenger_create_info,
		validation_features
	);

	gContext->instance = gContext->context.createInstance(instance_create_info_chain.get<vk::InstanceCreateInfo>());

	gContext->debug_utils_messenger = gContext->instance.createDebugUtilsMessengerEXT(debug_utils_messenger_create_info);

	auto devices = gContext->instance.enumeratePhysicalDevices();
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

	gContext->physical_device = std::move(devices.at(device_index));

	auto properties = gContext->physical_device.getQueueFamilyProperties();

	for (size_t i = 0; i < properties.size(); i++)
	{
		if (properties[i].queueFlags & vk::QueueFlagBits::eGraphics)
		{
			gContext->queue_family_index = static_cast<uint32_t>(i);
			break;
		}
	}

	auto all_device_extensions = gContext->physical_device.enumerateDeviceExtensionProperties();

	for (auto device_extension : all_device_extensions)
	{
	//	std::cout << device_extension.extensionName << std::endl;
	}

	auto device_extensions = {
		VK_KHR_SWAPCHAIN_EXTENSION_NAME,

		// dynamic pipeline
		VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME,
		VK_EXT_EXTENDED_DYNAMIC_STATE_3_EXTENSION_NAME,

		// raytracing
		VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME,
		VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,
		VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME
	};

	auto queue_priority = { 1.0f };

	auto queue_info = vk::DeviceQueueCreateInfo()
		.setQueueFamilyIndex(gContext->queue_family_index)
		.setQueuePriorities(queue_priority);

	auto device_features = gContext->physical_device.getFeatures2<vk::PhysicalDeviceFeatures2,
		vk::PhysicalDeviceVulkan13Features, 

		// dynamic pipeline
		vk::PhysicalDeviceExtendedDynamicState3FeaturesEXT,

		// raytracing
		vk::PhysicalDeviceRayTracingPipelineFeaturesKHR,
		vk::PhysicalDeviceAccelerationStructureFeaturesKHR,
		vk::PhysicalDeviceBufferAddressFeaturesEXT
	>();

	auto device_info = vk::DeviceCreateInfo()
		.setQueueCreateInfoCount(1)
		.setPQueueCreateInfos(&queue_info)
		.setPEnabledExtensionNames(device_extensions)
		.setPEnabledFeatures(nullptr)
		.setPNext(&device_features.get<vk::PhysicalDeviceFeatures2>());

	gContext->device = gContext->physical_device.createDevice(device_info);

	gContext->queue = gContext->device.getQueue(gContext->queue_family_index, 0);

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

	gContext->surface = vk::raii::SurfaceKHR(gContext->instance, surface_info);

	auto formats = gContext->physical_device.getSurfaceFormatsKHR(*gContext->surface);

	if ((formats.size() == 1) && (formats.at(0).format == vk::Format::eUndefined))
	{
		gContext->surface_format = {
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
				gContext->surface_format = format;
				found = true;
				break;
			}
		}
		if (!found)
		{
			gContext->surface_format = formats.at(0);
		}
	}

	auto command_pool_info = vk::CommandPoolCreateInfo()
		.setFlags(vk::CommandPoolCreateFlagBits::eResetCommandBuffer)
		.setQueueFamilyIndex(gContext->queue_family_index);

	gContext->command_pool = gContext->device.createCommandPool(command_pool_info);

	auto command_buffer_allocate_info = vk::CommandBufferAllocateInfo()
		.setCommandBufferCount(1)
		.setLevel(vk::CommandBufferLevel::ePrimary)
		.setCommandPool(*gContext->command_pool);

	auto command_buffers = gContext->device.allocateCommandBuffers(command_buffer_allocate_info);
	gContext->command_buffer = std::move(command_buffers.at(0));

	gContext->pipeline_state.color_attachment_format = gContext->surface_format.format;
	gContext->pipeline_state.depth_stencil_format = gContext->depth_stencil.format;

	gContext->buffers_synchronized = false;

	createSwapchain(width, height);

	begin();
}

BackendVK::~BackendVK()
{
	end();

	delete gContext;
	gContext = nullptr;
}

void BackendVK::resize(uint32_t width, uint32_t height)
{
	end();
	createSwapchain(width, height);
	begin();
}

void BackendVK::setTopology(Topology topology)
{
	if (gContext->topology == topology)
		return;

	gContext->topology = topology;
	gContext->topology_dirty = true;
}

void BackendVK::setViewport(std::optional<Viewport> viewport)
{
	if (gContext->viewport == viewport)
		return;

	gContext->viewport = viewport;
	gContext->viewport_dirty = true;
}

void BackendVK::setScissor(std::optional<Scissor> scissor)
{
	if (gContext->scissor == scissor)
		return;

	gContext->scissor = scissor;
	gContext->scissor_dirty = true;
}

void BackendVK::setTexture(uint32_t binding, TextureHandle* handle)
{
	auto texture = (TextureVK*)handle;
	gContext->textures[binding] = texture;
}

void BackendVK::setRenderTarget(RenderTargetHandle* handle)
{
	auto render_target = (RenderTargetVK*)handle;

	if (gContext->render_target == render_target)
		return;

	gContext->pipeline_state.color_attachment_format = render_target->getTexture()->getFormat();
	gContext->pipeline_state.depth_stencil_format = render_target->getDepthStencilFormat();
	gContext->render_target = render_target;
	EnsureRenderPassDeactivated();

	if (!gContext->viewport.has_value())
		gContext->viewport_dirty = true;

	if (!gContext->scissor.has_value())
		gContext->scissor_dirty = true;
}

void BackendVK::setRenderTarget(std::nullopt_t value)
{
	if (gContext->render_target == nullptr)
		return;

	gContext->pipeline_state.color_attachment_format = gContext->surface_format.format;
	gContext->pipeline_state.depth_stencil_format = gContext->depth_stencil.format;
	gContext->render_target = nullptr;
	EnsureRenderPassDeactivated();

	if (!gContext->viewport.has_value())
		gContext->viewport_dirty = true;

	if (!gContext->scissor.has_value())
		gContext->scissor_dirty = true;
}

void BackendVK::setShader(ShaderHandle* handle)
{
	auto shader = (ShaderVK*)handle;
	gContext->pipeline_state.shader = shader;
}

void BackendVK::setRaytracingShader(RaytracingShaderHandle* handle)
{
	auto shader = (RaytracingShaderVK*)handle;
	gContext->raytracing_pipeline_state.shader = shader;
}

void BackendVK::setVertexBuffer(VertexBufferHandle* handle)
{
	auto buffer = (VertexBufferVK*)handle;

	if (buffer == gContext->vertex_buffer)
		return;

	gContext->vertex_buffer = buffer;
	gContext->vertex_buffer_dirty = true;
}

void BackendVK::setIndexBuffer(IndexBufferHandle* handle)
{
	auto buffer = (IndexBufferVK*)handle;

	if (buffer == gContext->index_buffer)
		return;

	gContext->index_buffer = buffer;
	gContext->index_buffer_dirty = true;
}

void BackendVK::setUniformBuffer(uint32_t binding, UniformBufferHandle* handle)
{
	auto buffer = (UniformBufferVK*)handle;
	gContext->uniform_buffers[binding] = buffer;
}

void BackendVK::setAccelerationStructure(uint32_t binding, AccelerationStructureHandle* handle)
{
	auto acceleration_structure = (AccelerationStructureVK*)handle;
	gContext->acceleration_structures[binding] = acceleration_structure;
}

void BackendVK::setBlendMode(const BlendMode& value)
{
	if (gContext->blend_mode == value)
		return;

	gContext->blend_mode = value;
	gContext->blend_mode_dirty = true;
}

void BackendVK::setDepthMode(std::optional<DepthMode> depth_mode)
{
	if (gContext->depth_mode == depth_mode)
		return;

	gContext->depth_mode = depth_mode;
	gContext->depth_mode_dirty = true;
}

void BackendVK::setStencilMode(std::optional<StencilMode> stencil_mode)
{
	gContext->command_buffer.setStencilTestEnable(stencil_mode.has_value());
}

void BackendVK::setCullMode(CullMode cull_mode)
{	
	gContext->cull_mode = cull_mode;
	gContext->cull_mode_dirty = true;
}

void BackendVK::setSampler(Sampler value)
{
	gContext->sampler_state.sampler = value;
}

void BackendVK::setTextureAddress(TextureAddress value)
{
	gContext->sampler_state.texture_address = value;
}

void BackendVK::clear(const std::optional<glm::vec4>& color, const std::optional<float>& depth,
	const std::optional<uint8_t>& stencil)
{
	EnsureRenderPassActivated();

	auto width = gContext->render_target ? gContext->render_target->getTexture()->getWidth() : gContext->width;
	auto height = gContext->render_target ? gContext->render_target->getTexture()->getHeight() : gContext->height;

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

		gContext->command_buffer.clearAttachments({ attachment }, { clear_rect });
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

		gContext->command_buffer.clearAttachments({ attachment }, { clear_rect });
	}
}

void BackendVK::draw(uint32_t vertex_count, uint32_t vertex_offset)
{
	PrepareForDrawing();
	EnsureRenderPassActivated();
	gContext->command_buffer.draw(vertex_count, 1, vertex_offset, 0);
}

void BackendVK::drawIndexed(uint32_t index_count, uint32_t index_offset)
{
	PrepareForDrawing();
	EnsureRenderPassActivated();
	gContext->command_buffer.drawIndexed(index_count, 1, index_offset, 0, 0);
}

void BackendVK::readPixels(const glm::i32vec2& pos, const glm::i32vec2& size, TextureHandle* dst_texture_handle)
{
}

std::vector<uint8_t> BackendVK::getPixels()
{
	return { (uint8_t)BackendType::Vulkan };
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
	assert(gContext->render_target != nullptr);

	EnsureRenderPassDeactivated();

	auto shader = gContext->raytracing_pipeline_state.shader;
	assert(shader);

	const auto& pipeline_layout = shader->getPipelineLayout();

	if (!gContext->raytracing_pipeline_states.contains(gContext->raytracing_pipeline_state))
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
			.setLayout(*pipeline_layout)
			.setStages(pipeline_shader_stage_create_info)
			.setGroups(raytracing_shader_groups)
			.setMaxPipelineRayRecursionDepth(1);

		auto pipeline = gContext->device.createRayTracingPipelineKHR(nullptr, nullptr, raytracing_pipeline_create_info);

		gContext->raytracing_pipeline_states.insert({ gContext->raytracing_pipeline_state, std::move(pipeline) });
	}

	const auto& pipeline = gContext->raytracing_pipeline_states.at(gContext->raytracing_pipeline_state);

	gContext->command_buffer.bindPipeline(vk::PipelineBindPoint::eRayTracingKHR, *pipeline);
	
	for (const auto& required_descriptor_binding : shader->getRequiredDescriptorBindings())
	{
		auto binding = required_descriptor_binding.binding;

		auto write_descriptor_set = vk::WriteDescriptorSet()
			.setDescriptorCount(1)
			.setDstBinding(binding)
			//.setDstSet() // TODO: it seems we need iterate through required_descriptor_sets, not .._bindings
			.setDescriptorType(required_descriptor_binding.descriptorType);

		if (required_descriptor_binding.descriptorType == vk::DescriptorType::eAccelerationStructureKHR)
		{
			auto acceleration_structure = gContext->acceleration_structures.at(binding);

			auto write_descriptor_set_acceleration_structure = vk::WriteDescriptorSetAccelerationStructureKHR()
				.setAccelerationStructureCount(1)
				.setPAccelerationStructures(&*acceleration_structure->getTlas());

			write_descriptor_set.setPNext(&write_descriptor_set_acceleration_structure);
		}
		else if (required_descriptor_binding.descriptorType == vk::DescriptorType::eStorageImage)
		{
			auto render_target_image_view = *gContext->render_target->getTexture()->getImageView();

			auto descriptor_image_info = vk::DescriptorImageInfo()
				.setImageLayout(vk::ImageLayout::eGeneral)
				.setImageView(render_target_image_view);

			write_descriptor_set.setPImageInfo(&descriptor_image_info);
		}
		else if (required_descriptor_binding.descriptorType == vk::DescriptorType::eUniformBuffer)
		{
			auto buffer = gContext->uniform_buffers.at(binding);

			auto descriptor_buffer_info = vk::DescriptorBufferInfo()
				.setBuffer(*buffer->getBuffer())
				.setRange(VK_WHOLE_SIZE);

			write_descriptor_set.setPBufferInfo(&descriptor_buffer_info);
		}
		else
		{
			assert(false);
		}

		gContext->command_buffer.pushDescriptorSetKHR(vk::PipelineBindPoint::eRayTracingKHR, *pipeline_layout, 0, { write_descriptor_set });
	}

	static std::optional<vk::StridedDeviceAddressRegionKHR> raygen_shader_binding_table;
	static std::optional<vk::StridedDeviceAddressRegionKHR> miss_shader_binding_table;
	static std::optional<vk::StridedDeviceAddressRegionKHR> hit_shader_binding_table;
	auto callable_shader_binding_table = vk::StridedDeviceAddressRegionKHR();

	if (!raygen_shader_binding_table.has_value())
	{
		static auto ray_tracing_pipeline_properties = gContext->physical_device.getProperties2<vk::PhysicalDeviceProperties2,
			vk::PhysicalDeviceRayTracingPipelinePropertiesKHR>().get<vk::PhysicalDeviceRayTracingPipelinePropertiesKHR>();

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

	gContext->command_buffer.traceRaysKHR(raygen_shader_binding_table.value(), miss_shader_binding_table.value(), hit_shader_binding_table.value(),
		callable_shader_binding_table, width, height, depth);
}

void BackendVK::present()
{
	end(); 

	const auto& render_complete_semaphore = gContext->frames.at(gContext->semaphore_index).render_complete_semaphore;

	auto present_info = vk::PresentInfoKHR()
		.setWaitSemaphoreCount(1)
		.setPWaitSemaphores(&*render_complete_semaphore)
		.setSwapchainCount(1)
		.setPSwapchains(&*gContext->swapchain)
		.setPImageIndices(&gContext->frame_index);

	auto present_result = gContext->queue.presentKHR(present_info);

	gContext->execute_after_present.flush();
	gContext->staging_objects.clear();

	gContext->semaphore_index = (gContext->semaphore_index + 1) % gContext->frames.size();

	begin();
}

void BackendVK::begin()
{
	const auto& image_acquired_semaphore = gContext->frames.at(gContext->semaphore_index).image_acquired_semaphore;

	auto [result, image_index] = gContext->swapchain.acquireNextImage(UINT64_MAX, *image_acquired_semaphore);

	gContext->frame_index = image_index;

	assert(!gContext->working);
	gContext->working = true;

	gContext->topology_dirty = true;
	gContext->viewport_dirty = true;
	gContext->scissor_dirty = true;
	gContext->cull_mode_dirty = true;
	gContext->vertex_buffer_dirty = true;
	gContext->index_buffer_dirty = true;
	gContext->blend_mode_dirty = true;
	gContext->depth_mode_dirty = true;

	auto begin_info = vk::CommandBufferBeginInfo()
		.setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit);

	gContext->command_buffer.begin(begin_info);
}

void BackendVK::end()
{
	assert(gContext->working);
	gContext->working = false;

	EnsureRenderPassDeactivated();
	gContext->command_buffer.end();

	const auto& frame = gContext->frames.at(gContext->frame_index);

	auto wait_result = gContext->device.waitForFences({ *frame.fence }, true, UINT64_MAX);

	gContext->device.resetFences({ *frame.fence });

	const auto& render_complete_semaphore = gContext->frames.at(gContext->semaphore_index).render_complete_semaphore;
	const auto& image_acquired_semaphore = gContext->frames.at(gContext->semaphore_index).image_acquired_semaphore;

	auto wait_dst_stage_mask = vk::PipelineStageFlags{
		vk::PipelineStageFlagBits::eColorAttachmentOutput
	};

	auto submit_info = vk::SubmitInfo()
		.setPWaitDstStageMask(&wait_dst_stage_mask)
		.setWaitSemaphoreCount(1)
		.setPWaitSemaphores(&*image_acquired_semaphore)
		.setCommandBufferCount(1)
		.setPCommandBuffers(&*gContext->command_buffer)
		.setSignalSemaphoreCount(1)
		.setPSignalSemaphores(&*render_complete_semaphore);

	gContext->queue.submit({ submit_info }, *frame.fence);
	gContext->queue.waitIdle();
}

TextureHandle* BackendVK::createTexture(uint32_t width, uint32_t height, Format format, void* memory, bool mipmap)
{
	auto texture = new TextureVK(width, height, format, memory, mipmap);
	return (TextureHandle*)texture;
}

void BackendVK::destroyTexture(TextureHandle* handle)
{
	gContext->execute_after_present.add([handle] {
		auto texture = (TextureVK*)handle;

		auto remove_from_global = [&] {
			for (const auto& [binding, _texture] : gContext->textures)
			{
				if (texture == _texture)
				{
					gContext->textures.erase(binding);
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

ShaderHandle* BackendVK::createShader(const VertexLayout& vertex_layout, const std::string& vertex_code,
	const std::string& fragment_code, const std::vector<std::string>& defines)
{
	auto shader = new ShaderVK(vertex_layout, vertex_code, fragment_code, defines);
	return (ShaderHandle*)shader;
}

void BackendVK::destroyShader(ShaderHandle* handle)
{
	gContext->execute_after_present.add([handle] {
		auto shader = (ShaderVK*)handle;

		for (const auto& [state, pipeline] : gContext->pipeline_states)
		{
			if (state.shader != shader)
				continue;

			gContext->pipeline_states.erase(state);
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
	gContext->execute_after_present.add([handle] {
		auto shader = (RaytracingShaderVK*)handle;

		for (const auto& [state, pipeline] : gContext->raytracing_pipeline_states)
		{
			if (state.shader != shader)
				continue;

			gContext->raytracing_pipeline_states.erase(state);
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
	gContext->execute_after_present.add([handle] {
		auto buffer = (VertexBufferVK*)handle;
		delete buffer;
	});
}

void BackendVK::writeVertexBufferMemory(VertexBufferHandle* handle, void* memory, size_t size, size_t stride)
{
	auto buffer = (VertexBufferVK*)handle;
	buffer->write(memory, size);
	buffer->setStride(stride);

	if (gContext->vertex_buffer == buffer)
		gContext->vertex_buffer_dirty = true;
}

IndexBufferHandle* BackendVK::createIndexBuffer(size_t size, size_t stride)
{
	auto buffer = new IndexBufferVK(size, stride);
	return (IndexBufferHandle*)buffer;
}

void BackendVK::destroyIndexBuffer(IndexBufferHandle* handle)
{
	gContext->execute_after_present.add([handle] {
		auto buffer = (IndexBufferVK*)handle;
		delete buffer;
	});
}

void BackendVK::writeIndexBufferMemory(IndexBufferHandle* handle, void* memory, size_t size, size_t stride)
{
	auto buffer = (IndexBufferVK*)handle;
	buffer->write(memory, size);
	buffer->setStride(stride);

	if (gContext->index_buffer == buffer)
		gContext->index_buffer_dirty = true;
}

UniformBufferHandle* BackendVK::createUniformBuffer(size_t size)
{
	auto buffer = new UniformBufferVK(size);
	return (UniformBufferHandle*)buffer;
}

void BackendVK::destroyUniformBuffer(UniformBufferHandle* handle)
{
	gContext->execute_after_present.add([handle] {
		auto buffer = (UniformBufferVK*)handle;

		auto remove_from_global = [&] {
			for (const auto& [binding, _buffer] : gContext->uniform_buffers)
			{
				if (buffer == _buffer)
				{
					gContext->uniform_buffers.erase(binding);
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
	const std::vector<uint32_t>& indices, const glm::mat4& transform)
{
	auto acceleration_structure = new AccelerationStructureVK(vertices, indices, transform);
	return (AccelerationStructureHandle*)acceleration_structure;
}

void BackendVK::destroyAccelerationStructure(AccelerationStructureHandle* handle)
{
	gContext->execute_after_present.add([handle] {
		auto acceleration_structure = (AccelerationStructureVK*)handle;

		auto remove_from_global = [&] {
			for (const auto& [binding, _acceleration_structure] : gContext->acceleration_structures)
			{
				if (acceleration_structure == _acceleration_structure)
				{
					gContext->acceleration_structures.erase(binding);
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
	auto surface_capabilities = gContext->physical_device.getSurfaceCapabilitiesKHR(*gContext->surface);

	// https://github.com/nvpro-samples/nvpro_core/blob/f2c05e161bba9ab9a8c96c0173bf0edf7c168dfa/nvvk/swapchain_vk.cpp#L143
	// Determine the number of VkImage's to use in the swap chain (we desire to
	// own only 1 image at a time, besides the images being displayed and
	// queued for display):
	
	uint32_t desired_number_of_swapchain_images = surface_capabilities.minImageCount + 1;

	if ((surface_capabilities.maxImageCount > 0) && (desired_number_of_swapchain_images > surface_capabilities.maxImageCount))
	{
		// Application must settle for fewer images than desired:
		desired_number_of_swapchain_images = surface_capabilities.maxImageCount;
	}

	auto max_width = surface_capabilities.maxImageExtent.width;
	auto max_height = surface_capabilities.maxImageExtent.height;

	gContext->width = glm::min(width, max_width);
	gContext->height = glm::min(height, max_height);

	auto image_extent = vk::Extent2D()
		.setWidth(gContext->width)
		.setHeight(gContext->height);

	auto swapchain_info = vk::SwapchainCreateInfoKHR()
		.setSurface(*gContext->surface)
		.setMinImageCount(desired_number_of_swapchain_images)
		.setImageFormat(gContext->surface_format.format)
		.setImageColorSpace(gContext->surface_format.colorSpace)
		.setImageExtent(image_extent)
		.setImageUsage(vk::ImageUsageFlagBits::eColorAttachment)
		.setPreTransform(vk::SurfaceTransformFlagBitsKHR::eIdentity)
		.setImageArrayLayers(1)
		.setImageSharingMode(vk::SharingMode::eExclusive)
		.setQueueFamilyIndexCount(1)
		.setPQueueFamilyIndices(&gContext->queue_family_index)
		.setPresentMode(vk::PresentModeKHR::eFifo)
		.setClipped(true)
		.setCompositeAlpha(vk::CompositeAlphaFlagBitsKHR::eOpaque)
		.setOldSwapchain(*gContext->swapchain);

	gContext->swapchain = gContext->device.createSwapchainKHR(swapchain_info);

	auto backbuffers = gContext->swapchain.getImages();

	gContext->frames.clear();

	for (auto& backbuffer : backbuffers)
	{
		auto frame = ContextVK::Frame();

		auto fence_info = vk::FenceCreateInfo()
			.setFlags(vk::FenceCreateFlagBits::eSignaled);

		frame.fence = gContext->device.createFence(fence_info);

		frame.image_acquired_semaphore = gContext->device.createSemaphore({});
		frame.render_complete_semaphore = gContext->device.createSemaphore({});

		auto image_view_info = vk::ImageViewCreateInfo()
			.setViewType(vk::ImageViewType::e2D)
			.setFormat(gContext->surface_format.format)
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

		frame.backbuffer_color_image_view = gContext->device.createImageView(image_view_info);

		OneTimeSubmit(gContext->device, gContext->command_pool, gContext->queue, [&](auto& cmdbuf) {
			SetImageLayout(cmdbuf, backbuffer, gContext->surface_format.format, vk::ImageLayout::eUndefined,
				vk::ImageLayout::ePresentSrcKHR);
		});

		gContext->frames.push_back(std::move(frame));
	}

	// depth stencil

	auto depth_stencil_image_create_info = vk::ImageCreateInfo()
		.setImageType(vk::ImageType::e2D)
		.setFormat(gContext->depth_stencil.format)
		.setExtent({ width, height, 1 })
		.setMipLevels(1)
		.setArrayLayers(1)
		.setSamples(vk::SampleCountFlagBits::e1)
		.setTiling(vk::ImageTiling::eOptimal)
		.setUsage(vk::ImageUsageFlagBits::eDepthStencilAttachment);

	gContext->depth_stencil.image = gContext->device.createImage(depth_stencil_image_create_info);

	auto depth_stencil_mem_req = gContext->depth_stencil.image.getMemoryRequirements();

	auto depth_stencil_memory_allocate_info = vk::MemoryAllocateInfo()
		.setAllocationSize(depth_stencil_mem_req.size)
		.setMemoryTypeIndex(GetMemoryType(vk::MemoryPropertyFlagBits::eDeviceLocal, depth_stencil_mem_req.memoryTypeBits));

	gContext->depth_stencil.memory = gContext->device.allocateMemory(depth_stencil_memory_allocate_info);

	gContext->depth_stencil.image.bindMemory(*gContext->depth_stencil.memory, 0);

	auto depth_stencil_view_subresource_range = vk::ImageSubresourceRange()
		.setLevelCount(1)
		.setLayerCount(1)
		.setAspectMask(vk::ImageAspectFlagBits::eDepth | vk::ImageAspectFlagBits::eStencil);

	auto depth_stencil_view_create_info = vk::ImageViewCreateInfo()
		.setViewType(vk::ImageViewType::e2D)
		.setImage(*gContext->depth_stencil.image)
		.setFormat(gContext->depth_stencil.format)
		.setSubresourceRange(depth_stencil_view_subresource_range);

	gContext->depth_stencil.view = gContext->device.createImageView(depth_stencil_view_create_info);

	OneTimeSubmit(gContext->device, gContext->command_pool, gContext->queue, [&](auto& cmdbuf) {
		SetImageLayout(cmdbuf, *gContext->depth_stencil.image, gContext->depth_stencil.format, vk::ImageLayout::eUndefined,
			vk::ImageLayout::eDepthStencilAttachmentOptimal);
	});
}

#endif
