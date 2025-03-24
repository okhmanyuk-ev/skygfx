#include "backend_gl.h"

#ifdef SKYGFX_HAS_OPENGL

//#define SKYGFX_OPENGL_VALIDATION_ENABLED

#include <unordered_map>
#include <unordered_set>
#include <stdexcept>
#include <iostream>
#include "shader_compiler.h"

#if defined(SKYGFX_PLATFORM_WINDOWS)
	#define GLEW_STATIC
	#include <GL/glew.h>
	#include <GL/wglew.h>
	#pragma comment(lib, "opengl32")
	#pragma comment(lib, "glu32")
#elif defined(SKYGFX_PLATFORM_IOS)
	#include <OpenGLES/ES3/gl.h>
	#include <OpenGLES/ES3/glext.h>
	#import <Foundation/Foundation.h>
	#import <UIKit/UIKit.h>
	#import <GLKit/GLKit.h>
#elif defined(SKYGFX_PLATFORM_MACOS)
	#include <OpenGL/OpenGL.h>
	#include <OpenGL/gl3.h>
	#import <AppKit/AppKit.h>
#elif defined(SKYGFX_PLATFORM_EMSCRIPTEN)
	#include <EGL/egl.h>
	#include <EGL/eglext.h>
	#include <EGL/eglplatform.h>
	#include <GLES3/gl3.h>
#endif

#if defined(SKYGFX_PLATFORM_WINDOWS)
extern "C" {
	_declspec(dllexport) uint32_t NvOptimusEnablement = 1;
	_declspec(dllexport) uint32_t AmdPowerXpressRequestHighPerformance = 1;
}
#endif

#if defined(SKYGFX_PLATFORM_EMSCRIPTEN)
#define GL_TEXTURE_MAX_ANISOTROPY_EXT 0x84FE
#endif

using namespace skygfx;

#ifdef SKYGFX_OPENGL_VALIDATION_ENABLED
void GLAPIENTRY DebugMessageCallback(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length,
	const GLchar* message, const void* userParam)
{
	static const std::unordered_map<GLenum, std::string> SourceMap = {
		{ GL_DEBUG_SOURCE_API, "GL_DEBUG_SOURCE_API" },
		{ GL_DEBUG_SOURCE_WINDOW_SYSTEM, "GL_DEBUG_SOURCE_WINDOW_SYSTEM" },
		{ GL_DEBUG_SOURCE_SHADER_COMPILER, "GL_DEBUG_SOURCE_SHADER_COMPILER" },
		{ GL_DEBUG_SOURCE_THIRD_PARTY, "GL_DEBUG_SOURCE_THIRD_PARTY" },
		{ GL_DEBUG_SOURCE_APPLICATION, "GL_DEBUG_SOURCE_APPLICATION" },
		{ GL_DEBUG_SOURCE_OTHER, "GL_DEBUG_SOURCE_OTHER" },
	};

	static const std::unordered_map<GLenum, std::string> TypeMap = {
		{ GL_DEBUG_TYPE_ERROR, "GL_DEBUG_TYPE_ERROR" },
		{ GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR, "GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR" },
		{ GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR, "GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR" },
		{ GL_DEBUG_TYPE_PORTABILITY, "GL_DEBUG_TYPE_PORTABILITY" },
		{ GL_DEBUG_TYPE_PERFORMANCE, "GL_DEBUG_TYPE_PERFORMANCE" },
		{ GL_DEBUG_TYPE_MARKER, "GL_DEBUG_TYPE_MARKER" },
		{ GL_DEBUG_TYPE_PUSH_GROUP, "GL_DEBUG_TYPE_PUSH_GROUP" },
		{ GL_DEBUG_TYPE_POP_GROUP, "GL_DEBUG_TYPE_POP_GROUP" },
		{ GL_DEBUG_TYPE_OTHER, "GL_DEBUG_TYPE_OTHER" },
	};

	static const std::unordered_map<GLenum, std::string> SeverityMap = {
		{ GL_DEBUG_SEVERITY_HIGH, "GL_DEBUG_SEVERITY_HIGH" },
		{ GL_DEBUG_SEVERITY_MEDIUM, "GL_DEBUG_SEVERITY_MEDIUM" },
		{ GL_DEBUG_SEVERITY_LOW, "GL_DEBUG_SEVERITY_LOW" },
		{ GL_DEBUG_SEVERITY_NOTIFICATION, "GL_DEBUG_SEVERITY_NOTIFICATION" },
	};

	std::string source_str = "unknown";
	std::string type_str = "unknown";
	std::string severity_str = "unknown";

	if (SourceMap.contains(source))
		source_str = SourceMap.at(source);

	if (TypeMap.contains(type))
		type_str = TypeMap.at(type);

	if (SeverityMap.contains(severity))
		severity_str = SeverityMap.at(severity);

	std::cout << "[opengl debug] name: " << source_str <<
		", type: " << type_str <<
		", id: " << id <<
		", severity: " << severity_str <<
		", message: " << message << std::endl;
}

void FlushErrors()
{
	static const std::unordered_map<GLenum, std::string> ErrorMap = {
		{ GL_INVALID_ENUM, "GL_INVALID_ENUM" }, // Set when an enumeration parameter is not legal.
		{ GL_INVALID_VALUE, "GL_INVALID_VALUE" }, // Set when a value parameter is not legal.
		{ GL_INVALID_OPERATION, "GL_INVALID_OPERATION" }, // Set when the state for a command is not legal for its given parameters.
#ifdef GL_STACK_OVERFLOW // emscripten
		{ GL_STACK_OVERFLOW, "GL_STACK_OVERFLOW" }, // Set when a stack pushing operation causes a stack overflow.
#endif
#ifdef GL_STACK_UNDERFLOW // emscripten
		{ GL_STACK_UNDERFLOW, "GL_STACK_UNDERFLOW" }, // Set when a stack popping operation occurs while the stack is at its lowest point.
#endif
		{ GL_OUT_OF_MEMORY, "GL_OUT_OF_MEMORY" }, // Set when a memory allocation operation cannot allocate(enough) memory.
		{ GL_INVALID_FRAMEBUFFER_OPERATION, "GL_INVALID_FRAMEBUFFER_OPERATION" }, // Set when reading or writing to a framebuffer that is not complete.
	};

	while(true)
	{
		auto error = glGetError();

		if (error == GL_NO_ERROR)
			break;

		auto name = ErrorMap.contains(error) ? ErrorMap.at(error) : "UNKNOWN";

		std::cout << "[opengl] error: " << name << " (" << error << ")" << std::endl;
	}
}
#endif

static const std::unordered_map<VertexFormat, GLint> VertexFormatSizeMap = {
	{ VertexFormat::Float1, 1 },
	{ VertexFormat::Float2, 2 },
	{ VertexFormat::Float3, 3 },
	{ VertexFormat::Float4, 4 },
	{ VertexFormat::UChar1, 1 },
	{ VertexFormat::UChar2, 2 },
	{ VertexFormat::UChar4, 4 },
	{ VertexFormat::UChar1Normalized, 1 },
	{ VertexFormat::UChar2Normalized, 2 },
	{ VertexFormat::UChar4Normalized, 4 }
};

static const std::unordered_map<VertexFormat, GLint> VertexFormatTypeMap = {
	{ VertexFormat::Float1, GL_FLOAT },
	{ VertexFormat::Float2, GL_FLOAT },
	{ VertexFormat::Float3, GL_FLOAT },
	{ VertexFormat::Float4, GL_FLOAT },
	{ VertexFormat::UChar1, GL_UNSIGNED_BYTE },
	{ VertexFormat::UChar2, GL_UNSIGNED_BYTE },
	{ VertexFormat::UChar4, GL_UNSIGNED_BYTE },
	{ VertexFormat::UChar1Normalized, GL_UNSIGNED_BYTE },
	{ VertexFormat::UChar2Normalized, GL_UNSIGNED_BYTE },
	{ VertexFormat::UChar4Normalized, GL_UNSIGNED_BYTE }
};

static const std::unordered_map<PixelFormat, GLenum> PixelFormatTypeMap = {
	{ PixelFormat::R32Float, GL_FLOAT },
	{ PixelFormat::RG32Float, GL_FLOAT },
	{ PixelFormat::RGB32Float, GL_FLOAT },
	{ PixelFormat::RGBA32Float, GL_FLOAT },
	{ PixelFormat::R8UNorm, GL_UNSIGNED_BYTE },
	{ PixelFormat::RG8UNorm, GL_UNSIGNED_BYTE },
	{ PixelFormat::RGBA8UNorm, GL_UNSIGNED_BYTE },
};

static const std::unordered_map<VertexFormat, GLboolean> VertexFormatNormalizeMap = {
	{ VertexFormat::Float1, GL_FALSE },
	{ VertexFormat::Float2, GL_FALSE },
	{ VertexFormat::Float3, GL_FALSE },
	{ VertexFormat::Float4, GL_FALSE },
	{ VertexFormat::UChar1, GL_FALSE },
	{ VertexFormat::UChar2, GL_FALSE },
	{ VertexFormat::UChar4, GL_FALSE },
	{ VertexFormat::UChar1Normalized, GL_TRUE },
	{ VertexFormat::UChar2Normalized, GL_TRUE },
	{ VertexFormat::UChar4Normalized, GL_TRUE }
};

static const std::unordered_map<ComparisonFunc, GLenum> ComparisonFuncMap = {
	{ ComparisonFunc::Always, GL_ALWAYS },
	{ ComparisonFunc::Never, GL_NEVER },
	{ ComparisonFunc::Less, GL_LESS },
	{ ComparisonFunc::Equal, GL_EQUAL },
	{ ComparisonFunc::NotEqual, GL_NOTEQUAL },
	{ ComparisonFunc::LessEqual, GL_LEQUAL },
	{ ComparisonFunc::Greater, GL_GREATER },
	{ ComparisonFunc::GreaterEqual, GL_GEQUAL }
};

static const std::unordered_map<PixelFormat, GLenum> TextureInternalFormatMap = {
#if defined(SKYGFX_PLATFORM_IOS) | defined(SKYGFX_PLATFORM_EMSCRIPTEN)
	// webgl and gles in ios devices cannot correctly handle 32-bit floating textures
	// yes, we force webgl to use 16-bit floating textures for all platforms because of ios
	{ PixelFormat::R32Float, GL_R16F },
	{ PixelFormat::RG32Float, GL_RG16F },
	{ PixelFormat::RGB32Float, GL_RGB16F },
	{ PixelFormat::RGBA32Float, GL_RGBA16F },
#else
	{ PixelFormat::R32Float, GL_R32F },
	{ PixelFormat::RG32Float, GL_RG32F },
	{ PixelFormat::RGB32Float, GL_RGB32F },
	{ PixelFormat::RGBA32Float, GL_RGBA32F },
#endif
	{ PixelFormat::R8UNorm, GL_R8 },
	{ PixelFormat::RG8UNorm, GL_RG8 },
	{ PixelFormat::RGBA8UNorm, GL_RGBA8 }
};

static const std::unordered_map<PixelFormat, GLenum> TextureFormatMap = {
	{ PixelFormat::R32Float, GL_RED },
	{ PixelFormat::RG32Float, GL_RG },
	{ PixelFormat::RGB32Float, GL_RGB },
	{ PixelFormat::RGBA32Float, GL_RGBA },
	{ PixelFormat::R8UNorm, GL_RED },
	{ PixelFormat::RG8UNorm, GL_RG },
	{ PixelFormat::RGBA8UNorm, GL_RGBA },
};

class ShaderGL
{
private:
	GLuint mProgram;
	ShaderReflection mVertRefl;
	ShaderReflection mFragRefl;

	struct {
		bool es;
		uint32_t version;
		bool enable_420pack_extension;
		bool force_flattened_io_blocks;
	} options;

public:
	auto getProgram() const { return mProgram; }

public:
	ShaderGL(const std::string& vertex_code, const std::string& fragment_code,
		std::vector<std::string> defines)
	{
		defines.push_back("FLIP_TEXCOORD_Y");

		auto vertex_shader_spirv = CompileGlslToSpirv(ShaderStage::Vertex, vertex_code, defines);
		auto fragment_shader_spirv = CompileGlslToSpirv(ShaderStage::Fragment, fragment_code, defines);

#if defined(SKYGFX_PLATFORM_IOS)
		options.es = true;
		options.version = 300;
		options.enable_420pack_extension = false;
		options.force_flattened_io_blocks = false;
		// TODO: android can be 320
		// TODO: since 310 we have uniform(std140, binding = 1), 300 have uniform(std140)
#elif defined(SKYGFX_PLATFORM_WINDOWS)
		options.es = false;
		options.version = 450;
		options.enable_420pack_extension = true;
		options.force_flattened_io_blocks = true;
#elif defined(SKYGFX_PLATFORM_MACOS)
		options.es = false;
		options.version = 410;
		options.enable_420pack_extension = false;
		options.force_flattened_io_blocks = true;
#elif defined(SKYGFX_PLATFORM_EMSCRIPTEN)
		options.es = true;
		options.version = 300;
		options.enable_420pack_extension = false;
		options.force_flattened_io_blocks = false;
#endif

		auto glsl_vert = skygfx::CompileSpirvToGlsl(vertex_shader_spirv, options.es, options.version,
			options.enable_420pack_extension, options.force_flattened_io_blocks);

		auto glsl_frag = skygfx::CompileSpirvToGlsl(fragment_shader_spirv, options.es, options.version,
			options.enable_420pack_extension, options.force_flattened_io_blocks);

		auto throw_error = [](auto shader, auto get_length_func, auto get_info_log_func) {
			GLint length = 0;
			get_length_func(shader, GL_INFO_LOG_LENGTH, &length);
			std::string str;
			str.resize(length);
			get_info_log_func(shader, length, &length, &str[0]);
			throw std::runtime_error(str);
		};

		auto compile_shader = [&](auto type, const std::string& glsl) {
			auto shader = glCreateShader(type);
			auto v = glsl.c_str();
			glShaderSource(shader, 1, &v, NULL);
			glCompileShader(shader);

			GLint isCompiled = 0;
			glGetShaderiv(shader, GL_COMPILE_STATUS, &isCompiled);

			if (isCompiled == GL_FALSE)
				throw_error(shader, glGetShaderiv, glGetShaderInfoLog);

			return shader;
		};

		auto vertexShader = compile_shader(GL_VERTEX_SHADER, glsl_vert);
		auto fragmentShader = compile_shader(GL_FRAGMENT_SHADER, glsl_frag);

		mProgram = glCreateProgram();
		glAttachShader(mProgram, vertexShader);
		glAttachShader(mProgram, fragmentShader);
		glLinkProgram(mProgram);

		GLint link_status = 0;
		glGetProgramiv(mProgram, GL_LINK_STATUS, &link_status);

		if (link_status == GL_FALSE)
			throw_error(mProgram, glGetProgramiv, glGetProgramInfoLog);

		glDeleteShader(vertexShader);
		glDeleteShader(fragmentShader);

		mVertRefl = MakeSpirvReflection(vertex_shader_spirv);
		mFragRefl = MakeSpirvReflection(fragment_shader_spirv);

		bool need_fix_bindings =
			(options.es && options.version <= 300) ||
			(!options.es && options.version < 420 && !options.enable_420pack_extension);

		if (need_fix_bindings)
		{
			auto for_each_descriptor_binding = [&](auto type, std::function<void(uint32_t binding, const ShaderReflection::Descriptor& descriptor)> callback){
				for (const auto& reflection : { mVertRefl, mFragRefl })
				{
					if (!reflection.typed_descriptor_bindings.contains(type))
						continue;

					for (const auto& [binding, descriptor] : reflection.typed_descriptor_bindings.at(type))
					{
						callback(binding, descriptor);
					}
				};
			};
			for_each_descriptor_binding(ShaderReflection::DescriptorType::UniformBuffer, [&](auto binding, const auto& descriptor){
				auto block_index = glGetUniformBlockIndex(mProgram, descriptor.type_name.c_str());
				glUniformBlockBinding(mProgram, block_index, binding);
			});
			GLint prevProgram = 0;
			glGetIntegerv(GL_CURRENT_PROGRAM, &prevProgram);
			glUseProgram(mProgram);
			for_each_descriptor_binding(ShaderReflection::DescriptorType::CombinedImageSampler, [&](auto binding, const auto& descriptor){
				auto location = glGetUniformLocation(mProgram, descriptor.name.c_str());
				glUniform1i(location, binding);
			});
			glUseProgram(prevProgram);
		}
	}

	~ShaderGL()
	{
		glDeleteProgram(mProgram);
	}
};

static std::vector<uint8_t> FlipPixels(const void* memory, uint32_t width, uint32_t height, uint32_t channels_count,
	uint32_t channel_size)
{
	auto row_size = width * channels_count * channel_size;
	auto image_size = height * row_size;
	auto result = std::vector<uint8_t>(image_size);
	for (uint32_t i = 0; i < height; i++)
	{
		auto src = (void*)(size_t(memory) + (size_t(i) * row_size));
		auto dst = (void*)(size_t(result.data()) + (size_t(height - 1 - i) * row_size));
		memcpy(dst, src, row_size);
	}
	return result;
}

class TextureGL
{
public:
	class ScopedBind : public skygfx::noncopyable
	{
	public:
		ScopedBind(GLuint texture)
		{
			glGetIntegerv(GL_TEXTURE_BINDING_2D, &mLastTexture);
			glBindTexture(GL_TEXTURE_2D, texture);
		}

		~ScopedBind()
		{
			glBindTexture(GL_TEXTURE_2D, mLastTexture);
		}

	private:
		GLint mLastTexture = 0;
	};

public:
	auto getGLTexture() const { return mTexture; }
	auto getWidth() const { return mWidth; }
	auto getHeight() const { return mHeight; }
	auto getFormat() const { return mFormat; }
	auto getMipCount() const { return mMipCount; }

private:
	GLuint mTexture = 0;
	uint32_t mWidth = 0;
	uint32_t mHeight = 0;
	uint32_t mMipCount = 0;
	PixelFormat mFormat;

public:
	TextureGL(uint32_t width, uint32_t height, PixelFormat format, uint32_t mip_count) :
		mWidth(width),
		mHeight(height),
		mFormat(format),
		mMipCount(mip_count)
	{
		glGenTextures(1, &mTexture);

		auto internal_format = TextureInternalFormatMap.at(mFormat);
		auto texture_format = TextureFormatMap.at(mFormat);
		auto format_type = PixelFormatTypeMap.at(mFormat);
		auto binding = ScopedBind(mTexture);

		for (uint32_t i = 0; i < mip_count; i++)
		{
			auto mip_width = GetMipWidth(width, i);
			auto mip_height = GetMipHeight(height, i);
			glTexImage2D(GL_TEXTURE_2D, i, internal_format, mip_width, mip_height, 0, texture_format,
				format_type, NULL);
		}

		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, mip_count - 1);
	}

	~TextureGL()
	{
		glDeleteTextures(1, &mTexture);
	}

	void write(uint32_t width, uint32_t height, const void* memory,
		uint32_t mip_level, uint32_t offset_x, uint32_t offset_y)
	{
		auto channels_count = GetFormatChannelsCount(mFormat);
		auto channel_size = GetFormatChannelSize(mFormat);
		auto format_type = PixelFormatTypeMap.at(mFormat);
		auto texture_format = TextureFormatMap.at(mFormat);
		auto flipped_image = FlipPixels(memory, width, height, channels_count, channel_size);
		auto mip_height = GetMipHeight(mHeight, mip_level);
		auto binding = ScopedBind(mTexture);

		glTexSubImage2D(GL_TEXTURE_2D, mip_level, offset_x, (mip_height - height) - offset_y, width, height,
			texture_format, format_type, flipped_image.data());
	}

	std::vector<uint8_t> read(uint32_t mip_level) const
	{
		GLuint fbo = 0;
		glGenFramebuffers(1, &fbo);
		
		GLint old_fbo;
		glGetIntegerv(GL_FRAMEBUFFER_BINDING, &old_fbo);
		
		glBindFramebuffer(GL_FRAMEBUFFER, fbo);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, 
			GL_TEXTURE_2D, mTexture, mip_level);

#ifdef SKYGFX_OPENGL_VALIDATION_ENABLED
		auto status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
		assert(status == GL_FRAMEBUFFER_COMPLETE);
#endif

		auto mip_width = GetMipWidth(mWidth, mip_level);
		auto mip_height = GetMipHeight(mHeight, mip_level);

		auto texture_format = TextureFormatMap.at(mFormat);
		auto format_type = PixelFormatTypeMap.at(mFormat);

		auto channels_count = GetFormatChannelsCount(mFormat);
		auto channel_size = GetFormatChannelSize(mFormat);
		size_t row_size = mip_width * channels_count * channel_size;
		size_t image_size = mip_height * row_size;

		GLint pack_alignment;
		glGetIntegerv(GL_PACK_ALIGNMENT, &pack_alignment);
		glPixelStorei(GL_PACK_ALIGNMENT, 1);

		std::vector<uint8_t> buffer(image_size);
		glReadPixels(0, 0, mip_width, mip_height, texture_format, format_type, buffer.data());

		glPixelStorei(GL_PACK_ALIGNMENT, pack_alignment);
		glBindFramebuffer(GL_FRAMEBUFFER, old_fbo);
		glDeleteFramebuffers(1, &fbo);

		return FlipPixels(buffer.data(), mip_width, mip_height, channels_count, channel_size);
	}

	void generateMips()
	{
		auto binding = ScopedBind(mTexture);
		glGenerateMipmap(GL_TEXTURE_2D);
	}
};

class RenderTargetGL
{
public:
	auto getGLFramebuffer() const { return mFramebuffer; }
	auto getTexture() const { return mTexture; }

private:
	GLuint mFramebuffer = 0;
	GLuint mDepthStencilRenderbuffer = 0;
	TextureGL* mTexture = nullptr;

public:
	RenderTargetGL(TextureGL* texture) : mTexture(texture)
	{
		GLint last_fbo;
		GLint last_rbo;

		glGetIntegerv(GL_FRAMEBUFFER_BINDING, &last_fbo);
		glGetIntegerv(GL_RENDERBUFFER_BINDING, &last_rbo);

		glGenRenderbuffers(1, &mDepthStencilRenderbuffer);
		glBindRenderbuffer(GL_RENDERBUFFER, mDepthStencilRenderbuffer);
		glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, mTexture->getWidth(), mTexture->getHeight());

		glGenFramebuffers(1, &mFramebuffer);
		glBindFramebuffer(GL_FRAMEBUFFER, mFramebuffer);
		glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, mDepthStencilRenderbuffer);

#ifdef SKYGFX_OPENGL_VALIDATION_ENABLED
		auto status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
		assert(status == GL_FRAMEBUFFER_COMPLETE);
#endif

		glBindFramebuffer(GL_FRAMEBUFFER, last_fbo);
		glBindRenderbuffer(GL_RENDERBUFFER, last_rbo);
	}

	~RenderTargetGL()
	{
		glDeleteFramebuffers(1, &mFramebuffer);
		glDeleteRenderbuffers(1, &mDepthStencilRenderbuffer);
	}
};

class BufferGL
{
public:
	auto getGLBuffer() const { return mBuffer; }

private:
	GLuint mBuffer = 0;
	GLenum mType = 0;
	size_t mSize = 0;
	std::unique_ptr<uint8_t[]> mStagingBuffer;

public:
	BufferGL(size_t size, GLenum type) : mType(type), mSize(size), mStagingBuffer(std::make_unique<uint8_t[]>(size))
	{
		glGenBuffers(1, &mBuffer);
		glBindBuffer(type, mBuffer);
		glBufferData(type, size, NULL, GL_DYNAMIC_DRAW);
	}

	~BufferGL()
	{
		glDeleteBuffers(1, &mBuffer);
	}

	void write(const void* memory, size_t size)
	{
		assert(mSize >= size);
		glBindBuffer(mType, mBuffer);
		if (size == mSize)
		{
			glBufferData(mType, size, memory, GL_DYNAMIC_DRAW);
		}
		else
		{
			memcpy(mStagingBuffer.get(), memory, size);
			glBufferData(mType, mSize, mStagingBuffer.get(), GL_DYNAMIC_DRAW);
		}
	}
};

class VertexBufferGL : public BufferGL
{
public:
	auto getStride() const { return mStride; }
	void setStride(size_t value) { mStride = value; }

private:
	size_t mStride = 0;

public:
	VertexBufferGL(size_t size, size_t stride) : BufferGL(size, GL_ARRAY_BUFFER),
		mStride(stride)
	{
	}
};

class IndexBufferGL : public BufferGL
{
public:
	auto getStride() const { return mStride; }
	void setStride(size_t value) { mStride = value; }

private:
	size_t mStride = 0;

public:
	IndexBufferGL(size_t size, size_t stride) : BufferGL(size, GL_ELEMENT_ARRAY_BUFFER),
		mStride(stride)
	{
	}
};

class UniformBufferGL : public BufferGL
{
public:
	UniformBufferGL(size_t size) : BufferGL(size, GL_UNIFORM_BUFFER)
	{
		assert(size % 16 == 0);
	}
};

struct SamplerStateGL
{
	Sampler sampler = Sampler::Linear;
	TextureAddress texture_address = TextureAddress::Clamp;
	AnisotropyLevel anisotropy_level = AnisotropyLevel::None;

	bool operator==(const SamplerStateGL& other) const = default;
};

SKYGFX_MAKE_HASHABLE(SamplerStateGL,
	t.sampler,
	t.texture_address,
	t.anisotropy_level
);

#if defined(SKYGFX_PLATFORM_WINDOWS)
static HGLRC WglContext;
static HDC gHDC;
#elif defined(SKYGFX_PLATFORM_IOS)
static GLKView* gGLKView = nullptr;
#elif defined(SKYGFX_PLATFORM_MACOS)
NSOpenGLView* glView;
NSOpenGLContext *glContext;
#elif defined(SKYGFX_PLATFORM_EMSCRIPTEN)
EGLDisplay gEglDisplay;
EGLSurface gEglSurface;
EGLContext gEglContext;
EGLConfig gEglConfig;
#endif

struct ContextGL
{
	ContextGL()
	{
		glGenVertexArrays(1, &vao);
		glBindVertexArray(vao);

		glGenBuffers(1, &pixel_buffer);

		int max_draw_buffers;
		glGetIntegerv(GL_MAX_DRAW_BUFFERS, &max_draw_buffers);

		for (int i = 0; i < max_draw_buffers; i++)
		{
			draw_buffers.push_back(GL_COLOR_ATTACHMENT0 + i);
		}

		glGetIntegerv(GL_MAX_VERTEX_ATTRIBS, &max_vertex_attribs);
	}

	~ContextGL()
	{
		glDeleteVertexArrays(1, &vao);
		glDeleteBuffers(1, &pixel_buffer);

		for (const auto& [state, objects_map] : sampler_states)
		{
			for (const auto& [type, object] : objects_map)
			{
				glDeleteSamplers(1, &object);
			}
		}
	}

	int max_vertex_attribs;

	std::vector<uint32_t> draw_buffers;

	uint32_t width = 0;
	uint32_t height = 0;

#ifdef EMSCRIPTEN
	bool has_anisotropy_extension = false;
#endif

	ExecuteList execute_after_present;

	std::unordered_map<uint32_t, TextureGL*> textures;
	std::unordered_set<uint32_t> dirty_textures;

	enum class SamplerType
	{
		Mipmap,
		NoMipmap
	};

	std::unordered_map<SamplerStateGL, std::unordered_map<SamplerType, GLuint>> sampler_states;
	SamplerStateGL sampler_state;

	std::vector<RenderTargetGL*> render_targets;

	GLuint pixel_buffer;
	GLuint vao;

	GLenum topology;
	ShaderGL* shader = nullptr;
	std::vector<VertexBufferGL*> vertex_buffers; // TODO: store pointer and count, not std::vector
	IndexBufferGL* index_buffer = nullptr;
	std::optional<Viewport> viewport;
	std::optional<Scissor> scissor;
	FrontFace front_face = FrontFace::Clockwise;
	std::vector<InputLayout> input_layouts;
	std::optional<DepthMode> depth_mode;

	bool shader_dirty = false;
	bool vertex_array_dirty = false;
	bool index_buffer_dirty = false;
	bool viewport_dirty = true;
	bool scissor_dirty = true;
	bool sampler_state_dirty = true;
	bool front_face_dirty = true;
	bool depth_mode_dirty = true;

	uint32_t getBackbufferWidth();
	uint32_t getBackbufferHeight();
	PixelFormat getBackbufferFormat();
};

static ContextGL* gContext = nullptr;

uint32_t ContextGL::getBackbufferWidth()
{
	return !render_targets.empty() ? render_targets.at(0)->getTexture()->getWidth() : width;
}

uint32_t ContextGL::getBackbufferHeight()
{
	return !render_targets.empty() ? render_targets.at(0)->getTexture()->getHeight() : height;
}

PixelFormat ContextGL::getBackbufferFormat()
{
	return !render_targets.empty() ? render_targets.at(0)->getTexture()->getFormat() : PixelFormat::RGBA8UNorm;
}

static void EnsureScissor()
{
	if (!gContext->scissor_dirty)
		return;

	gContext->scissor_dirty = false;

	const auto& scissor = gContext->scissor;

	if (!scissor.has_value())
	{
		glDisable(GL_SCISSOR_TEST);
		return;
	}

	glEnable(GL_SCISSOR_TEST);

	auto x = (GLint)glm::round(scissor->position.x);
	auto y = (GLint)glm::round(gContext->height - scissor->position.y - scissor->size.y); // TODO: need different calculations when render target
	auto width = (GLint)glm::round(scissor->size.x);
	auto height = (GLint)glm::round(scissor->size.y);
	glScissor(x, y, width, height);
}

static void EnsureDepthMode()
{
	if (!gContext->depth_mode_dirty)
		return;

	gContext->depth_mode_dirty = false;

	const auto& depth_mode = gContext->depth_mode;

	if (!depth_mode.has_value())
	{
		glDisable(GL_DEPTH_TEST);
		glDepthMask(GL_TRUE);
		return;
	}

	glEnable(GL_DEPTH_TEST);
	glDepthFunc(ComparisonFuncMap.at(depth_mode->func));
	glDepthMask(depth_mode->write_mask);
}

static void EnsureGraphicsState(bool draw_indexed)
{
	if (gContext->shader_dirty)
	{
		glUseProgram(gContext->shader->getProgram());
		gContext->vertex_array_dirty = true;
		gContext->index_buffer_dirty = draw_indexed;
		gContext->shader_dirty = false;
	}

	if (gContext->index_buffer_dirty && draw_indexed)
	{
		gContext->index_buffer_dirty = false;
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, gContext->index_buffer->getGLBuffer());
	}

	if (gContext->vertex_array_dirty)
	{
		gContext->vertex_array_dirty = false;

		std::unordered_set<uint32_t> active_locations;

		for (size_t i = 0; i < gContext->vertex_buffers.size(); i++)
		{
			auto vertex_buffer = gContext->vertex_buffers.at(i);
			auto stride = (GLsizei)vertex_buffer->getStride();

			glBindBuffer(GL_ARRAY_BUFFER, vertex_buffer->getGLBuffer());

			const auto& input_layout = gContext->input_layouts.at(i);

			for (const auto& [location, attribute] : input_layout.attributes)
			{
				active_locations.insert(location);

				auto index = (GLuint)location;
				auto size = (GLint)VertexFormatSizeMap.at(attribute.format);
				auto type = (GLenum)VertexFormatTypeMap.at(attribute.format);
				auto normalized = (GLboolean)VertexFormatNormalizeMap.at(attribute.format);
				auto pointer = (void*)attribute.offset;
				glVertexAttribPointer(index, size, type, normalized, stride, pointer);
				glVertexAttribDivisor(index, input_layout.rate == InputLayout::Rate::Vertex ? 0 : 1);
			}
		}

		for (int i = 0; i < gContext->max_vertex_attribs; i++)
		{
			if (active_locations.contains(i))
				glEnableVertexAttribArray(i);
			else
				glDisableVertexAttribArray(i);
		}
	}

	for (auto binding : gContext->dirty_textures)
	{
		auto texture = gContext->textures.at(binding);

		glActiveTexture(GL_TEXTURE0 + binding);
		glBindTexture(GL_TEXTURE_2D, texture->getGLTexture());
	}

	gContext->dirty_textures.clear();

	if (gContext->sampler_state_dirty)
	{
		gContext->sampler_state_dirty = false;

		const auto& value = gContext->sampler_state;

		if (!gContext->sampler_states.contains(value))
		{
			const static std::unordered_map<Sampler, std::unordered_map<ContextGL::SamplerType, GLint>> SamplerMap = {
				{ Sampler::Nearest, {
					{ ContextGL::SamplerType::Mipmap, GL_NEAREST_MIPMAP_NEAREST },
					{ ContextGL::SamplerType::NoMipmap, GL_NEAREST },
				} },
				{ Sampler::Linear, {
					{ ContextGL::SamplerType::Mipmap, GL_LINEAR_MIPMAP_LINEAR },
					{ ContextGL::SamplerType::NoMipmap, GL_LINEAR },
				} },
			};

			const static std::unordered_map<TextureAddress, GLint> TextureAddressMap = {
				{ TextureAddress::Clamp, GL_CLAMP_TO_EDGE },
				{ TextureAddress::Wrap, GL_REPEAT },
				{ TextureAddress::MirrorWrap, GL_MIRRORED_REPEAT }
			};

			std::unordered_map<ContextGL::SamplerType, GLuint> sampler_state_map;

			for (auto sampler_type : { ContextGL::SamplerType::Mipmap, ContextGL::SamplerType::NoMipmap })
			{
				GLuint sampler_object;
				glGenSamplers(1, &sampler_object);

				static const std::unordered_map<AnisotropyLevel, GLfloat> AnisotropyLevelMap = {
					{ AnisotropyLevel::None, 1.0f },
					{ AnisotropyLevel::X2, 2.0f },
					{ AnisotropyLevel::X4, 4.0f },
					{ AnisotropyLevel::X8, 8.0f },
					{ AnisotropyLevel::X16, 16.0f },
				};

				glSamplerParameteri(sampler_object, GL_TEXTURE_MIN_FILTER, SamplerMap.at(value.sampler).at(sampler_type));
				glSamplerParameteri(sampler_object, GL_TEXTURE_MAG_FILTER, SamplerMap.at(value.sampler).at(ContextGL::SamplerType::NoMipmap));
				glSamplerParameteri(sampler_object, GL_TEXTURE_WRAP_S, TextureAddressMap.at(value.texture_address));
				glSamplerParameteri(sampler_object, GL_TEXTURE_WRAP_T, TextureAddressMap.at(value.texture_address));

				// when we use nearest filtering we MUST disable anisotropy
				auto anisotropy_level = AnisotropyLevelMap.at(value.sampler == Sampler::Nearest ? AnisotropyLevel::None : value.anisotropy_level);

#if defined(SKYGFX_PLATFORM_EMSCRIPTEN)
				if (gContext->has_anisotropy_extension)
					glSamplerParameterf(sampler_object, GL_TEXTURE_MAX_ANISOTROPY_EXT, anisotropy_level);
#else
				glSamplerParameterf(sampler_object, GL_TEXTURE_MAX_ANISOTROPY, anisotropy_level);
#endif

				sampler_state_map.insert({ sampler_type, sampler_object });
			}

			gContext->sampler_states.insert({ value, sampler_state_map });
		}

		for (auto [binding, texture_handle] : gContext->textures)
		{
			bool texture_has_mips = ((TextureGL*)texture_handle)->getMipCount() > 1;
			auto sampler_type = texture_has_mips ? ContextGL::SamplerType::Mipmap : ContextGL::SamplerType::NoMipmap;
			auto sampler = gContext->sampler_states.at(value).at(sampler_type);
			glBindSampler(binding, sampler);
		}
	}

	if (gContext->front_face_dirty)
	{
		gContext->front_face_dirty = false;

		static const std::unordered_map<FrontFace, GLenum> FrontFaceMap = {
			{ FrontFace::Clockwise, GL_CW },
			{ FrontFace::CounterClockwise, GL_CCW }
		};

		glFrontFace(FrontFaceMap.at(gContext->front_face));
	}

	if (gContext->viewport_dirty)
	{
		gContext->viewport_dirty = false;

		auto width = static_cast<float>(gContext->getBackbufferWidth());
		auto height = static_cast<float>(gContext->getBackbufferHeight());

		auto viewport = gContext->viewport.value_or(Viewport{ { 0.0f, 0.0f }, { width, height } });

		glViewport(
			(GLint)viewport.position.x,
			(GLint)viewport.position.y,
			(GLint)viewport.size.x,
			(GLint)viewport.size.y);

#if defined(SKYGFX_PLATFORM_WINDOWS)
		glDepthRange((GLclampd)viewport.min_depth, (GLclampd)viewport.max_depth);
#elif defined(SKYGFX_PLATFORM_IOS) | defined(SKYGFX_PLATFORM_MACOS) | defined(SKYGFX_PLATFORM_EMSCRIPTEN)
		glDepthRangef((GLfloat)viewport.min_depth, (GLfloat)viewport.max_depth);
#endif
	}

	EnsureScissor();
	EnsureDepthMode();
}

BackendGL::BackendGL(void* window, uint32_t width, uint32_t height, Adapter adapter)
{
#if defined(SKYGFX_PLATFORM_WINDOWS)
	NvOptimusEnablement = adapter == Adapter::HighPerformance ? 1 : 0;
	AmdPowerXpressRequestHighPerformance = adapter == Adapter::HighPerformance ? 1 : 0;

	gHDC = GetDC((HWND)window);

	PIXELFORMATDESCRIPTOR pfd;
	memset(&pfd, 0, sizeof(PIXELFORMATDESCRIPTOR));
	pfd.nSize = sizeof(PIXELFORMATDESCRIPTOR);
	pfd.nVersion = 1;
	pfd.iLayerType = PFD_MAIN_PLANE;
	pfd.dwFlags = PFD_DOUBLEBUFFER | PFD_SUPPORT_OPENGL | PFD_DRAW_TO_WINDOW;
	pfd.iPixelType = PFD_TYPE_RGBA;
	pfd.cColorBits = 24;
	pfd.cDepthBits = 24;
	pfd.cStencilBits = 8;
	pfd.cAlphaBits = 8;

	int nPixelFormat = ChoosePixelFormat(gHDC, &pfd);
	SetPixelFormat(gHDC, nPixelFormat, &pfd);

	WglContext = wglCreateContext(gHDC);
	wglMakeCurrent(gHDC, WglContext);

	glewInit();

	const int pixelAttribs[] = {
		WGL_DRAW_TO_WINDOW_ARB, GL_TRUE,
		WGL_SUPPORT_OPENGL_ARB, GL_TRUE,
		WGL_DOUBLE_BUFFER_ARB, GL_TRUE,
		WGL_PIXEL_TYPE_ARB, WGL_TYPE_RGBA_ARB,
		WGL_ACCELERATION_ARB, WGL_FULL_ACCELERATION_ARB,
		WGL_COLOR_BITS_ARB, 32,
		WGL_ALPHA_BITS_ARB, 8,
		WGL_DEPTH_BITS_ARB, 24,
		WGL_STENCIL_BITS_ARB, 8,
		WGL_SAMPLE_BUFFERS_ARB, GL_TRUE,
		WGL_SAMPLES_ARB, 1,
		0
	};

	int pixelFormatID;
	UINT numFormats;
	wglChoosePixelFormatARB(gHDC, pixelAttribs, NULL, 1, &pixelFormatID, &numFormats);

	memset(&pfd, 0, sizeof(PIXELFORMATDESCRIPTOR));

	DescribePixelFormat(gHDC, pixelFormatID, sizeof(pfd), &pfd);
	SetPixelFormat(gHDC, pixelFormatID, &pfd);

	int attribs[] =
	{
		WGL_CONTEXT_MAJOR_VERSION_ARB, 4,
		WGL_CONTEXT_MINOR_VERSION_ARB, 5,
		WGL_CONTEXT_PROFILE_MASK_ARB, WGL_CONTEXT_CORE_PROFILE_BIT_ARB,
		WGL_CONTEXT_FLAGS_ARB, /*WGL_CONTEXT_DEBUG_BIT_ARB*/0,
		0
	};

	wglMakeCurrent(NULL, NULL);
	wglDeleteContext(WglContext);
	WglContext = wglCreateContextAttribsARB(gHDC, 0, attribs);
	wglMakeCurrent(gHDC, WglContext);
#elif defined(SKYGFX_PLATFORM_IOS)
	auto _window = (UIWindow*)window;
	auto rootView = [[_window rootViewController] view];

	gGLKView = [[GLKView alloc] initWithFrame:[_window frame]];
	gGLKView.context = [[EAGLContext alloc]initWithAPI:kEAGLRenderingAPIOpenGLES3];
	gGLKView.drawableColorFormat = GLKViewDrawableColorFormatRGBA8888;
	gGLKView.drawableDepthFormat = GLKViewDrawableDepthFormat24;
	gGLKView.drawableStencilFormat = GLKViewDrawableStencilFormat8;
	gGLKView.drawableMultisample = GLKViewDrawableMultisampleNone;
	gGLKView.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;

	EAGLContext.currentContext = gGLKView.context;

	[rootView addSubview:gGLKView];
#elif defined(SKYGFX_PLATFORM_MACOS)
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

	NSOpenGLPixelFormatAttribute pixelFormatAttribs[] = {
		NSOpenGLPFAOpenGLProfile, NSOpenGLProfileVersion4_1Core,
		NSOpenGLPFAColorSize,     24,
		NSOpenGLPFAAlphaSize,     8,
		NSOpenGLPFADepthSize,     24,
		NSOpenGLPFAStencilSize,   8,
		NSOpenGLPFADoubleBuffer,  true,
		NSOpenGLPFAAccelerated,   true,
		NSOpenGLPFANoRecovery,    true,
		0,                        0,
	};

	auto pixel_format = [[NSOpenGLPixelFormat alloc] initWithAttributes:pixelFormatAttribs];

	auto bounds = [contentView bounds];

	glView = [[NSOpenGLView alloc] initWithFrame:bounds pixelFormat:pixel_format];

	[pixel_format release];

	[glView setAutoresizingMask:(NSViewHeightSizable | NSViewWidthSizable | NSViewMinXMargin |
		NSViewMaxXMargin | NSViewMinYMargin | NSViewMaxYMargin)];

	// GLFW creates a helper contentView that handles things like keyboard and drag and
	// drop events. We don't want to clobber that view if it exists. Instead we just
	// add ourselves as a subview and make the view resize automatically.
	if (contentView != nil)
	{
		[contentView addSubview:glView];
	}
	else if (nsWindow != nil)
	{
		[nsWindow setContentView:glView];
	}

	glContext = [glView openGLContext];

	[glContext makeCurrentContext];

	GLint interval = 0;
	[glContext setValues:&interval forParameter:NSOpenGLContextParameterSwapInterval];

	// When initializing NSOpenGLView programmatically (as we are), this sometimes doesn't
	// get hooked up properly (especially when there are existing window elements). This ensures
	// we are valid. Otherwise, you'll probably get a GL_INVALID_FRAMEBUFFER_OPERATION when
	// trying to glClear() for the first time.

	void (^set_view)(void) = ^(void) {
		[glContext setView:glView];
	};

	if([NSThread isMainThread])
	{
		set_view();
	}
	else
	{
		dispatch_sync(dispatch_get_main_queue(),set_view);
	}
#elif defined(SKYGFX_PLATFORM_EMSCRIPTEN)
	const EGLint attribs[] = {
		EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT_KHR,
		EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
		EGL_RED_SIZE, 8,
		EGL_GREEN_SIZE, 8,
		EGL_BLUE_SIZE, 8,
		EGL_ALPHA_SIZE, 8,
		EGL_DEPTH_SIZE, 24,
		EGL_STENCIL_SIZE, 8,
		EGL_NONE
	};
	const EGLint context_attribs[] = {
		EGL_CONTEXT_CLIENT_VERSION, 3,
		EGL_NONE
	};
	gEglDisplay = eglGetDisplay(EGL_DEFAULT_DISPLAY);
	eglInitialize(gEglDisplay, NULL, NULL);
	EGLint num_configs;
	eglChooseConfig(gEglDisplay, attribs, &gEglConfig, 1, &num_configs);
	gEglSurface = eglCreateWindowSurface(gEglDisplay, gEglConfig, (EGLNativeWindowType)window, NULL);
	gEglContext = eglCreateContext(gEglDisplay, gEglConfig, NULL, context_attribs);
	eglMakeCurrent(gEglDisplay, gEglSurface, gEglSurface, gEglContext);
#endif

#ifdef SKYGFX_OPENGL_VALIDATION_ENABLED
	glEnable(GL_DEBUG_OUTPUT);
	glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
	glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE, GL_DEBUG_SEVERITY_NOTIFICATION, 0, NULL, GL_FALSE);
	glDebugMessageCallback(DebugMessageCallback, nullptr);
#endif

	GLint num_extensions;
	glGetIntegerv(GL_NUM_EXTENSIONS, &num_extensions);
	std::unordered_set<std::string> extensions;

	for (GLint i = 0; i < num_extensions; i++)
	{
		auto extension = glGetStringi(GL_EXTENSIONS, i);
		extensions.insert((const char*)extension);
	//	std::cout << extension << std::endl;
	}

	gContext = new ContextGL();

	gContext->width = width;
	gContext->height = height;

#if defined(SKYGFX_PLATFORM_EMSCRIPTEN)
	gContext->has_anisotropy_extension = extensions.contains("GL_EXT_texture_filter_anisotropic");
#endif
}

BackendGL::~BackendGL()
{
	delete gContext;
	gContext = nullptr;

#if defined(SKYGFX_PLATFORM_WINDOWS)
	wglDeleteContext(WglContext);
#elif defined(SKYGFX_PLATFORM_MACOS)
	[glView release];
#endif
}

void BackendGL::resize(uint32_t width, uint32_t height)
{
	gContext->width = width;
	gContext->height = height;

	if (!gContext->viewport.has_value())
		gContext->viewport_dirty = true;
}

void BackendGL::setVsync(bool value)
{
#if defined(SKYGFX_PLATFORM_WINDOWS)
	wglSwapIntervalEXT(value ? 1 : 0);
#endif
}

void BackendGL::setTopology(Topology topology)
{
	static const std::unordered_map<Topology, GLenum> TopologyMap = {
		{ Topology::PointList, GL_POINTS },
		{ Topology::LineList, GL_LINES },
		{ Topology::LineStrip, GL_LINE_STRIP },
		{ Topology::TriangleList, GL_TRIANGLES },
		{ Topology::TriangleStrip, GL_TRIANGLE_STRIP }
	};

	gContext->topology = TopologyMap.at(topology);
}

void BackendGL::setViewport(std::optional<Viewport> viewport)
{
	gContext->viewport = viewport;
	gContext->viewport_dirty = true;
}

void BackendGL::setScissor(std::optional<Scissor> scissor)
{
	gContext->scissor = scissor;
	gContext->scissor_dirty = true;
}

void BackendGL::setTexture(uint32_t binding, TextureHandle* handle)
{
	gContext->textures[binding] = (TextureGL*)handle;
	gContext->dirty_textures.insert(binding);
	gContext->sampler_state_dirty = true;
}

void BackendGL::setRenderTarget(const RenderTarget** render_target, size_t count)
{
	if (count == 0)
	{
#if defined(SKYGFX_PLATFORM_WINDOWS) | defined(SKYGFX_PLATFORM_MACOS) | defined(SKYGFX_PLATFORM_EMSCRIPTEN)
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
#elif defined(SKYGFX_PLATFORM_IOS)
		[gGLKView bindDrawable];
#endif
		gContext->render_targets.clear();

		if (!gContext->viewport.has_value())
			gContext->viewport_dirty = true;

		return;
	}
	std::vector<RenderTargetGL*> render_targets;

	for (size_t i = 0; i < count; i++)
	{
		auto target = (RenderTargetGL*)(RenderTargetHandle*)*(RenderTarget*)render_target[i];
		render_targets.push_back(target);
	}

	glBindFramebuffer(GL_FRAMEBUFFER, render_targets.at(0)->getGLFramebuffer());

	for (size_t i = 0; i < render_targets.size(); i++)
	{
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + (GLenum)i, GL_TEXTURE_2D,
			render_targets.at(i)->getTexture()->getGLTexture(), 0);
	}

#ifdef SKYGFX_OPENGL_VALIDATION_ENABLED
	auto status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
	assert(status == GL_FRAMEBUFFER_COMPLETE);
#endif

	glDrawBuffers((GLsizei)render_targets.size(), gContext->draw_buffers.data());

	gContext->render_targets = render_targets;

	if (!gContext->viewport.has_value())
		gContext->viewport_dirty = true;
}

void BackendGL::setShader(ShaderHandle* handle)
{
	gContext->shader = (ShaderGL*)handle;
	gContext->shader_dirty = true;
}

void BackendGL::setInputLayout(const std::vector<InputLayout>& value)
{
	gContext->input_layouts = value;
	gContext->vertex_array_dirty = true;
}

void BackendGL::setVertexBuffer(const VertexBuffer** vertex_buffer, size_t count)
{
	gContext->vertex_buffers.clear();
	for (size_t i = 0; i < count; i++)
	{
		auto buffer = (VertexBufferGL*)(VertexBufferHandle*)*(VertexBuffer*)vertex_buffer[i];
		gContext->vertex_buffers.push_back(buffer);
	}
	gContext->vertex_array_dirty = true;
}

void BackendGL::setIndexBuffer(IndexBufferHandle* handle)
{
	gContext->index_buffer = (IndexBufferGL*)handle;
	gContext->index_buffer_dirty = true;
}

void BackendGL::setUniformBuffer(uint32_t binding, UniformBufferHandle* handle)
{
	auto buffer = (UniformBufferGL*)handle;
	glBindBufferBase(GL_UNIFORM_BUFFER, binding, buffer->getGLBuffer());
}

void BackendGL::setBlendMode(const std::optional<BlendMode>& blend_mode)
{
	if (!blend_mode.has_value())
	{
		glDisable(GL_BLEND);
		return;
	}

	const static std::unordered_map<Blend, GLenum> BlendMap = {
		{ Blend::One, GL_ONE },
		{ Blend::Zero, GL_ZERO },
		{ Blend::SrcColor, GL_SRC_COLOR },
		{ Blend::InvSrcColor, GL_ONE_MINUS_SRC_COLOR },
		{ Blend::SrcAlpha, GL_SRC_ALPHA },
		{ Blend::InvSrcAlpha, GL_ONE_MINUS_SRC_ALPHA },
		{ Blend::DstColor, GL_DST_COLOR },
		{ Blend::InvDstColor, GL_ONE_MINUS_DST_COLOR },
		{ Blend::DstAlpha, GL_DST_ALPHA },
		{ Blend::InvDstAlpha, GL_ONE_MINUS_DST_ALPHA }
	};

	const static std::unordered_map<BlendFunction, GLenum> BlendOpMap = {
		{ BlendFunction::Add, GL_FUNC_ADD },
		{ BlendFunction::Subtract, GL_FUNC_SUBTRACT },
		{ BlendFunction::ReverseSubtract, GL_FUNC_REVERSE_SUBTRACT },
		{ BlendFunction::Min, GL_MIN },
		{ BlendFunction::Max, GL_MAX },
	};

	const auto& blend = blend_mode.value();

	glEnable(GL_BLEND);
	glBlendEquationSeparate(BlendOpMap.at(blend.color_func), BlendOpMap.at(blend.alpha_func));
	glBlendFuncSeparate(BlendMap.at(blend.color_src), BlendMap.at(blend.color_dst),
		BlendMap.at(blend.alpha_src), BlendMap.at(blend.alpha_dst));
	glColorMask(blend.color_mask.red, blend.color_mask.green, blend.color_mask.blue, blend.color_mask.alpha);
}

void BackendGL::setDepthMode(const std::optional<DepthMode>& depth_mode)
{
	gContext->depth_mode = depth_mode;
	gContext->depth_mode_dirty = true;
}

void BackendGL::setStencilMode(const std::optional<StencilMode>& stencil_mode)
{
	if (!stencil_mode.has_value())
	{
		glDisable(GL_STENCIL_TEST);
		return;
	}

	static const std::unordered_map<StencilOp, GLenum> StencilOpMap = {
		{ StencilOp::Keep, GL_KEEP },
		{ StencilOp::Zero, GL_ZERO },
		{ StencilOp::Replace, GL_REPLACE },
		{ StencilOp::IncrementSaturation, GL_INCR },
		{ StencilOp::DecrementSaturation, GL_DECR },
		{ StencilOp::Invert, GL_INVERT },
		{ StencilOp::Increment, GL_INCR_WRAP },
		{ StencilOp::Decrement, GL_DECR_WRAP },
	};

	auto stencil_mode_nn = stencil_mode.value();

	glEnable(GL_STENCIL_TEST);
	glStencilMask(stencil_mode_nn.write_mask);
	glStencilOp(StencilOpMap.at(stencil_mode_nn.fail_op), StencilOpMap.at(stencil_mode_nn.depth_fail_op), StencilOpMap.at(stencil_mode_nn.pass_op));
	glStencilFunc(ComparisonFuncMap.at(stencil_mode_nn.func), stencil_mode_nn.reference, stencil_mode_nn.read_mask);
}

void BackendGL::setCullMode(CullMode cull_mode)
{
	if (cull_mode == CullMode::None)
	{
		glDisable(GL_CULL_FACE);
		return;
	}

	static const std::unordered_map<CullMode, GLenum> CullMap = {
		{ CullMode::None, GL_NONE },
		{ CullMode::Front, GL_FRONT },
		{ CullMode::Back, GL_BACK }
	};

	glEnable(GL_CULL_FACE);
	glCullFace(CullMap.at(cull_mode));
}

void BackendGL::setSampler(Sampler value)
{
	gContext->sampler_state.sampler = value;
	gContext->sampler_state_dirty = true;
}

void BackendGL::setAnisotropyLevel(AnisotropyLevel value)
{
	gContext->sampler_state.anisotropy_level = value;
	gContext->sampler_state_dirty = true;
}

void BackendGL::setTextureAddress(TextureAddress value)
{
	gContext->sampler_state.texture_address = value;
	gContext->sampler_state_dirty = true;
}

void BackendGL::setFrontFace(FrontFace value)
{
	gContext->front_face = value;
	gContext->front_face_dirty = true;
}

void BackendGL::setDepthBias(const std::optional<DepthBias> depth_bias)
{
	if (!depth_bias.has_value())
	{
		glDisable(GL_POLYGON_OFFSET_FILL);
		return;
	}

	glEnable(GL_POLYGON_OFFSET_FILL);
	glPolygonOffset(depth_bias->factor, depth_bias->units);
}

void BackendGL::clear(const std::optional<glm::vec4>& color, const std::optional<float>& depth,
	const std::optional<uint8_t>& stencil)
{
	EnsureScissor();
	EnsureDepthMode();

	auto scissor_enabled = gContext->scissor.has_value();
	auto depth_mask_disabled = gContext->depth_mode && !gContext->depth_mode->write_mask;

	if (scissor_enabled)
		glDisable(GL_SCISSOR_TEST);

	if (depth_mask_disabled)
		glDepthMask(GL_TRUE);

	GLbitfield flags = 0;

	if (color.has_value())
	{
		flags |= GL_COLOR_BUFFER_BIT;
		auto _color = color.value();
		glClearColor(_color.r, _color.g, _color.b, _color.a);
	}

	if (depth.has_value())
	{
		flags |= GL_DEPTH_BUFFER_BIT;
		glClearDepthf(depth.value());
	}

	if (stencil.has_value())
	{
		flags |= GL_STENCIL_BUFFER_BIT;
		glClearStencil(stencil.value());
	}

	glClear(flags);

	if (depth_mask_disabled)
		glDepthMask(GL_FALSE);

	if (scissor_enabled)
		glEnable(GL_SCISSOR_TEST);
}

void BackendGL::draw(uint32_t vertex_count, uint32_t vertex_offset, uint32_t instance_count)
{
	EnsureGraphicsState(false);
	auto mode = gContext->topology;
	auto first = (GLint)vertex_offset;
	auto count = (GLsizei)vertex_count;
	auto primcount = (GLsizei)instance_count;
	glDrawArraysInstanced(mode, first, count, primcount);
}

void BackendGL::drawIndexed(uint32_t index_count, uint32_t index_offset, uint32_t instance_count)
{
	EnsureGraphicsState(true);
	auto mode = gContext->topology;
	auto count = (GLsizei)index_count;
	auto index_size = gContext->index_buffer->getStride();
	auto type = index_size == 2 ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT;
	auto indices = (void*)(size_t)(index_offset * index_size);
	auto primcount = (GLsizei)instance_count;
	glDrawElementsInstanced(mode, count, type, indices, primcount);
}

void BackendGL::copyBackbufferToTexture(const glm::i32vec2& src_pos, const glm::i32vec2& size, const glm::i32vec2& dst_pos,
	TextureHandle* dst_texture_handle)
{
	if (size.x <= 0 || size.y <= 0)
		return;

	auto dst_texture = reinterpret_cast<TextureGL*>(dst_texture_handle);
	auto format = gContext->getBackbufferFormat();
	auto backbuffer_height = gContext->getBackbufferHeight();

	assert(dst_texture->getWidth() >= static_cast<uint32_t>(dst_pos.x + size.x));
	assert(dst_texture->getHeight() >= static_cast<uint32_t>(dst_pos.y + size.y));
	assert(dst_texture->getFormat() == format);

	auto y = backbuffer_height - src_pos.y - size.y;

	TextureGL::ScopedBind binding(dst_texture->getGLTexture());
	glCopyTexSubImage2D(GL_TEXTURE_2D, 0, dst_pos.x, dst_pos.y, src_pos.x, y, size.x, size.y);
}

void BackendGL::present()
{
#ifdef SKYGFX_OPENGL_VALIDATION_ENABLED
	FlushErrors();
#endif
#if defined(SKYGFX_PLATFORM_WINDOWS)
	SwapBuffers(gHDC);
#elif defined(SKYGFX_PLATFORM_IOS)
	[gGLKView display];
#elif defined(SKYGFX_PLATFORM_MACOS)
	[glContext flushBuffer];
#elif defined(SKYGFX_PLATFORM_EMSCRIPTEN)
	eglSwapBuffers(gEglDisplay, gEglSurface);
#endif
	gContext->execute_after_present.flush();
}

TextureHandle* BackendGL::createTexture(uint32_t width, uint32_t height, PixelFormat format,
	uint32_t mip_count)
{
	auto texture = new TextureGL(width, height, format, mip_count);
	return (TextureHandle*)texture;
}

void BackendGL::writeTexturePixels(TextureHandle* handle, uint32_t width, uint32_t height, const void* memory,
	uint32_t mip_level, uint32_t offset_x, uint32_t offset_y)
{
	auto texture = (TextureGL*)handle;
	texture->write(width, height, memory, mip_level, offset_x, offset_y);
}

std::vector<uint8_t> BackendGL::readTexturePixels(TextureHandle* handle, uint32_t mip_level)
{
	auto texture = (TextureGL*)handle;
	return texture->read(mip_level);
}

void BackendGL::generateMips(TextureHandle* handle)
{
	auto texture = (TextureGL*)handle;
	texture->generateMips();
}

void BackendGL::destroyTexture(TextureHandle* handle)
{
	auto texture = (TextureGL*)handle;
	delete texture;
}

RenderTargetHandle* BackendGL::createRenderTarget(uint32_t width, uint32_t height, TextureHandle* texture_handle)
{
	auto texture = (TextureGL*)texture_handle;
	auto render_target = new RenderTargetGL(texture);
	return (RenderTargetHandle*)render_target;
}

void BackendGL::destroyRenderTarget(RenderTargetHandle* handle)
{
	auto render_target = (RenderTargetGL*)handle;
	delete render_target;
}

ShaderHandle* BackendGL::createShader(const std::string& vertex_code,
	const std::string& fragment_code, const std::vector<std::string>& defines)
{
	auto shader = new ShaderGL(vertex_code, fragment_code, defines);
	return (ShaderHandle*)shader;
}

void BackendGL::destroyShader(ShaderHandle* handle)
{
	auto shader = (ShaderGL*)handle;
	delete shader;
}

VertexBufferHandle* BackendGL::createVertexBuffer(size_t size, size_t stride)
{
	auto buffer = new VertexBufferGL(size, stride);
	return (VertexBufferHandle*)buffer;
}

void BackendGL::destroyVertexBuffer(VertexBufferHandle* handle)
{
	gContext->execute_after_present.add([handle] {
		auto buffer = (VertexBufferGL*)handle;
		delete buffer;
	});
}

void BackendGL::writeVertexBufferMemory(VertexBufferHandle* handle, const void* memory, size_t size, size_t stride)
{
	auto buffer = (VertexBufferGL*)handle;
	buffer->write(memory, size);
	buffer->setStride(stride);
}

IndexBufferHandle* BackendGL::createIndexBuffer(size_t size, size_t stride)
{
	auto buffer = new IndexBufferGL(size, stride);
	return (IndexBufferHandle*)buffer;
}

void BackendGL::destroyIndexBuffer(IndexBufferHandle* handle)
{
	gContext->execute_after_present.add([handle] {
		auto buffer = (IndexBufferGL*)handle;

		if (gContext->index_buffer == buffer)
			gContext->index_buffer = nullptr;

		delete buffer;
	});
}

void BackendGL::writeIndexBufferMemory(IndexBufferHandle* handle, const void* memory, size_t size, size_t stride)
{
	auto buffer = (IndexBufferGL*)handle;
	buffer->write(memory, size);
	buffer->setStride(stride);
}

UniformBufferHandle* BackendGL::createUniformBuffer(size_t size)
{
	auto buffer = new UniformBufferGL(size);
	return (UniformBufferHandle*)buffer;
}

void BackendGL::destroyUniformBuffer(UniformBufferHandle* handle)
{
	gContext->execute_after_present.add([handle] {
		auto buffer = (UniformBufferGL*)handle;
		delete buffer;
	});
}

void BackendGL::writeUniformBufferMemory(UniformBufferHandle* handle, const void* memory, size_t size)
{
	auto buffer = (UniformBufferGL*)handle;
	buffer->write(memory, size);
}

#endif
