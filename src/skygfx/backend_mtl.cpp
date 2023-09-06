#include "backend_mtl.h"

#ifdef SKYGFX_HAS_METAL

#include "shader_compiler.h"
#include <unordered_map>
#include <unordered_set>

#if defined(SKYGFX_PLATFORM_MACOS)
	#import <AppKit/AppKit.h>
#elif defined(SKYGFX_PLATFORM_IOS)
	#import <UIKit/UIKit.h>
#endif

#import <Metal/Metal.h>
#import <MetalKit/MetalKit.h>

using namespace skygfx;

class ShaderMetal;
class TextureMetal;
class RenderTargetMetal;
class BufferMetal;
class IndexBufferMetal;

struct PipelineStateMetal
{
	ShaderMetal* shader = nullptr;
	MTLPixelFormat color_attachment_pixel_format;
	MTLPixelFormat depth_stencil_attachment_pixel_format;
	std::optional<BlendMode> blend_mode;

	bool operator==(const PipelineStateMetal& other) const = default;
};

SKYGFX_MAKE_HASHABLE(PipelineStateMetal,
	t.shader,
	t.color_attachment_pixel_format,
	t.depth_stencil_attachment_pixel_format,
	t.blend_mode
);
	
struct SamplerStateMetal
{
	Sampler sampler = Sampler::Linear;
	TextureAddress texture_address = TextureAddress::Clamp;

	bool operator==(const SamplerStateMetal& other) const = default;
};

SKYGFX_MAKE_HASHABLE(SamplerStateMetal,
	t.sampler,
	t.texture_address
);
	
struct DepthStencilStateMetal
{
	std::optional<DepthMode> depth_mode;
	std::optional<StencilMode> stencil_mode;

	bool operator==(const DepthStencilStateMetal& other) const = default;
};

SKYGFX_MAKE_HASHABLE(DepthStencilStateMetal,
	t.depth_mode,
	t.stencil_mode
);

struct ContextMTL
{
	static inline uint32_t VertexBufferStageBinding = 30;

	NSAutoreleasePool* autorelease_pool = nullptr;
	id<MTLDevice> device = nullptr;
	MTKView* view = nullptr;
	id<MTLCommandQueue> command_queue = nullptr;
	id<MTLCommandBuffer> command_buffer = nullptr;
	id<MTLRenderCommandEncoder> render_command_encoder = nullptr;
	id<MTLBlitCommandEncoder> blit_command_encoder = nullptr;
	MTLPrimitiveType primitive_type = MTLPrimitiveTypeTriangle;
	MTLIndexType index_type = MTLIndexTypeUInt16;
	IndexBufferMetal* index_buffer = nullptr;
	BufferMetal* vertex_buffer = nullptr;
	std::unordered_map<uint32_t, BufferMetal*> uniform_buffers;
	std::unordered_map<uint32_t, TextureMetal*> textures;

	bool pipeline_state_dirty = true;
	bool cull_mode_dirty = true;
	bool viewport_dirty = true;
	bool scissor_dirty = true;
	bool sampler_state_dirty = true;
	bool depth_stencil_state_dirty = true;
	bool render_target_dirty = true;

	PipelineStateMetal pipeline_state;
	CullMode cull_mode = CullMode::None;
	std::optional<Viewport> viewport;
	std::optional<Scissor> scissor;
	SamplerStateMetal sampler_state;
	DepthStencilStateMetal depth_stencil_state;
	RenderTargetMetal* render_target = nullptr;

	uint32_t width = 0;
	uint32_t height = 0;

	std::unordered_map<SamplerStateMetal, id<MTLSamplerState>> sampler_states;
	std::unordered_map<DepthStencilStateMetal, id<MTLDepthStencilState>> depth_stencil_states;
	std::unordered_map<PipelineStateMetal, id<MTLRenderPipelineState>> pipeline_states;

	std::vector<id<MTLBuffer>> staging_objects;
};

static ContextMTL* gContext = nullptr;

static void BeginRenderPass(const std::optional<glm::vec4>& color = std::nullopt,
	const std::optional<float>& depth = std::nullopt, const std::optional<uint8_t>& stencil = std::nullopt);
static void EndRenderPass();
static void BeginBlitPass();
static void EndBlitPass();
static void Begin();
static void End();

static const std::unordered_map<Format, MTLVertexFormat> VertexFormatMap = {
	{ Format::Float1, MTLVertexFormatFloat },
	{ Format::Float2, MTLVertexFormatFloat2 },
	{ Format::Float3, MTLVertexFormatFloat3 },
	{ Format::Float4, MTLVertexFormatFloat4 },
	{ Format::Byte1, MTLVertexFormatUCharNormalized },
	{ Format::Byte2, MTLVertexFormatUChar2Normalized },
	{ Format::Byte3, MTLVertexFormatUChar3Normalized },
	{ Format::Byte4, MTLVertexFormatUChar4Normalized }
};

static const std::unordered_map<Format, MTLPixelFormat> PixelFormatMap = {
	{ Format::Float1, MTLPixelFormatR32Float },
	{ Format::Float2, MTLPixelFormatRG32Float },
	// { Format::Float3, MTLPixelFormatRGB32Float },
	{ Format::Float4, MTLPixelFormatRGBA32Float },
	{ Format::Byte1, MTLPixelFormatR8Unorm },
	{ Format::Byte2, MTLPixelFormatRG8Unorm },
	// { Format::Byte3, ?? },
	{ Format::Byte4, MTLPixelFormatRGBA8Unorm }
};

class ShaderMetal
{
public:
	auto getMetalVertFunc() const { return mVertFunc; }
	auto getMetalFragFunc() const { return mFragFunc; }
	auto getMetalVertexDescriptor() const { return mVertexDescriptor; }
	
private:
	id<MTLLibrary> mVertLib = nullptr;
	id<MTLLibrary> mFragLib = nullptr;
	id<MTLFunction> mVertFunc = nullptr;
	id<MTLFunction> mFragFunc = nullptr;
	MTLVertexDescriptor* mVertexDescriptor = nullptr;
	
public:
	ShaderMetal(const VertexLayout& vertex_layout, const std::string& vertex_code, const std::string& fragment_code,
		std::vector<std::string> defines)
	{
		AddShaderLocationDefines(vertex_layout, defines);

		auto vertex_shader_spirv = CompileGlslToSpirv(ShaderStage::Vertex, vertex_code, defines);
		auto fragment_shader_spirv = CompileGlslToSpirv(ShaderStage::Fragment, fragment_code, defines);

		auto msl_vert = CompileSpirvToMsl(vertex_shader_spirv);
		auto msl_frag = CompileSpirvToMsl(fragment_shader_spirv);

		NSError* error = nil;
	
		mVertLib = [gContext->device newLibraryWithSource:[NSString stringWithUTF8String:msl_vert.c_str()] options:nil error:&error];
	
		if (!mVertLib)
		{
			auto reason = error.localizedDescription.UTF8String;
			throw std::runtime_error(reason);
		}
	
		mFragLib = [gContext->device newLibraryWithSource:[NSString stringWithUTF8String:msl_frag.c_str()] options:nil error:&error];
	
		if (!mFragLib)
		{
			auto reason = error.localizedDescription.UTF8String;
			throw std::runtime_error(reason);
		}

		mVertFunc = [mVertLib newFunctionWithName:@"main0"];
		mFragFunc = [mFragLib newFunctionWithName:@"main0"];
		mVertexDescriptor = [[MTLVertexDescriptor alloc] init];
		
		for (int i = 0; i < vertex_layout.attributes.size(); i++)
		{
			const auto& attrib = vertex_layout.attributes.at(i);
			auto desc = mVertexDescriptor.attributes[i];
			desc.format = VertexFormatMap.at(attrib.format);
			desc.offset = attrib.offset;
			desc.bufferIndex = ContextMTL::VertexBufferStageBinding;
		}

		auto layout = mVertexDescriptor.layouts[ContextMTL::VertexBufferStageBinding];
		layout.stride = vertex_layout.stride;
		layout.stepRate = 1;
		layout.stepFunction = MTLVertexStepFunctionPerVertex;
	}

	~ShaderMetal()
	{
		[mVertLib release];
		[mFragLib release];
		[mVertexDescriptor release];
		[mVertFunc release];
		[mFragFunc release];
	}
};

class TextureMetal
{
public:
	auto getMetalTexture() const { return mTexture; }
	auto getWidth() const { return mWidth; }
	auto getHeight() const { return mHeight; }
	auto getFormat() const { return mFormat; }
	auto getMipCount() const { return mMipCount; }

private:
	id<MTLTexture> mTexture = nullptr;
	uint32_t mWidth = 0;
	uint32_t mHeight = 0;
	uint32_t mMipCount = 0;
	Format mFormat;

public:
	TextureMetal(uint32_t width, uint32_t height, Format format, uint32_t mip_count) :
		mWidth(width),
		mHeight(height),
		mFormat(format),
		mMipCount(mip_count)
	{
		auto desc = [[MTLTextureDescriptor alloc] init];
		desc.width = width;
		desc.height = height;
		desc.mipmapLevelCount = mip_count;
		desc.pixelFormat = PixelFormatMap.at(format);
		desc.textureType = MTLTextureType2D;
		desc.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
#if defined(SKYGFX_PLATFORM_MACOS)
		desc.storageMode = MTLStorageModeManaged;
#elif defined(SKYGFX_PLATFORM_IOS)
		desc.storageMode = MTLStorageModeShared;
#endif
		mTexture = [gContext->device newTextureWithDescriptor:desc];
		[desc release];
	}

	~TextureMetal()
	{
		[mTexture release];
	}

	void write(uint32_t width, uint32_t height, Format format, void* memory,
		uint32_t mip_level, uint32_t offset_x, uint32_t offset_y)
	{
		auto region = MTLRegionMake2D(offset_x, offset_y, width, height);
		auto channels = GetFormatChannelsCount(format);
		auto channel_size = GetFormatChannelSize(format);

		[mTexture replaceRegion:region mipmapLevel:mip_level withBytes:memory
			bytesPerRow:width * channels * channel_size];
	}

	void read(uint32_t pos_x, uint32_t pos_y, uint32_t width, uint32_t height,
		uint32_t mip_level, void* dst_memory)
	{
		// not implemented
	}

	void generateMips()
	{
		auto cmd = gContext->command_queue.commandBuffer;
		auto enc = cmd.blitCommandEncoder;

		[enc generateMipmapsForTexture:mTexture];
		[enc endEncoding];
		[cmd commit];
		[cmd waitUntilCompleted];
	}
};

class RenderTargetMetal
{
public:
	auto getTexture() const { return mTexture; }
	auto getMetalDepthStencilTexture() const { return mDepthStencilTexture; }
	
private:
	TextureMetal* mTexture = nullptr;
	id<MTLTexture> mDepthStencilTexture = nullptr;
	
public:
	RenderTargetMetal(uint32_t width, uint32_t height, TextureMetal* texture) :
		mTexture(texture)
	{
		auto desc = [[MTLTextureDescriptor alloc] init];
		desc.width = width;
		desc.height = height;
		desc.pixelFormat = MTLPixelFormatDepth32Float_Stencil8;
		desc.textureType = MTLTextureType2D;
		desc.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
		desc.storageMode = MTLStorageModePrivate;

		mDepthStencilTexture = [gContext->device newTextureWithDescriptor:desc];

		[desc release];
	}
	
	~RenderTargetMetal()
	{
		[mDepthStencilTexture release];
	}
};

static id<MTLBuffer> CreateBuffer(size_t size)
{
	return [gContext->device newBufferWithLength:size options:MTLResourceStorageModeShared];
}

static void DestroyStaging(id<MTLBuffer> buffer)
{
	gContext->staging_objects.push_back(buffer);
}

static void ReleaseStagingObjects()
{
	for (auto buffer : gContext->staging_objects)
	{
		[buffer release];
	}
	gContext->staging_objects.clear();
}

class BufferMetal
{
public:
	auto getMetalBuffer() const { return mBuffer; }

private:
	id<MTLBuffer> mBuffer;
	size_t mSize = 0;

public:
	BufferMetal(size_t size) : mSize(size)
	{
		mBuffer = CreateBuffer(mSize);
	}
	
	~BufferMetal()
	{
		[mBuffer release];
	}
	
	void write(void* memory, size_t size)
	{
		assert(size <= mSize);

		auto staging_buffer = CreateBuffer(size);
		memcpy(staging_buffer.contents, memory, size);

		EndRenderPass();
		BeginBlitPass();

		[gContext->blit_command_encoder
			copyFromBuffer:staging_buffer
			sourceOffset:0
			toBuffer:mBuffer
			destinationOffset:0
			size:size];

		EndBlitPass();
		BeginRenderPass();

		DestroyStaging(staging_buffer);
	}
};

class IndexBufferMetal : public BufferMetal
{
public:
	auto getStride() const { return mStride; }
	void setStride(size_t value) { mStride = value; }
	
private:
	size_t mStride = 0;
	
public:
	IndexBufferMetal(size_t size, size_t stride) : BufferMetal(size), mStride(stride)
	{
	}
};

static NSAutoreleasePool* gFrameAutoreleasePool = nullptr;

static void BeginRenderPass(const std::optional<glm::vec4>& color,
	const std::optional<float>& depth, const std::optional<uint8_t>& stencil)
{
	assert(gContext->render_command_encoder == nullptr);

	auto color_texture = gContext->render_target ?
		gContext->render_target->getTexture()->getMetalTexture() :
		gContext->view.currentDrawable.texture;

	auto depth_stencil_texture = gContext->render_target ?
		gContext->render_target->getMetalDepthStencilTexture() :
		gContext->view.depthStencilTexture;

	auto descriptor = [[MTLRenderPassDescriptor alloc] init];

	descriptor.colorAttachments[0].texture = color_texture;
	descriptor.colorAttachments[0].storeAction = MTLStoreActionStore;
	
	descriptor.depthAttachment.texture = depth_stencil_texture;
	descriptor.depthAttachment.storeAction = MTLStoreActionStore;
	
	descriptor.stencilAttachment.texture = depth_stencil_texture;
	descriptor.stencilAttachment.storeAction = MTLStoreActionStore;
	
	if (color.has_value())
	{
		auto col = color.value();

		descriptor.colorAttachments[0].clearColor = MTLClearColorMake(col.r, col.g, col.b, col.a);
		descriptor.colorAttachments[0].loadAction = MTLLoadActionClear;
	}
	else
	{
		descriptor.colorAttachments[0].loadAction = MTLLoadActionLoad;
	}

	if (depth.has_value())
	{
		descriptor.depthAttachment.clearDepth = depth.value();
		descriptor.depthAttachment.loadAction = MTLLoadActionClear;
	}
	else
	{
		descriptor.depthAttachment.loadAction = MTLLoadActionLoad;
	}

	if (stencil.has_value())
	{
		descriptor.stencilAttachment.clearStencil = stencil.value();
		descriptor.stencilAttachment.loadAction = MTLLoadActionClear;
	}
	else
	{
		descriptor.stencilAttachment.loadAction = MTLLoadActionLoad;
	}

	gContext->render_command_encoder = [gContext->command_buffer renderCommandEncoderWithDescriptor:descriptor];
	gContext->pipeline_state_dirty = true;
}

static void EndRenderPass()
{
	assert(gContext->render_command_encoder != nullptr);
	[gContext->render_command_encoder endEncoding];
	gContext->render_command_encoder = nullptr;
}

static void BeginBlitPass()
{
	assert(gContext->blit_command_encoder == nullptr);
	gContext->blit_command_encoder = [gContext->command_buffer blitCommandEncoder];
}

static void EndBlitPass()
{
	assert(gContext->blit_command_encoder != nullptr);
	[gContext->blit_command_encoder endEncoding];
	gContext->blit_command_encoder = nullptr;
}

static void Begin()
{
	gFrameAutoreleasePool = [[NSAutoreleasePool alloc] init];

	gContext->command_buffer = gContext->command_queue.commandBuffer;

	BeginRenderPass();

	gContext->cull_mode_dirty = true;
	gContext->viewport_dirty = true;
	gContext->scissor_dirty = true;
	gContext->sampler_state_dirty = true;
	gContext->depth_stencil_state_dirty = true;
	gContext->pipeline_state_dirty = true;

	auto width = (uint32_t)gContext->view.drawableSize.width;
	auto height = (uint32_t)gContext->view.drawableSize.height;
	
	if (gContext->width != width || gContext->height != height)
	{
		gContext->width = width;
		gContext->height = height;
		
		if (!gContext->viewport.has_value())
			gContext->viewport_dirty = true;
	
		if (!gContext->scissor.has_value())
			gContext->scissor_dirty = true;
	}
}

static void End()
{
	EndRenderPass();
	
	[gContext->command_buffer presentDrawable:gContext->view.currentDrawable];
	[gContext->command_buffer commit];
	[gContext->command_buffer waitUntilCompleted];
	
	[gFrameAutoreleasePool release];
}

static void EnsureGraphicsState()
{
	if (gContext->render_target_dirty)
	{
		gContext->render_target_dirty = false;

		EndRenderPass();
		BeginRenderPass();

		auto color_attachment_pixel_format = gContext->render_target ?
			gContext->render_target->getTexture()->getMetalTexture().pixelFormat :
			gContext->view.colorPixelFormat;

		if (gContext->pipeline_state.color_attachment_pixel_format != color_attachment_pixel_format)
		{
			gContext->pipeline_state.color_attachment_pixel_format = color_attachment_pixel_format;
			gContext->pipeline_state_dirty = true;
		}

		auto depth_stencil_attachment_pixel_format = gContext->render_target ?
			gContext->render_target->getMetalDepthStencilTexture().pixelFormat :
			gContext->view.depthStencilPixelFormat;

		if (gContext->pipeline_state.depth_stencil_attachment_pixel_format != depth_stencil_attachment_pixel_format)
		{
			gContext->pipeline_state.depth_stencil_attachment_pixel_format = depth_stencil_attachment_pixel_format;
			gContext->pipeline_state_dirty = true;
		}
	}

	if (gContext->sampler_state_dirty)
	{
		gContext->sampler_state_dirty = false;

		if (!gContext->sampler_states.contains(gContext->sampler_state))
		{
			const static std::unordered_map<Sampler, MTLSamplerMinMagFilter> SamplerMinMagFilter = {
				{ Sampler::Linear, MTLSamplerMinMagFilterLinear  },
				{ Sampler::Nearest, MTLSamplerMinMagFilterNearest },
			};

			const static std::unordered_map<Sampler, MTLSamplerMipFilter> SamplerMipFilter = {
				{ Sampler::Linear, MTLSamplerMipFilterLinear  },
				{ Sampler::Nearest, MTLSamplerMipFilterNearest },
			};

			const static std::unordered_map<TextureAddress, MTLSamplerAddressMode> TextureAddressMap = {
				{ TextureAddress::Clamp, MTLSamplerAddressModeClampToEdge },
				{ TextureAddress::Wrap, MTLSamplerAddressModeRepeat },
				{ TextureAddress::MirrorWrap, MTLSamplerAddressModeMirrorRepeat }
			};

			auto desc = [[MTLSamplerDescriptor alloc] init];
			desc.magFilter = SamplerMinMagFilter.at(gContext->sampler_state.sampler);
			desc.minFilter = SamplerMinMagFilter.at(gContext->sampler_state.sampler);
			desc.mipFilter = SamplerMipFilter.at(gContext->sampler_state.sampler);
			desc.rAddressMode = TextureAddressMap.at(gContext->sampler_state.texture_address);
			desc.sAddressMode = TextureAddressMap.at(gContext->sampler_state.texture_address);
			desc.tAddressMode = TextureAddressMap.at(gContext->sampler_state.texture_address);

			auto sampler_state = [gContext->device newSamplerStateWithDescriptor:desc];

			gContext->sampler_states.insert({ gContext->sampler_state, sampler_state });

			[desc release];
		}
	}

	for (auto [binding, texture] : gContext->textures)
	{
		[gContext->render_command_encoder
			setFragmentTexture:texture->getMetalTexture()
			atIndex:binding];
		[gContext->render_command_encoder
			setFragmentSamplerState:gContext->sampler_states.at(gContext->sampler_state)
			atIndex:binding];
	}

	[gContext->render_command_encoder
		setVertexBuffer:gContext->vertex_buffer->getMetalBuffer()
		offset:0
		atIndex:ContextMTL::VertexBufferStageBinding];

	if (gContext->depth_stencil_state_dirty)
	{
		gContext->depth_stencil_state_dirty = false;

		if (!gContext->depth_stencil_states.contains(gContext->depth_stencil_state))
		{
			const static std::unordered_map<ComparisonFunc, MTLCompareFunction> ComparisonFuncMap = {
				{ ComparisonFunc::Always, MTLCompareFunctionAlways },
				{ ComparisonFunc::Never, MTLCompareFunctionNever },
				{ ComparisonFunc::Less, MTLCompareFunctionLess },
				{ ComparisonFunc::Equal, MTLCompareFunctionEqual },
				{ ComparisonFunc::NotEqual, MTLCompareFunctionNotEqual },
				{ ComparisonFunc::LessEqual, MTLCompareFunctionLessEqual },
				{ ComparisonFunc::Greater, MTLCompareFunctionGreater },
				{ ComparisonFunc::GreaterEqual, MTLCompareFunctionGreaterEqual }
			};

			const static std::unordered_map<StencilOp, MTLStencilOperation> StencilOpMap = {
				{ StencilOp::Keep, MTLStencilOperationKeep },
				{ StencilOp::Zero, MTLStencilOperationZero },
				{ StencilOp::Replace, MTLStencilOperationReplace },
				{ StencilOp::IncrementSaturation, MTLStencilOperationIncrementClamp },
				{ StencilOp::DecrementSaturation, MTLStencilOperationDecrementClamp },
				{ StencilOp::Invert, MTLStencilOperationInvert },
				{ StencilOp::Increment, MTLStencilOperationIncrementWrap },
				{ StencilOp::Decrement, MTLStencilOperationDecrementWrap },
			};

			auto depth_mode = gContext->depth_stencil_state.depth_mode.value_or(DepthMode());
			auto stencil_mode = gContext->depth_stencil_state.stencil_mode.value_or(StencilMode());

			auto desc = [[MTLDepthStencilDescriptor alloc] init];
			desc.depthWriteEnabled = gContext->depth_stencil_state.depth_mode.has_value();
			desc.depthCompareFunction = ComparisonFuncMap.at(depth_mode.func);

			desc.backFaceStencil.depthFailureOperation = StencilOpMap.at(stencil_mode.depth_fail_op);
			desc.backFaceStencil.stencilFailureOperation = StencilOpMap.at(stencil_mode.fail_op);
			desc.backFaceStencil.stencilCompareFunction = ComparisonFuncMap.at(stencil_mode.func);
			desc.backFaceStencil.depthStencilPassOperation = StencilOpMap.at(stencil_mode.pass_op);
			desc.backFaceStencil.readMask = stencil_mode.read_mask;
			desc.backFaceStencil.writeMask = stencil_mode.write_mask;

			desc.frontFaceStencil = desc.backFaceStencil;

			auto depth_stencil_state = [gContext->device newDepthStencilStateWithDescriptor:desc];

			gContext->depth_stencil_states.insert({ gContext->depth_stencil_state, depth_stencil_state });

			[desc release];
		}

		if (gContext->depth_stencil_state.stencil_mode.has_value())
		{
			auto reference = gContext->depth_stencil_state.stencil_mode.value().reference;
			[gContext->render_command_encoder setStencilReferenceValue:reference];
		}

		[gContext->render_command_encoder
			setDepthStencilState:gContext->depth_stencil_states.at(gContext->depth_stencil_state)];
	}

	if (gContext->pipeline_state_dirty)
	{
		gContext->pipeline_state_dirty = false;

		if (!gContext->pipeline_states.contains(gContext->pipeline_state))
		{
			auto shader = gContext->pipeline_state.shader;

			auto depth_stencil_pixel_format = gContext->pipeline_state.depth_stencil_attachment_pixel_format;

			auto desc = [[MTLRenderPipelineDescriptor alloc] init];
			desc.vertexFunction = shader->getMetalVertFunc();
			desc.fragmentFunction = shader->getMetalFragFunc();
			desc.vertexDescriptor = shader->getMetalVertexDescriptor();
			desc.depthAttachmentPixelFormat = depth_stencil_pixel_format;
			desc.stencilAttachmentPixelFormat = depth_stencil_pixel_format;

			auto attachment_0 = desc.colorAttachments[0];
			attachment_0.pixelFormat = gContext->pipeline_state.color_attachment_pixel_format;

			const static std::unordered_map<Blend, MTLBlendFactor> BlendMap = {
				{ Blend::One, MTLBlendFactorOne },
				{ Blend::Zero, MTLBlendFactorZero },
				{ Blend::SrcColor, MTLBlendFactorSourceColor },
				{ Blend::InvSrcColor, MTLBlendFactorOneMinusSourceColor },
				{ Blend::SrcAlpha, MTLBlendFactorSourceAlpha },
				{ Blend::InvSrcAlpha, MTLBlendFactorOneMinusSourceAlpha },
				{ Blend::DstColor, MTLBlendFactorDestinationColor },
				{ Blend::InvDstColor, MTLBlendFactorOneMinusDestinationColor },
				{ Blend::DstAlpha, MTLBlendFactorDestinationAlpha },
				{ Blend::InvDstAlpha, MTLBlendFactorOneMinusDestinationAlpha }
			};

			const static std::unordered_map<BlendFunction, MTLBlendOperation> BlendOpMap = {
				{ BlendFunction::Add, MTLBlendOperationAdd },
				{ BlendFunction::Subtract, MTLBlendOperationSubtract },
				{ BlendFunction::ReverseSubtract, MTLBlendOperationReverseSubtract },
				{ BlendFunction::Min, MTLBlendOperationMin },
				{ BlendFunction::Max, MTLBlendOperationMax },
			};

			attachment_0.blendingEnabled = gContext->pipeline_state.blend_mode.has_value();

			if (gContext->pipeline_state.blend_mode.has_value())
			{
				const auto& blend_mode = gContext->pipeline_state.blend_mode.value();

				attachment_0.sourceRGBBlendFactor = BlendMap.at(blend_mode.color_src);
				attachment_0.sourceAlphaBlendFactor = BlendMap.at(blend_mode.alpha_src);
				attachment_0.destinationRGBBlendFactor = BlendMap.at(blend_mode.color_dst);
				attachment_0.destinationAlphaBlendFactor = BlendMap.at(blend_mode.alpha_dst);

				attachment_0.rgbBlendOperation = BlendOpMap.at(blend_mode.color_func);
				attachment_0.alphaBlendOperation = BlendOpMap.at(blend_mode.alpha_func);

				attachment_0.writeMask = MTLColorWriteMaskNone;

				if (blend_mode.color_mask.red)
					attachment_0.writeMask |= MTLColorWriteMaskRed;

				if (blend_mode.color_mask.green)
					attachment_0.writeMask |= MTLColorWriteMaskGreen;

				if (blend_mode.color_mask.blue)
					attachment_0.writeMask |= MTLColorWriteMaskBlue;

				if (blend_mode.color_mask.alpha)
					attachment_0.writeMask |= MTLColorWriteMaskAlpha;
			}

			NSError* error = nullptr;

			auto pso = [gContext->device newRenderPipelineStateWithDescriptor:desc error:&error];

			if (!pso)
			{
				auto reason = error.localizedDescription.UTF8String;
				throw std::runtime_error(reason);
			}

			gContext->pipeline_states[gContext->pipeline_state] = pso;
		}

		auto pso = gContext->pipeline_states.at(gContext->pipeline_state);

		[gContext->render_command_encoder setRenderPipelineState:pso];
	}

	for (auto [binding, buffer] : gContext->uniform_buffers)
	{
		[gContext->render_command_encoder setVertexBuffer:buffer->getMetalBuffer() offset:0 atIndex:binding];
		[gContext->render_command_encoder setFragmentBuffer:buffer->getMetalBuffer() offset:0 atIndex:binding];
	}

	if (gContext->cull_mode_dirty)
	{
		gContext->cull_mode_dirty = false;

		static const std::unordered_map<CullMode, MTLCullMode> CullModes = {
			{ CullMode::None, MTLCullModeNone },
			{ CullMode::Back, MTLCullModeBack },
			{ CullMode::Front, MTLCullModeFront }
		};
		[gContext->render_command_encoder setCullMode:CullModes.at(gContext->cull_mode)];
		[gContext->render_command_encoder setFrontFacingWinding:MTLWindingClockwise];
	}

	float width;
	float height;

	if (gContext->render_target == nullptr)
	{
		width = static_cast<float>(gContext->width);
		height = static_cast<float>(gContext->height);
	}
	else
	{
		auto texture = gContext->render_target->getTexture()->getMetalTexture();

		width = static_cast<float>(texture.width);
		height = static_cast<float>(texture.height);
	}

	if (gContext->viewport_dirty)
	{
		gContext->viewport_dirty = false;

		auto _viewport = gContext->viewport.value_or(Viewport{ { 0.0f, 0.0f }, { width, height } });

		MTLViewport viewport;
		viewport.originX = _viewport.position.x;
		viewport.originY = _viewport.position.y;
		viewport.width = _viewport.size.x;
		viewport.height = _viewport.size.y;
		viewport.znear = _viewport.min_depth;
		viewport.zfar = _viewport.max_depth;

		[gContext->render_command_encoder setViewport:viewport];
	}

	if (gContext->scissor_dirty)
	{
		gContext->scissor_dirty = false;

		auto _scissor = gContext->scissor.value_or(Scissor{ { 0.0f, 0.0f }, { width, height } });

		if (_scissor.position.x < 0.0f)
		{
			_scissor.size.x -= _scissor.position.x;
			_scissor.position.x = 0.0f;
		}

		if (_scissor.position.y < 0.0f)
		{
			_scissor.size.y -= _scissor.position.y;
			_scissor.position.y = 0.0f;
		}

		MTLScissorRect scissor;
		scissor.x = _scissor.position.x;
		scissor.y = _scissor.position.y;
		scissor.width = _scissor.size.x;
		scissor.height = _scissor.size.y;

		scissor.x = glm::min((uint32_t)scissor.x, (uint32_t)width);
		scissor.y = glm::min((uint32_t)scissor.y, (uint32_t)height);

		if (scissor.x + scissor.width > width)
			scissor.width = width - scissor.x;

		if (scissor.y + scissor.height > height)
			scissor.height = height - scissor.y;

		[gContext->render_command_encoder setScissorRect:scissor];
	}
}

BackendMetal::BackendMetal(void* window, uint32_t width, uint32_t height)
{
	gContext = new ContextMTL();

	gContext->autorelease_pool = [[NSAutoreleasePool alloc] init];
	gContext->device = MTLCreateSystemDefaultDevice();
	
	gContext->view = [[MTKView alloc] init];
	gContext->view.device = gContext->device;
	gContext->view.colorPixelFormat = MTLPixelFormatRGBA8Unorm;
	gContext->view.depthStencilPixelFormat = MTLPixelFormatDepth32Float_Stencil8;
	gContext->view.paused = YES;
	gContext->view.enableSetNeedsDisplay = NO;
	gContext->view.framebufferOnly = NO;

	auto metal_layer = (CAMetalLayer*)gContext->view.layer;
	metal_layer.magnificationFilter = kCAFilterNearest;

#if defined(SKYGFX_PLATFORM_MACOS)
	gContext->view.autoresizingMask = NSViewHeightSizable | NSViewWidthSizable | NSViewMinXMargin |
		NSViewMaxXMargin | NSViewMinYMargin | NSViewMaxYMargin;

	NSObject* nwh = (NSObject*)window;
	NSView* contentView = nil;
	NSWindow* nsWindow = nil;
	
	if ([nwh isKindOfClass:[NSView class]])
	{
		contentView = (NSView*)nwh;
	}
	else if ([nwh isKindOfClass:[NSWindow class]])
	{
		nsWindow = (NSWindow*)nwh;
		contentView = [nsWindow contentView];
	}
	
	gContext->view.frame = [contentView bounds];
	
	if (contentView != nil)
	{
		[contentView addSubview:gContext->view];
	}
	else if (nsWindow != nil)
	{
		[nsWindow setContentView:gContext->view];
	}
#elif defined(SKYGFX_PLATFORM_IOS)
	auto _window = (UIWindow*)window;
	auto root_view = [[_window rootViewController] view];
	gContext->view.frame = [root_view bounds];
	[root_view addSubview:gContext->view];
#endif

	gContext->width = width;
	gContext->height = height;

	gContext->command_queue = gContext->device.newCommandQueue;
			
	Begin();
}

BackendMetal::~BackendMetal()
{
	End();
	ReleaseStagingObjects();

	for (auto [_, sampler_state] : gContext->sampler_states)
	{
		[sampler_state release];
	}
	[gContext->command_queue release];
	[gContext->view release];
	[gContext->device release];
	[gContext->autorelease_pool release];

	delete gContext;
	gContext = nullptr;
}

void BackendMetal::resize(uint32_t width, uint32_t height)
{
}

void BackendMetal::setVsync(bool value)
{
}

void BackendMetal::setTopology(Topology topology)
{
	const static std::unordered_map<Topology, MTLPrimitiveType> TopologyMap = {
		{ Topology::PointList, MTLPrimitiveTypePoint },
		{ Topology::LineList, MTLPrimitiveTypeLine },
		{ Topology::LineStrip, MTLPrimitiveTypeLineStrip },
		{ Topology::TriangleList, MTLPrimitiveTypeTriangle },
		{ Topology::TriangleStrip, MTLPrimitiveTypeTriangleStrip }
	};

	gContext->primitive_type = TopologyMap.at(topology);
}

void BackendMetal::setViewport(std::optional<Viewport> viewport)
{
	if (gContext->viewport == viewport)
		return;
		
	gContext->viewport = viewport;
	gContext->viewport_dirty = true;
}

void BackendMetal::setScissor(std::optional<Scissor> scissor)
{
	if (gContext->scissor == scissor)
		return;
	
	gContext->scissor = scissor;
	gContext->scissor_dirty = true;
}

void BackendMetal::setTexture(uint32_t binding, TextureHandle* handle)
{
	auto texture = (TextureMetal*)handle;
	
	if (gContext->textures[binding] == texture)
		return;
	
	gContext->textures[binding] = texture;
	gContext->sampler_state_dirty = true;
}

void BackendMetal::setRenderTarget(RenderTargetHandle* handle)
{
	auto render_target = (RenderTargetMetal*)handle;
	
	if (gContext->render_target == render_target)
		return;

	gContext->render_target = render_target;
	gContext->render_target_dirty = true;
}

void BackendMetal::setRenderTarget(std::nullopt_t value)
{
	if (gContext->render_target == nullptr)
		return;

	gContext->render_target = nullptr;
	gContext->render_target_dirty = true;
}

void BackendMetal::setShader(ShaderHandle* handle)
{
	auto shader = (ShaderMetal*)handle;
	
	if (gContext->pipeline_state.shader == shader)
		return;
	
	gContext->pipeline_state.shader = shader;
	gContext->pipeline_state_dirty = true;
}

void BackendMetal::setVertexBuffer(VertexBufferHandle* handle)
{
	auto buffer = (BufferMetal*)handle;
	gContext->vertex_buffer = buffer;
}

void BackendMetal::setIndexBuffer(IndexBufferHandle* handle)
{
	auto buffer = (IndexBufferMetal*)handle;
	gContext->index_buffer = buffer;
	gContext->index_type = buffer->getStride() == 2 ? MTLIndexTypeUInt16 : MTLIndexTypeUInt32;
}

void BackendMetal::setUniformBuffer(uint32_t binding, UniformBufferHandle* handle)
{
	auto buffer = (BufferMetal*)handle;
	gContext->uniform_buffers[binding] = buffer;
}

void BackendMetal::setBlendMode(const std::optional<BlendMode>& blend_mode)
{
	if (gContext->pipeline_state.blend_mode == blend_mode)
		return;
	
	gContext->pipeline_state.blend_mode = blend_mode;
	gContext->pipeline_state_dirty = true;
}

void BackendMetal::setDepthMode(const std::optional<DepthMode>& depth_mode)
{
	if (gContext->depth_stencil_state.depth_mode == depth_mode)
		return;
	
	gContext->depth_stencil_state.depth_mode = depth_mode;
	gContext->depth_stencil_state_dirty = true;
}

void BackendMetal::setStencilMode(const std::optional<StencilMode>& stencil_mode)
{
	if (gContext->depth_stencil_state.stencil_mode == stencil_mode)
		return;
	
	gContext->depth_stencil_state.stencil_mode = stencil_mode;
	gContext->depth_stencil_state_dirty = true;
}

void BackendMetal::setCullMode(CullMode cull_mode)
{
	if (gContext->cull_mode == cull_mode)
		return;

	gContext->cull_mode = cull_mode;
	gContext->cull_mode_dirty = true;
}

void BackendMetal::setSampler(Sampler value)
{
	if (gContext->sampler_state.sampler == value)
		return;
		
	gContext->sampler_state.sampler = value;
	gContext->sampler_state_dirty = true;
}

void BackendMetal::setTextureAddress(TextureAddress value)
{
	if (gContext->sampler_state.texture_address == value)
		return;
		
	gContext->sampler_state.texture_address = value;
	gContext->sampler_state_dirty = true;
}

void BackendMetal::setFrontFace(FrontFace value)
{
}

void BackendMetal::setDepthBias(const std::optional<DepthBias> depth_bias)
{
}

void BackendMetal::clear(const std::optional<glm::vec4>& color, const std::optional<float>& depth,
	const std::optional<uint8_t>& stencil)
{
	EndRenderPass();
	BeginRenderPass(color, depth, stencil);
}

void BackendMetal::draw(uint32_t vertex_count, uint32_t vertex_offset, uint32_t instance_count)
{
	EnsureGraphicsState();
	[gContext->render_command_encoder
		drawPrimitives:gContext->primitive_type
		vertexStart:vertex_offset
		vertexCount:vertex_count
		instanceCount:instance_count];
}

void BackendMetal::drawIndexed(uint32_t index_count, uint32_t index_offset, uint32_t instance_count)
{
	EnsureGraphicsState();

	auto index_size = gContext->index_type == MTLIndexTypeUInt32 ? 4 : 2;

	[gContext->render_command_encoder
		drawIndexedPrimitives:gContext->primitive_type
		indexCount:index_count
		indexType:gContext->index_type
		indexBuffer:gContext->index_buffer->getMetalBuffer()
		indexBufferOffset:index_offset * index_size
		instanceCount:instance_count];
}

void BackendMetal::readPixels(const glm::i32vec2& pos, const glm::i32vec2& size, TextureHandle* dst_texture_handle)
{
	if (size.x <= 0 || size.y <= 0)
		return;

	if (pos.x + size.x <= 0 || pos.y + size.y <= 0)
		return;

	auto dst_texture = (TextureMetal*)dst_texture_handle;

	assert(dst_texture->getMetalTexture().width == size.x);
	assert(dst_texture->getMetalTexture().height == size.y);

	auto src_texture = gContext->render_target ?
		gContext->render_target->getTexture()->getMetalTexture() :
		gContext->view.currentDrawable.texture;

	float src_x = pos.x;
	float src_y = pos.y;
	float src_w = size.x;
	float src_h = size.y;

	float tex_w = (float)src_texture.width;
	float tex_h = (float)src_texture.height;

	if (src_x >= tex_w)
		return;

	if (src_y >= tex_h)
		return;

	float dst_x = 0.0f;
	float dst_y = 0.0f;

	if (src_x < 0.0f)
	{
		dst_x -= src_x;
		src_w += src_x;
		src_x = 0.0f;
	}

	if (src_y < 0.0f)
	{
		dst_y -= src_y;
		src_h += src_y;
		src_y = 0.0f;
	}

	if (src_x + src_w > tex_w)
	{
		src_w = tex_w - src_x;
	}

	if (src_y + src_h > tex_h)
	{
		src_h = tex_h - src_y;
	}

	EndRenderPass();
	BeginBlitPass();

	[gContext->blit_command_encoder
		copyFromTexture:src_texture
		sourceSlice:0
		sourceLevel:0
		sourceOrigin:MTLOriginMake((uint32_t)src_x, (uint32_t)src_y, 0)
		sourceSize:MTLSizeMake((uint32_t)src_w, (uint32_t)src_h, 1)
		toTexture:dst_texture->getMetalTexture()
		destinationSlice:0
		destinationLevel:0
		destinationOrigin:MTLOriginMake((uint32_t)dst_x, (uint32_t)dst_y, 0)];

	EndBlitPass();
	BeginRenderPass();
}

void BackendMetal::present()
{
	End();
	[gContext->view draw];
	ReleaseStagingObjects();
	Begin();
}

TextureHandle* BackendMetal::createTexture(uint32_t width, uint32_t height, Format format,
	uint32_t mip_count)
{
	auto texture = new TextureMetal(width, height, format, mip_count);
	return (TextureHandle*)texture;
}

void BackendMetal::writeTexturePixels(TextureHandle* handle, uint32_t width, uint32_t height, Format format, void* memory,
	uint32_t mip_level, uint32_t offset_x, uint32_t offset_y)
{
	auto texture = (TextureMetal*)handle;
	texture->write(width, height, format, memory, mip_level, offset_x, offset_y);
}

void BackendMetal::readTexturePixels(TextureHandle* handle, uint32_t pos_x, uint32_t pos_y, uint32_t width, uint32_t height,
	uint32_t mip_level, void* dst_memory)
{
	auto texture = (TextureMetal*)handle;
	texture->read(pos_x, pos_y, width, height, mip_level, dst_memory);
}

void BackendMetal::generateMips(TextureHandle* handle)
{
	auto texture = (TextureMetal*)handle;
	texture->generateMips();
}

void BackendMetal::destroyTexture(TextureHandle* handle)
{
	auto texture = (TextureMetal*)handle;
	delete texture;
}

RenderTargetHandle* BackendMetal::createRenderTarget(uint32_t width, uint32_t height, TextureHandle* texture_handle)
{
	auto texture = (TextureMetal*)texture_handle;
	auto render_target = new RenderTargetMetal(width, height, texture);
	return (RenderTargetHandle*)render_target;
}

void BackendMetal::destroyRenderTarget(RenderTargetHandle* handle)
{
	auto render_target = (RenderTargetMetal*)handle;
	delete render_target;
}

ShaderHandle* BackendMetal::createShader(const VertexLayout& vertex_layout, const std::string& vertex_code,
	const std::string& fragment_code, const std::vector<std::string>& defines)
{
	auto shader = new ShaderMetal(vertex_layout, vertex_code, fragment_code, defines);
	return (ShaderHandle*)shader;
}

void BackendMetal::destroyShader(ShaderHandle* handle)
{
	auto shader = (ShaderMetal*)handle;
	
	if (gContext->pipeline_state.shader == shader)
	{
		gContext->pipeline_state.shader = nullptr;
		gContext->pipeline_state_dirty = true;
	}
	
	// TODO: erase (and free) pso with this shader from gPipelineStates
	
	delete shader;
}

VertexBufferHandle* BackendMetal::createVertexBuffer(size_t size, size_t stride)
{
	auto buffer = new BufferMetal(size); // stride ?
	return (VertexBufferHandle*)buffer;
}

void BackendMetal::destroyVertexBuffer(VertexBufferHandle* handle)
{
	auto buffer = (BufferMetal*)handle;

	if (gContext->vertex_buffer == buffer)
		gContext->vertex_buffer = nullptr;

	delete buffer;
}

void BackendMetal::writeVertexBufferMemory(VertexBufferHandle* handle, void* memory, size_t size, size_t stride)
{
	auto buffer = (BufferMetal*)handle;
	buffer->write(memory, size); // stride ?
}

IndexBufferHandle* BackendMetal::createIndexBuffer(size_t size, size_t stride)
{
	auto buffer = new IndexBufferMetal(size, stride);
	return (IndexBufferHandle*)buffer;
}

void BackendMetal::destroyIndexBuffer(IndexBufferHandle* handle)
{
	auto buffer = (IndexBufferMetal*)handle;

	if (gContext->index_buffer == buffer)
		gContext->index_buffer = nullptr;

	delete buffer;
}

void BackendMetal::writeIndexBufferMemory(IndexBufferHandle* handle, void* memory, size_t size, size_t stride)
{
	auto buffer = (IndexBufferMetal*)handle;
	buffer->write(memory, size);
	buffer->setStride(stride);
}

UniformBufferHandle* BackendMetal::createUniformBuffer(size_t size)
{
	auto buffer = new BufferMetal(size);
	return (UniformBufferHandle*)buffer;
}

void BackendMetal::destroyUniformBuffer(UniformBufferHandle* handle)
{
	auto buffer = (BufferMetal*)handle;
	delete buffer;
}

void BackendMetal::writeUniformBufferMemory(UniformBufferHandle* handle, void* memory, size_t size)
{
	auto buffer = (BufferMetal*)handle;
	buffer->write(memory, size);
}

#endif
