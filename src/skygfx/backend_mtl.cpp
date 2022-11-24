#include "backend_mtl.h"

#ifdef SKYGFX_HAS_METAL

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
	
struct DepthStencilStateMetal
{
	std::optional<DepthMode> depth_mode;
	std::optional<StencilMode> stencil_mode;

	bool operator==(const DepthStencilStateMetal& value) const
	{
		return
			depth_mode == value.depth_mode &&
			stencil_mode == value.stencil_mode;
	}
};

SKYGFX_MAKE_HASHABLE(DepthStencilStateMetal,
	t.depth_mode,
	t.stencil_mode);

static NSAutoreleasePool* gAutoreleasePool = nullptr;
static id<MTLDevice> gDevice = nullptr;
static MTKView* gView = nullptr;
static id<MTLCommandQueue> gCommandQueue = nullptr;
static id<MTLCommandBuffer> gCommandBuffer = nullptr;
static MTLRenderPassDescriptor* gRenderPassDescriptor = nullptr;
static id<MTLRenderCommandEncoder> gRenderCommandEncoder = nullptr;
static id<MTLBlitCommandEncoder> gBlitCommandEncoder = nullptr;
static MTLPrimitiveType gPrimitiveType = MTLPrimitiveTypeTriangle;
static MTLIndexType gIndexType = MTLIndexTypeUInt16;
static IndexBufferMetal* gIndexBuffer = nullptr;
static BufferMetal* gVertexBuffer = nullptr;
static std::unordered_map<uint32_t, BufferMetal*> gUniformBuffers;

static std::unordered_set<uint32_t> gDirtyTextures;
static std::unordered_map<uint32_t, TextureMetal*> gTextures;

static bool gCullModeDirty = true;
static CullMode gCullMode = CullMode::None;

static const uint32_t gVertexBufferStageBinding = 30;

static float gBackbufferScaleFactor = 1.0f;
static uint32_t gBackbufferWidth = 0;
static uint32_t gBackbufferHeight = 0;

static bool gViewportDirty = true;
static std::optional<Viewport> gViewport;

static bool gScissorDirty = true;
static std::optional<Scissor> gScissor;

static bool gSamplerStateDirty = true;
static SamplerStateMetal gSamplerState;
static std::unordered_map<SamplerStateMetal, id<MTLSamplerState>> gSamplerStates;

static bool gDepthStencilStateDirty = true;
static DepthStencilStateMetal gDepthStencilState;
static std::unordered_map<DepthStencilStateMetal, id<MTLDepthStencilState>> gDepthStencilStates;

static bool gPipelineStateDirty = true;
static PipelineStateMetal gPipelineState;
static std::unordered_map<PipelineStateMetal, id<MTLRenderPipelineState>> gPipelineStates;

static std::unordered_set<BufferMetal*> gUsedBuffersInThisFrame;

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
			{ Vertex::Attribute::Format::Float1, MTLVertexFormatFloat },
			{ Vertex::Attribute::Format::Float2, MTLVertexFormatFloat2 },
			{ Vertex::Attribute::Format::Float3, MTLVertexFormatFloat3 },
			{ Vertex::Attribute::Format::Float4, MTLVertexFormatFloat4 },
			{ Vertex::Attribute::Format::Byte1, MTLVertexFormatUCharNormalized },
			{ Vertex::Attribute::Format::Byte2, MTLVertexFormatUChar2Normalized },
			{ Vertex::Attribute::Format::Byte3, MTLVertexFormatUChar3Normalized },
			{ Vertex::Attribute::Format::Byte4, MTLVertexFormatUChar4Normalized }
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
	auto isMipmap() const { return mMipmap; }
	
private:
	id<MTLTexture> mTexture = nullptr;
	bool mMipmap = false;
	
public:
	TextureMetal(uint32_t width, uint32_t height, uint32_t channels, void* memory, bool mipmap) : mMipmap(mipmap)
	{
		auto desc = [[MTLTextureDescriptor alloc] init];
		desc.width = width;
		desc.height = height;
		desc.pixelFormat = MTLPixelFormatRGBA8Unorm;
		desc.textureType = MTLTextureType2D;
		desc.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
#if defined(SKYGFX_PLATFORM_MACOS)
		desc.storageMode = MTLStorageModeManaged;
#elif defined(SKYGFX_PLATFORM_IOS)
		desc.storageMode = MTLStorageModeShared;
#endif
	
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
				generateMips();
			}
		}
	}

	~TextureMetal()
	{
		[mTexture release];
	}
	
	void generateMips()
	{
		assert(mMipmap);
		
		if (!mMipmap)
			return;
		
		auto cmd = gCommandQueue.commandBuffer;
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
	
private:
	TextureMetal* mTexture = nullptr;
	
public:
	RenderTargetMetal(TextureMetal* texture) :
		mTexture(texture)
	{
	}
};

class BufferMetal
{
private:
	std::vector<id<MTLBuffer>> mBuffers;
	size_t mSize = 0;
	bool mUsed = false;
	uint32_t mIndex = 0;
	
public:
	BufferMetal(size_t size) : mSize(size)
	{
		mBuffers.push_back(createBuffer());
	}
	
	~BufferMetal()
	{
		for (auto buffer : mBuffers)
		{
			[buffer release];
		}
	}
	
	void write(void* memory, size_t size)
	{
		assert(size <= mSize);
		
		if (mUsed)
		{
			mIndex += 1;
			mUsed = false;
		}
		
		if (mIndex >= mBuffers.size())
		{
			mBuffers.push_back(createBuffer());
		}
		
		auto buffer = mBuffers.at(mIndex);

		memcpy(buffer.contents, memory, size);
#if defined(SKYGFX_PLATFORM_MACOS)
		[buffer didModifyRange:NSMakeRange(0, size)];
#endif
	}
	
	void markUsed()
	{
		mUsed = true;
	}
	
	void resetIndex()
	{
		mIndex = 0;
		mUsed = false;
	}
	
	auto getMetalBuffer() const
	{
		return mBuffers.at(mIndex);
	}
	
private:
	id<MTLBuffer> createBuffer()
	{
#if defined(SKYGFX_PLATFORM_MACOS)
		auto storageMode = MTLResourceStorageModeManaged;
#elif defined(SKYGFX_PLATFORM_IOS)
		auto storageMode = MTLResourceStorageModeShared;
#endif

		return [gDevice newBufferWithLength:mSize options:storageMode];
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

static void endEncoding()
{
	if (gRenderCommandEncoder)
	{
		[gRenderCommandEncoder endEncoding];
		gRenderCommandEncoder = nullptr;
	}
	
	if (gBlitCommandEncoder)
	{
		[gBlitCommandEncoder endEncoding];
		gBlitCommandEncoder = nullptr;
	}
}

static void beginRenderEncoding()
{
	assert(gRenderCommandEncoder == nullptr);
	endEncoding();
	gRenderCommandEncoder = [gCommandBuffer renderCommandEncoderWithDescriptor:gRenderPassDescriptor];
}

static void beginBlitEncoding()
{
	assert(gBlitCommandEncoder == nullptr);
	endEncoding();
	gBlitCommandEncoder = [gCommandBuffer blitCommandEncoder];
}

static void begin()
{
	gFrameAutoreleasePool = [[NSAutoreleasePool alloc] init];

	gCommandBuffer = gCommandQueue.commandBuffer;
	gRenderPassDescriptor = gView.currentRenderPassDescriptor;
	gRenderPassDescriptor.colorAttachments[0].storeAction = MTLStoreActionStore;
	
	beginRenderEncoding();
	
	gCullModeDirty = true;
	gViewportDirty = true;
	gScissorDirty = true;
	gSamplerStateDirty = true;
	gDepthStencilStateDirty = true;
	gPipelineStateDirty = true;
	
	for (auto [binding, texture] : gTextures)
	{
		gDirtyTextures.insert(binding);
	}
	
	auto width = (uint32_t)gView.frame.size.width;
	auto height = (uint32_t)gView.frame.size.height;
	
	if (gBackbufferWidth != width || gBackbufferHeight != height)
	{
		gBackbufferWidth = width;
		gBackbufferHeight = height;
		
		if (!gViewport.has_value())
			gViewportDirty = true;
	
		if (!gScissor.has_value())
			gScissorDirty = true;
	}
}

static void end()
{
	endEncoding();
	
	[gCommandBuffer presentDrawable:gView.currentDrawable];
	[gCommandBuffer commit];
	[gCommandBuffer waitUntilCompleted];
	
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
		endEncoding();
		color_attachment.texture = render_texture;
		beginRenderEncoding();
	}
}

BackendMetal::BackendMetal(void* window, uint32_t width, uint32_t height)
{
	gAutoreleasePool = [[NSAutoreleasePool alloc] init];

	gDevice = MTLCreateSystemDefaultDevice();

	auto frame = CGRectMake(0.0f, 0.0f, (float)width, (float)height);
	
	gView = [[MTKView alloc] initWithFrame:frame device:gDevice];
	gView.colorPixelFormat = MTLPixelFormatRGBA8Unorm;
	gView.depthStencilPixelFormat = MTLPixelFormatDepth32Float_Stencil8;
	gView.paused = YES;
	gView.enableSetNeedsDisplay = NO;
	gView.framebufferOnly = NO;
	
	auto metal_layer = (CAMetalLayer*)gView.layer;
	metal_layer.magnificationFilter = kCAFilterNearest;
	
#if defined(SKYGFX_PLATFORM_MACOS)
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
	
	[gView setAutoresizingMask:(NSViewHeightSizable | NSViewWidthSizable | NSViewMinXMargin |
		NSViewMaxXMargin | NSViewMinYMargin | NSViewMaxYMargin )];

	gBackbufferScaleFactor = gView.window.backingScaleFactor;
#elif defined(SKYGFX_PLATFORM_IOS)
	auto _window = (UIWindow*)window;
	auto root_view = [[_window rootViewController] view];
	[root_view addSubview:gView];
#endif
	
	gBackbufferWidth = width;
	gBackbufferHeight = height;

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
	if (gViewport == viewport)
		return;
		
	gViewport = viewport;
	gViewportDirty = true;
}

void BackendMetal::setScissor(std::optional<Scissor> scissor)
{
	if (gScissor == scissor)
		return;
	
	gScissor = scissor;
	gScissorDirty = true;
}

void BackendMetal::setTexture(uint32_t binding, TextureHandle* handle)
{
	auto texture = (TextureMetal*)handle;
	
	if (gTextures[binding] == texture)
		return;
	
	gTextures[binding] = texture;
	gDirtyTextures.insert(binding);
	gSamplerStateDirty = true;
}

void BackendMetal::setRenderTarget(RenderTargetHandle* handle)
{
	auto render_target = (RenderTargetMetal*)handle;
	
	if (gPipelineState.render_target == render_target)
		return;
	
	gPipelineState.render_target = render_target;
	gPipelineStateDirty = true;
}

void BackendMetal::setRenderTarget(std::nullptr_t value)
{
	if (gPipelineState.render_target == nullptr)
		return;

	gPipelineState.render_target = nullptr;
	gPipelineStateDirty = true;
}

void BackendMetal::setShader(ShaderHandle* handle)
{
	auto shader = (ShaderMetal*)handle;
	
	if (gPipelineState.shader == shader)
		return;
	
	gPipelineState.shader = shader;
	gPipelineStateDirty = true;
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
	if (gPipelineState.blend_mode == value)
		return;
	
	gPipelineState.blend_mode = value;
	gPipelineStateDirty = true;
}

void BackendMetal::setDepthMode(std::optional<DepthMode> depth_mode)
{
	if (gDepthStencilState.depth_mode == depth_mode)
		return;
	
	gDepthStencilState.depth_mode = depth_mode;
	gDepthStencilStateDirty = true;
}

void BackendMetal::setStencilMode(std::optional<StencilMode> stencil_mode)
{
	if (gDepthStencilState.stencil_mode == stencil_mode)
		return;
	
	gDepthStencilState.stencil_mode = stencil_mode;
	gDepthStencilStateDirty = true;
}

void BackendMetal::setCullMode(CullMode cull_mode)
{
	if (gCullMode == cull_mode)
		return;

	gCullMode = cull_mode;
	gCullModeDirty = true;
}

void BackendMetal::setSampler(Sampler value)
{
	if (gSamplerState.sampler == value)
		return;
		
	gSamplerState.sampler = value;
	gSamplerStateDirty = true;
}

void BackendMetal::setTextureAddress(TextureAddress value)
{
	if (gSamplerState.texture_address == value)
		return;
		
	gSamplerState.texture_address = value;
	gSamplerStateDirty = true;
}

void BackendMetal::clear(const std::optional<glm::vec4>& color, const std::optional<float>& depth,
	const std::optional<uint8_t>& stencil)
{
	ensureRenderTarget();

	if (color.has_value())
	{
		auto col = color.value();

		gRenderPassDescriptor.colorAttachments[0].clearColor = MTLClearColorMake(col.r, col.g, col.b, col.a);
		gRenderPassDescriptor.colorAttachments[0].loadAction = MTLLoadActionClear;
	}

	if (depth.has_value())
	{
		gRenderPassDescriptor.depthAttachment.clearDepth = depth.value();
		gRenderPassDescriptor.depthAttachment.loadAction = MTLLoadActionClear;
	}

	if (stencil.has_value())
	{
		gRenderPassDescriptor.stencilAttachment.clearStencil = stencil.value();
		gRenderPassDescriptor.stencilAttachment.loadAction = MTLLoadActionClear;
	}

	endEncoding();
	beginRenderEncoding();
	
	gRenderPassDescriptor.colorAttachments[0].loadAction = MTLLoadActionLoad;
	gRenderPassDescriptor.depthAttachment.loadAction = MTLLoadActionLoad;
	gRenderPassDescriptor.stencilAttachment.loadAction = MTLLoadActionLoad;
}

void BackendMetal::draw(uint32_t vertex_count, uint32_t vertex_offset)
{
	prepareForDrawing();
	[gRenderCommandEncoder drawPrimitives:gPrimitiveType vertexStart:vertex_offset
		vertexCount:vertex_count];
}

void BackendMetal::drawIndexed(uint32_t index_count, uint32_t index_offset)
{
	prepareForDrawing();
	
	auto index_size = gIndexType == MTLIndexTypeUInt32 ? 4 : 2;
	
	gIndexBuffer->markUsed();
	gUsedBuffersInThisFrame.insert(gIndexBuffer);
	
	[gRenderCommandEncoder drawIndexedPrimitives:gPrimitiveType indexCount:index_count
		indexType:gIndexType indexBuffer:gIndexBuffer->getMetalBuffer()
		indexBufferOffset:index_offset * index_size];
}

void BackendMetal::readPixels(const glm::ivec2& pos, const glm::ivec2& size, TextureHandle* dst_texture_handle)
{
	if (size.x <= 0 || size.y <= 0)
		return;
		
	if (pos.x + size.x <= 0 || pos.y + size.y <= 0)
		return;

	auto dst_texture = (TextureMetal*)dst_texture_handle;

	assert(dst_texture->getMetalTexture().width == size.x);
	assert(dst_texture->getMetalTexture().height == size.y);

	ensureRenderTarget();
	
	auto src_texture = gRenderPassDescriptor.colorAttachments[0].texture;
	
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

	//if (gPipelineState.render_target == nullptr) // TODO: should be uncommented or removed
	//{
	//	src_x *= gBackbufferScaleFactor;
	//	src_y *= gBackbufferScaleFactor;
	//	src_w *= gBackbufferScaleFactor;
	//	src_h *= gBackbufferScaleFactor;
	//}
	
	beginBlitEncoding();

	[gBlitCommandEncoder
		copyFromTexture:src_texture
		sourceSlice:0
		sourceLevel:0
		sourceOrigin:MTLOriginMake((uint32_t)src_x, (uint32_t)src_y, 0)
		sourceSize:MTLSizeMake((uint32_t)src_w, (uint32_t)src_h, 1)
		toTexture:dst_texture->getMetalTexture()
		destinationSlice:0
		destinationLevel:0
		destinationOrigin:MTLOriginMake((uint32_t)dst_x, (uint32_t)dst_y, 0)
	];
	
	if (dst_texture->isMipmap())
		[gBlitCommandEncoder generateMipmapsForTexture:dst_texture->getMetalTexture()];
	
	beginRenderEncoding();
}

void BackendMetal::present()
{
	end();

	[gView draw];
	
	for (auto buffer : gUsedBuffersInThisFrame)
	{
		buffer->resetIndex();
	}

	gUsedBuffersInThisFrame.clear();
	
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
	auto render_target = new RenderTargetMetal(texture);
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
	{
		gPipelineState.shader = nullptr;
		gPipelineStateDirty = true;
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
	
	if (gSamplerStateDirty)
	{
		gSamplerStateDirty = false;
	
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
			[gRenderCommandEncoder setFragmentSamplerState:gSamplerStates.at(gSamplerState) atIndex:binding];
		}
	}
	
	for (auto binding : gDirtyTextures)
	{
		auto texture = gTextures.at(binding);
		[gRenderCommandEncoder setFragmentTexture:texture->getMetalTexture() atIndex:binding];
	}
	
	gDirtyTextures.clear();

	gVertexBuffer->markUsed();
	gUsedBuffersInThisFrame.insert(gVertexBuffer);

	[gRenderCommandEncoder setVertexBuffer:gVertexBuffer->getMetalBuffer() offset:0 atIndex:gVertexBufferStageBinding];
	
	if (gDepthStencilStateDirty)
	{
		gDepthStencilStateDirty = false;
		
		if (!gDepthStencilStates.contains(gDepthStencilState))
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
		
			auto depth_mode = gDepthStencilState.depth_mode.value_or(DepthMode());
			auto stencil_mode = gDepthStencilState.stencil_mode.value_or(StencilMode());
		
			auto desc = [[MTLDepthStencilDescriptor alloc] init];
			desc.depthWriteEnabled = gDepthStencilState.depth_mode.has_value();
			desc.depthCompareFunction = ComparisonFuncMap.at(depth_mode.func);

			desc.backFaceStencil.depthFailureOperation = StencilOpMap.at(stencil_mode.depth_fail_op);
			desc.backFaceStencil.stencilFailureOperation = StencilOpMap.at(stencil_mode.fail_op);
			desc.backFaceStencil.stencilCompareFunction = ComparisonFuncMap.at(stencil_mode.func);
			desc.backFaceStencil.depthStencilPassOperation = StencilOpMap.at(stencil_mode.pass_op);
			desc.backFaceStencil.readMask = stencil_mode.read_mask;
			desc.backFaceStencil.writeMask = stencil_mode.write_mask;
			
			desc.frontFaceStencil = desc.backFaceStencil;

			auto depth_stencil_state = [gDevice newDepthStencilStateWithDescriptor:desc];

			gDepthStencilStates.insert({ gDepthStencilState, depth_stencil_state });

			[desc release];
		}

		if (gDepthStencilState.stencil_mode.has_value())
		{
			auto reference = gDepthStencilState.stencil_mode.value().reference;
			[gRenderCommandEncoder setStencilReferenceValue:reference];
		}

		[gRenderCommandEncoder setDepthStencilState:gDepthStencilStates.at(gDepthStencilState)];
	}

	if (gPipelineStateDirty)
	{
		gPipelineStateDirty = false;

		if (!gPipelineStates.contains(gPipelineState))
		{
			auto shader = gPipelineState.shader;
			
			auto pixel_format = gPipelineState.render_target ?
				gPipelineState.render_target->getTexture()->getMetalTexture().pixelFormat :
				gView.colorPixelFormat;
			
			auto depth_stencil_pixel_format = gView.depthStencilPixelFormat;
			
			auto desc = [[MTLRenderPipelineDescriptor alloc] init];
			desc.vertexFunction = shader->getMetalVertFunc();
			desc.fragmentFunction = shader->getMetalFragFunc();
			desc.vertexDescriptor = shader->getMetalVertexDescriptor();
			desc.depthAttachmentPixelFormat = depth_stencil_pixel_format;
			desc.stencilAttachmentPixelFormat = depth_stencil_pixel_format;
			
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

			attachment_0.writeMask = MTLColorWriteMaskNone;
			
			if (blend_mode.color_mask.red)
				attachment_0.writeMask |= MTLColorWriteMaskRed;
			
			if (blend_mode.color_mask.green)
				attachment_0.writeMask |= MTLColorWriteMaskGreen;
			
			if (blend_mode.color_mask.blue)
				attachment_0.writeMask |= MTLColorWriteMaskBlue;
			
			if (blend_mode.color_mask.alpha)
				attachment_0.writeMask |= MTLColorWriteMaskAlpha;
					
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
	}

	for (auto [binding, buffer] : gUniformBuffers)
	{
		buffer->markUsed();
		gUsedBuffersInThisFrame.insert(buffer);

		[gRenderCommandEncoder setVertexBuffer:buffer->getMetalBuffer() offset:0 atIndex:binding];
		[gRenderCommandEncoder setFragmentBuffer:buffer->getMetalBuffer() offset:0 atIndex:binding];
	}
		
	if (gCullModeDirty)
	{
		gCullModeDirty = false;
		
		static const std::unordered_map<CullMode, MTLCullMode> CullModes = {
			{ CullMode::None, MTLCullModeNone },
			{ CullMode::Back, MTLCullModeBack },
			{ CullMode::Front, MTLCullModeFront }
		};
		[gRenderCommandEncoder setCullMode:CullModes.at(gCullMode)];
		[gRenderCommandEncoder setFrontFacingWinding:MTLWindingClockwise];
	}
	
	float width;
	float height;

	if (gPipelineState.render_target == nullptr)
	{
		width = static_cast<float>(gBackbufferWidth);
		height = static_cast<float>(gBackbufferHeight);
	}
	else
	{
		auto texture = gPipelineState.render_target->getTexture()->getMetalTexture();
		
		width = static_cast<float>(texture.width);
		height = static_cast<float>(texture.height);
	}
	
	if (gViewportDirty)
	{
		gViewportDirty = false;

		auto _viewport = gViewport.value_or(Viewport{ { 0.0f, 0.0f }, { width, height } });

		if (gPipelineState.render_target == nullptr)
		{
			_viewport.position *= gBackbufferScaleFactor;
			_viewport.size *= gBackbufferScaleFactor;
		}

		MTLViewport viewport;
		viewport.originX = _viewport.position.x;
		viewport.originY = _viewport.position.y;
		viewport.width = _viewport.size.x;
		viewport.height = _viewport.size.y;
		viewport.znear = _viewport.min_depth;
		viewport.zfar = _viewport.max_depth;

		[gRenderCommandEncoder setViewport:viewport];
	}

	if (gScissorDirty)
	{
		gScissorDirty = false;
		
		auto _scissor = gScissor.value_or(Scissor{ { 0.0f, 0.0f }, { width, height } });
		
		if (gPipelineState.render_target == nullptr)
		{
			_scissor.position *= gBackbufferScaleFactor;
			_scissor.size *= gBackbufferScaleFactor;
			
			width *= gBackbufferScaleFactor;
			height *= gBackbufferScaleFactor;
		}
		
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

		[gRenderCommandEncoder setScissorRect:scissor];
	}
}

#endif
