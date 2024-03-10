#include "backend_webgpu.h"

#ifdef SKYGFX_HAS_WEBGPU

#include "shader_compiler.h"

#define WEBGPU_CPP_IMPLEMENTATION
#include <webgpu/webgpu.hpp>

#ifdef SKYGFX_PLATFORM_EMSCRIPTEN
#include <emscripten.h>
#include <emscripten/html5.h>
#include <emscripten/html5_webgpu.h>
#elif SKYGFX_PLATFORM_WINDOWS
#include <Windows.h>
#endif

using namespace skygfx;

static void* gWindow = nullptr;
static uint32_t gWidth;
static uint32_t gHeight;

BackendWebGPU::BackendWebGPU(void* window, uint32_t width, uint32_t height, Adapter _adapter)
{
	gWindow = window;
	gWidth = width;
	gHeight = height;
}

BackendWebGPU::~BackendWebGPU()
{
}

void BackendWebGPU::resize(uint32_t width, uint32_t height)
{
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

void BackendWebGPU::setRenderTarget(const RenderTarget** render_target, size_t count)
{
}

void BackendWebGPU::setShader(ShaderHandle* handle)
{
}

void BackendWebGPU::setVertexBuffer(const VertexBuffer** vertex_buffer, size_t count)
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
}

void BackendWebGPU::drawIndexed(uint32_t index_count, uint32_t index_offset, uint32_t instance_count)
{
}

void BackendWebGPU::readPixels(const glm::i32vec2& pos, const glm::i32vec2& size, TextureHandle* dst_texture_handle)
{
}

//#define SPIRV

#ifdef SPIRV
static const char* vertexShader = R"(
#version 450

void main()
{
    vec2 p = vec2(0.0, 0.0);
    
    if (gl_VertexIndex == 0u) {
        p = vec2(-0.5, -0.5);
    } else if (gl_VertexIndex == 1u) {
        p = vec2(0.5, -0.5);
    } else {
        p = vec2(0.0, 0.5);
    }
    
    gl_Position = vec4(p, 0.0, 1.0);
})";

static const char* fragmentShader = R"(
#version 450

layout(location = 0) out vec4 fragColor;

void main()
{
    fragColor = vec4(0.0, 0.4, 1.0, 1.0);
})";
#else
static const char* vertexShader = R"(
@vertex
fn main(@builtin(vertex_index) in_vertex_index: u32) -> @builtin(position) vec4f {
	var p = vec2f(0.0, 0.0);
	if (in_vertex_index == 0u) {
		p = vec2f(-0.5, -0.5);
	} else if (in_vertex_index == 1u) {
		p = vec2f(0.5, -0.5);
	} else {
		p = vec2f(0.0, 0.5);
	}
	return vec4f(p, 0.0, 1.0);
})";

static const char* fragmentShader = R"(
@fragment
fn main() -> @location(0) vec4f {
	return vec4f(0.0, 0.4, 1.0, 1.0);
})";
#endif

struct context_webgpu
{
	wgpu::Device device;
	wgpu::Queue queue;
	wgpu::Surface surface;
	std::unique_ptr<wgpu::ErrorCallback> uncapturedErrorCallbackHandle;
	wgpu::TextureFormat surfaceFormat = wgpu::TextureFormat::Undefined;
	wgpu::RenderPipeline pipeline;
};

static context_webgpu ctx;

static wgpu::TextureView  GetNextSurfaceTextureView() {
	wgpu::SurfaceTexture surfaceTexture;
	ctx.surface.getCurrentTexture(&surfaceTexture);
	if (surfaceTexture.status != wgpu::SurfaceGetCurrentTextureStatus::Success) {
		return nullptr;
	}
	wgpu::Texture texture = surfaceTexture.texture;

	wgpu::TextureViewDescriptor viewDescriptor;
	viewDescriptor.format = texture.getFormat();
	viewDescriptor.dimension = wgpu::TextureViewDimension::_2D;
	viewDescriptor.baseMipLevel = 0;
	viewDescriptor.mipLevelCount = 1;
	viewDescriptor.baseArrayLayer = 0;
	viewDescriptor.arrayLayerCount = 1;
	viewDescriptor.aspect = wgpu::TextureAspect::All;

	return texture.createView(viewDescriptor);
}

static WGPUSurface glfwGetWGPUSurface(WGPUInstance instance) {
#ifdef SKYGFX_PLATFORM_WINDOWS
	WGPUSurfaceDescriptorFromWindowsHWND surfaceDescriptorFrom;
	surfaceDescriptorFrom.chain.next = NULL;
	surfaceDescriptorFrom.chain.sType = WGPUSType_SurfaceDescriptorFromWindowsHWND;
	surfaceDescriptorFrom.hinstance = GetModuleHandle(NULL);
	surfaceDescriptorFrom.hwnd = gWindow;
#elif SKYGFX_PLATFORM_EMSCRIPTEN
	WGPUSurfaceDescriptorFromCanvasHTMLSelector surfaceDescriptorFrom;
	surfaceDescriptorFrom.chain.next = NULL;
	surfaceDescriptorFrom.chain.sType = WGPUSType_SurfaceDescriptorFromCanvasHTMLSelector;
	surfaceDescriptorFrom.selector = "canvas";
#endif
	WGPUSurfaceDescriptor surfaceDescriptor;
	surfaceDescriptor.nextInChain = &surfaceDescriptorFrom.chain;
	return wgpuInstanceCreateSurface(instance, &surfaceDescriptor);
}

static wgpu::ShaderModule CreateShaderModule(ShaderStage stage, const std::string& code)
{
#ifdef SPIRV
	auto spirv = CompileGlslToSpirv(stage, code);
	wgpu::ShaderModuleSPIRVDescriptor shaderCodeDesc;
	shaderCodeDesc.chain.next = nullptr;
	shaderCodeDesc.chain.sType = wgpu::SType::ShaderModuleSPIRVDescriptor;
	shaderCodeDesc.code = spirv.data();
	shaderCodeDesc.codeSize = spirv.size();
#else
	wgpu::ShaderModuleWGSLDescriptor shaderCodeDesc;
	shaderCodeDesc.chain.next = nullptr;
	shaderCodeDesc.chain.sType = wgpu::SType::ShaderModuleWGSLDescriptor;
	shaderCodeDesc.code = code.c_str();
#endif

	wgpu::ShaderModuleDescriptor shaderDesc;
#ifdef WEBGPU_BACKEND_WGPU
	shaderDesc.hintCount = 0;
	shaderDesc.hints = nullptr;
#endif
	shaderDesc.nextInChain = &shaderCodeDesc.chain;

	return ctx.device.createShaderModule(shaderDesc);
}

static void InitializePipeline() {
	wgpu::RenderPipelineDescriptor pipelineDesc;

	pipelineDesc.vertex.bufferCount = 0;
	pipelineDesc.vertex.buffers = nullptr;

	auto vertex_shader = CreateShaderModule(ShaderStage::Vertex, vertexShader);
	auto fragment_shader = CreateShaderModule(ShaderStage::Fragment, fragmentShader);

	pipelineDesc.vertex.module = vertex_shader;
	pipelineDesc.vertex.entryPoint = "main";
	pipelineDesc.vertex.constantCount = 0;
	pipelineDesc.vertex.constants = nullptr;

	pipelineDesc.primitive.topology = wgpu::PrimitiveTopology::TriangleList;
	pipelineDesc.primitive.stripIndexFormat = wgpu::IndexFormat::Undefined;
	pipelineDesc.primitive.frontFace = wgpu::FrontFace::CCW;
	pipelineDesc.primitive.cullMode = wgpu::CullMode::None;

	wgpu::FragmentState fragmentState;
	fragmentState.module = fragment_shader;
	fragmentState.entryPoint = "main";
	fragmentState.constantCount = 0;
	fragmentState.constants = nullptr;

	wgpu::BlendState blendState;
	blendState.color.srcFactor = wgpu::BlendFactor::SrcAlpha;
	blendState.color.dstFactor = wgpu::BlendFactor::OneMinusSrcAlpha;
	blendState.color.operation = wgpu::BlendOperation::Add;
	blendState.alpha.srcFactor = wgpu::BlendFactor::Zero;
	blendState.alpha.dstFactor = wgpu::BlendFactor::One;
	blendState.alpha.operation = wgpu::BlendOperation::Add;

	wgpu::ColorTargetState colorTarget;
	colorTarget.format = ctx.surfaceFormat;
	colorTarget.blend = &blendState;
	colorTarget.writeMask = wgpu::ColorWriteMask::All;

	fragmentState.targetCount = 1;
	fragmentState.targets = &colorTarget;
	pipelineDesc.fragment = &fragmentState;

	pipelineDesc.depthStencil = nullptr;
	pipelineDesc.multisample.count = 1;
	pipelineDesc.multisample.mask = ~0u;
	pipelineDesc.multisample.alphaToCoverageEnabled = false;
	pipelineDesc.layout = nullptr;

	ctx.pipeline = ctx.device.createRenderPipeline(pipelineDesc);

	vertex_shader.release();
	fragment_shader.release();
}

static bool WgpuInitialize() {
	wgpu::Instance instance = wgpuCreateInstance(nullptr);

	ctx.surface = glfwGetWGPUSurface(instance);

#ifdef SKYGFX_PLATFORM_EMSCRIPTEN
	ctx.device = emscripten_webgpu_get_device();
	wgpu::Adapter adapter{};
#else

	wgpu::RequestAdapterOptions adapterOpts = {};
	adapterOpts.compatibleSurface = ctx.surface;
	wgpu::Adapter adapter = instance.requestAdapter(adapterOpts);

	wgpu::DeviceDescriptor deviceDesc = {};
	deviceDesc.requiredFeatureCount = 0;
	deviceDesc.requiredLimits = nullptr;
	deviceDesc.defaultQueue.nextInChain = nullptr;
	deviceDesc.deviceLostCallback = [](WGPUDeviceLostReason reason, char const* message, void* /* pUserData */) {
		std::cout << "Device lost: reason " << reason;
		if (message)
			std::cout << " (" << message << ")";
		std::cout << std::endl;
	};
	ctx.device = adapter.requestDevice(deviceDesc);
#endif
	instance.release();

	ctx.uncapturedErrorCallbackHandle = ctx.device.setUncapturedErrorCallback([](wgpu::ErrorType type, char const* message) {
		std::cout << "Uncaptured device error: type " << type;
		if (message)
			std::cout << " (" << message << ")";
		std::cout << std::endl;
	});

	ctx.queue = ctx.device.getQueue();

	wgpu::SurfaceConfiguration config = {};
	config.width = 640;
	config.height = 480;
	config.usage = wgpu::TextureUsage::RenderAttachment;
	ctx.surfaceFormat = ctx.surface.getPreferredFormat(adapter);
	config.format = ctx.surfaceFormat;
	config.viewFormatCount = 0;
	config.viewFormats = nullptr;
	config.device = ctx.device;
	config.presentMode = wgpu::PresentMode::Fifo;
	config.alphaMode = wgpu::CompositeAlphaMode::Auto;
	ctx.surface.configure(config);

#ifndef SKYGFX_PLATFORM_EMSCRIPTEN
	adapter.release();
#endif

	InitializePipeline();
	return true;
}

void BackendWebGPU::present()
{
	static bool inited = false;

	if (!inited)
	{
		inited = true;
		WgpuInitialize();
		InitializePipeline();
	}

	wgpu::TextureView targetView = GetNextSurfaceTextureView();
	if (!targetView)
		return;

	wgpu::CommandEncoderDescriptor encoderDesc = {};
	wgpu::CommandEncoder encoder = wgpuDeviceCreateCommandEncoder(ctx.device, &encoderDesc);

	wgpu::RenderPassDescriptor renderPassDesc = {};

	wgpu::RenderPassColorAttachment renderPassColorAttachment = {};
	renderPassColorAttachment.view = targetView;
	renderPassColorAttachment.resolveTarget = nullptr;
	renderPassColorAttachment.loadOp = wgpu::LoadOp::Clear;
	renderPassColorAttachment.storeOp = wgpu::StoreOp::Store;
	renderPassColorAttachment.clearValue = WGPUColor{ 0.9, 0.1, 0.2, 1.0 };
#ifndef WEBGPU_BACKEND_WGPU
	renderPassColorAttachment.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
#endif

	renderPassDesc.colorAttachmentCount = 1;
	renderPassDesc.colorAttachments = &renderPassColorAttachment;
	renderPassDesc.depthStencilAttachment = nullptr;
	renderPassDesc.timestampWrites = nullptr;

	wgpu::RenderPassEncoder renderPass = encoder.beginRenderPass(renderPassDesc);

	renderPass.setPipeline(ctx.pipeline);
	renderPass.draw(3, 1, 0, 0);

	renderPass.end();
	renderPass.release();

	wgpu::CommandBufferDescriptor cmdBufferDescriptor = {};
	wgpu::CommandBuffer command = encoder.finish(cmdBufferDescriptor);
	encoder.release();

	ctx.queue.submit(1, &command);
	command.release();

	targetView.release();
#ifndef __EMSCRIPTEN__
	ctx.surface.present();
#endif

#if defined(WEBGPU_BACKEND_DAWN)
	device.tick();
#elif defined(WEBGPU_BACKEND_WGPU)
	ctx.device.poll(false);
#endif
}

TextureHandle* BackendWebGPU::createTexture(uint32_t width, uint32_t height, Format format,
	uint32_t mip_count)
{
	return nullptr;
}

void BackendWebGPU::writeTexturePixels(TextureHandle* handle, uint32_t width, uint32_t height, Format format, const void* memory,
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
	return nullptr;
}

void BackendWebGPU::destroyShader(ShaderHandle* handle)
{
}

VertexBufferHandle* BackendWebGPU::createVertexBuffer(size_t size, size_t stride)
{
	return nullptr;
}

void BackendWebGPU::destroyVertexBuffer(VertexBufferHandle* handle)
{
}

void BackendWebGPU::writeVertexBufferMemory(VertexBufferHandle* handle, const void* memory, size_t size, size_t stride)
{
}

IndexBufferHandle* BackendWebGPU::createIndexBuffer(size_t size, size_t stride)
{
	return nullptr;
}

void BackendWebGPU::writeIndexBufferMemory(IndexBufferHandle* handle, const void* memory, size_t size, size_t stride)
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

void BackendWebGPU::writeUniformBufferMemory(UniformBufferHandle* handle, const void* memory, size_t size)
{
}

#endif
