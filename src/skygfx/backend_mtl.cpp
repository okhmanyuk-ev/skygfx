#include "backend_mtl.h"

#ifdef SKYGFX_HAS_METAL

#include <unordered_map>

#import <AppKit/AppKit.h>
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
	RenderTargetMetal* render_target = nullptr;
	BlendMode blend_mode = BlendStates::AlphaBlend;
	
	bool operator==(const PipelineStateMetal& value) const
	{
		return
			shader == value.shader &&
			render_target == value.render_target &&
			blend_mode == value.blend_mode;
	}
};

SKYGFX_MAKE_HASHABLE(PipelineStateMetal,
	t.shader,
	t.render_target,
	t.blend_mode);
	
struct SamplerStateMetal
{
	Sampler sampler = Sampler::Linear;
	TextureAddress texture_address = TextureAddress::Clamp;

	bool operator==(const SamplerStateMetal& value) const
	{
		return
			sampler == value.sampler &&
			texture_address == value.texture_address;
	}
};

SKYGFX_MAKE_HASHABLE(SamplerStateMetal,
	t.sampler,
	t.texture_address);

static NSAutoreleasePool* gAutoreleasePool = nullptr;
static id<MTLDevice> gDevice = nullptr;
static MTKView* gView = nullptr;
static id<MTLCommandQueue> gCommandQueue = nullptr;
static id<MTLCommandBuffer> gCommandBuffer = nullptr;
static MTLRenderPassDescriptor* gRenderPassDescriptor = nullptr;
static id<MTLRenderCommandEncoder> gRenderCommandEncoder = nullptr;
static MTLPrimitiveType gPrimitiveType = MTLPrimitiveTypeTriangle;
static MTLIndexType gIndexType = MTLIndexTypeUInt16;
static IndexBufferMetal* gIndexBuffer = nullptr;
static BufferMetal* gVertexBuffer = nullptr;
static std::unordered_map<uint32_t, BufferMetal*> gUniformBuffers;
static std::unordered_map<uint32_t, TextureMetal*> gTextures;
static SamplerStateMetal gSamplerState;
static std::unordered_map<SamplerStateMetal, id<MTLSamplerState>> gSamplerStates;
static CullMode gCullMode = CullMode::None;
static const uint32_t gVertexBufferStageBinding = 30;

static PipelineStateMetal gPipelineState;
static std::unordered_map<PipelineStateMetal, id<MTLRenderPipelineState>> gPipelineStates;

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
	ShaderMetal(const Vertex::Layout& layout, const std::string& vertex_code, const std::string& fragment_code,
		std::vector<std::string> defines)
	{
		AddShaderLocationDefines(layout, defines);

		auto vertex_shader_spirv = CompileGlslToSpirv(ShaderStage::Vertex, vertex_code, defines);
		auto fragment_shader_spirv = CompileGlslToSpirv(ShaderStage::Fragment, fragment_code, defines);

		auto msl_vert = CompileSpirvToMsl(vertex_shader_spirv);
		auto msl_frag = CompileSpirvToMsl(fragment_shader_spirv);

		NSError* error = nil;
	
		mVertLib = [gDevice newLibraryWithSource:[NSString stringWithUTF8String:msl_vert.c_str()] options:nil error:&error];
	
		if (!mVertLib)
		{
			auto reason = error.localizedDescription.UTF8String;
			throw std::runtime_error(reason);
		}
	
		mFragLib = [gDevice newLibraryWithSource:[NSString stringWithUTF8String:msl_frag.c_str()] options:nil error:&error];
	
		if (!mFragLib)
		{
			auto reason = error.localizedDescription.UTF8String;
			throw std::runtime_error(reason);
		}

		mVertFunc = [mVertLib newFunctionWithName:@"main0"];
		mFragFunc = [mFragLib newFunctionWithName:@"main0"];
	
		static const std::unordered_map<Vertex::Attribute::Format, MTLVertexFormat> Format = {
			{ Vertex::Attribute::Format::R32F, MTLVertexFormatFloat },
			{ Vertex::Attribute::Format::R32G32F, MTLVertexFormatFloat2 },
			{ Vertex::Attribute::Format::R32G32B32F, MTLVertexFormatFloat3 },
			{ Vertex::Attribute::Format::R32G32B32A32F, MTLVertexFormatFloat4 },
			//{ Vertex::Attribute::Format::R8UN, },
			//{ Vertex::Attribute::Format::R8G8UN, },
			//{ Vertex::Attribute::Format::R8G8B8UN, },
			{ Vertex::Attribute::Format::R8G8B8A8UN, MTLVertexFormatUChar4Normalized }
		};
	
		mVertexDescriptor = [[MTLVertexDescriptor alloc] init];
		
		for (int i = 0; i < layout.attributes.size(); i++)
		{
			const auto& attrib = layout.attributes.at(i);
			auto desc = mVertexDescriptor.attributes[i];
			desc.format = Format.at(attrib.format);
			desc.offset = attrib.offset;
			desc.bufferIndex = gVertexBufferStageBinding;
		}

		auto vertex_layout = mVertexDescriptor.layouts[gVertexBufferStageBinding];
		vertex_layout.stride = layout.stride;
		vertex_layout.stepRate = 1;
		vertex_layout.stepFunction = MTLVertexStepFunctionPerVertex;
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
	
private:
	id<MTLTexture> mTexture = nullptr;
	
public:
	TextureMetal(uint32_t width, uint32_t height, uint32_t channels, void* memory, bool mipmap)
	{
		auto desc = [[MTLTextureDescriptor alloc] init];
		desc.width = width;
		desc.height = height;
		desc.pixelFormat = MTLPixelFormatRGBA8Unorm;
		desc.textureType = MTLTextureType2D;
		desc.storageMode = MTLStorageModeManaged;
		desc.usage = MTLResourceUsageSample | MTLResourceUsageRead | MTLResourceUsageWrite;
	
		if (mipmap)
		{
			int height_levels = ceil(log2(height));
			int width_levels = ceil(log2(width));
			int mip_count = (height_levels > width_levels) ? height_levels : width_levels;
			desc.mipmapLevelCount = mip_count;
		}

		mTexture = [gDevice newTextureWithDescriptor:desc];

		[desc release];

		if (memory != nullptr)
		{
			auto region = MTLRegionMake2D(0, 0, width, height);
			[mTexture replaceRegion:region mipmapLevel:0 withBytes:memory bytesPerRow:width * channels];
		
			if (mipmap)
			{
				auto cmd = gCommandQueue.commandBuffer;
				auto enc = cmd.blitCommandEncoder;
			
				[enc generateMipmapsForTexture:mTexture];
				[enc endEncoding];
				[cmd commit];
				[cmd waitUntilCompleted];
			}
		}
	}

	~TextureMetal()
	{
		[mTexture release];
	}
};

class RenderTargetMetal
{
public:
	auto getTexture() const { return mTexture; }
	
private:
	TextureMetal* mTexture = nullptr;
	
public:
	RenderTargetMetal(uint32_t width, uint32_t height, TextureMetal* texture) :
		mTexture(texture)
	{
	}
};

class BufferMetal
{
public:
	auto getMetalBuffer() const { return mBuffer; }
	
private:
	id<MTLBuffer> mBuffer = nullptr;
	
public:
	BufferMetal(size_t size)
	{
		mBuffer = [gDevice newBufferWithLength:size options:MTLResourceStorageModeManaged];
	}
	
	~BufferMetal()
	{
		[mBuffer release];
	}
	
	void write(void* memory, size_t size)
	{
		memcpy(mBuffer.contents, memory, size);
		[mBuffer didModifyRange:NSMakeRange(0, size)];
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

static void begin()
{
	gFrameAutoreleasePool = [[NSAutoreleasePool alloc] init];

	gCommandBuffer = gCommandQueue.commandBuffer;
	gRenderPassDescriptor = gView.currentRenderPassDescriptor;
	gRenderPassDescriptor.colorAttachments[0].storeAction = MTLStoreActionStore;
	gRenderCommandEncoder = [gCommandBuffer renderCommandEncoderWithDescriptor:gRenderPassDescriptor];
}

static void end()
{
	[gRenderCommandEncoder endEncoding];
	[gCommandBuffer presentDrawable:gView.currentDrawable];
	[gCommandBuffer commit];
	
	[gFrameAutoreleasePool release];
}

static void ensureRenderTarget()
{
	auto render_texture = gPipelineState.render_target ?
		gPipelineState.render_target->getTexture()->getMetalTexture() :
		gView.currentDrawable.texture;
	
	auto color_attachment = gRenderPassDescriptor.colorAttachments[0];
	
	if (color_attachment.texture != render_texture)
	{
		[gRenderCommandEncoder endEncoding];
		[gCommandBuffer commit];
		gCommandBuffer = gCommandQueue.commandBuffer;
		color_attachment.texture = render_texture;
		color_attachment.loadAction = MTLLoadActionLoad;
		gRenderCommandEncoder = [gCommandBuffer renderCommandEncoderWithDescriptor:gRenderPassDescriptor];
	}
}

BackendMetal::BackendMetal(void* window, uint32_t width, uint32_t height)
{
	gAutoreleasePool = [[NSAutoreleasePool alloc] init];

	gDevice = MTLCreateSystemDefaultDevice();

	auto frame = CGRectMake(0.0f, 0.0f, (float)width, (float)height);
	gView = [[MTKView alloc] initWithFrame:frame device:gDevice];

	gView.colorPixelFormat = MTLPixelFormatBGRA8Unorm_sRGB;

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

	if (contentView != nil)
	{
		[contentView addSubview:gView];
	}
	else
	{
		if (nil != nsWindow)
			[nsWindow setContentView:gView];
	}
	
	gCommandQueue = gDevice.newCommandQueue;
		
	begin();
}

BackendMetal::~BackendMetal()
{
	end();
	for (auto [_, sampler_state] : gSamplerStates)
	{
		[sampler_state release];
	}
	[gCommandQueue release];
	[gView release];
	[gDevice release];
	[gAutoreleasePool release];
}

void BackendMetal::resize(uint32_t width, uint32_t height)
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

	gPrimitiveType = TopologyMap.at(topology);
}

void BackendMetal::setViewport(std::optional<Viewport> viewport)
{
}

void BackendMetal::setScissor(std::optional<Scissor> scissor)
{
}

void BackendMetal::setTexture(uint32_t binding, TextureHandle* handle)
{
	auto texture = (TextureMetal*)handle;
	gTextures[binding] = texture;
}

void BackendMetal::setRenderTarget(RenderTargetHandle* handle)
{
	auto render_target = (RenderTargetMetal*)handle;
	gPipelineState.render_target = render_target;
}

void BackendMetal::setRenderTarget(std::nullptr_t value)
{
	gPipelineState.render_target = nullptr;
}

void BackendMetal::setShader(ShaderHandle* handle)
{
	auto shader = (ShaderMetal*)handle;
	gPipelineState.shader = shader;
}

void BackendMetal::setVertexBuffer(VertexBufferHandle* handle)
{
	auto buffer = (BufferMetal*)handle;
	gVertexBuffer = buffer;
}

void BackendMetal::setIndexBuffer(IndexBufferHandle* handle)
{
	auto buffer = (IndexBufferMetal*)handle;
	gIndexBuffer = buffer;
	gIndexType = buffer->getStride() == 2 ? MTLIndexTypeUInt16 : MTLIndexTypeUInt32;
}

void BackendMetal::setUniformBuffer(uint32_t binding, UniformBufferHandle* handle)
{
	auto buffer = (BufferMetal*)handle;
	gUniformBuffers[binding] = buffer;
}

void BackendMetal::setBlendMode(const BlendMode& value)
{
	gPipelineState.blend_mode = value;
}

void BackendMetal::setDepthMode(std::optional<DepthMode> depth_mode)
{
}

void BackendMetal::setStencilMode(std::optional<StencilMode> stencil_mode)
{
}

void BackendMetal::setCullMode(CullMode cull_mode)
{
	gCullMode = cull_mode;
}

void BackendMetal::setSampler(Sampler value)
{
	gSamplerState.sampler = value;
}

void BackendMetal::setTextureAddress(TextureAddress value)
{
	gSamplerState.texture_address = value;
}

void BackendMetal::clear(const std::optional<glm::vec4>& color, const std::optional<float>& depth,
	const std::optional<uint8_t>& stencil)
{
	ensureRenderTarget();
	
	auto col = color.value();
	auto clear_color = MTLClearColorMake(col.r, col.g, col.b, col.a);

	gRenderPassDescriptor.colorAttachments[0].clearColor = clear_color;
	gRenderPassDescriptor.colorAttachments[0].loadAction = MTLLoadActionClear;
	
	[gRenderCommandEncoder endEncoding];
	gRenderCommandEncoder = [gCommandBuffer renderCommandEncoderWithDescriptor:gRenderPassDescriptor];
}

void BackendMetal::draw(uint32_t vertex_count, uint32_t vertex_offset)
{
	prepareForDrawing();
	[gRenderCommandEncoder drawPrimitives:gPrimitiveType vertexStart:vertex_offset vertexCount:vertex_count];
}

void BackendMetal::drawIndexed(uint32_t index_count, uint32_t index_offset)
{
	prepareForDrawing();
	[gRenderCommandEncoder drawIndexedPrimitives:gPrimitiveType indexCount:index_count indexType:gIndexType indexBuffer:gIndexBuffer->getMetalBuffer() indexBufferOffset:index_offset];
}

void BackendMetal::readPixels(const glm::ivec2& pos, const glm::ivec2& size, TextureHandle* dst_texture_handle)
{
}

void BackendMetal::present()
{
	end();
	[gView draw];
	// TODO: maybe here gAutoreleasePool->drain();
	begin();
}

TextureHandle* BackendMetal::createTexture(uint32_t width, uint32_t height, uint32_t channels, void* memory, bool mipmap)
{
	auto texture = new TextureMetal(width, height, channels, memory, mipmap);
	return (TextureHandle*)texture;
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

ShaderHandle* BackendMetal::createShader(const Vertex::Layout& layout, const std::string& vertex_code,
	const std::string& fragment_code, const std::vector<std::string>& defines)
{
	auto shader = new ShaderMetal(layout, vertex_code, fragment_code, defines);
	return (ShaderHandle*)shader;
}

void BackendMetal::destroyShader(ShaderHandle* handle)
{
	auto shader = (ShaderMetal*)handle;
	
	if (gPipelineState.shader == shader)
		gPipelineState.shader = nullptr;

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

	if (gVertexBuffer == buffer)
		gVertexBuffer = nullptr;

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
	
	if (gIndexBuffer == buffer)
		gIndexBuffer = nullptr;
	
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

void BackendMetal::prepareForDrawing()
{
	ensureRenderTarget();
	
	if (!gSamplerStates.contains(gSamplerState))
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
		desc.magFilter = SamplerMinMagFilter.at(gSamplerState.sampler);
		desc.minFilter = SamplerMinMagFilter.at(gSamplerState.sampler);
		desc.mipFilter = SamplerMipFilter.at(gSamplerState.sampler);
		desc.rAddressMode = TextureAddressMap.at(gSamplerState.texture_address);
		desc.sAddressMode = TextureAddressMap.at(gSamplerState.texture_address);
		desc.tAddressMode = TextureAddressMap.at(gSamplerState.texture_address);
		
		auto sampler_state = [gDevice newSamplerStateWithDescriptor:desc];
		
		gSamplerStates.insert({ gSamplerState, sampler_state });
		
		[desc release];
	}
	
	for (auto [binding, texture] : gTextures)
	{
		[gRenderCommandEncoder setFragmentTexture:texture->getMetalTexture() atIndex:binding];
		[gRenderCommandEncoder setFragmentSamplerState:gSamplerStates.at(gSamplerState) atIndex:binding];
	}

	[gRenderCommandEncoder setVertexBuffer:gVertexBuffer->getMetalBuffer() offset:0 atIndex:gVertexBufferStageBinding];

	if (!gPipelineStates.contains(gPipelineState))
	{
		auto shader = gPipelineState.shader;
		
		auto pixel_format = gPipelineState.render_target ?
			MTLPixelFormatRGBA8Unorm :
			MTLPixelFormatBGRA8Unorm_sRGB;
		
		auto desc = [[MTLRenderPipelineDescriptor alloc] init];
		desc.vertexFunction = shader->getMetalVertFunc();
		desc.fragmentFunction = shader->getMetalFragFunc();
		desc.vertexDescriptor = shader->getMetalVertexDescriptor();
		
		auto attachment_0 = desc.colorAttachments[0];
		attachment_0.pixelFormat = pixel_format;

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
		
		const auto& blend_mode = gPipelineState.blend_mode;
		
		attachment_0.blendingEnabled = true;
		
		attachment_0.sourceRGBBlendFactor = BlendMap.at(blend_mode.color_src_blend);
		attachment_0.sourceAlphaBlendFactor = BlendMap.at(blend_mode.alpha_src_blend);
		attachment_0.destinationRGBBlendFactor = BlendMap.at(blend_mode.color_dst_blend);
		attachment_0.destinationAlphaBlendFactor = BlendMap.at(blend_mode.alpha_dst_blend);
		
		attachment_0.rgbBlendOperation = BlendOpMap.at(blend_mode.color_blend_func);
		attachment_0.alphaBlendOperation = BlendOpMap.at(blend_mode.alpha_blend_func);

		NSError* error = nullptr;

		auto pso = [gDevice newRenderPipelineStateWithDescriptor:desc error:&error];

		if (!pso)
		{
			auto reason = error.localizedDescription.UTF8String;
			throw std::runtime_error(reason);
		}
		
		gPipelineStates[gPipelineState] = pso;
	}
	
	auto pso = gPipelineStates.at(gPipelineState);
	
	[gRenderCommandEncoder setRenderPipelineState:pso];

	for (auto [binding, buffer] : gUniformBuffers)
	{
		[gRenderCommandEncoder setVertexBuffer:buffer->getMetalBuffer() offset:0 atIndex:binding];
		[gRenderCommandEncoder setFragmentBuffer:buffer->getMetalBuffer() offset:0 atIndex:binding];
	}
	
	static const std::unordered_map<CullMode, MTLCullMode> CullModes = {
		{ CullMode::None, MTLCullModeNone },
		{ CullMode::Back, MTLCullModeBack },
		{ CullMode::Front, MTLCullModeFront }
	};
	
	[gRenderCommandEncoder setCullMode:CullModes.at(gCullMode)];
	[gRenderCommandEncoder setFrontFacingWinding:MTLWindingClockwise];
}

#endif
