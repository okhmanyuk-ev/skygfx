#include "backend_vk.h"

#ifdef SKYGFX_HAS_VULKAN

//#define SKYGFX_VULKAN_VALIDATION_ENABLED

#include "shader_compiler.h"
#include <vulkan/vulkan_raii.hpp>
#include <iostream>

using namespace skygfx;

class ObjectVK;
class ShaderVK;
class RaytracingShaderVK;
class UniformBufferVK;
class StorageBufferVK;
class BottomLevelAccelerationStructureVK;
class TopLevelAccelerationStructureVK;
class TextureVK;
class RenderTargetVK;
class VertexBufferVK;
class IndexBufferVK;

struct PipelineStateVK
{
	ShaderVK* shader = nullptr;
	std::vector<vk::Format> color_attachment_formats;
	std::optional<vk::Format> depth_stencil_format;
	std::optional<InputLayout> input_layout;

	bool operator==(const PipelineStateVK& other) const = default;
};

SKYGFX_MAKE_HASHABLE(PipelineStateVK,
	t.shader,
	t.color_attachment_formats,
	t.depth_stencil_format,
	t.input_layout
);

struct RaytracingPipelineStateVK
{
	RaytracingShaderVK* shader = nullptr;

	bool operator==(const RaytracingPipelineStateVK& other) const = default;
};

SKYGFX_MAKE_HASHABLE(RaytracingPipelineStateVK,
	t.shader
);

struct SamplerStateVK
{
	Sampler sampler = Sampler::Linear;
	TextureAddress texture_address = TextureAddress::Clamp;

	bool operator==(const SamplerStateVK& other) const = default;
};

SKYGFX_MAKE_HASHABLE(SamplerStateVK,
	t.sampler,
	t.texture_address
);

using VulkanObject = std::variant<
	vk::raii::Buffer,
	vk::raii::Image,
	vk::raii::DeviceMemory,
	vk::raii::Pipeline,
	vk::raii::AccelerationStructureKHR
>;

struct ContextVK
{
	ContextVK();
	~ContextVK();

	vk::raii::Context context;
	vk::raii::Instance instance = nullptr;
#if defined(SKYGFX_VULKAN_VALIDATION_ENABLED)
	vk::raii::DebugUtilsMessengerEXT debug_utils_messenger = nullptr;
#endif
	vk::raii::PhysicalDevice physical_device = nullptr;
	vk::raii::Queue queue = nullptr;
	vk::raii::Device device = nullptr;
	uint32_t queue_family_index = -1;
	vk::SurfaceFormatKHR surface_format;
	vk::raii::SurfaceKHR surface = nullptr;
	vk::raii::SwapchainKHR swapchain = nullptr;
	vk::raii::CommandPool command_pool = nullptr;

	constexpr static vk::Format DefaultDepthStencilFormat = vk::Format::eD32SfloatS8Uint;

	bool working = false;

	uint32_t width = 0;
	uint32_t height = 0;

	struct Frame
	{
		vk::raii::Fence fence = nullptr;
		std::shared_ptr<TextureVK> swapchain_texture;
		std::shared_ptr<RenderTargetVK> swapchain_target;
		vk::raii::Semaphore image_acquired_semaphore = nullptr;
		vk::raii::Semaphore render_complete_semaphore = nullptr;
		vk::raii::CommandBuffer command_buffer = nullptr;
		std::vector<VulkanObject> staging_objects;
	};

	std::vector<Frame> frames;

	uint32_t semaphore_index = 0;
	uint32_t frame_index = 0;

	Frame& getCurrentFrame() { return frames.at(frame_index); }

	std::unordered_map<uint32_t, TextureVK*> textures;
	std::unordered_map<uint32_t, UniformBufferVK*> uniform_buffers;
	std::unordered_map<uint32_t, StorageBufferVK*> storage_buffers;
	std::unordered_map<uint32_t, TopLevelAccelerationStructureVK*> top_level_acceleration_structures;

	std::unordered_map<PipelineStateVK, vk::raii::Pipeline> pipeline_states;

	RaytracingPipelineStateVK raytracing_pipeline_state;
	std::unordered_map<RaytracingPipelineStateVK, vk::raii::Pipeline> raytracing_pipeline_states;

	SamplerStateVK sampler_state;
	std::unordered_map<SamplerStateVK, vk::raii::Sampler> sampler_states;

	std::vector<RenderTargetVK*> render_targets;

	PipelineStateVK pipeline_state;
	std::optional<Scissor> scissor;
	std::optional<Viewport> viewport;
	std::optional<DepthMode> depth_mode = DepthMode();
	std::optional<StencilMode> stencil_mode;
	CullMode cull_mode = CullMode::None;
	FrontFace front_face = FrontFace::Clockwise;
	Topology topology = Topology::TriangleList;
	VertexBufferVK* vertex_buffer = nullptr;
	IndexBufferVK* index_buffer = nullptr;
	std::optional<BlendMode> blend_mode;

	bool pipeline_state_dirty = true;
	bool scissor_dirty = true;
	bool viewport_dirty = true;
	bool depth_mode_dirty = true;
	bool stencil_mode_dirty = true;
	bool cull_mode_dirty = true;
	bool front_face_dirty = true;
	bool topology_dirty = true;
	bool vertex_buffer_dirty = true;
	bool index_buffer_dirty = true;
	bool blend_mode_dirty = true;

	std::unordered_set<uint32_t> graphics_pipeline_ignore_bindings;

	uint32_t getBackbufferWidth();
	uint32_t getBackbufferHeight();
	vk::Format getBackbufferFormat();

	bool render_pass_active = false;

	vk::PipelineStageFlags2 current_memory_stage = vk::PipelineStageFlagBits2::eTransfer;

	std::unordered_set<ObjectVK*> objects;
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

static std::tuple<vk::raii::Buffer, vk::raii::DeviceMemory> CreateBuffer(size_t size, vk::BufferUsageFlags usage)
{
	auto buffer_create_info = vk::BufferCreateInfo()
		.setSize(size)
		.setUsage(usage)
		.setSharingMode(vk::SharingMode::eExclusive);

	auto buffer = gContext->device.createBuffer(buffer_create_info);

	auto memory_requirements = buffer.getMemoryRequirements();
	auto memory_type = GetMemoryType(vk::MemoryPropertyFlagBits::eHostVisible, memory_requirements.memoryTypeBits);

	auto memory_allocate_info = vk::MemoryAllocateInfo()
		.setAllocationSize(memory_requirements.size)
		.setMemoryTypeIndex(memory_type);

	auto device_memory = gContext->device.allocateMemory(memory_allocate_info);

	buffer.bindMemory(*device_memory, 0);

	return { std::move(buffer), std::move(device_memory) };
}

static vk::raii::ImageView CreateImageView(vk::Image image, vk::Format format, vk::ImageAspectFlags aspect_flags, uint32_t mip_levels = 1)
{
	auto image_subresource_range = vk::ImageSubresourceRange()
		.setAspectMask(aspect_flags)
		.setLevelCount(mip_levels)
		.setLayerCount(1);

	auto image_view_create_info = vk::ImageViewCreateInfo()
		.setImage(image)
		.setViewType(vk::ImageViewType::e2D)
		.setFormat(format)
		.setSubresourceRange(image_subresource_range);

	return gContext->device.createImageView(image_view_create_info);
}

static std::tuple<vk::raii::Image, vk::raii::DeviceMemory, vk::raii::ImageView> CreateImage(uint32_t width, uint32_t height,
	vk::Format format, vk::ImageUsageFlags usage, vk::ImageAspectFlags aspect_flags, uint32_t mip_levels = 1)
{
	auto image_create_info = vk::ImageCreateInfo()
		.setImageType(vk::ImageType::e2D)
		.setFormat(format)
		.setExtent({ width, height, 1 })
		.setMipLevels(mip_levels)
		.setArrayLayers(1)
		.setSamples(vk::SampleCountFlagBits::e1)
		.setTiling(vk::ImageTiling::eOptimal)
		.setUsage(usage)
		.setSharingMode(vk::SharingMode::eExclusive)
		.setInitialLayout(vk::ImageLayout::eUndefined);

	auto image = gContext->device.createImage(image_create_info);

	auto memory_requirements = image.getMemoryRequirements();
	auto memory_type = GetMemoryType(vk::MemoryPropertyFlagBits::eDeviceLocal, memory_requirements.memoryTypeBits);

	auto memory_allocate_info = vk::MemoryAllocateInfo()
		.setAllocationSize(memory_requirements.size)
		.setMemoryTypeIndex(memory_type);

	auto device_memory = gContext->device.allocateMemory(memory_allocate_info);

	image.bindMemory(*device_memory, 0);

	auto image_view = CreateImageView(*image, format, aspect_flags, mip_levels);

	return { std::move(image), std::move(device_memory), std::move(image_view) };
}

static vk::DeviceAddress GetBufferDeviceAddress(vk::Buffer buffer)
{
	auto info = vk::BufferDeviceAddressInfo()
		.setBuffer(buffer);

	return gContext->device.getBufferAddress(info);
};

static void WriteToBuffer(vk::raii::DeviceMemory& memory, const void* data, size_t size)
{
	auto ptr = memory.mapMemory(0, size);
	memcpy(ptr, data, size);
	memory.unmapMemory();
};

static void DestroyStaging(VulkanObject&& object)
{
	gContext->getCurrentFrame().staging_objects.push_back(std::move(object));
}

static void ReleaseStaging()
{
	gContext->getCurrentFrame().staging_objects.clear();
}

static void BeginRenderPass();
static void EndRenderPass();
static void EnsureRenderPassActivated();
static void EnsureRenderPassDeactivated();
static void SetImageMemoryBarrier(const vk::raii::CommandBuffer& cmdbuf, vk::Image image, vk::ImageAspectFlags aspect_mask,
	vk::ImageLayout old_layout, vk::ImageLayout new_layout, uint32_t base_mip_level = 0,
	uint32_t level_count = VK_REMAINING_MIP_LEVELS, uint32_t base_array_layer = 0,
	uint32_t layer_count = VK_REMAINING_ARRAY_LAYERS);

static void SetImageMemoryBarrier(const vk::raii::CommandBuffer& cmdbuf, vk::Image image, vk::Format format,
	vk::ImageLayout old_layout, vk::ImageLayout new_layout, uint32_t base_mip_level = 0,
	uint32_t level_count = VK_REMAINING_MIP_LEVELS, uint32_t base_array_layer = 0,
	uint32_t layer_count = VK_REMAINING_ARRAY_LAYERS);

static void EnsureMemoryState(const vk::raii::CommandBuffer& cmdbuf, vk::PipelineStageFlags2 stage);
static void OneTimeSubmit(std::function<void(const vk::raii::CommandBuffer&)> func);

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

static const std::unordered_map<vk::Format, Format> ReversedFormatMap = {
	{ vk::Format::eR32Sfloat, Format::Float1 },
	{ vk::Format::eR32G32Sfloat, Format::Float2 },
	{ vk::Format::eR32G32B32Sfloat, Format::Float3 },
	{ vk::Format::eR32G32B32A32Sfloat, Format::Float4 },
	{ vk::Format::eR8Unorm, Format::Byte1 },
	{ vk::Format::eR8G8Unorm, Format::Byte2 },
	{ vk::Format::eR8G8B8Unorm, Format::Byte3 },
	{ vk::Format::eR8G8B8A8Unorm, Format::Byte4 }
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
	{ ShaderReflection::Descriptor::Type::AccelerationStructure, vk::DescriptorType::eAccelerationStructureKHR },
	{ ShaderReflection::Descriptor::Type::StorageBuffer, vk::DescriptorType::eStorageBuffer }
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
		.setSetLayouts(*descriptor_set_layout);

	auto pipeline_layout = gContext->device.createPipelineLayout(pipeline_layout_create_info);

	return { std::move(pipeline_layout), std::move(descriptor_set_layout), required_descriptor_bindings };
}

class ObjectVK
{
public:
	virtual ~ObjectVK() {}
};

class ShaderVK : public ObjectVK
{
public:
	const auto& getPipelineLayout() const { return mPipelineLayout; }
	const auto& getVertexShaderModule() const { return mVertexShaderModule; }
	const auto& getFragmentShaderModule() const { return mFragmentShaderModule; }
	const auto& getRequiredDescriptorBindings() const { return mRequiredDescriptorBindings; }

private:
	vk::raii::DescriptorSetLayout mDescriptorSetLayout = nullptr;
	vk::raii::PipelineLayout mPipelineLayout = nullptr;
	vk::raii::ShaderModule mVertexShaderModule = nullptr;
	vk::raii::ShaderModule mFragmentShaderModule = nullptr;
	std::vector<vk::DescriptorSetLayoutBinding> mRequiredDescriptorBindings;

public:
	ShaderVK(const std::string& vertex_code, const std::string& fragment_code,
		std::vector<std::string> defines)
	{
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
	}
};

class RaytracingShaderVK : public ObjectVK
{
public:
	const auto& getRaygenShaderModule() const { return mRaygenShaderModule; }
	const auto& getMissShaderModules() const { return mMissShaderModules; }
	const auto& getClosestHitShaderModule() const { return mClosestHitShaderModule; }
	const auto& getPipelineLayout() const { return mPipelineLayout; }
	const auto& getRequiredDescriptorBindings() const { return mRequiredDescriptorBindings; }

private:
	vk::raii::ShaderModule mRaygenShaderModule = nullptr;
	std::vector<vk::raii::ShaderModule> mMissShaderModules;
	vk::raii::ShaderModule mClosestHitShaderModule = nullptr;
	vk::raii::DescriptorSetLayout mDescriptorSetLayout = nullptr;
	vk::raii::PipelineLayout mPipelineLayout = nullptr;
	std::vector<vk::DescriptorSetLayoutBinding> mRequiredDescriptorBindings;

public:
	RaytracingShaderVK(const std::string& raygen_code, const std::vector<std::string>& miss_codes,
		const std::string& closesthit_code, std::vector<std::string> defines)
	{
		auto raygen_shader_spirv = CompileGlslToSpirv(ShaderStage::Raygen, raygen_code);
		auto closesthit_shader_spirv = CompileGlslToSpirv(ShaderStage::ClosestHit, closesthit_code);

		auto raygen_shader_module_create_info = vk::ShaderModuleCreateInfo()
			.setCode(raygen_shader_spirv);

		auto closesthit_shader_module_create_info = vk::ShaderModuleCreateInfo()
			.setCode(closesthit_shader_spirv);

		mRaygenShaderModule = gContext->device.createShaderModule(raygen_shader_module_create_info);
		mClosestHitShaderModule = gContext->device.createShaderModule(closesthit_shader_module_create_info);

		std::vector<std::vector<uint32_t>> spirvs = {
			raygen_shader_spirv,
			closesthit_shader_spirv
		};

		for (const auto& miss_code : miss_codes)
		{
			auto miss_shader_spirv = CompileGlslToSpirv(ShaderStage::Miss, miss_code);

			auto miss_shader_module_create_info = vk::ShaderModuleCreateInfo()
				.setCode(miss_shader_spirv);

			auto miss_shader_module = gContext->device.createShaderModule(miss_shader_module_create_info);

			mMissShaderModules.push_back(std::move(miss_shader_module));
			spirvs.push_back(miss_shader_spirv);
		}

		std::tie(mPipelineLayout, mDescriptorSetLayout, mRequiredDescriptorBindings) = CreatePipelineLayout(spirvs);
	}
};

class TextureVK : public ObjectVK
{
public:
	auto getImage() const { return mImagePtr; }
	const auto& getImageView() const { return mImageView; }
	auto getFormat() const { return mFormat; }
	auto getWidth() const { return mWidth; }
	auto getHeight() const { return mHeight; }
	
private:
	std::optional<vk::raii::Image> mImage;
	std::optional<vk::raii::DeviceMemory> mDeviceMemory;
	vk::Image mImagePtr;
	vk::raii::ImageView mImageView = nullptr;
	uint32_t mWidth = 0;
	uint32_t mHeight = 0;
	uint32_t mMipCount = 0;
	vk::Format mFormat;
	vk::ImageLayout mCurrentState = vk::ImageLayout::eUndefined;

public:
	TextureVK(uint32_t width, uint32_t height, vk::Format format, uint32_t mip_count) :
		mWidth(width),
		mHeight(height),
		mFormat(format),
		mMipCount(mip_count)
	{
		auto usage = 
			vk::ImageUsageFlagBits::eSampled |
			vk::ImageUsageFlagBits::eTransferDst |
			vk::ImageUsageFlagBits::eTransferSrc |
			vk::ImageUsageFlagBits::eColorAttachment |
			vk::ImageUsageFlagBits::eStorage;

		std::tie(mImage, mDeviceMemory, mImageView) = CreateImage(width, height, format, usage,
			vk::ImageAspectFlagBits::eColor, mip_count);

		mImagePtr = *mImage.value();
	}

	TextureVK(uint32_t width, uint32_t height, vk::Format format, vk::Image image) :
		mWidth(width),
		mHeight(height),
		mFormat(format),
		mImagePtr(image)
	{
		mImageView = CreateImageView(image, format, vk::ImageAspectFlagBits::eColor);
	}

	~TextureVK()
	{
		if (mImage.has_value())
			DestroyStaging(std::move(mImage.value()));

		if (mDeviceMemory.has_value())
			DestroyStaging(std::move(mDeviceMemory.value()));
	}

	void write(uint32_t width, uint32_t height, Format format, void* memory,
		uint32_t mip_level, uint32_t offset_x, uint32_t offset_y)
	{
		EnsureRenderPassDeactivated();

		auto channels = GetFormatChannelsCount(format);
		auto channel_size = GetFormatChannelSize(format);
		auto size = width * height * channels * channel_size;

		auto [upload_buffer, upload_buffer_memory] = CreateBuffer(size, vk::BufferUsageFlagBits::eTransferSrc);

		WriteToBuffer(upload_buffer_memory, memory, size);

		ensureState(gContext->getCurrentFrame().command_buffer, vk::ImageLayout::eTransferDstOptimal);

		auto image_subresource_layers = vk::ImageSubresourceLayers()
			.setAspectMask(vk::ImageAspectFlagBits::eColor)
			.setMipLevel(mip_level)
			.setLayerCount(1);

		auto region = vk::BufferImageCopy()
			.setImageSubresource(image_subresource_layers)
			.setImageExtent({ width, height, 1 });

		gContext->getCurrentFrame().command_buffer.copyBufferToImage(*upload_buffer, mImagePtr,
			vk::ImageLayout::eTransferDstOptimal, { region });

		DestroyStaging(std::move(upload_buffer));
		DestroyStaging(std::move(upload_buffer_memory));
	}

	void read(uint32_t pos_x, uint32_t pos_y, uint32_t width, uint32_t height,
		uint32_t mip_level, void* dst_memory)
	{
		EnsureRenderPassDeactivated();
		gContext->getCurrentFrame().command_buffer.end();

		auto submit_info = vk::SubmitInfo()
			.setCommandBuffers(*gContext->getCurrentFrame().command_buffer);

		gContext->queue.submit(submit_info);
		gContext->queue.waitIdle();

		auto _format = ReversedFormatMap.at(mFormat);
		auto channels_count = GetFormatChannelsCount(_format);
		auto channel_size = GetFormatChannelSize(_format);
		auto size = width * height * channels_count * channel_size;

		auto [staging_buffer, staging_buffer_memory] = CreateBuffer(size, vk::BufferUsageFlagBits::eTransferDst);

		auto subresource = vk::ImageSubresourceLayers()
			.setAspectMask(vk::ImageAspectFlagBits::eColor)
			.setMipLevel(mip_level)
			.setLayerCount(1);

		auto region = vk::BufferImageCopy2()
			.setImageSubresource(subresource)
			.setBufferImageHeight(height)
			.setBufferRowLength(0)
			.setImageExtent({ width, height, 1 });

		auto copy_image_to_buffer_info = vk::CopyImageToBufferInfo2()
			.setSrcImage(getImage())
			.setSrcImageLayout(vk::ImageLayout::eTransferSrcOptimal)
			.setDstBuffer(*staging_buffer)
			.setRegions(region);

		OneTimeSubmit([&](auto& cmdbuf) {
			ensureState(cmdbuf, vk::ImageLayout::eTransferSrcOptimal);
			cmdbuf.copyImageToBuffer2(copy_image_to_buffer_info);
		});

		auto ptr = staging_buffer_memory.mapMemory(0, size);
		memcpy(dst_memory, ptr, size);
		staging_buffer_memory.unmapMemory();

		auto begin_info = vk::CommandBufferBeginInfo()
			.setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit);

		gContext->getCurrentFrame().command_buffer.begin(begin_info);
	}

	void generateMips()
	{
		ensureState(gContext->getCurrentFrame().command_buffer, vk::ImageLayout::eTransferSrcOptimal);

		for (uint32_t i = 1; i < mMipCount; i++)
		{
			SetImageMemoryBarrier(gContext->getCurrentFrame().command_buffer, mImagePtr, vk::ImageAspectFlagBits::eColor,
				vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal, i, 1);

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
				.setSrcOffsets({ vk::Offset3D{ 0, 0, 0 }, vk::Offset3D{ int32_t(mWidth >> (i - 1)), int32_t(mHeight >> (i - 1)), 1 } })
				.setDstOffsets({ vk::Offset3D{ 0, 0, 0 }, vk::Offset3D{ int32_t(mWidth >> i), int32_t(mHeight >> i), 1 } });

			gContext->getCurrentFrame().command_buffer.blitImage(mImagePtr, vk::ImageLayout::eTransferSrcOptimal,
				mImagePtr, vk::ImageLayout::eTransferDstOptimal, { mip_region }, vk::Filter::eLinear);

			SetImageMemoryBarrier(gContext->getCurrentFrame().command_buffer, mImagePtr, vk::ImageAspectFlagBits::eColor,
				vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eTransferSrcOptimal, i, 1);
		}
	}

	void ensureState(const vk::raii::CommandBuffer& cmdbuf, vk::ImageLayout state)
	{
		if (mCurrentState == state)
			return;

		SetImageMemoryBarrier(cmdbuf, mImagePtr, vk::ImageAspectFlagBits::eColor, mCurrentState, state);
		mCurrentState = state;
	}
};

class RenderTargetVK : public ObjectVK
{
public:
	auto getTexture() const { return mTexture; }
	auto getDepthStencilFormat() const { return mDepthStencilFormat; }
	const auto& getDepthStencilImage() const { return mDepthStencilImage; }
	const auto& getDepthStencilView() const { return mDepthStencilView; }

private:
	TextureVK* mTexture;
	vk::Format mDepthStencilFormat = ContextVK::DefaultDepthStencilFormat;
	vk::raii::Image mDepthStencilImage = nullptr;
	vk::raii::ImageView mDepthStencilView = nullptr;
	vk::raii::DeviceMemory mDepthStencilMemory = nullptr;

public:
	RenderTargetVK(uint32_t width, uint32_t height, TextureVK* _texture) : mTexture(_texture)
	{
		std::tie(mDepthStencilImage, mDepthStencilMemory, mDepthStencilView) = CreateImage(width, height, mDepthStencilFormat,
			vk::ImageUsageFlagBits::eDepthStencilAttachment, vk::ImageAspectFlagBits::eDepth | vk::ImageAspectFlagBits::eStencil);

		OneTimeSubmit([&](auto& cmdbuf) {
			SetImageMemoryBarrier(cmdbuf, *mDepthStencilImage, mDepthStencilFormat, vk::ImageLayout::eUndefined,
				vk::ImageLayout::eDepthStencilAttachmentOptimal);
		});
	}
};

class BufferVK : public ObjectVK
{
public:
	const auto& getBuffer() const { return mBuffer; }

private:
	vk::raii::Buffer mBuffer = nullptr;
	vk::raii::DeviceMemory mDeviceMemory = nullptr;

public:
	BufferVK(size_t size, vk::BufferUsageFlags usage)
	{
		usage |= vk::BufferUsageFlagBits::eTransferDst;
		std::tie(mBuffer, mDeviceMemory) = CreateBuffer(size, usage);
	}

	~BufferVK()
	{
		DestroyStaging(std::move(mBuffer));
		DestroyStaging(std::move(mDeviceMemory));
	}

	void write(void* memory, size_t size)
	{
		EnsureRenderPassDeactivated();
		EnsureMemoryState(gContext->getCurrentFrame().command_buffer, vk::PipelineStageFlagBits2::eTransfer);

		if (size < 65536)
		{
			gContext->getCurrentFrame().command_buffer.updateBuffer<uint8_t>(*mBuffer, 0, { (uint32_t)size, (uint8_t*)memory });
			return;
		}

		auto [staging_buffer, staging_buffer_memory] = CreateBuffer(size, vk::BufferUsageFlagBits::eTransferSrc);

		WriteToBuffer(staging_buffer_memory, memory, size);

		auto region = vk::BufferCopy()
			.setSize(size);

		gContext->getCurrentFrame().command_buffer.copyBuffer(*staging_buffer, *mBuffer, { region });
		
		DestroyStaging(std::move(staging_buffer));
		DestroyStaging(std::move(staging_buffer_memory));
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

class StorageBufferVK : public BufferVK
{
public:
	StorageBufferVK(size_t size) : BufferVK(size, vk::BufferUsageFlagBits::eStorageBuffer)
	{
	}
};

static vk::IndexType GetIndexTypeFromStride(size_t stride)
{
	return stride == 2 ? vk::IndexType::eUint16 : vk::IndexType::eUint32;
}

class BottomLevelAccelerationStructureVK : public ObjectVK
{
public:
	const auto& getBlas() const { return mBlas; }

private:
	vk::raii::AccelerationStructureKHR mBlas = nullptr;
	vk::raii::Buffer mBlasBuffer = nullptr;
	vk::raii::DeviceMemory mBlasMemory = nullptr;

public:
	BottomLevelAccelerationStructureVK(void* vertex_memory, uint32_t vertex_count, uint32_t vertex_stride,
		void* index_memory, uint32_t index_count, uint32_t index_stride, const glm::mat4& _transform)
	{
		auto transform = glm::transpose(_transform);

		auto [vertex_buffer, vertex_buffer_memory] = CreateBuffer(vertex_count * vertex_stride,
			vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR | vk::BufferUsageFlagBits::eShaderDeviceAddress);

		auto [index_buffer, index_buffer_memory] = CreateBuffer(index_count * index_stride,
			vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR | vk::BufferUsageFlagBits::eShaderDeviceAddress);

		auto [transform_buffer, transform_buffer_memory] = CreateBuffer(sizeof(transform), 
			vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR | vk::BufferUsageFlagBits::eShaderDeviceAddress);

		WriteToBuffer(vertex_buffer_memory, vertex_memory, vertex_count * vertex_stride);
		WriteToBuffer(index_buffer_memory, index_memory, index_count * index_stride);
		WriteToBuffer(transform_buffer_memory, &transform, sizeof(transform));

		auto vertex_buffer_device_address = GetBufferDeviceAddress(*vertex_buffer);
		auto index_buffer_device_address = GetBufferDeviceAddress(*index_buffer);
		auto transform_buffer_device_address = GetBufferDeviceAddress(*transform_buffer);

		auto triangles = vk::AccelerationStructureGeometryTrianglesDataKHR()
			.setVertexFormat(vk::Format::eR32G32B32Sfloat)
			.setVertexData(vertex_buffer_device_address)
			.setMaxVertex(vertex_count)
			.setVertexStride(vertex_stride)
			.setIndexType(GetIndexTypeFromStride(index_stride))
			.setIndexData(index_buffer_device_address)
			.setTransformData(transform_buffer_device_address);

		auto geometry_data = vk::AccelerationStructureGeometryDataKHR()
			.setTriangles(triangles);

		auto geometry = vk::AccelerationStructureGeometryKHR()
			.setGeometryType(vk::GeometryTypeKHR::eTriangles)
			.setGeometry(geometry_data)
			.setFlags(vk::GeometryFlagBitsKHR::eOpaque);

		auto build_geometry_info = vk::AccelerationStructureBuildGeometryInfoKHR()
			.setType(vk::AccelerationStructureTypeKHR::eBottomLevel)
			.setFlags(vk::BuildAccelerationStructureFlagBitsKHR::ePreferFastTrace)
			.setGeometries(geometry);

		auto build_sizes = gContext->device.getAccelerationStructureBuildSizesKHR(
			vk::AccelerationStructureBuildTypeKHR::eDevice, build_geometry_info, { 1 });

		std::tie(mBlasBuffer, mBlasMemory) = CreateBuffer(build_sizes.accelerationStructureSize,
			vk::BufferUsageFlagBits::eAccelerationStructureStorageKHR);

		auto create_info = vk::AccelerationStructureCreateInfoKHR()
			.setBuffer(*mBlasBuffer)
			.setType(vk::AccelerationStructureTypeKHR::eBottomLevel)
			.setSize(build_sizes.accelerationStructureSize);

		mBlas = gContext->device.createAccelerationStructureKHR(create_info);

		auto [scratch_buffer, scratch_memory] = CreateBuffer(build_sizes.buildScratchSize,
			vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eShaderDeviceAddress);

		auto scratch_buffer_addr = GetBufferDeviceAddress(*scratch_buffer);

		build_geometry_info
			.setMode(vk::BuildAccelerationStructureModeKHR::eBuild)
			.setDstAccelerationStructure(*mBlas)
			.setScratchData(scratch_buffer_addr);

		auto build_range_info = vk::AccelerationStructureBuildRangeInfoKHR()
			.setPrimitiveCount(static_cast<uint32_t>(index_count / 3));

		auto build_geometry_infos = { build_geometry_info };
		std::vector build_range_infos = { &build_range_info };

		OneTimeSubmit([&](auto& cmdbuf) {
			cmdbuf.buildAccelerationStructuresKHR(build_geometry_infos, build_range_infos);
		});
	}

	~BottomLevelAccelerationStructureVK()
	{
		DestroyStaging(std::move(mBlas));
		DestroyStaging(std::move(mBlasBuffer));
		DestroyStaging(std::move(mBlasMemory));
	}
};

class TopLevelAccelerationStructureVK : public ObjectVK
{
public:
	const auto& getTlas() const { return mTlas; }

private:
	vk::raii::AccelerationStructureKHR mTlas = nullptr;
	vk::raii::Buffer mTlasBuffer = nullptr;
	vk::raii::DeviceMemory mTlasMemory = nullptr;

public:
	TopLevelAccelerationStructureVK(
		const std::vector<std::tuple<uint32_t, BottomLevelAccelerationStructureHandle*>>& bottom_level_acceleration_structures)
	{
		auto transform = glm::mat4(1.0f);

		std::vector<vk::AccelerationStructureInstanceKHR> instances;

		for (auto [custom_index, handle] : bottom_level_acceleration_structures)
		{
			const auto& blas = *(BottomLevelAccelerationStructureVK*)handle;

			auto blas_device_address_info = vk::AccelerationStructureDeviceAddressInfoKHR()
				.setAccelerationStructure(*blas.getBlas());

			auto blas_device_address = gContext->device.getAccelerationStructureAddressKHR(blas_device_address_info);

			auto instance = vk::AccelerationStructureInstanceKHR()
				.setTransform(*(vk::TransformMatrixKHR*)&transform)
				.setMask(0xFF)
				.setInstanceShaderBindingTableRecordOffset(0)
				.setInstanceCustomIndex(custom_index) // gl_InstanceCustomIndexEXT
				.setFlags(vk::GeometryInstanceFlagBitsKHR::eTriangleFacingCullDisable)
				.setAccelerationStructureReference(blas_device_address);

			instances.push_back(instance);
		}

		auto instance_buffer_size = sizeof(vk::AccelerationStructureInstanceKHR) * instances.size();

		auto [instance_buffer, instance_buffer_memory] = CreateBuffer(instance_buffer_size,
			vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR | vk::BufferUsageFlagBits::eShaderDeviceAddress);

		WriteToBuffer(instance_buffer_memory, instances.data(), instance_buffer_size);

		auto instance_buffer_addr = GetBufferDeviceAddress(*instance_buffer);

		auto geometry_instances = vk::AccelerationStructureGeometryInstancesDataKHR()
			.setData(instance_buffer_addr);

		auto geometry_data = vk::AccelerationStructureGeometryDataKHR()
			.setInstances(geometry_instances);

		auto geometry = vk::AccelerationStructureGeometryKHR()
			.setGeometryType(vk::GeometryTypeKHR::eInstances)
			.setGeometry(geometry_data)
			.setFlags(vk::GeometryFlagBitsKHR::eOpaque);

		auto build_geometry_info = vk::AccelerationStructureBuildGeometryInfoKHR()
			.setType(vk::AccelerationStructureTypeKHR::eTopLevel)
			.setFlags(vk::BuildAccelerationStructureFlagBitsKHR::ePreferFastTrace)
			.setGeometries(geometry);

		auto build_sizes = gContext->device.getAccelerationStructureBuildSizesKHR(
			vk::AccelerationStructureBuildTypeKHR::eDevice, build_geometry_info, { (uint32_t)instances.size() });

		std::tie(mTlasBuffer, mTlasMemory) = CreateBuffer(build_sizes.accelerationStructureSize,
			vk::BufferUsageFlagBits::eAccelerationStructureStorageKHR);

		auto create_info = vk::AccelerationStructureCreateInfoKHR()
			.setBuffer(*mTlasBuffer)
			.setType(vk::AccelerationStructureTypeKHR::eTopLevel)
			.setSize(build_sizes.accelerationStructureSize);

		mTlas = gContext->device.createAccelerationStructureKHR(create_info);

		auto [scratch_buffer, scratch_memory] = CreateBuffer(build_sizes.buildScratchSize,
			vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eShaderDeviceAddress);

		auto scratch_buffer_addr = GetBufferDeviceAddress(*scratch_buffer);

		build_geometry_info
			.setMode(vk::BuildAccelerationStructureModeKHR::eBuild)
			.setDstAccelerationStructure(*mTlas)
			.setScratchData(scratch_buffer_addr);

		auto build_range_info = vk::AccelerationStructureBuildRangeInfoKHR()
			.setPrimitiveCount((uint32_t)instances.size());

		auto build_geometry_infos = { build_geometry_info };
		std::vector build_range_infos = { &build_range_info };

		OneTimeSubmit([&](auto& cmdbuf) {
			cmdbuf.buildAccelerationStructuresKHR(build_geometry_infos, build_range_infos);
		});
	}

	~TopLevelAccelerationStructureVK()
	{
		DestroyStaging(std::move(mTlas));
		DestroyStaging(std::move(mTlasBuffer));
		DestroyStaging(std::move(mTlasMemory));
	}
};

ContextVK::ContextVK() :
#if defined(SKYGFX_PLATFORM_WINDOWS)
	context(vk::raii::Context())
#elif defined(SKYGFX_PLATFORM_MACOS) | defined(SKYGFX_PLATFORM_IOS)
	context(vk::raii::Context(vkGetInstanceProcAddr));
#endif
{
}

ContextVK::~ContextVK()
{
	for (auto object : objects)
		delete object;
}

uint32_t ContextVK::getBackbufferWidth()
{
	return !render_targets.empty() ? render_targets.at(0)->getTexture()->getWidth() : width;
}

uint32_t ContextVK::getBackbufferHeight()
{
	return !render_targets.empty() ? render_targets.at(0)->getTexture()->getHeight() : height;
}

vk::Format ContextVK::getBackbufferFormat()
{
	// TODO: wtf when mrt
	return !render_targets.empty() ? render_targets.at(0)->getTexture()->getFormat() : FormatMap.at(Format::Byte4); //gContext->surface_format.format;
}

static void BeginRenderPass()
{
	assert(!gContext->render_pass_active);
	gContext->render_pass_active = true;

	auto targets = gContext->render_targets;

	if (targets.empty())
		targets = { gContext->getCurrentFrame().swapchain_target.get() };

	std::vector<vk::RenderingAttachmentInfo> color_attachments;
	std::optional<vk::RenderingAttachmentInfo> depth_stencil_attachment;

	for (auto target : targets)
	{
		auto color_attachment = vk::RenderingAttachmentInfo()
			.setImageView(*target->getTexture()->getImageView())
			.setImageLayout(vk::ImageLayout::eGeneral)
			.setLoadOp(vk::AttachmentLoadOp::eLoad)
			.setStoreOp(vk::AttachmentStoreOp::eStore);

		color_attachments.push_back(color_attachment);

		if (!depth_stencil_attachment.has_value())
		{
			depth_stencil_attachment = vk::RenderingAttachmentInfo()
				.setImageView(*target->getDepthStencilView())
				.setImageLayout(vk::ImageLayout::eDepthStencilAttachmentOptimal)
				.setLoadOp(vk::AttachmentLoadOp::eLoad)
				.setStoreOp(vk::AttachmentStoreOp::eStore);
		}
	}

	auto width = gContext->getBackbufferWidth();
	auto height = gContext->getBackbufferHeight();

	auto rendering_info = vk::RenderingInfo()
		.setRenderArea({ { 0, 0 }, { width, height } })
		.setLayerCount(1)
		.setColorAttachments(color_attachments);

	if (depth_stencil_attachment.has_value())
	{
		rendering_info.setPDepthAttachment(&depth_stencil_attachment.value());
		rendering_info.setPStencilAttachment(&depth_stencil_attachment.value());
	}

	gContext->getCurrentFrame().command_buffer.beginRendering(rendering_info);
}

static void EndRenderPass()
{
	assert(gContext->render_pass_active);
	gContext->render_pass_active = false;

	gContext->getCurrentFrame().command_buffer.endRendering();
}

static void EnsureRenderPassActivated()
{
	if (gContext->render_pass_active)
		return;

	BeginRenderPass();
}

static void EnsureRenderPassDeactivated()
{
	if (!gContext->render_pass_active)
		return;

	EndRenderPass();
}

static void SetImageMemoryBarrier(const vk::raii::CommandBuffer& cmdbuf, vk::Image image, vk::ImageAspectFlags aspect_mask,
	vk::ImageLayout old_layout, vk::ImageLayout new_layout, uint32_t base_mip_level, uint32_t level_count,
	uint32_t base_array_layer, uint32_t layer_count)
{
	assert(new_layout != vk::ImageLayout::eUndefined && new_layout != vk::ImageLayout::ePreinitialized);

	constexpr vk::PipelineStageFlags depth_stage_mask = vk::PipelineStageFlagBits::eEarlyFragmentTests |
		vk::PipelineStageFlagBits::eLateFragmentTests;

	constexpr vk::PipelineStageFlags sampled_stage_mask = vk::PipelineStageFlagBits::eVertexShader |
		vk::PipelineStageFlagBits::eFragmentShader;

	vk::PipelineStageFlags src_stage_mask = vk::PipelineStageFlagBits::eTopOfPipe;
	vk::PipelineStageFlags dst_stage_mask = vk::PipelineStageFlagBits::eBottomOfPipe;

	vk::AccessFlags src_access_mask;
	vk::AccessFlags dst_access_mask;

	switch (old_layout)
	{
	case vk::ImageLayout::eUndefined:
		break;

	case vk::ImageLayout::eGeneral:
		src_stage_mask = vk::PipelineStageFlagBits::eAllCommands;
		src_access_mask = vk::AccessFlagBits::eMemoryWrite;
		break;

	case vk::ImageLayout::eColorAttachmentOptimal:
		src_stage_mask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
		src_access_mask = vk::AccessFlagBits::eColorAttachmentWrite;
		break;

	case vk::ImageLayout::eDepthStencilAttachmentOptimal:
		src_stage_mask = depth_stage_mask;
		src_access_mask = vk::AccessFlagBits::eDepthStencilAttachmentWrite;
		break;

	case vk::ImageLayout::eDepthStencilReadOnlyOptimal:
		src_stage_mask = depth_stage_mask | sampled_stage_mask;
		break;

	case vk::ImageLayout::eShaderReadOnlyOptimal:
		src_stage_mask = sampled_stage_mask;
		break;

	case vk::ImageLayout::eTransferSrcOptimal:
		src_stage_mask = vk::PipelineStageFlagBits::eTransfer;
		break;

	case vk::ImageLayout::eTransferDstOptimal:
		src_stage_mask = vk::PipelineStageFlagBits::eTransfer;
		src_access_mask = vk::AccessFlagBits::eTransferWrite;
		break;

	case vk::ImageLayout::ePreinitialized:
		src_stage_mask = vk::PipelineStageFlagBits::eHost;
		src_access_mask = vk::AccessFlagBits::eHostWrite;
		break;

	case vk::ImageLayout::ePresentSrcKHR:
		break;

	default:
		assert(false);
		break;
	}

	switch (new_layout)
	{
	case vk::ImageLayout::eGeneral:
		dst_stage_mask = vk::PipelineStageFlagBits::eAllCommands;
		dst_access_mask = vk::AccessFlagBits::eMemoryRead | vk::AccessFlagBits::eMemoryWrite;
		break;

	case vk::ImageLayout::eColorAttachmentOptimal:
		dst_stage_mask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
		dst_access_mask = vk::AccessFlagBits::eColorAttachmentRead | vk::AccessFlagBits::eColorAttachmentWrite;
		break;

	case vk::ImageLayout::eDepthStencilAttachmentOptimal:
		dst_stage_mask = depth_stage_mask;
		dst_access_mask = vk::AccessFlagBits::eDepthStencilAttachmentRead | vk::AccessFlagBits::eDepthStencilAttachmentWrite;
		break;

	case vk::ImageLayout::eDepthStencilReadOnlyOptimal:
		dst_stage_mask = depth_stage_mask | sampled_stage_mask;
		dst_access_mask = vk::AccessFlagBits::eDepthStencilAttachmentRead | vk::AccessFlagBits::eShaderRead |
			vk::AccessFlagBits::eInputAttachmentRead;
		break;

	case vk::ImageLayout::eShaderReadOnlyOptimal:
		dst_stage_mask = sampled_stage_mask;
		dst_access_mask = vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eInputAttachmentRead;
		break;

	case vk::ImageLayout::eTransferSrcOptimal:
		dst_stage_mask = vk::PipelineStageFlagBits::eTransfer;
		dst_access_mask = vk::AccessFlagBits::eTransferRead;
		break;

	case vk::ImageLayout::eTransferDstOptimal:
		dst_stage_mask = vk::PipelineStageFlagBits::eTransfer;
		dst_access_mask = vk::AccessFlagBits::eTransferWrite;
		break;

	case vk::ImageLayout::ePresentSrcKHR:
		// vkQueuePresentKHR performs automatic visibility operations
		break;

	default:
		assert(false);
		break;
	}

	auto subresource_range = vk::ImageSubresourceRange()
		.setAspectMask(aspect_mask)
		.setBaseMipLevel(base_mip_level)
		.setLevelCount(level_count)
		.setBaseArrayLayer(base_array_layer)
		.setLayerCount(layer_count);

	auto image_memory_barrier = vk::ImageMemoryBarrier()
		.setSrcAccessMask(src_access_mask)
		.setDstAccessMask(dst_access_mask)
		.setOldLayout(old_layout)
		.setNewLayout(new_layout)
		.setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
		.setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
		.setImage(image)
		.setSubresourceRange(subresource_range);

	cmdbuf.pipelineBarrier(src_stage_mask, dst_stage_mask, {}, {}, {}, { image_memory_barrier });
}

static void SetImageMemoryBarrier(const vk::raii::CommandBuffer& cmdbuf, vk::Image image, vk::Format format,
	vk::ImageLayout old_layout, vk::ImageLayout new_layout, uint32_t base_mip_level, uint32_t level_count,
	uint32_t base_array_layer, uint32_t layer_count)
{
	vk::ImageAspectFlags aspect_mask;
	if (new_layout == vk::ImageLayout::eDepthStencilAttachmentOptimal)
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
	SetImageMemoryBarrier(cmdbuf, image, aspect_mask, old_layout, new_layout, base_mip_level, level_count,
		base_array_layer, layer_count);
}

static void SetMemoryBarrier(const vk::raii::CommandBuffer& cmdbuf, vk::PipelineStageFlags2 src_stage,
	vk::PipelineStageFlags2 dst_stage)
{
	auto memory_barrier = vk::MemoryBarrier2()
		.setSrcStageMask(src_stage)
		.setSrcAccessMask(vk::AccessFlagBits2::eMemoryWrite)
		.setDstStageMask(dst_stage)
		.setDstAccessMask(vk::AccessFlagBits2::eMemoryWrite | vk::AccessFlagBits2::eMemoryRead);

	auto dependency_info = vk::DependencyInfo()
		.setMemoryBarriers(memory_barrier);

	cmdbuf.pipelineBarrier2(dependency_info);
}

static void EnsureMemoryState(const vk::raii::CommandBuffer& cmdbuf, vk::PipelineStageFlags2 stage)
{
	if (gContext->current_memory_stage == stage)
		return;

	EnsureRenderPassDeactivated();
	SetMemoryBarrier(cmdbuf, gContext->current_memory_stage, stage);

	gContext->current_memory_stage = stage;
}

static void OneTimeSubmit(std::function<void(const vk::raii::CommandBuffer&)> func)
{
	auto command_buffer_allocate_info = vk::CommandBufferAllocateInfo()
		.setCommandBufferCount(1)
		.setCommandPool(*gContext->command_pool)
		.setLevel(vk::CommandBufferLevel::ePrimary);

	auto command_buffers = gContext->device.allocateCommandBuffers(command_buffer_allocate_info);
	auto cmdbuf = std::move(command_buffers.at(0));

	auto command_buffer_begin_info = vk::CommandBufferBeginInfo()
		.setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit);

	cmdbuf.begin(command_buffer_begin_info);
	func(cmdbuf);
	cmdbuf.end();
	
	auto submit_info = vk::SubmitInfo()
		.setCommandBuffers(*cmdbuf);

	gContext->queue.submit(submit_info);
	gContext->queue.waitIdle();
}

static vk::raii::Sampler CreateSamplerState(const SamplerStateVK& sampler_state)
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
		.setMagFilter(FilterMap.at(sampler_state.sampler))
		.setMinFilter(FilterMap.at(sampler_state.sampler))
		.setMipmapMode(vk::SamplerMipmapMode::eLinear)
		.setAddressModeU(AddressModeMap.at(sampler_state.texture_address))
		.setAddressModeV(AddressModeMap.at(sampler_state.texture_address))
		.setAddressModeW(AddressModeMap.at(sampler_state.texture_address))
		.setMinLod(-1000)
		.setMaxLod(1000)
		.setMaxAnisotropy(1.0f);

	return gContext->device.createSampler(sampler_create_info);
}

static void PushDescriptorBuffer(vk::raii::CommandBuffer& cmdlist, vk::PipelineBindPoint pipeline_bind_point,
	const vk::raii::PipelineLayout& pipeline_layout, uint32_t binding, vk::DescriptorType type,
	const vk::raii::Buffer& buffer)
{
	auto descriptor_buffer_info = vk::DescriptorBufferInfo()
		.setBuffer(*buffer)
		.setRange(VK_WHOLE_SIZE);

	auto write_descriptor_set = vk::WriteDescriptorSet()
		.setDstBinding(binding)
		.setDescriptorCount(1)
		.setDescriptorType(type)
		.setBufferInfo(descriptor_buffer_info);

	cmdlist.pushDescriptorSetKHR(pipeline_bind_point,
		*pipeline_layout, 0, write_descriptor_set);
}

static void PushDescriptorTexture(vk::raii::CommandBuffer& cmdlist, vk::PipelineBindPoint pipeline_bind_point,
	const vk::raii::PipelineLayout& pipeline_layout, uint32_t binding)
{
	if (!gContext->sampler_states.contains(gContext->sampler_state))
	{
		auto sampler = CreateSamplerState(gContext->sampler_state);
		gContext->sampler_states.insert({ gContext->sampler_state, std::move(sampler) });
	}

	auto texture = gContext->textures.at(binding);
	texture->ensureState(cmdlist, vk::ImageLayout::eGeneral);

	const auto& sampler = gContext->sampler_states.at(gContext->sampler_state);

	auto descriptor_image_info = vk::DescriptorImageInfo()
		.setSampler(*sampler)
		.setImageView(*texture->getImageView())
		.setImageLayout(vk::ImageLayout::eGeneral);

	auto write_descriptor_set = vk::WriteDescriptorSet()
		.setDstBinding(binding)
		.setDescriptorCount(1)
		.setDescriptorType(vk::DescriptorType::eCombinedImageSampler)
		.setImageInfo(descriptor_image_info);

	cmdlist.pushDescriptorSetKHR(pipeline_bind_point,
		*pipeline_layout, 0, write_descriptor_set);
}

static void PushDescriptorUniformBuffer(vk::raii::CommandBuffer& cmdlist, vk::PipelineBindPoint pipeline_bind_point,
	const vk::raii::PipelineLayout& pipeline_layout, uint32_t binding)
{
	auto buffer = gContext->uniform_buffers.at(binding);

	PushDescriptorBuffer(cmdlist, pipeline_bind_point, pipeline_layout, binding,
		vk::DescriptorType::eUniformBuffer, buffer->getBuffer());
}

static void PushDescriptorAccelerationStructure(vk::raii::CommandBuffer& cmdlist, vk::PipelineBindPoint pipeline_bind_point,
	const vk::raii::PipelineLayout& pipeline_layout, uint32_t binding)
{
	auto acceleration_structure = gContext->top_level_acceleration_structures.at(binding);

	auto write_descriptor_set_acceleration_structure = vk::WriteDescriptorSetAccelerationStructureKHR()
		.setAccelerationStructures(*acceleration_structure->getTlas());

	auto write_descriptor_set = vk::WriteDescriptorSet()
		.setDstBinding(binding)
		.setDescriptorCount(1)
		.setDescriptorType(vk::DescriptorType::eAccelerationStructureKHR)
		.setPNext(&write_descriptor_set_acceleration_structure);

	cmdlist.pushDescriptorSetKHR(pipeline_bind_point,
		*pipeline_layout, 0, write_descriptor_set);
}

static void PushDescriptorStorageImage(vk::raii::CommandBuffer& cmdlist, vk::PipelineBindPoint pipeline_bind_point,
	const vk::raii::PipelineLayout& pipeline_layout, uint32_t binding)
{
	auto texture = gContext->render_targets.at(0)->getTexture();
	texture->ensureState(cmdlist, vk::ImageLayout::eGeneral);

	auto descriptor_image_info = vk::DescriptorImageInfo()
		.setImageLayout(vk::ImageLayout::eGeneral)
		.setImageView(*texture->getImageView());

	auto write_descriptor_set = vk::WriteDescriptorSet()
		.setDstBinding(binding)
		.setDescriptorCount(1)
		.setDescriptorType(vk::DescriptorType::eStorageImage)
		.setImageInfo(descriptor_image_info);

	cmdlist.pushDescriptorSetKHR(pipeline_bind_point,
		*pipeline_layout, 0, write_descriptor_set);
}

static void PushDescriptorStorageBuffer(vk::raii::CommandBuffer& cmdlist, vk::PipelineBindPoint pipeline_bind_point,
	const vk::raii::PipelineLayout& pipeline_layout, uint32_t binding)
{
	auto buffer = gContext->storage_buffers.at(binding);

	PushDescriptorBuffer(cmdlist, pipeline_bind_point, pipeline_layout, binding,
		vk::DescriptorType::eStorageBuffer, buffer->getBuffer());
}

static void PushDescriptors(vk::raii::CommandBuffer& cmdlist, vk::PipelineBindPoint pipeline_bind_point,
	const vk::raii::PipelineLayout& pipeline_layout, const std::vector<vk::DescriptorSetLayoutBinding>& required_descriptor_bindings,
	const std::unordered_set<uint32_t>& ignore_bindings = {})
{
	using PushTypedCallback = std::function<void(vk::raii::CommandBuffer& cmdlist, vk::PipelineBindPoint pipeline_bind_point,
		const vk::raii::PipelineLayout& pipeline_layout, uint32_t binding)>;

	static const std::unordered_map<vk::DescriptorType, PushTypedCallback> PushTypedCallbacks = {
		{ vk::DescriptorType::eCombinedImageSampler, PushDescriptorTexture },
		{ vk::DescriptorType::eUniformBuffer, PushDescriptorUniformBuffer },
		{ vk::DescriptorType::eAccelerationStructureKHR, PushDescriptorAccelerationStructure },
		{ vk::DescriptorType::eStorageImage, PushDescriptorStorageImage },
		{ vk::DescriptorType::eStorageBuffer, PushDescriptorStorageBuffer },
	};

	for (const auto& required_descriptor_binding : required_descriptor_bindings)
	{
		auto binding = required_descriptor_binding.binding;

		if (ignore_bindings.contains(binding))
			continue;

		auto type = required_descriptor_binding.descriptorType;
		const auto& callback = PushTypedCallbacks.at(type);
		callback(cmdlist, pipeline_bind_point, pipeline_layout, binding);
	}
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

struct RaytracingShaderBindingTable
{
	vk::raii::Buffer raygen_buffer;
	vk::raii::DeviceMemory raygen_memory;
	vk::StridedDeviceAddressRegionKHR raygen_address;

	vk::raii::Buffer miss_buffer;
	vk::raii::DeviceMemory miss_memory;
	vk::StridedDeviceAddressRegionKHR miss_address;

	vk::raii::Buffer hit_buffer;
	vk::raii::DeviceMemory hit_memory;
	vk::StridedDeviceAddressRegionKHR hit_address;

	vk::StridedDeviceAddressRegionKHR callable_address;
};

static RaytracingShaderBindingTable CreateRaytracingShaderBindingTable(const vk::raii::Pipeline& pipeline)
{
	auto ray_tracing_pipeline_properties = gContext->physical_device.getProperties2<vk::PhysicalDeviceProperties2,
		vk::PhysicalDeviceRayTracingPipelinePropertiesKHR>().get<vk::PhysicalDeviceRayTracingPipelinePropertiesKHR>();

	auto handle_size = ray_tracing_pipeline_properties.shaderGroupHandleSize;
	auto handle_size_aligned = AlignUp(handle_size, ray_tracing_pipeline_properties.shaderGroupHandleAlignment);

	auto [raygen_buffer, raygen_memory] = CreateBuffer(handle_size_aligned,
		vk::BufferUsageFlagBits::eShaderBindingTableKHR | vk::BufferUsageFlagBits::eShaderDeviceAddress);

	auto [miss_buffer, miss_memory] = CreateBuffer(handle_size_aligned, 
		vk::BufferUsageFlagBits::eShaderBindingTableKHR | vk::BufferUsageFlagBits::eShaderDeviceAddress);

	auto [hit_buffer, hit_memory] = CreateBuffer(handle_size_aligned,
		vk::BufferUsageFlagBits::eShaderBindingTableKHR | vk::BufferUsageFlagBits::eShaderDeviceAddress);

	const auto& shader = gContext->raytracing_pipeline_state.shader;

	auto raygen_shader_count = 1;
	auto miss_shader_count = shader->getMissShaderModules().size();
	auto hit_shader_count = 1;

	auto group_count = raygen_shader_count + miss_shader_count + hit_shader_count;
	auto sbt_size = group_count * handle_size_aligned;

	auto shader_handle_storage = pipeline.getRayTracingShaderGroupHandlesKHR<uint8_t>(0, (uint32_t)group_count, sbt_size);

	auto get_shader_ptr = [&](size_t index) {
		return shader_handle_storage.data() + (handle_size_aligned * index);
	};

	WriteToBuffer(raygen_memory, get_shader_ptr(0), handle_size * raygen_shader_count);
	WriteToBuffer(miss_memory, get_shader_ptr(raygen_shader_count), handle_size * miss_shader_count);
	WriteToBuffer(hit_memory, get_shader_ptr(raygen_shader_count + miss_shader_count), handle_size * hit_shader_count);

	auto raygen_address = vk::StridedDeviceAddressRegionKHR()
		.setStride(handle_size_aligned)
		.setSize(handle_size_aligned)
		.setDeviceAddress(GetBufferDeviceAddress(*raygen_buffer));

	auto miss_address = vk::StridedDeviceAddressRegionKHR()
		.setStride(handle_size_aligned)
		.setSize(handle_size_aligned)
		.setDeviceAddress(GetBufferDeviceAddress(*miss_buffer));

	auto hit_address = vk::StridedDeviceAddressRegionKHR()
		.setStride(handle_size_aligned)
		.setSize(handle_size_aligned)
		.setDeviceAddress(GetBufferDeviceAddress(*hit_buffer));

	auto callable_shader_binding_table = vk::StridedDeviceAddressRegionKHR();

	return RaytracingShaderBindingTable{
		.raygen_buffer = std::move(raygen_buffer),
		.raygen_memory = std::move(raygen_memory),
		.raygen_address = raygen_address,

		.miss_buffer = std::move(miss_buffer),
		.miss_memory = std::move(miss_memory),
		.miss_address = miss_address,

		.hit_buffer = std::move(hit_buffer),
		.hit_memory = std::move(hit_memory),
		.hit_address = hit_address,

		.callable_address = callable_shader_binding_table
	};
}

static vk::raii::Pipeline CreateGraphicsPipeline(const PipelineStateVK& pipeline_state)
{
	auto pipeline_shader_stage_create_info = {
		vk::PipelineShaderStageCreateInfo()
			.setStage(vk::ShaderStageFlagBits::eVertex)
			.setModule(*pipeline_state.shader->getVertexShaderModule())
			.setPName("main"),

		vk::PipelineShaderStageCreateInfo()
			.setStage(vk::ShaderStageFlagBits::eFragment)
			.setModule(*pipeline_state.shader->getFragmentShaderModule())
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

	auto pipeline_color_blend_state_create_info = vk::PipelineColorBlendStateCreateInfo()
		.setAttachmentCount((uint32_t)pipeline_state.color_attachment_formats.size());

	const auto& input_layout_nn = pipeline_state.input_layout.value();

	auto vertex_input_binding_description = vk::VertexInputBindingDescription()
		.setStride(static_cast<uint32_t>(input_layout_nn.stride))
		.setInputRate(vk::VertexInputRate::eVertex)
		.setBinding(0);

	std::vector<vk::VertexInputAttributeDescription> vertex_input_attribute_descriptions;

	for (size_t i = 0; i < input_layout_nn.attributes.size(); i++)
	{
		const auto& attribute = input_layout_nn.attributes.at(i);

		auto vertex_input_attribute_description = vk::VertexInputAttributeDescription()
			.setBinding(0)
			.setLocation((uint32_t)i)
			.setFormat(FormatMap.at(attribute.format))
			.setOffset(static_cast<uint32_t>(attribute.offset));

		vertex_input_attribute_descriptions.push_back(vertex_input_attribute_description);
	}

	auto pipeline_vertex_input_state_create_info = vk::PipelineVertexInputStateCreateInfo()
		.setVertexBindingDescriptions(vertex_input_binding_description)
		.setVertexAttributeDescriptions(vertex_input_attribute_descriptions);

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
		vk::DynamicState::eColorBlendEnableEXT,
		vk::DynamicState::eStencilTestEnable
	};

	auto pipeline_dynamic_state_create_info = vk::PipelineDynamicStateCreateInfo()
		.setDynamicStates(dynamic_states);

	auto pipeline_rendering_create_info = vk::PipelineRenderingCreateInfo()
		.setColorAttachmentFormats(pipeline_state.color_attachment_formats)
		.setDepthAttachmentFormat(pipeline_state.depth_stencil_format.value())
		.setStencilAttachmentFormat(pipeline_state.depth_stencil_format.value());

	auto graphics_pipeline_create_info = vk::GraphicsPipelineCreateInfo()
		.setLayout(*pipeline_state.shader->getPipelineLayout())
		.setFlags(vk::PipelineCreateFlagBits())
		.setStages(pipeline_shader_stage_create_info)
		.setPVertexInputState(&pipeline_vertex_input_state_create_info)
		.setPInputAssemblyState(&pipeline_input_assembly_state_create_info)
		.setPViewportState(&pipeline_viewport_state_create_info)
		.setPRasterizationState(&pipeline_rasterization_state_create_info)
		.setPMultisampleState(&pipeline_multisample_state_create_info)
		.setPDepthStencilState(&pipeline_depth_stencil_state_create_info)
		.setPColorBlendState(&pipeline_color_blend_state_create_info) // TODO: this can be nullptr https://registry.khronos.org/vulkan/specs/1.3-extensions/man/html/VkGraphicsPipelineCreateInfo.html
		.setPDynamicState(&pipeline_dynamic_state_create_info)
		.setRenderPass(nullptr)
		.setPNext(&pipeline_rendering_create_info);

	return gContext->device.createGraphicsPipeline(nullptr, graphics_pipeline_create_info);
}

static vk::raii::Pipeline CreateRaytracingPipeline(const RaytracingPipelineStateVK& pipeline_state)
{
	std::vector<vk::PipelineShaderStageCreateInfo> stages;
	std::vector<vk::RayTracingShaderGroupCreateInfoKHR> groups;

	auto addShader = [&](vk::ShaderStageFlagBits stage, vk::RayTracingShaderGroupTypeKHR group_type, const vk::raii::ShaderModule& shader_module) {
		auto stage_create_info = vk::PipelineShaderStageCreateInfo()
			.setStage(stage)
			.setModule(*shader_module)
			.setPName("main");

		stages.push_back(stage_create_info);

		auto group = vk::RayTracingShaderGroupCreateInfoKHR()
			.setType(group_type)
			.setGeneralShader(VK_SHADER_UNUSED_KHR)
			.setClosestHitShader(VK_SHADER_UNUSED_KHR)
			.setAnyHitShader(VK_SHADER_UNUSED_KHR)
			.setIntersectionShader(VK_SHADER_UNUSED_KHR);

		auto index = (uint32_t)(stages.size() - 1);

		if (group_type == vk::RayTracingShaderGroupTypeKHR::eGeneral)
		{
			group.setGeneralShader(index);
		}
		else if (group_type == vk::RayTracingShaderGroupTypeKHR::eTrianglesHitGroup && stage == vk::ShaderStageFlagBits::eClosestHitKHR)
		{
			group.setClosestHitShader(index);
		}
		else
		{
			assert(false);
		}

		groups.push_back(group);
	};

	addShader(vk::ShaderStageFlagBits::eRaygenKHR, vk::RayTracingShaderGroupTypeKHR::eGeneral,
		pipeline_state.shader->getRaygenShaderModule());

	for (const auto& miss_module : pipeline_state.shader->getMissShaderModules())
	{
		addShader(vk::ShaderStageFlagBits::eMissKHR, vk::RayTracingShaderGroupTypeKHR::eGeneral, miss_module);
	}

	addShader(vk::ShaderStageFlagBits::eClosestHitKHR, vk::RayTracingShaderGroupTypeKHR::eTrianglesHitGroup,
		pipeline_state.shader->getClosestHitShaderModule());

	auto raytracing_pipeline_create_info = vk::RayTracingPipelineCreateInfoKHR()
		.setLayout(*pipeline_state.shader->getPipelineLayout())
		.setStages(stages)
		.setGroups(groups)
		.setMaxPipelineRayRecursionDepth(3);

	return gContext->device.createRayTracingPipelineKHR(nullptr, nullptr, raytracing_pipeline_create_info);
}

static void EnsureVertexBuffer(vk::raii::CommandBuffer& cmdlist)
{
	if (!gContext->vertex_buffer_dirty)
		return;

	gContext->vertex_buffer_dirty = false;
	
	cmdlist.bindVertexBuffers2(0, { *gContext->vertex_buffer->getBuffer() }, { 0 }, nullptr,
		{ gContext->vertex_buffer->getStride() });
}

static void EnsureIndexBuffer(vk::raii::CommandBuffer& cmdlist)
{
	if (!gContext->index_buffer_dirty)
		return;

	gContext->index_buffer_dirty = false;
	
	auto index_type = GetIndexTypeFromStride(gContext->index_buffer->getStride());
	cmdlist.bindIndexBuffer(*gContext->index_buffer->getBuffer(), 0, index_type);
}

static void EnsureTopology(vk::raii::CommandBuffer& cmdlist)
{
	if (!gContext->topology_dirty)
		return;

	gContext->topology_dirty = false;

	static const std::unordered_map<Topology, vk::PrimitiveTopology> TopologyMap = {
		{ Topology::PointList, vk::PrimitiveTopology::ePointList },
		{ Topology::LineList, vk::PrimitiveTopology::eLineList },
		{ Topology::LineStrip, vk::PrimitiveTopology::eLineStrip },
		{ Topology::TriangleList, vk::PrimitiveTopology::eTriangleList },
		{ Topology::TriangleStrip, vk::PrimitiveTopology::eTriangleStrip },
	};

	auto topology = TopologyMap.at(gContext->topology);

	cmdlist.setPrimitiveTopology(topology);
}

static void EnsureViewport(vk::raii::CommandBuffer& cmdlist)
{
	if (!gContext->viewport_dirty)
		return;

	gContext->viewport_dirty = false;

	auto width = static_cast<float>(gContext->getBackbufferWidth());
	auto height = static_cast<float>(gContext->getBackbufferHeight());

	auto value = gContext->viewport.value_or(Viewport{ { 0.0f, 0.0f }, { width, height } });

	auto viewport = vk::Viewport()
		.setX(value.position.x)
		.setY(value.size.y - value.position.y)
		.setWidth(value.size.x)
		.setHeight(-value.size.y)
		.setMinDepth(value.min_depth)
		.setMaxDepth(value.max_depth);

	cmdlist.setViewport(0, { viewport });
}

static void EnsureScissor(vk::raii::CommandBuffer& cmdlist)
{
	if (!gContext->scissor_dirty)
		return;

	gContext->scissor_dirty = false;

	auto width = static_cast<float>(gContext->getBackbufferWidth());
	auto height = static_cast<float>(gContext->getBackbufferHeight());

	auto value = gContext->scissor.value_or(Scissor{ { 0.0f, 0.0f }, { width, height } });

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

	cmdlist.setScissor(0, { rect });
}

static void EnsureCullMode(vk::raii::CommandBuffer& cmdlist)
{
	if (!gContext->cull_mode_dirty)
		return;

	gContext->cull_mode_dirty = false;

	const static std::unordered_map<CullMode, vk::CullModeFlags> CullModeMap = {
		{ CullMode::None, vk::CullModeFlagBits::eNone },
		{ CullMode::Front, vk::CullModeFlagBits::eFront },
		{ CullMode::Back, vk::CullModeFlagBits::eBack },
	};

	cmdlist.setCullMode(CullModeMap.at(gContext->cull_mode));
}

static void EnsureFrontFace(vk::raii::CommandBuffer& cmdlist)
{
	if (!gContext->front_face_dirty)
		return;

	gContext->front_face_dirty = false;

	const static std::unordered_map<FrontFace, vk::FrontFace> FrontFaceMap = {
		{ FrontFace::Clockwise, vk::FrontFace::eClockwise },
		{ FrontFace::CounterClockwise, vk::FrontFace::eCounterClockwise },
	};

	cmdlist.setFrontFace(FrontFaceMap.at(gContext->front_face));
}

static void EnsureBlendMode(vk::raii::CommandBuffer& cmdlist)
{
	if (!gContext->blend_mode_dirty)
		return;

	gContext->blend_mode_dirty = false;
	
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

	const auto& blend_mode = gContext->blend_mode.value_or(BlendStates::Opaque);

	auto color_mask = vk::ColorComponentFlags();

	if (blend_mode.color_mask.red)
		color_mask |= vk::ColorComponentFlagBits::eR;

	if (blend_mode.color_mask.green)
		color_mask |= vk::ColorComponentFlagBits::eG;

	if (blend_mode.color_mask.blue)
		color_mask |= vk::ColorComponentFlagBits::eB;

	if (blend_mode.color_mask.alpha)
		color_mask |= vk::ColorComponentFlagBits::eA;

	auto color_blend_equation = vk::ColorBlendEquationEXT()
		.setSrcColorBlendFactor(BlendFactorMap.at(blend_mode.color_src))
		.setDstColorBlendFactor(BlendFactorMap.at(blend_mode.color_dst))
		.setColorBlendOp(BlendFuncMap.at(blend_mode.color_func))
		.setSrcAlphaBlendFactor(BlendFactorMap.at(blend_mode.alpha_src))
		.setDstAlphaBlendFactor(BlendFactorMap.at(blend_mode.alpha_dst))
		.setAlphaBlendOp(BlendFuncMap.at(blend_mode.alpha_func));

	std::vector<uint32_t> blend_enable_array;
	std::vector<vk::ColorComponentFlags> color_mask_array;
	std::vector<vk::ColorBlendEquationEXT> color_blend_equation_array;

	if (gContext->render_targets.empty())
	{
		blend_enable_array = { gContext->blend_mode.has_value() };
		color_mask_array = { color_mask };
		color_blend_equation_array = { color_blend_equation };
	}
	else
	{
		for (size_t i = 0; i < gContext->render_targets.size(); i++)
		{
			blend_enable_array.push_back(gContext->blend_mode.has_value());
			color_mask_array.push_back(color_mask);
			color_blend_equation_array.push_back(color_blend_equation);
		}
	}

	cmdlist.setColorBlendEnableEXT(0, blend_enable_array);
	cmdlist.setColorWriteMaskEXT(0, color_mask_array);
	cmdlist.setColorBlendEquationEXT(0, color_blend_equation_array);
}

static void EnsureDepthMode(vk::raii::CommandBuffer& cmdlist)
{
	if (!gContext->depth_mode_dirty)
		return;

	gContext->depth_mode_dirty = false;

	if (gContext->depth_mode.has_value())
	{
		cmdlist.setDepthTestEnable(true);
		cmdlist.setDepthWriteEnable(gContext->depth_mode.value().write_mask);
		cmdlist.setDepthCompareOp(CompareOpMap.at(gContext->depth_mode.value().func));
	}
	else
	{
		cmdlist.setDepthTestEnable(false);
		cmdlist.setDepthWriteEnable(false);
		cmdlist.setDepthCompareOp(vk::CompareOp::eAlways);
	}
}

static void EnsureStencilMode(vk::raii::CommandBuffer& cmdlist)
{
	if (!gContext->stencil_mode_dirty)
		return;

	gContext->stencil_mode_dirty = false;

	cmdlist.setStencilTestEnable(gContext->stencil_mode.has_value());
}

static void EnsureGraphicsPipelineState(vk::raii::CommandBuffer& cmdlist)
{
	if (!gContext->pipeline_state_dirty)
		return;

	gContext->pipeline_state_dirty = false;

	if (!gContext->pipeline_states.contains(gContext->pipeline_state))
	{
		auto pipeline = CreateGraphicsPipeline(gContext->pipeline_state);
		gContext->pipeline_states.insert({ gContext->pipeline_state, std::move(pipeline) });
	}

	const auto& pipeline = gContext->pipeline_states.at(gContext->pipeline_state);
	cmdlist.bindPipeline(vk::PipelineBindPoint::eGraphics, *pipeline);

	gContext->graphics_pipeline_ignore_bindings.clear();
}

static void EnsureRaytracingPipelineState(vk::raii::CommandBuffer& cmdlist)
{
	if (!gContext->raytracing_pipeline_states.contains(gContext->raytracing_pipeline_state))
	{
		auto pipeline = CreateRaytracingPipeline(gContext->raytracing_pipeline_state);
		gContext->raytracing_pipeline_states.insert({ gContext->raytracing_pipeline_state, std::move(pipeline) });
	}

	const auto& pipeline = gContext->raytracing_pipeline_states.at(gContext->raytracing_pipeline_state);
	cmdlist.bindPipeline(vk::PipelineBindPoint::eRayTracingKHR, *pipeline);
}

static void EnsureGraphicsDescriptors(vk::raii::CommandBuffer& cmdlist)
{
	const auto& pipeline_layout = gContext->pipeline_state.shader->getPipelineLayout();
	const auto& required_descriptor_bindings = gContext->pipeline_state.shader->getRequiredDescriptorBindings();
	auto& ignore_bindings = gContext->graphics_pipeline_ignore_bindings;

	PushDescriptors(cmdlist, vk::PipelineBindPoint::eGraphics, pipeline_layout, required_descriptor_bindings, ignore_bindings);

	for (const auto& descriptor_binding : required_descriptor_bindings)
	{
		ignore_bindings.insert(descriptor_binding.binding);
	}
}

static void EnsureRaytracingDescriptors(vk::raii::CommandBuffer& cmdlist)
{
	const auto& pipeline_layout = gContext->raytracing_pipeline_state.shader->getPipelineLayout();
	const auto& required_descriptor_bindings = gContext->raytracing_pipeline_state.shader->getRequiredDescriptorBindings();

	PushDescriptors(cmdlist, vk::PipelineBindPoint::eRayTracingKHR, pipeline_layout, required_descriptor_bindings);
}

static void EnsureGraphicsState(bool draw_indexed)
{
	auto& cmdlist = gContext->getCurrentFrame().command_buffer;

	EnsureMemoryState(cmdlist, vk::PipelineStageFlagBits2::eAllGraphics);
	EnsureGraphicsPipelineState(cmdlist);
	EnsureGraphicsDescriptors(cmdlist);
	EnsureVertexBuffer(cmdlist);

	if (draw_indexed)
		EnsureIndexBuffer(cmdlist);

	EnsureTopology(cmdlist);
	EnsureViewport(cmdlist);
	EnsureScissor(cmdlist);
	EnsureCullMode(cmdlist);
	EnsureFrontFace(cmdlist);
	EnsureBlendMode(cmdlist);
	EnsureDepthMode(cmdlist);
	EnsureStencilMode(cmdlist);
	EnsureRenderPassActivated();
}

static void EnsureRaytracingState()
{
	auto& cmdlist = gContext->getCurrentFrame().command_buffer;

	EnsureRenderPassDeactivated();
	EnsureMemoryState(cmdlist, vk::PipelineStageFlagBits2::eAllGraphics);
	EnsureRaytracingPipelineState(cmdlist);
	EnsureRaytracingDescriptors(cmdlist);
}

static void WaitForGpu()
{
	const auto& fence = gContext->getCurrentFrame().fence;
	auto wait_result = gContext->device.waitForFences({ *fence }, true, UINT64_MAX);
	ReleaseStaging();
}

static void CreateSwapchain(uint32_t width, uint32_t height)
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

	auto format = gContext->surface_format.format;

	auto swapchain_info = vk::SwapchainCreateInfoKHR()
		.setSurface(*gContext->surface)
		.setMinImageCount(desired_number_of_swapchain_images)
		.setImageFormat(format)
		.setImageColorSpace(gContext->surface_format.colorSpace)
		.setImageExtent(image_extent)
		.setImageUsage(vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferSrc)
		.setPreTransform(vk::SurfaceTransformFlagBitsKHR::eIdentity)
		.setImageArrayLayers(1)
		.setImageSharingMode(vk::SharingMode::eExclusive)
		.setQueueFamilyIndices(gContext->queue_family_index)
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

		auto command_buffer_allocate_info = vk::CommandBufferAllocateInfo()
			.setCommandBufferCount(1)
			.setLevel(vk::CommandBufferLevel::ePrimary)
			.setCommandPool(*gContext->command_pool);

		auto command_buffers = gContext->device.allocateCommandBuffers(command_buffer_allocate_info);

		frame.command_buffer = std::move(command_buffers.at(0));

		frame.swapchain_texture = std::make_shared<TextureVK>(gContext->width, gContext->height, format, backbuffer);
		frame.swapchain_target = std::make_shared<RenderTargetVK>(gContext->width, gContext->height, frame.swapchain_texture.get());

		gContext->frames.push_back(std::move(frame));
	}

	gContext->frame_index = 0;
	gContext->semaphore_index = 0;
}

static void MoveToNextFrame()
{
	const auto& image_acquired_semaphore = gContext->frames.at(gContext->semaphore_index).image_acquired_semaphore;

	auto [result, image_index] = gContext->swapchain.acquireNextImage(UINT64_MAX, *image_acquired_semaphore);

	gContext->frame_index = image_index;
}

static void Begin()
{
	assert(!gContext->working);
	gContext->working = true;

	gContext->pipeline_state_dirty = true;
	gContext->topology_dirty = true;
	gContext->viewport_dirty = true;
	gContext->scissor_dirty = true;
	gContext->cull_mode_dirty = true;
	gContext->front_face_dirty = true;
	gContext->vertex_buffer_dirty = true;
	gContext->index_buffer_dirty = true;
	gContext->blend_mode_dirty = true;
	gContext->depth_mode_dirty = true;
	gContext->stencil_mode_dirty = true;

	auto begin_info = vk::CommandBufferBeginInfo()
		.setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit);

	gContext->getCurrentFrame().command_buffer.begin(begin_info);
}

static void End()
{
	assert(gContext->working);
	gContext->working = false;

	EnsureRenderPassDeactivated();

	gContext->getCurrentFrame().swapchain_texture->ensureState(gContext->getCurrentFrame().command_buffer,
		vk::ImageLayout::ePresentSrcKHR);

	gContext->getCurrentFrame().command_buffer.end();

	const auto& frame = gContext->getCurrentFrame();

	gContext->device.resetFences({ *frame.fence });

	auto wait_dst_stage_mask = vk::PipelineStageFlags{
		vk::PipelineStageFlagBits::eAllCommands
	};

	auto submit_info = vk::SubmitInfo()
		.setWaitDstStageMask(wait_dst_stage_mask)
		.setWaitSemaphores(*frame.image_acquired_semaphore)
		.setCommandBuffers(*frame.command_buffer)
		.setSignalSemaphores(*frame.render_complete_semaphore);

	gContext->queue.submit(submit_info, *frame.fence);
}

#if defined(SKYGFX_VULKAN_VALIDATION_ENABLED)
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
	std::cerr << "\t" << "messageIdName   = <" << pCallbackData->pMessageIdName << ">\n";
	std::cerr << "\t" << "messageIdNumber = " << pCallbackData->messageIdNumber << "\n";
	std::cerr << "\t" << "message         = <" << pCallbackData->pMessage << ">\n";
	if (pCallbackData->queueLabelCount > 0)
	{
		std::cerr << "\t" << "Queue Labels:\n";
		for (uint32_t i = 0; i < pCallbackData->queueLabelCount; i++)
		{
			std::cerr << "\t\t" << "labelName = <" << pCallbackData->pQueueLabels[i].pLabelName << ">\n";
		}
	}
	if (pCallbackData->cmdBufLabelCount > 0)
	{
		std::cerr << "\t" << "CommandBuffer Labels:\n";
		for (uint32_t i = 0; i < pCallbackData->cmdBufLabelCount; i++)
		{
			std::cerr << "\t\t" << "labelName = <" << pCallbackData->pCmdBufLabels[i].pLabelName << ">\n";
		}
	}
	if (pCallbackData->objectCount > 0)
	{
		std::cerr << "\t" << "Objects:\n";
		for (uint32_t i = 0; i < pCallbackData->objectCount; i++)
		{
			std::cerr << "\t\t" << "Object " << i << "\n";
			std::cerr << "\t\t\t" << "objectType   = " << vk::to_string(static_cast<vk::ObjectType>(pCallbackData->pObjects[i].objectType))
				<< "\n";
			std::cerr << "\t\t\t" << "objectHandle = " << pCallbackData->pObjects[i].objectHandle << "\n";
			if (pCallbackData->pObjects[i].pObjectName)
			{
				std::cerr << "\t\t\t" << "objectName   = <" << pCallbackData->pObjects[i].pObjectName << ">\n";
			}
		}
	}
	return VK_TRUE;
}
#endif

BackendVK::BackendVK(void* window, uint32_t width, uint32_t height, Adapter adapter,
	const std::unordered_set<Feature>& features)
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
#if defined(SKYGFX_VULKAN_VALIDATION_ENABLED)
		VK_EXT_DEBUG_UTILS_EXTENSION_NAME,
#endif
	};
	
#if defined(SKYGFX_VULKAN_VALIDATION_ENABLED)
	auto layers = {
		"VK_LAYER_KHRONOS_validation"
	};
#endif

	auto version = gContext->context.enumerateInstanceVersion();

	auto major_version = VK_API_VERSION_MAJOR(version);
	auto minor_version = VK_API_VERSION_MINOR(version);
	auto patch_version = VK_API_VERSION_PATCH(version);

	//std::cout << "available vulkan version: " << major_version << "." << minor_version << std::endl;

	auto application_info = vk::ApplicationInfo()
		.setApiVersion(VK_API_VERSION_1_3);

	auto instance_create_info = vk::InstanceCreateInfo()
		.setPEnabledExtensionNames(extensions)
#if defined(SKYGFX_VULKAN_VALIDATION_ENABLED)
		.setPEnabledLayerNames(layers)
#endif
		.setPApplicationInfo(&application_info);

#if defined(SKYGFX_VULKAN_VALIDATION_ENABLED)
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
#endif

	auto instance_create_info_chain = vk::StructureChain<
		vk::InstanceCreateInfo
#if defined(SKYGFX_VULKAN_VALIDATION_ENABLED)
		,
		vk::DebugUtilsMessengerCreateInfoEXT,
		vk::ValidationFeaturesEXT
#endif
	>(
		instance_create_info
#if defined(SKYGFX_VULKAN_VALIDATION_ENABLED)
		,
		debug_utils_messenger_create_info,
		validation_features
#endif
	);

	gContext->instance = gContext->context.createInstance(instance_create_info_chain.get<vk::InstanceCreateInfo>());

#if defined(SKYGFX_VULKAN_VALIDATION_ENABLED)
	gContext->debug_utils_messenger = gContext->instance.createDebugUtilsMessengerEXT(debug_utils_messenger_create_info);
#endif

	auto devices = gContext->instance.enumeratePhysicalDevices();
	size_t device_index = 0;
	auto preferred_device_type = adapter == Adapter::HighPerformance ? vk::PhysicalDeviceType::eDiscreteGpu : vk::PhysicalDeviceType::eIntegratedGpu;
	for (size_t i = 0; i < devices.size(); i++)
	{
		auto properties = devices.at(i).getProperties();
		if (properties.deviceType == preferred_device_type)
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

	std::vector device_extensions = {
		VK_KHR_SWAPCHAIN_EXTENSION_NAME,

		// dynamic pipeline
		VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME,
		VK_EXT_EXTENDED_DYNAMIC_STATE_3_EXTENSION_NAME,
	};

	if (features.contains(Feature::Raytracing))
	{
		device_extensions.push_back(VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME);
		device_extensions.push_back(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME);
		device_extensions.push_back(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME);
	}

	auto queue_priority = { 1.0f };

	auto queue_info = vk::DeviceQueueCreateInfo()
		.setQueueFamilyIndex(gContext->queue_family_index)
		.setQueuePriorities(queue_priority);

	auto default_device_features = gContext->physical_device.getFeatures2<
		vk::PhysicalDeviceFeatures2,
		vk::PhysicalDeviceVulkan13Features,
		vk::PhysicalDeviceExtendedDynamicState3FeaturesEXT
	>();

	auto raytracing_device_features = gContext->physical_device.getFeatures2<
		vk::PhysicalDeviceFeatures2,
		vk::PhysicalDeviceVulkan13Features,
		vk::PhysicalDeviceExtendedDynamicState3FeaturesEXT,
		vk::PhysicalDeviceBufferAddressFeaturesEXT,
		vk::PhysicalDeviceRayTracingPipelineFeaturesKHR,
		vk::PhysicalDeviceAccelerationStructureFeaturesKHR
	>();

	auto device_info = vk::DeviceCreateInfo()
		.setQueueCreateInfos(queue_info)
		.setPEnabledExtensionNames(device_extensions)
		.setPEnabledFeatures(nullptr);

	if (features.contains(Feature::Raytracing))
		device_info.setPNext(&raytracing_device_features.get<vk::PhysicalDeviceFeatures2>());
	else
		device_info.setPNext(&default_device_features.get<vk::PhysicalDeviceFeatures2>());

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

	gContext->pipeline_state.color_attachment_formats = { gContext->surface_format.format };
	gContext->pipeline_state.depth_stencil_format = ContextVK::DefaultDepthStencilFormat;

	CreateSwapchain(width, height);
	MoveToNextFrame();
	Begin();
}

BackendVK::~BackendVK()
{
	End();
	WaitForGpu();

	delete gContext;
	gContext = nullptr;
}

void BackendVK::resize(uint32_t width, uint32_t height)
{
	End();
	WaitForGpu();
	CreateSwapchain(width, height);
	MoveToNextFrame();
	Begin();
}

void BackendVK::setVsync(bool value)
{
	// TODO: implement
}

void BackendVK::setTopology(Topology topology)
{
	gContext->topology = topology;
	gContext->topology_dirty = true;
}

void BackendVK::setViewport(std::optional<Viewport> viewport)
{
	gContext->viewport = viewport;
	gContext->viewport_dirty = true;
}

void BackendVK::setScissor(std::optional<Scissor> scissor)
{
	gContext->scissor = scissor;
	gContext->scissor_dirty = true;
}

void BackendVK::setTexture(uint32_t binding, TextureHandle* handle)
{
	gContext->textures[binding] = (TextureVK*)handle;
	gContext->graphics_pipeline_ignore_bindings.erase(binding);
}

void BackendVK::setRenderTarget(const std::vector<RenderTargetHandle*>& handles)
{
	std::vector<RenderTargetVK*> render_targets;
	std::vector<vk::Format> color_attachment_formats;
	std::optional<vk::Format> depth_stencil_format;

	for (auto handle : handles)
	{
		auto render_target = (RenderTargetVK*)handle;
		render_targets.push_back(render_target);
		color_attachment_formats.push_back(render_target->getTexture()->getFormat());

		if (!depth_stencil_format.has_value())
			depth_stencil_format = render_target->getDepthStencilFormat();
	}

	if (gContext->render_targets.size() != render_targets.size())
		gContext->blend_mode_dirty = true;

	gContext->pipeline_state_dirty = true;
	gContext->pipeline_state.color_attachment_formats = color_attachment_formats;
	gContext->pipeline_state.depth_stencil_format = depth_stencil_format;
	gContext->render_targets = render_targets;
	EnsureRenderPassDeactivated();

	if (!gContext->viewport.has_value())
		gContext->viewport_dirty = true;

	if (!gContext->scissor.has_value())
		gContext->scissor_dirty = true;
}

void BackendVK::setRenderTarget(std::nullopt_t value)
{
	gContext->pipeline_state_dirty = true;
	gContext->pipeline_state.color_attachment_formats = { gContext->surface_format.format };
	gContext->pipeline_state.depth_stencil_format = ContextVK::DefaultDepthStencilFormat;
	gContext->render_targets.clear();
	EnsureRenderPassDeactivated();

	if (!gContext->viewport.has_value())
		gContext->viewport_dirty = true;

	if (!gContext->scissor.has_value())
		gContext->scissor_dirty = true;
}

void BackendVK::setShader(ShaderHandle* handle)
{
	gContext->pipeline_state.shader = (ShaderVK*)handle;
	gContext->pipeline_state_dirty = true;
}

void BackendVK::setInputLayout(const InputLayout& value)
{
	gContext->pipeline_state.input_layout = value;
	gContext->pipeline_state_dirty = true;
}

void BackendVK::setRaytracingShader(RaytracingShaderHandle* handle)
{
	auto shader = (RaytracingShaderVK*)handle;
	gContext->raytracing_pipeline_state.shader = shader;
}

void BackendVK::setVertexBuffer(VertexBufferHandle* handle)
{
	gContext->vertex_buffer = (VertexBufferVK*)handle;
	gContext->vertex_buffer_dirty = true;
}

void BackendVK::setIndexBuffer(IndexBufferHandle* handle)
{
	gContext->index_buffer = (IndexBufferVK*)handle;
	gContext->index_buffer_dirty = true;
}

void BackendVK::setUniformBuffer(uint32_t binding, UniformBufferHandle* handle)
{
	gContext->uniform_buffers[binding] = (UniformBufferVK*)handle;
	gContext->graphics_pipeline_ignore_bindings.erase(binding);
}

void BackendVK::setStorageBuffer(uint32_t binding, StorageBufferHandle* handle)
{
	gContext->storage_buffers[binding] = (StorageBufferVK*)handle;
}

void BackendVK::setAccelerationStructure(uint32_t binding, TopLevelAccelerationStructureHandle* handle)
{
	gContext->top_level_acceleration_structures[binding] = (TopLevelAccelerationStructureVK*)handle;
}

void BackendVK::setBlendMode(const std::optional<BlendMode>& value)
{
	gContext->blend_mode = value;
	gContext->blend_mode_dirty = true;
}

void BackendVK::setDepthMode(const std::optional<DepthMode>& depth_mode)
{
	gContext->depth_mode = depth_mode;
	gContext->depth_mode_dirty = true;
}

void BackendVK::setStencilMode(const std::optional<StencilMode>& stencil_mode)
{
	gContext->stencil_mode_dirty = true;
	gContext->stencil_mode = stencil_mode;
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

void BackendVK::setFrontFace(FrontFace value)
{
	gContext->front_face = value;
	gContext->front_face_dirty = true;
}

void BackendVK::setDepthBias(const std::optional<DepthBias> depth_bias)
{
}

void BackendVK::clear(const std::optional<glm::vec4>& color, const std::optional<float>& depth,
	const std::optional<uint8_t>& stencil)
{
	EnsureRenderPassActivated();

	auto width = gContext->getBackbufferWidth();
	auto height = gContext->getBackbufferHeight();

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
			.setColorAttachment(0) // TODO: clear all attachments
			.setClearValue(clear_value);

		gContext->getCurrentFrame().command_buffer.clearAttachments({ attachment }, { clear_rect });
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

		gContext->getCurrentFrame().command_buffer.clearAttachments({ attachment }, { clear_rect });
	}
}

void BackendVK::draw(uint32_t vertex_count, uint32_t vertex_offset, uint32_t instance_count)
{
	EnsureGraphicsState(false);
	gContext->getCurrentFrame().command_buffer.draw(vertex_count, instance_count, vertex_offset, 0);
}

void BackendVK::drawIndexed(uint32_t index_count, uint32_t index_offset, uint32_t instance_count)
{
	EnsureGraphicsState(true);
	gContext->getCurrentFrame().command_buffer.drawIndexed(index_count, instance_count, index_offset, 0, 0);
}

void BackendVK::readPixels(const glm::i32vec2& pos, const glm::i32vec2& size, TextureHandle* dst_texture_handle)
{
	auto dst_texture = (TextureVK*)dst_texture_handle;
	auto dst_format = gContext->getBackbufferFormat();

	assert(dst_texture->getWidth() == size.x);
	assert(dst_texture->getHeight() == size.y);
	assert(dst_texture->getFormat() == dst_format);

	if (size.x <= 0 || size.y <= 0)
		return;

	EnsureRenderPassDeactivated();

	auto pos_x = static_cast<int32_t>(pos.x);
	auto pos_y = static_cast<int32_t>(pos.y);
	auto width = static_cast<uint32_t>(size.x);
	auto height = static_cast<uint32_t>(size.y);

	auto src_target = !gContext->render_targets.empty() ? gContext->render_targets.at(0) : gContext->getCurrentFrame().swapchain_target.get();
	auto src_texture = src_target->getTexture();
	auto src_image = src_texture->getImage();
	auto dst_image = dst_texture->getImage();

	auto subresource = vk::ImageSubresourceLayers()
		.setAspectMask(vk::ImageAspectFlagBits::eColor)
		.setLayerCount(1);

	auto region = vk::ImageCopy2()
		.setSrcSubresource(subresource)
		.setDstSubresource(subresource)
		.setSrcOffset({ pos_x, pos_y, 0 })
		.setDstOffset({ 0, 0, 0 })
		.setExtent({ width, height, 1 });

	src_texture->ensureState(gContext->getCurrentFrame().command_buffer, vk::ImageLayout::eTransferSrcOptimal);
	dst_texture->ensureState(gContext->getCurrentFrame().command_buffer, vk::ImageLayout::eTransferDstOptimal);

	auto copy_image_info = vk::CopyImageInfo2()
		.setSrcImage(src_image)
		.setDstImage(dst_image)
		.setSrcImageLayout(vk::ImageLayout::eTransferSrcOptimal)
		.setDstImageLayout(vk::ImageLayout::eTransferDstOptimal)
		.setRegions(region);

	gContext->getCurrentFrame().command_buffer.copyImage2(copy_image_info);
}

void BackendVK::dispatchRays(uint32_t width, uint32_t height, uint32_t depth)
{
	assert(!gContext->render_targets.empty());

	EnsureRaytracingState();

	const auto& pipeline = gContext->raytracing_pipeline_states.at(gContext->raytracing_pipeline_state);
	static auto binding_table = CreateRaytracingShaderBindingTable(pipeline);

	gContext->getCurrentFrame().command_buffer.traceRaysKHR(binding_table.raygen_address, binding_table.miss_address,
		binding_table.hit_address, binding_table.callable_address, width, height, depth);
}

void BackendVK::present()
{
	End();

	const auto& render_complete_semaphore = gContext->getCurrentFrame().render_complete_semaphore;

	auto present_info = vk::PresentInfoKHR()
		.setWaitSemaphores(*render_complete_semaphore)
		.setSwapchains(*gContext->swapchain)
		.setImageIndices(gContext->frame_index);

	auto present_result = gContext->queue.presentKHR(present_info);

	gContext->semaphore_index = (gContext->semaphore_index + 1) % gContext->frames.size();

	MoveToNextFrame();
	WaitForGpu();
	Begin();
}

TextureHandle* BackendVK::createTexture(uint32_t width, uint32_t height, Format format,
	uint32_t mip_count)
{
	auto texture = new TextureVK(width, height, FormatMap.at(format), mip_count);
	gContext->objects.insert(texture);
	return (TextureHandle*)texture;
}

void BackendVK::writeTexturePixels(TextureHandle* handle, uint32_t width, uint32_t height, Format format, void* memory,
	uint32_t mip_level, uint32_t offset_x, uint32_t offset_y)
{
	auto texture = (TextureVK*)handle;
	texture->write(width, height, format, memory, mip_level, offset_x, offset_y);
}

void BackendVK::readTexturePixels(TextureHandle* handle, uint32_t pos_x, uint32_t pos_y, uint32_t width, uint32_t height,
	uint32_t mip_level, void* dst_memory)
{
	auto texture = (TextureVK*)handle;
	texture->read(pos_x, pos_y, width, height, mip_level, dst_memory);
}

void BackendVK::generateMips(TextureHandle* handle)
{
	auto texture = (TextureVK*)handle;
	texture->generateMips();
}

void BackendVK::destroyTexture(TextureHandle* handle)
{
	auto texture = (TextureVK*)handle;
	
	std::erase_if(gContext->textures, [&](const auto& item) {
		const auto& [binding, _texture] = item;
		return texture == _texture;
	});

	gContext->objects.erase(texture);
	delete texture;
}

RenderTargetHandle* BackendVK::createRenderTarget(uint32_t width, uint32_t height, TextureHandle* texture_handle)
{
	auto texture = (TextureVK*)texture_handle;
	auto render_target = new RenderTargetVK(width, height, texture);
	gContext->objects.insert(render_target);
	return (RenderTargetHandle*)render_target;
}

void BackendVK::destroyRenderTarget(RenderTargetHandle* handle)
{
	auto render_target = (RenderTargetVK*)handle;
	gContext->objects.erase(render_target);
	delete render_target;
}

ShaderHandle* BackendVK::createShader(const std::string& vertex_code, const std::string& fragment_code,
	const std::vector<std::string>& defines)
{
	auto shader = new ShaderVK(vertex_code, fragment_code, defines);
	gContext->objects.insert(shader);
	return (ShaderHandle*)shader;
}

void BackendVK::destroyShader(ShaderHandle* handle)
{
	auto shader = (ShaderVK*)handle;

	for (auto& [state, pipeline] : gContext->pipeline_states)
	{
		if (state.shader != shader)
			continue;

		DestroyStaging(std::move(pipeline));
	}

	std::erase_if(gContext->pipeline_states, [&](const auto& item) {
		const auto& [state, pipeline] = item;
		return state.shader == shader;
	});

	gContext->objects.erase(shader);
	delete shader;
}

RaytracingShaderHandle* BackendVK::createRaytracingShader(const std::string& raygen_code, const std::vector<std::string>& miss_code,
	const std::string& closesthit_code, const std::vector<std::string>& defines)
{
	auto shader = new RaytracingShaderVK(raygen_code, miss_code, closesthit_code, defines);
	gContext->objects.insert(shader);
	return (RaytracingShaderHandle*)shader;
}

void BackendVK::destroyRaytracingShader(RaytracingShaderHandle* handle)
{
	auto shader = (RaytracingShaderVK*)handle;

	std::erase_if(gContext->raytracing_pipeline_states, [&](const auto& item) {
		const auto& [state, pipeline] = item;
		return state.shader == shader;
	});

	gContext->objects.erase(shader);
	delete shader;
}

VertexBufferHandle* BackendVK::createVertexBuffer(size_t size, size_t stride)
{
	auto buffer = new VertexBufferVK(size, stride);
	gContext->objects.insert(buffer);
	return (VertexBufferHandle*)buffer;
}

void BackendVK::destroyVertexBuffer(VertexBufferHandle* handle)
{
	auto buffer = (VertexBufferVK*)handle;
	gContext->objects.erase(buffer);
	delete buffer;
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
	gContext->objects.insert(buffer);
	return (IndexBufferHandle*)buffer;
}

void BackendVK::destroyIndexBuffer(IndexBufferHandle* handle)
{
	auto buffer = (IndexBufferVK*)handle;
	gContext->objects.erase(buffer);
	delete buffer;
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
	gContext->objects.insert(buffer);
	return (UniformBufferHandle*)buffer;
}

void BackendVK::destroyUniformBuffer(UniformBufferHandle* handle)
{
	auto buffer = (UniformBufferVK*)handle;

	std::erase_if(gContext->uniform_buffers, [&](const auto& item) {
		const auto& [binding, _buffer] = item;
		return buffer == _buffer;
	});

	gContext->objects.erase(buffer);
	delete buffer;
}

void BackendVK::writeUniformBufferMemory(UniformBufferHandle* handle, void* memory, size_t size)
{
	auto buffer = (UniformBufferVK*)handle;
	buffer->write(memory, size);
}

BottomLevelAccelerationStructureHandle* BackendVK::createBottomLevelAccelerationStructure(void* vertex_memory,
	uint32_t vertex_count, uint32_t vertex_stride, void* index_memory, uint32_t index_count,
	uint32_t index_stride, const glm::mat4& transform)
{
	auto bottom_level_acceleration_structure = new BottomLevelAccelerationStructureVK(vertex_memory,
		vertex_count, vertex_stride, index_memory, index_count, index_stride, transform);
	gContext->objects.insert(bottom_level_acceleration_structure);
	return (BottomLevelAccelerationStructureHandle*)bottom_level_acceleration_structure;
}

void BackendVK::destroyBottomLevelAccelerationStructure(BottomLevelAccelerationStructureHandle* handle)
{
	auto bottom_level_acceleration_structure = (BottomLevelAccelerationStructureVK*)handle;
	gContext->objects.erase(bottom_level_acceleration_structure);
	delete bottom_level_acceleration_structure;
}

TopLevelAccelerationStructureHandle* BackendVK::createTopLevelAccelerationStructure(
	const std::vector<std::tuple<uint32_t, BottomLevelAccelerationStructureHandle*>>& bottom_level_acceleration_structures)
{
	auto top_level_acceleration_structure = new TopLevelAccelerationStructureVK(bottom_level_acceleration_structures);
	gContext->objects.insert(top_level_acceleration_structure);
	return (TopLevelAccelerationStructureHandle*)top_level_acceleration_structure;
}

void BackendVK::destroyTopLevelAccelerationStructure(TopLevelAccelerationStructureHandle* handle)
{
	auto top_level_acceleration_structure = (TopLevelAccelerationStructureVK*)handle;

	std::erase_if(gContext->top_level_acceleration_structures, [&](const auto& item) {
		const auto& [binding, _acceleration_structure] = item;
		return top_level_acceleration_structure == _acceleration_structure;
	});

	gContext->objects.erase(top_level_acceleration_structure);
	delete top_level_acceleration_structure;
}

StorageBufferHandle* BackendVK::createStorageBuffer(size_t size)
{
	auto buffer = new StorageBufferVK(size);
	gContext->objects.insert(buffer);
	return (StorageBufferHandle*)buffer;
}

void BackendVK::destroyStorageBuffer(StorageBufferHandle* handle)
{
	auto buffer = (StorageBufferVK*)handle;

	std::erase_if(gContext->storage_buffers, [&](const auto& item) {
		const auto& [binding, _buffer] = item;
		return buffer == _buffer;
	});

	gContext->objects.erase(buffer);
	delete buffer;
}

void BackendVK::writeStorageBufferMemory(StorageBufferHandle* handle, void* memory, size_t size)
{
	auto buffer = (StorageBufferVK*)handle;
	buffer->write(memory, size);
}

#endif
