#include "backend_webgpu.h"

#ifdef SKYGFX_HAS_WEBGPU

#include "shader_compiler.h"

#define WEBGPU_CPP_IMPLEMENTATION
#include <webgpu/webgpu.hpp>

#ifdef SKYGFX_PLATFORM_WINDOWS
#include <Windows.h>
#endif

using namespace skygfx;

class ShaderWebGPU;

struct PipelineStateWebGPU
{
	ShaderWebGPU* shader = nullptr;

	bool operator==(const PipelineStateWebGPU& other) const = default;
};

SKYGFX_MAKE_HASHABLE(PipelineStateWebGPU,
	t.shader
);

struct ContextWebGPU
{
	wgpu::Instance instance = nullptr;
	wgpu::Surface surface = nullptr;
	wgpu::SwapChain swapchain = nullptr;
	wgpu::Device device = nullptr;
	wgpu::Queue queue = nullptr;
	wgpu::TextureFormat swapchain_format = wgpu::TextureFormat::Undefined;

	wgpu::CommandEncoder command_encoder = nullptr;
	wgpu::RenderPassEncoder render_pass_encoder = nullptr;

	std::unordered_map<PipelineStateWebGPU, wgpu::RenderPipeline> pipeline_states;

	PipelineStateWebGPU pipeline_state;
	bool pipeline_state_dirty = true;

	std::unique_ptr<wgpu::ErrorCallback> uncaptured_error_callback = nullptr;

	wgpu::TextureView backbuffer_texture_view = nullptr;
};

static ContextWebGPU* gContext = nullptr;

class ShaderWebGPU
{
private:
	wgpu::ShaderModule mShaderModule = nullptr;

public:
	const wgpu::ShaderModule& getShaderModule() const { return mShaderModule; }

public:
	ShaderWebGPU(const std::string& vertex_code, const std::string& fragment_code,
		std::vector<std::string> defines)
	{
		//auto vertex_shader_spirv = CompileGlslToSpirv(ShaderStage::Vertex, vertex_code, defines);
		//auto fragment_shader_spirv = CompileGlslToSpirv(ShaderStage::Fragment, fragment_code, defines);

		const char* shaderSource = R"(
			@vertex
			fn vs_main(@builtin(vertex_index) in_vertex_index: u32) -> @builtin(position) vec4<f32> {
				var p = vec2f(0.0, 0.0);
				if (in_vertex_index == 0u) {
					p = vec2f(-0.5, -0.5);
				} else if (in_vertex_index == 1u) {
					p = vec2f(0.5, -0.5);
				} else {
					p = vec2f(0.0, 0.5);
				}
				return vec4f(p, 0.0, 1.0);
			}

			@fragment
			fn fs_main() -> @location(0) vec4f {
				return vec4f(0.0, 0.4, 1.0, 1.0);
			})";

		wgpu::ShaderModuleWGSLDescriptor wgsl_desc;
		wgsl_desc.chain.next = nullptr;
		wgsl_desc.chain.sType = wgpu::SType::ShaderModuleWGSLDescriptor;
		wgsl_desc.code = shaderSource;

		wgpu::ShaderModuleDescriptor desc;
		desc.nextInChain = &wgsl_desc.chain;
#ifdef WEBGPU_BACKEND_WGPU
		desc.hintCount = 0;
		desc.hints = nullptr;
#endif

		mShaderModule = gContext->device.createShaderModule(desc);
	}
};

static void BeginRenderPass()
{
	assert(gContext->render_pass_encoder == nullptr);

	wgpu::RenderPassColorAttachment color_attachment;
	color_attachment.view = gContext->backbuffer_texture_view;
	color_attachment.resolveTarget = nullptr;
	color_attachment.loadOp = wgpu::LoadOp::Clear;
	color_attachment.storeOp = wgpu::StoreOp::Store;
	color_attachment.clearValue = wgpu::Color{ 0.9, 0.1, 0.2, 1.0 };

	wgpu::RenderPassDescriptor desc;
	desc.colorAttachmentCount = 1;
	desc.colorAttachments = &color_attachment;
	desc.depthStencilAttachment = nullptr;
	desc.timestampWriteCount = 0;
	desc.timestampWrites = nullptr;

	gContext->render_pass_encoder = gContext->command_encoder.beginRenderPass(desc);
}

static void EndRenderPass()
{
	assert(gContext->render_pass_encoder != nullptr);
	gContext->render_pass_encoder.end();
	gContext->render_pass_encoder.release();
	gContext->render_pass_encoder = nullptr;
}

static void EnsureRenderPassActivated()
{
	if (gContext->render_pass_encoder != nullptr)
		return;

	BeginRenderPass();
}

static void EnsureRenderPassDeactivated()
{
	if (gContext->render_pass_encoder == nullptr)
		return;

	EndRenderPass();
}

static void Begin()
{
	assert(gContext->command_encoder == nullptr);
	gContext->command_encoder = gContext->device.createCommandEncoder(wgpu::CommandEncoderDescriptor{});

	gContext->backbuffer_texture_view = gContext->swapchain.getCurrentTextureView();

	gContext->pipeline_state_dirty = true;
}

static void End()
{
	assert(gContext->command_encoder != nullptr);

	EnsureRenderPassDeactivated();

	gContext->backbuffer_texture_view.release();
	gContext->backbuffer_texture_view = nullptr;

	auto command = gContext->command_encoder.finish(wgpu::CommandBufferDescriptor{});
	gContext->command_encoder.release();
	gContext->queue.submit(command);
	command.release();

	gContext->command_encoder = nullptr;
}

static wgpu::RenderPipeline CreateGraphicsPipeline(const PipelineStateWebGPU& pipeline_state)
{
	wgpu::BlendState blendState;
	blendState.color.srcFactor = wgpu::BlendFactor::SrcAlpha;
	blendState.color.dstFactor = wgpu::BlendFactor::OneMinusSrcAlpha;
	blendState.color.operation = wgpu::BlendOperation::Add;
	blendState.alpha.srcFactor = wgpu::BlendFactor::Zero;
	blendState.alpha.dstFactor = wgpu::BlendFactor::One;
	blendState.alpha.operation = wgpu::BlendOperation::Add;

	wgpu::ColorTargetState colorTarget;
	colorTarget.format = gContext->swapchain_format;
	colorTarget.blend = &blendState;
	colorTarget.writeMask = wgpu::ColorWriteMask::All;

	const auto& shader_module = pipeline_state.shader->getShaderModule();

	wgpu::FragmentState fragmentState;
	fragmentState.module = shader_module;
	fragmentState.entryPoint = "fs_main";
	fragmentState.constantCount = 0;
	fragmentState.constants = nullptr;
	fragmentState.targetCount = 1;
	fragmentState.targets = &colorTarget;

	wgpu::RenderPipelineDescriptor pipelineDesc;
	pipelineDesc.vertex.bufferCount = 0;
	pipelineDesc.vertex.buffers = nullptr;
	pipelineDesc.vertex.module = shader_module;
	pipelineDesc.vertex.entryPoint = "vs_main";
	pipelineDesc.vertex.constantCount = 0;
	pipelineDesc.vertex.constants = nullptr;

	pipelineDesc.fragment = &fragmentState;

	pipelineDesc.primitive.topology = wgpu::PrimitiveTopology::TriangleList;
	pipelineDesc.primitive.stripIndexFormat = wgpu::IndexFormat::Undefined;
	pipelineDesc.primitive.frontFace = wgpu::FrontFace::CCW;
	pipelineDesc.primitive.cullMode = wgpu::CullMode::None;

	pipelineDesc.depthStencil = nullptr;
	pipelineDesc.multisample.count = 1;
	pipelineDesc.multisample.mask = ~0u;
	pipelineDesc.multisample.alphaToCoverageEnabled = false;
	pipelineDesc.layout = nullptr;

	return gContext->device.createRenderPipeline(pipelineDesc);
}

static void EnsureGraphicsPipelineState()
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
	gContext->render_pass_encoder.setPipeline(pipeline);
}

static void EnsureGraphicsState(bool draw_indexed)
{
	EnsureRenderPassActivated();
	EnsureGraphicsPipelineState();
}

static wgpu::SwapChain CreateSwapchain(uint32_t width, uint32_t height)
{
	wgpu::SwapChainDescriptor desc;
	desc.width = width;
	desc.height = height;
	desc.usage = wgpu::TextureUsage::RenderAttachment;
	desc.format = gContext->swapchain_format;
	desc.presentMode = wgpu::PresentMode::Fifo;

	return gContext->device.createSwapChain(gContext->surface, desc);
}

BackendWebGPU::BackendWebGPU(void* window, uint32_t width, uint32_t height, Adapter _adapter)
{
	gContext = new ContextWebGPU;
	gContext->instance = wgpu::createInstance(wgpu::InstanceDescriptor{});

#ifdef SKYGFX_PLATFORM_WINDOWS
	wgpu::SurfaceDescriptorFromWindowsHWND surface_desc_from;
	surface_desc_from.chain.sType = wgpu::SType::SurfaceDescriptorFromWindowsHWND;
	surface_desc_from.hwnd = window;
	surface_desc_from.hinstance = GetModuleHandle(NULL);
#endif

	wgpu::SurfaceDescriptor surface_desc = {};
	surface_desc.nextInChain = (wgpu::ChainedStruct*)&surface_desc_from;

	gContext->surface = gContext->instance.createSurface(surface_desc);

	wgpu::RequestAdapterOptions adapter_options;
	adapter_options.compatibleSurface = gContext->surface;
	adapter_options.powerPreference = _adapter == Adapter::HighPerformance ? wgpu::PowerPreference::HighPerformance : wgpu::PowerPreference::LowPower;

	auto adapter = gContext->instance.requestAdapter(adapter_options);

	wgpu::DeviceDescriptor deviceDesc;
	deviceDesc.requiredFeaturesCount = 0;
	deviceDesc.requiredLimits = nullptr;
	
	gContext->device = adapter.requestDevice(deviceDesc);

	gContext->uncaptured_error_callback = gContext->device.setUncapturedErrorCallback([](wgpu::ErrorType type, char const* message) {
		std::cout << "Device error: type " << type;
		if (message)
			std::cout << " (message: " << message << ")";
		std::cout << std::endl;
	});

	gContext->queue = gContext->device.getQueue();

	gContext->swapchain_format =
#ifdef WEBGPU_BACKEND_WGPU
		gContext->surface.getPreferredFormat(adapter);
#else
		TextureFormat::BGRA8Unorm;
#endif

	gContext->swapchain = CreateSwapchain(width, height);
	Begin();
}

BackendWebGPU::~BackendWebGPU()
{
	End();
	// TODO: release everything from gContext
	delete gContext;
}

void BackendWebGPU::resize(uint32_t width, uint32_t height)
{
	End();
	gContext->swapchain.present(); // TODO: present() is incorrect here, but without present we have crash here
	gContext->swapchain.release();
	gContext->swapchain = CreateSwapchain(width, height);
	Begin();
}

void BackendWebGPU::setVsync(bool value)
{
}

void BackendWebGPU::setTopology(Topology topology)
{
}

void BackendWebGPU::setViewport(std::optional<Viewport> viewport)
{
}

void BackendWebGPU::setScissor(std::optional<Scissor> scissor)
{
}

void BackendWebGPU::setTexture(uint32_t binding, TextureHandle* handle)
{
}

void BackendWebGPU::setInputLayout(const std::vector<InputLayout>& value)
{
}

void BackendWebGPU::setRenderTarget(const std::vector<RenderTargetHandle*>& handles)
{
}

void BackendWebGPU::setRenderTarget(std::nullopt_t value)
{
}

void BackendWebGPU::setShader(ShaderHandle* handle)
{
	gContext->pipeline_state.shader = (ShaderWebGPU*)handle;
	gContext->pipeline_state_dirty = true;
}

void BackendWebGPU::setVertexBuffer(const std::vector<VertexBufferHandle*>& handles)
{
}

void BackendWebGPU::setIndexBuffer(IndexBufferHandle* handle)
{
}

void BackendWebGPU::setUniformBuffer(uint32_t binding, UniformBufferHandle* handle)
{
}

void BackendWebGPU::setBlendMode(const std::optional<BlendMode>& blend_mode)
{
}

void BackendWebGPU::setDepthMode(const std::optional<DepthMode>& depth_mode)
{
}

void BackendWebGPU::setStencilMode(const std::optional<StencilMode>& stencil_mode)
{
}

void BackendWebGPU::setCullMode(CullMode cull_mode)
{
}

void BackendWebGPU::setSampler(Sampler value)
{
}

void BackendWebGPU::setTextureAddress(TextureAddress value)
{
}

void BackendWebGPU::setFrontFace(FrontFace value)
{
}

void BackendWebGPU::setDepthBias(const std::optional<DepthBias> depth_bias)
{
}

void BackendWebGPU::clear(const std::optional<glm::vec4>& color, const std::optional<float>& depth,
	const std::optional<uint8_t>& stencil)
{
}

void BackendWebGPU::draw(uint32_t vertex_count, uint32_t vertex_offset, uint32_t instance_count)
{
	EnsureGraphicsState(false);
	gContext->render_pass_encoder.draw(vertex_count, instance_count, vertex_offset, 0);
}

void BackendWebGPU::drawIndexed(uint32_t index_count, uint32_t index_offset, uint32_t instance_count)
{
	EnsureGraphicsState(true);
	//gContext->render_pass_encoder.drawIndexed(index_count, instance_count, index_offset, 0, 0);
	// TODO: remove next line and uncomment prev line
	gContext->render_pass_encoder.draw(index_count, instance_count, index_offset, 0);
}

void BackendWebGPU::readPixels(const glm::i32vec2& pos, const glm::i32vec2& size, TextureHandle* dst_texture_handle)
{
}

void BackendWebGPU::present()
{
	End();
	gContext->swapchain.present();
	Begin();
}

TextureHandle* BackendWebGPU::createTexture(uint32_t width, uint32_t height, Format format,
	uint32_t mip_count)
{
	return nullptr;
}

void BackendWebGPU::writeTexturePixels(TextureHandle* handle, uint32_t width, uint32_t height, Format format, void* memory,
	uint32_t mip_level, uint32_t offset_x, uint32_t offset_y)
{
}

void BackendWebGPU::readTexturePixels(TextureHandle* handle, uint32_t pos_x, uint32_t pos_y, uint32_t width, uint32_t height,
	uint32_t mip_level, void* dst_memory)
{
}

void BackendWebGPU::generateMips(TextureHandle* handle)
{
}

void BackendWebGPU::destroyTexture(TextureHandle* handle)
{
}

RenderTargetHandle* BackendWebGPU::createRenderTarget(uint32_t width, uint32_t height, TextureHandle* texture_handle)
{
	return nullptr;
}

void BackendWebGPU::destroyRenderTarget(RenderTargetHandle* handle)
{
}

ShaderHandle* BackendWebGPU::createShader(const std::string& vertex_code,
	const std::string& fragment_code, const std::vector<std::string>& defines)
{
	auto shader = new ShaderWebGPU(vertex_code, fragment_code, defines);
	return (ShaderHandle*)shader;
}

void BackendWebGPU::destroyShader(ShaderHandle* handle)
{
	auto shader = (ShaderWebGPU*)handle;
	delete shader;
}

VertexBufferHandle* BackendWebGPU::createVertexBuffer(size_t size, size_t stride)
{
	return nullptr;
}

void BackendWebGPU::destroyVertexBuffer(VertexBufferHandle* handle)
{
}

void BackendWebGPU::writeVertexBufferMemory(VertexBufferHandle* handle, void* memory, size_t size, size_t stride)
{
}

IndexBufferHandle* BackendWebGPU::createIndexBuffer(size_t size, size_t stride)
{
	return nullptr;
}

void BackendWebGPU::writeIndexBufferMemory(IndexBufferHandle* handle, void* memory, size_t size, size_t stride)
{
}

void BackendWebGPU::destroyIndexBuffer(IndexBufferHandle* handle)
{
}

UniformBufferHandle* BackendWebGPU::createUniformBuffer(size_t size)
{
	return nullptr;
}

void BackendWebGPU::destroyUniformBuffer(UniformBufferHandle* handle)
{
}

void BackendWebGPU::writeUniformBufferMemory(UniformBufferHandle* handle, void* memory, size_t size)
{
}

#endif
