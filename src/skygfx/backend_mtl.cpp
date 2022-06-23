#include "backend_mtl.h"

#ifdef SKYGFX_HAS_METAL

#define NS_PRIVATE_IMPLEMENTATION
#define MTL_PRIVATE_IMPLEMENTATION
#define MTK_PRIVATE_IMPLEMENTATION
#define CA_PRIVATE_IMPLEMENTATION

#include <Metal/Metal.hpp>
#include <AppKit/AppKit.hpp>
#include <MetalKit/MetalKit.hpp>

using namespace skygfx;

static MTL::Device* gDevice = nullptr;
static MTK::View* gView = nullptr;
static MTL::CommandQueue* gCommandQueue = nullptr;

class ShaderDataMetal
{
public:
	ShaderDataMetal(const Vertex::Layout& layout, const std::string& vertex_code, const std::string& fragment_code,
		std::vector<std::string> defines)
	{
	}

	~ShaderDataMetal()
	{
	}
};

class TextureDataMetal
{
public:
	TextureDataMetal(uint32_t _width, uint32_t _height, uint32_t channels, void* memory, bool _mipmap)
	{
	}

	~TextureDataMetal()
	{
	}
};

class RenderTargetDataMetal
{
public:
	RenderTargetDataMetal(uint32_t width, uint32_t height, TextureDataMetal* _texture_data)
	{
	}

	~RenderTargetDataMetal()
	{
	}
};

BackendMetal::BackendMetal(void* window, uint32_t width, uint32_t height)
{
	gDevice = MTL::CreateSystemDefaultDevice();
	
	auto frame = CGRect{ { 0.0, 0.0 }, { (float)width, (float)height } };
	
	gView = MTK::View::alloc()->init(frame, gDevice);
	gView->setColorPixelFormat(MTL::PixelFormat::PixelFormatBGRA8Unorm_sRGB);
	gView->setClearColor(MTL::ClearColor::Make(0.0, 1.0, 0.0, 1.0));

	gCommandQueue = gDevice->newCommandQueue();

	auto _window = (NS::Window*)window;
	_window->setContentView(gView);
}

BackendMetal::~BackendMetal()
{
	gCommandQueue->release();
	gView->release();
	gDevice->release();
}

void BackendMetal::resize(uint32_t width, uint32_t height)
{
}

void BackendMetal::setTopology(Topology topology)
{
}

void BackendMetal::setViewport(const Viewport& viewport)
{
}

void BackendMetal::setScissor(const Scissor& value)
{
}

void BackendMetal::setScissor(std::nullptr_t value)
{
}

void BackendMetal::setTexture(TextureHandle* handle)
{
}

void BackendMetal::setRenderTarget(RenderTargetHandle* handle)
{
}

void BackendMetal::setRenderTarget(std::nullptr_t value)
{
}

void BackendMetal::setShader(ShaderHandle* handle)
{
}

void BackendMetal::setVertexBuffer(const Buffer& buffer)
{
}

void BackendMetal::setIndexBuffer(const Buffer& buffer)
{
}

void BackendMetal::setUniformBuffer(int slot, void* memory, size_t size)
{
}

void BackendMetal::setBlendMode(const BlendMode& value)
{
}

void BackendMetal::setDepthMode(const DepthMode& value)
{
}

void BackendMetal::setStencilMode(const StencilMode& value)
{
}

void BackendMetal::setCullMode(const CullMode& value)
{
}

void BackendMetal::setSampler(const Sampler& value)
{
}

void BackendMetal::setTextureAddressMode(const TextureAddress& value)
{
}

void BackendMetal::clear(const std::optional<glm::vec4>& color, const std::optional<float>& depth,
	const std::optional<uint8_t>& stencil)
{
}

void BackendMetal::draw(uint32_t vertex_count, uint32_t vertex_offset)
{
}

void BackendMetal::drawIndexed(uint32_t index_count, uint32_t index_offset)
{
}

void BackendMetal::readPixels(const glm::ivec2& pos, const glm::ivec2& size, TextureHandle* dst_texture_handle)
{
}

void BackendMetal::present()
{
	auto pool = NS::AutoreleasePool::alloc()->init();

	auto cmd = gCommandQueue->commandBuffer();
	auto rpd = gView->currentRenderPassDescriptor();
	auto enc = cmd->renderCommandEncoder(rpd);
	enc->endEncoding();
	cmd->presentDrawable(gView->currentDrawable());
	cmd->commit();

	pool->release();
}

TextureHandle* BackendMetal::createTexture(uint32_t width, uint32_t height, uint32_t channels, void* memory, bool mipmap)
{
	auto texture = new TextureDataMetal(width, height, channels, memory, mipmap);
	return (TextureHandle*)texture;
}

void BackendMetal::destroyTexture(TextureHandle* handle)
{
	auto texture = (TextureDataMetal*)handle;
	delete texture;
}

RenderTargetHandle* BackendMetal::createRenderTarget(uint32_t width, uint32_t height, TextureHandle* texture_handle)
{
	auto texture = (TextureDataMetal*)texture_handle;
	auto render_target = new RenderTargetDataMetal(width, height, texture);
	return (RenderTargetHandle*)render_target;
}

void BackendMetal::destroyRenderTarget(RenderTargetHandle* handle)
{
	auto render_target = (RenderTargetDataMetal*)handle;
	delete render_target;
}

ShaderHandle* BackendMetal::createShader(const Vertex::Layout& layout, const std::string& vertex_code,
	const std::string& fragment_code, const std::vector<std::string>& defines)
{
	auto shader = new ShaderDataMetal(layout, vertex_code, fragment_code, defines);
	return (ShaderHandle*)shader;
}

void BackendMetal::destroyShader(ShaderHandle* handle)
{
	auto shader = (ShaderDataMetal*)handle;
	delete shader;
}

#endif
