#include "backend_gl.h"

#ifdef SKYGFX_HAS_OPENGL

#include <unordered_map>
#include <unordered_set>
#include <stdexcept>
#include <iostream>
#include "shader_compiler.h"

#if defined(SKYGFX_PLATFORM_WINDOWS)
	#define GLEW_STATIC
	#include <GL/glew.h>
	#include <GL/GL.h>
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

using namespace skygfx;

#if defined(SKYGFX_PLATFORM_WINDOWS)
void GLAPIENTRY DebugMessageCallback(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length,
	const GLchar* message, const void* userParam)
{
	if (type == GL_DEBUG_TYPE_PERFORMANCE) // spamming in getPixels func
		return;

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

	if (type == GL_DEBUG_TYPE_OTHER)
		return;

	std::cout << "[OpenGL] source: " << source_str << 
		", type: " << type_str <<
		", id: " << id <<
		", severity: " << severity_str << 
		", msg: " << message << std::endl;
}
#endif

void CheckErrors()
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

	auto error = glGetError();

	if (error == GL_NO_ERROR)
		return;

	std::cout << "BackendGL::CheckError: " << error << "(" << 
		(ErrorMap.contains(error) ? ErrorMap.at(error) : "UNKNOWN") << ")" << std::endl;
}

static const std::unordered_map<Format, GLint> SizeMap = {
	{ Format::Float1, 1 },
	{ Format::Float2, 2 },
	{ Format::Float3, 3 },
	{ Format::Float4, 4 },
	{ Format::Byte1, 1 },
	{ Format::Byte2, 2 },
	{ Format::Byte3, 3 },
	{ Format::Byte4, 4 }
};

static const std::unordered_map<Format, GLenum> FormatTypeMap = {
	{ Format::Float1, GL_FLOAT },
	{ Format::Float2, GL_FLOAT },
	{ Format::Float3, GL_FLOAT },
	{ Format::Float4, GL_FLOAT },
	{ Format::Byte1, GL_UNSIGNED_BYTE },
	{ Format::Byte2, GL_UNSIGNED_BYTE },
	{ Format::Byte3, GL_UNSIGNED_BYTE },
	{ Format::Byte4, GL_UNSIGNED_BYTE }
};

static const std::unordered_map<Format, GLboolean> NormalizeMap = {
	{ Format::Float1, GL_FALSE },
	{ Format::Float2, GL_FALSE },
	{ Format::Float3, GL_FALSE },
	{ Format::Float4, GL_FALSE },
	{ Format::Byte1, GL_TRUE },
	{ Format::Byte2, GL_TRUE },
	{ Format::Byte3, GL_TRUE },
	{ Format::Byte4, GL_TRUE }
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

static const std::unordered_map<Format, GLenum> TextureInternalFormatMap = {
	{ Format::Float1, GL_R32F },
	{ Format::Float2, GL_RG32F },
	{ Format::Float3, GL_RGB32F },
	{ Format::Float4, GL_RGBA32F },
	{ Format::Byte1, GL_R8 },
	{ Format::Byte2, GL_RG8 },
	{ Format::Byte3, GL_RGB8 },
	{ Format::Byte4, GL_RGBA8 }
};

static const std::unordered_map<Format, GLenum> TextureFormatMap = {
	{ Format::Float1, GL_RED },
	{ Format::Float2, GL_RG },
	{ Format::Float3, GL_RGB },
	{ Format::Float4, GL_RGBA },
	{ Format::Byte1, GL_RED },
	{ Format::Byte2, GL_RG },
	{ Format::Byte3, GL_RGB },
	{ Format::Byte4, GL_RGBA }
};

class ShaderGL
{
private:
	VertexLayout mVertexLayout;
	GLuint mProgram;
	GLuint mVao;
	ShaderReflection mVertRefl;
	ShaderReflection mFragRefl;
	
	struct {
		bool es;
		uint32_t version;
		bool enable_420pack_extension;
		bool force_flattened_io_blocks;
	} options;

public:
	ShaderGL(const VertexLayout& vertex_layout, const std::string& vertex_code, const std::string& fragment_code,
		std::vector<std::string> defines) : mVertexLayout(vertex_layout)
	{
		AddShaderLocationDefines(vertex_layout, defines);
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

		auto vertexShader = glCreateShader(GL_VERTEX_SHADER);
		auto v = glsl_vert.c_str();
		glShaderSource(vertexShader, 1, &v, NULL);
		glCompileShader(vertexShader);

		GLint isCompiled = 0;
		glGetShaderiv(vertexShader, GL_COMPILE_STATUS, &isCompiled);
		if (isCompiled == GL_FALSE)
		{
			GLint maxLength = 0;
			glGetShaderiv(vertexShader, GL_INFO_LOG_LENGTH, &maxLength);
			std::string errorLog;
			errorLog.resize(maxLength);
			glGetShaderInfoLog(vertexShader, maxLength, &maxLength, &errorLog[0]);
			throw std::runtime_error(errorLog);
		}

		auto fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
		auto f = glsl_frag.c_str();
		glShaderSource(fragmentShader, 1, &f, NULL);
		glCompileShader(fragmentShader);

		isCompiled = 0;
		glGetShaderiv(fragmentShader, GL_COMPILE_STATUS, &isCompiled);
		if (isCompiled == GL_FALSE)
		{
			GLint maxLength = 0;
			glGetShaderiv(fragmentShader, GL_INFO_LOG_LENGTH, &maxLength);
			std::string errorLog;
			errorLog.resize(maxLength);
			glGetShaderInfoLog(fragmentShader, maxLength, &maxLength, &errorLog[0]);
			throw std::runtime_error(errorLog);
		}
		
		mProgram = glCreateProgram();
		glAttachShader(mProgram, vertexShader);
		glAttachShader(mProgram, fragmentShader);
		glLinkProgram(mProgram);
		
		GLint link_status = 0;
		glGetProgramiv(mProgram, GL_LINK_STATUS, &link_status);
		if (link_status == GL_FALSE)
		{
			GLint maxLength = 0;
			glGetProgramiv(mProgram, GL_INFO_LOG_LENGTH, &maxLength);
			std::string errorLog;
			errorLog.resize(maxLength);
			glGetProgramInfoLog(mProgram, maxLength, &maxLength, &errorLog[0]);
			throw std::runtime_error(errorLog);
		}
		
		glDeleteShader(vertexShader);
		glDeleteShader(fragmentShader);

		glGenVertexArrays(1, &mVao);
		glBindVertexArray(mVao);

		for (int i = 0; i < vertex_layout.attributes.size(); i++)
		{
			const auto& attrib = vertex_layout.attributes.at(i);

			glEnableVertexAttribArray(i);
#if defined(SKYGFX_PLATFORM_WINDOWS)
			glVertexAttribFormat(i, SizeMap.at(attrib.format), FormatTypeMap.at(attrib.format),
				NormalizeMap.at(attrib.format), (GLuint)attrib.offset);
			glVertexAttribBinding(i, 0);
#endif
		}
		
		mVertRefl = MakeSpirvReflection(vertex_shader_spirv);
		mFragRefl = MakeSpirvReflection(fragment_shader_spirv);
		
		bool need_fix_uniform_bindings =
			(options.es && options.version <= 300) ||
			(!options.es && options.version < 420 && !options.enable_420pack_extension);

		if (need_fix_uniform_bindings)
		{
			auto fix_bindings = [&](const ShaderReflection& reflection) {
				for (const auto& [binding, descriptor] : reflection.descriptor_bindings)
				{
					if (descriptor.type != ShaderReflection::Descriptor::Type::UniformBuffer)
						continue;
					
					auto block_index = glGetUniformBlockIndex(mProgram, descriptor.type_name.c_str());
					glUniformBlockBinding(mProgram, block_index, binding);
				}
			};
			fix_bindings(mVertRefl);
			fix_bindings(mFragRefl);
		}
	}

	~ShaderGL()
	{
		glDeleteVertexArrays(1, &mVao);
		glDeleteProgram(mProgram);
	}

	void apply()
	{
		glUseProgram(mProgram);
		glBindVertexArray(mVao);
		
		bool need_fix_texture_bindings =
			(options.es && options.version <= 300) ||
			(!options.es && options.version < 420 && !options.enable_420pack_extension);
			
		if (need_fix_texture_bindings)
		{
			auto fix_bindings = [&](const ShaderReflection& reflection) {
				for (const auto& [binding, descriptor] : reflection.descriptor_bindings)
				{
					if (descriptor.type != ShaderReflection::Descriptor::Type::CombinedImageSampler)
						continue;
	
					auto location = glGetUniformLocation(mProgram, descriptor.name.c_str());
					glUniform1i(location, binding);
				}
			};
			fix_bindings(mVertRefl);
			fix_bindings(mFragRefl);
		}
	}
	
#if defined(SKYGFX_PLATFORM_IOS) | defined(SKYGFX_PLATFORM_MACOS) | defined(SKYGFX_PLATFORM_EMSCRIPTEN)
	void applyLayout()
	{
		for (int i = 0; i < mVertexLayout.attributes.size(); i++)
		{
			const auto& attrib = mVertexLayout.attributes.at(i);

			glVertexAttribPointer(i, SizeMap.at(attrib.format),
				FormatTypeMap.at(attrib.format), NormalizeMap.at(attrib.format),
				(GLsizei)mVertexLayout.stride, (void*)attrib.offset);
		}
	}
#endif
};

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
	Format mFormat;

public:
	TextureGL(uint32_t width, uint32_t height, Format format, uint32_t mip_count) :
		mWidth(width),
		mHeight(height),
		mFormat(format),
		mMipCount(mip_count)
	{
		glGenTextures(1, &mTexture);

		auto internal_format = TextureInternalFormatMap.at(mFormat);
		auto texture_format = TextureFormatMap.at(mFormat);
		auto format_type = FormatTypeMap.at(mFormat);
		auto binding = ScopedBind(mTexture);

		for (uint32_t i = 0; i < mip_count; i++)
		{
			auto mip_width = GetMipWidth(width, i);
			auto mip_height = GetMipHeight(height, i);
			glTexImage2D(GL_TEXTURE_2D, i, internal_format, mip_width, mip_height, 0, texture_format,
				format_type, nullptr);
		}
	}

	~TextureGL()
	{
		glDeleteTextures(1, &mTexture);
	}

	void write(uint32_t width, uint32_t height, Format format, void* memory,
		uint32_t mip_level, uint32_t offset_x, uint32_t offset_y)
	{
		auto channels_count = GetFormatChannelsCount(format);
		auto channel_size = GetFormatChannelSize(format);
		auto format_type = FormatTypeMap.at(format);
		auto texture_format = TextureFormatMap.at(format);

		auto row_size = width * channels_count * channel_size;
		auto image_size = height * row_size;
		auto flipped_image = std::vector<uint8_t>(image_size);

		for (uint32_t i = 0; i < height; i++)
		{
			auto src = (void*)(size_t(memory) + (size_t(i) * row_size));
			auto dst = (void*)(size_t(flipped_image.data()) + (size_t(height - 1 - i) * row_size));
			memcpy(dst, src, row_size);
		}

		auto mip_height = GetMipHeight(mHeight, mip_level);
		auto binding = ScopedBind(mTexture);

		glTexSubImage2D(GL_TEXTURE_2D, mip_level, offset_x, (mip_height - height) - offset_y, width, height,
			texture_format, format_type, flipped_image.data());
	}

	void read(uint32_t pos_x, uint32_t pos_y, uint32_t width, uint32_t height,
		uint32_t mip_level, void* dst_memory)
	{
		auto channels_count = GetFormatChannelsCount(mFormat);
		auto channel_size = GetFormatChannelSize(mFormat);
		auto format_type = FormatTypeMap.at(mFormat);
		auto texture_format = TextureFormatMap.at(mFormat);
		auto binding = ScopedBind(mTexture);

#ifndef EMSCRIPTEN // emscripten doesnt have this func
		glGetTexImage(GL_TEXTURE_2D, mip_level, texture_format, format_type, dst_memory);
#else
		std::cout << "warning: emscripten cannot read to cpu memory" << std::endl;
#endif

		auto row_size = width * channels_count * channel_size;
		auto temp_row = std::vector<uint8_t>(row_size);

		for (uint32_t i = 0; i < uint32_t(height / 2); i++)
		{
			auto src = (void*)(size_t(dst_memory) + (i * row_size));
			auto dst = (void*)(size_t(dst_memory) + ((height - i - 1) * row_size));
			memcpy(temp_row.data(), src, row_size);
			memcpy(src, dst, row_size);
			memcpy(dst, temp_row.data(), row_size);
		}
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

		glGenFramebuffers(1, &mFramebuffer);
		glGenRenderbuffers(1, &mDepthStencilRenderbuffer);

		glBindFramebuffer(GL_FRAMEBUFFER, mFramebuffer);
		glBindRenderbuffer(GL_RENDERBUFFER, mDepthStencilRenderbuffer);

		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, mTexture->getGLTexture(), 0);

		glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, mTexture->getWidth(), mTexture->getHeight());
		glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, mDepthStencilRenderbuffer);

		assert(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);

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

public:
	BufferGL(size_t size, GLenum type) : mType(type)
	{
		glGenBuffers(1, &mBuffer);
		glBindBuffer(type, mBuffer);
		glBufferData(type, size, nullptr, GL_DYNAMIC_DRAW);
	}

	~BufferGL()
	{
		glDeleteBuffers(1, &mBuffer);
	}

	void write(void* memory, size_t size)
	{
		glBindBuffer(mType, mBuffer);
#ifdef EMSCRIPTEN
		glBufferData(mType, size, memory, GL_DYNAMIC_DRAW);
#else
		auto ptr = glMapBufferRange(mType, 0, size, GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_BUFFER_BIT);
		memcpy(ptr, memory, size);
		glUnmapBuffer(mType);
#endif
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

	bool operator==(const SamplerStateGL& value) const
	{
		return
			sampler == value.sampler &&
			texture_address == value.texture_address;
	}
};

SKYGFX_MAKE_HASHABLE(SamplerStateGL,
	t.sampler,
	t.texture_address
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
		glGenBuffers(1, &pixel_buffer);
	}

	~ContextGL()
	{
		glDeleteBuffers(1, &pixel_buffer);

		for (const auto& [state, objects_map] : sampler_states)
		{
			for (const auto& [type, object] : objects_map)
			{
				glDeleteSamplers(1, &object);
			}
		}
	}
	uint32_t width = 0;
	uint32_t height = 0;

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

	RenderTargetGL* render_target = nullptr;

	GLenum index_type;
	GLuint pixel_buffer;

	GLenum topology;
	ShaderGL* shader = nullptr;
	VertexBufferGL* vertex_buffer = nullptr;
	IndexBufferGL* index_buffer = nullptr;
	std::optional<Viewport> viewport;
	std::optional<Scissor> scissor;
	FrontFace front_face = FrontFace::Clockwise;

	bool shader_dirty = false;
	bool vertex_buffer_dirty = false;
	bool index_buffer_dirty = false;
	bool viewport_dirty = true;
	bool scissor_dirty = true;
	bool sampler_state_dirty = true;
	bool front_face_dirty = true;

	uint32_t getBackbufferWidth();
	uint32_t getBackbufferHeight();
	Format getBackbufferFormat();
};

static ContextGL* gContext = nullptr;

uint32_t ContextGL::getBackbufferWidth()
{
	return render_target ? render_target->getTexture()->getWidth() : width;
}

uint32_t ContextGL::getBackbufferHeight()
{
	return render_target ? render_target->getTexture()->getHeight() : height;
}

Format ContextGL::getBackbufferFormat()
{
	return gContext->render_target ? gContext->render_target->getTexture()->getFormat() : Format::Byte4;
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

	glEnable(GL_DEBUG_OUTPUT);
	glDebugMessageCallback(DebugMessageCallback, 0);

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
		EGL_BLUE_SIZE, 8,
		EGL_GREEN_SIZE, 8,
		EGL_RED_SIZE, 8,
		EGL_DEPTH_SIZE, 24,
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

	gContext = new ContextGL();

	gContext->width = width;
	gContext->height = height;
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
	if (gContext->viewport == viewport)
		return;
		
	gContext->viewport = viewport;
	gContext->viewport_dirty = true;
}

void BackendGL::setScissor(std::optional<Scissor> scissor)
{
	if (gContext->scissor == scissor)
		return;
		
	gContext->scissor = scissor;
	gContext->scissor_dirty = true;
}

void BackendGL::setTexture(uint32_t binding, TextureHandle* handle)
{
	auto texture = (TextureGL*)handle;
	
	if (gContext->textures[binding] == texture)
		return;
		
	gContext->textures[binding] = texture;
	gContext->dirty_textures.insert(binding);
	gContext->sampler_state_dirty = true;
}

void BackendGL::setRenderTarget(RenderTargetHandle* handle)
{
	auto render_target = (RenderTargetGL*)handle;
	
	if (gContext->render_target == render_target)
		return;
	
	glBindFramebuffer(GL_FRAMEBUFFER, render_target->getGLFramebuffer());
	gContext->render_target = render_target;

	if (!gContext->viewport.has_value())
		gContext->viewport_dirty = true;
}

void BackendGL::setRenderTarget(std::nullopt_t value)
{
	if (gContext->render_target == nullptr)
		return;
		
#if defined(SKYGFX_PLATFORM_WINDOWS) | defined(SKYGFX_PLATFORM_MACOS) | defined(SKYGFX_PLATFORM_EMSCRIPTEN)
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
#elif defined(SKYGFX_PLATFORM_IOS)
	[gGLKView bindDrawable];
#endif
	gContext->render_target = nullptr;

	if (!gContext->viewport.has_value())
		gContext->viewport_dirty = true;
}

void BackendGL::setShader(ShaderHandle* handle)
{
	auto shader = (ShaderGL*)handle;

	if (gContext->shader == shader)
		return;
	
	gContext->shader = shader;
	gContext->shader_dirty = true;
}

void BackendGL::setVertexBuffer(VertexBufferHandle* handle)
{
	auto buffer = (VertexBufferGL*)handle;

	//if (gVertexBuffer == buffer) // TODO: gl emit errors when this code uncommented
	//	return;

	gContext->vertex_buffer = buffer;
	gContext->vertex_buffer_dirty = true;
}

void BackendGL::setIndexBuffer(IndexBufferHandle* handle)
{
	auto buffer = (IndexBufferGL*)handle;
	
	//if (gIndexBuffer == buffer) // TODO: gl emit errors when this code uncommented
	//	return;
	
	gContext->index_buffer = buffer;
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
	glBlendEquationSeparate(BlendOpMap.at(blend.color_blend_func), BlendOpMap.at(blend.alpha_blend_func));
	glBlendFuncSeparate(BlendMap.at(blend.color_src_blend), BlendMap.at(blend.color_dst_blend),
		BlendMap.at(blend.alpha_src_blend), BlendMap.at(blend.alpha_dst_blend));
	glColorMask(blend.color_mask.red, blend.color_mask.green, blend.color_mask.blue, blend.color_mask.alpha);
}

void BackendGL::setDepthMode(const std::optional<DepthMode>& depth_mode)
{
	if (!depth_mode.has_value())
	{
		glDisable(GL_DEPTH_TEST);
		return;
	}

	glEnable(GL_DEPTH_TEST);
	glDepthFunc(ComparisonFuncMap.at(depth_mode.value().func));
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
	if (gContext->sampler_state.sampler == value)
		return;

	gContext->sampler_state.sampler = value;
	gContext->sampler_state_dirty = true;
}

void BackendGL::setTextureAddress(TextureAddress value)
{
	if (gContext->sampler_state.texture_address == value)
		return;
		
	gContext->sampler_state.texture_address = value;
	gContext->sampler_state_dirty = true;
}

void BackendGL::setFrontFace(FrontFace value)
{
	if (gContext->front_face == value)
		return;

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
	auto scissor_was_enabled = glIsEnabled(GL_SCISSOR_TEST);

	if (scissor_was_enabled)
	{
		glDisable(GL_SCISSOR_TEST);
	}

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

	if (scissor_was_enabled)
	{
		glEnable(GL_SCISSOR_TEST);
	}
}

void BackendGL::draw(uint32_t vertex_count, uint32_t vertex_offset)
{
	prepareForDrawing();
	glDrawArrays(gContext->topology, (GLint)vertex_offset, (GLsizei)vertex_count);
}

void BackendGL::drawIndexed(uint32_t index_count, uint32_t index_offset)
{
	assert(gContext->index_buffer);
	prepareForDrawing();
	uint32_t index_size = gContext->index_type == GL_UNSIGNED_INT ? 4 : 2;
	glDrawElements(gContext->topology, (GLsizei)index_count, gContext->index_type, (void*)(size_t)(index_offset * index_size));
}

void BackendGL::readPixels(const glm::i32vec2& pos, const glm::i32vec2& size, TextureHandle* dst_texture_handle)
{
	auto dst_texture = (TextureGL*)dst_texture_handle;
	auto format = gContext->getBackbufferFormat();

	assert(dst_texture->getWidth() == size.x);
	assert(dst_texture->getHeight() == size.y);
	assert(dst_texture->getFormat() == format);

	if (size.x <= 0 || size.y <= 0)
		return;

	auto backbuffer_height = gContext->getBackbufferHeight();

	auto x = (GLint)pos.x;
	auto y = (GLint)(backbuffer_height - pos.y - size.y);
	auto width = (GLint)size.x;
	auto height = (GLint)size.y;

	auto channels_count = GetFormatChannelsCount(format);
	auto channel_size = GetFormatChannelSize(format);

	glBindBuffer(GL_PIXEL_PACK_BUFFER, gContext->pixel_buffer);
	glBufferData(GL_PIXEL_PACK_BUFFER, width * height * channels_count * channel_size, nullptr, GL_STATIC_READ);
	glReadPixels(x, y, width, height, TextureFormatMap.at(format), FormatTypeMap.at(format), 0);
	glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);

	auto binding = TextureGL::ScopedBind(dst_texture->getGLTexture());
	glBindBuffer(GL_PIXEL_UNPACK_BUFFER, gContext->pixel_buffer);

	glTexImage2D(GL_TEXTURE_2D, 0, TextureInternalFormatMap.at(format), width, height, 0,
		TextureFormatMap.at(format), FormatTypeMap.at(format), 0);

	glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
}

void BackendGL::present()
{
	CheckErrors();
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

TextureHandle* BackendGL::createTexture(uint32_t width, uint32_t height, Format format,
	uint32_t mip_count)
{
	auto texture = new TextureGL(width, height, format, mip_count);
	return (TextureHandle*)texture;
}

void BackendGL::writeTexturePixels(TextureHandle* handle, uint32_t width, uint32_t height, Format format, void* memory,
	uint32_t mip_level, uint32_t offset_x, uint32_t offset_y)
{
	auto texture = (TextureGL*)handle;
	texture->write(width, height, format, memory, mip_level, offset_x, offset_y);
}

void BackendGL::readTexturePixels(TextureHandle* handle, uint32_t pos_x, uint32_t pos_y, uint32_t width, uint32_t height,
	uint32_t mip_level, void* dst_memory)
{
	auto texture = (TextureGL*)handle;
	texture->read(pos_x, pos_y, width, height, mip_level, dst_memory);
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

ShaderHandle* BackendGL::createShader(const VertexLayout& vertex_layout, const std::string& vertex_code, 
	const std::string& fragment_code, const std::vector<std::string>& defines)
{
	auto shader = new ShaderGL(vertex_layout, vertex_code, fragment_code, defines);
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

		if (gContext->vertex_buffer == buffer)
			gContext->vertex_buffer = nullptr;

		delete buffer;
	});
}

void BackendGL::writeVertexBufferMemory(VertexBufferHandle* handle, void* memory, size_t size, size_t stride)
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

void BackendGL::writeIndexBufferMemory(IndexBufferHandle* handle, void* memory, size_t size, size_t stride)
{
	auto buffer = (VertexBufferGL*)handle;
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

void BackendGL::writeUniformBufferMemory(UniformBufferHandle* handle, void* memory, size_t size)
{
	auto buffer = (UniformBufferGL*)handle;
	buffer->write(memory, size);
}

void BackendGL::prepareForDrawing()
{
	assert(gContext->shader);
	assert(gContext->vertex_buffer);

	if (gContext->shader_dirty)
	{
		gContext->shader->apply();
		gContext->vertex_buffer_dirty = true;
		gContext->shader_dirty = false;
	}

	if (gContext->index_buffer_dirty)
	{
		gContext->index_buffer_dirty = false;
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, gContext->index_buffer->getGLBuffer());
		gContext->index_type = gContext->index_buffer->getStride() == 2 ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT;
	}

	if (gContext->vertex_buffer_dirty)
	{
		gContext->vertex_buffer_dirty = false;
		glBindBuffer(GL_ARRAY_BUFFER, gContext->vertex_buffer->getGLBuffer());
#if defined(SKYGFX_PLATFORM_WINDOWS)
		glBindVertexBuffer(0, gContext->vertex_buffer->getGLBuffer(), 0, (GLsizei)gContext->vertex_buffer->getStride());
#elif defined(SKYGFX_PLATFORM_IOS) | defined(SKYGFX_PLATFORM_MACOS) | defined(SKYGFX_PLATFORM_EMSCRIPTEN)
		gContext->shader->applyLayout();
#endif
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

				glSamplerParameteri(sampler_object, GL_TEXTURE_MIN_FILTER, SamplerMap.at(value.sampler).at(sampler_type));
				glSamplerParameteri(sampler_object, GL_TEXTURE_MAG_FILTER, SamplerMap.at(value.sampler).at(ContextGL::SamplerType::NoMipmap));
				glSamplerParameteri(sampler_object, GL_TEXTURE_WRAP_S, TextureAddressMap.at(value.texture_address));
				glSamplerParameteri(sampler_object, GL_TEXTURE_WRAP_T, TextureAddressMap.at(value.texture_address));

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
	
	if (gContext->scissor_dirty)
	{
		gContext->scissor_dirty = false;
		
		if (gContext->scissor.has_value())
		{
			auto value = gContext->scissor.value();
			
			glEnable(GL_SCISSOR_TEST);
			glScissor(
				(GLint)glm::round(value.position.x),
				(GLint)glm::round(gContext->height - value.position.y - value.size.y), // TODO: need different calculations when render target
				(GLint)glm::round(value.size.x),
				(GLint)glm::round(value.size.y));
		}
		else
		{
			glDisable(GL_SCISSOR_TEST);
		}
	}
}

#endif
