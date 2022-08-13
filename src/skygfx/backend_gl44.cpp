#include "backend_gl44.h"

#ifdef SKYGFX_HAS_OPENGL

#include <unordered_map>
#include <stdexcept>
#include <iostream>

#if defined(SKYGFX_PLATFORM_WINDOWS)
	#define GLEW_STATIC
	#include <GL/glew.h>
	#include <GL/GL.h>
	#include <GL/wglew.h>
	#pragma comment(lib, "opengl32")
	#pragma comment(lib, "glu32")
#elif defined(SKYGFX_PLATFORM_APPLE)
	#include <OpenGLES/ES3/gl.h>
	#include <OpenGLES/ES3/glext.h>
	#import <Foundation/Foundation.h>
	#import <UIKit/UIKit.h>
	#import <GLKit/GLKit.h>
#endif

using namespace skygfx;

#if defined(SKYGFX_PLATFORM_WINDOWS)
void GLAPIENTRY MessageCallback(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length,
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
	auto error = glGetError();
	/*
	 GL_NO_ERROR	0	No user error reported since the last call to glGetError.
	 GL_INVALID_ENUM	1280	Set when an enumeration parameter is not legal.
	 GL_INVALID_VALUE	1281	Set when a value parameter is not legal.
	 GL_INVALID_OPERATION	1282	Set when the state for a command is not legal for its given parameters.
	 GL_STACK_OVERFLOW	1283	Set when a stack pushing operation causes a stack overflow.
	 GL_STACK_UNDERFLOW	1284	Set when a stack popping operation occurs while the stack is at its lowest point.
	 GL_OUT_OF_MEMORY	1285	Set when a memory allocation operation cannot allocate (enough) memory.
	 GL_INVALID_FRAMEBUFFER_OPERATION	1286	Set when reading or writing to a framebuffer that is not complete.
	 */
	assert(error == GL_NO_ERROR);
}

static const std::unordered_map<Vertex::Attribute::Format, GLint> SizeMap = {
	{ Vertex::Attribute::Format::R32F, 1 },
	{ Vertex::Attribute::Format::R32G32F, 2 },
	{ Vertex::Attribute::Format::R32G32B32F, 3 },
	{ Vertex::Attribute::Format::R32G32B32A32F, 4 },
	{ Vertex::Attribute::Format::R8UN, 1 },
	{ Vertex::Attribute::Format::R8G8UN, 2 },
	{ Vertex::Attribute::Format::R8G8B8UN, 3 },
	{ Vertex::Attribute::Format::R8G8B8A8UN, 4 }
};

static const std::unordered_map<Vertex::Attribute::Format, GLenum> TypeMap = {
	{ Vertex::Attribute::Format::R32F, GL_FLOAT },
	{ Vertex::Attribute::Format::R32G32F, GL_FLOAT },
	{ Vertex::Attribute::Format::R32G32B32F, GL_FLOAT },
	{ Vertex::Attribute::Format::R32G32B32A32F, GL_FLOAT },
	{ Vertex::Attribute::Format::R8UN, GL_UNSIGNED_BYTE },
	{ Vertex::Attribute::Format::R8G8UN, GL_UNSIGNED_BYTE },
	{ Vertex::Attribute::Format::R8G8B8UN, GL_UNSIGNED_BYTE },
	{ Vertex::Attribute::Format::R8G8B8A8UN, GL_UNSIGNED_BYTE }
};

static const std::unordered_map<Vertex::Attribute::Format, GLboolean> NormalizeMap = {
	{ Vertex::Attribute::Format::R32F, GL_FALSE },
	{ Vertex::Attribute::Format::R32G32F, GL_FALSE },
	{ Vertex::Attribute::Format::R32G32B32F, GL_FALSE },
	{ Vertex::Attribute::Format::R32G32B32A32F, GL_FALSE },
	{ Vertex::Attribute::Format::R8UN, GL_TRUE },
	{ Vertex::Attribute::Format::R8G8UN, GL_TRUE },
	{ Vertex::Attribute::Format::R8G8B8UN, GL_TRUE },
	{ Vertex::Attribute::Format::R8G8B8A8UN, GL_TRUE }
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

class ShaderDataGL44
{
private:
	Vertex::Layout layout;
	GLuint program;
	GLuint vao;

public:
	ShaderDataGL44(const Vertex::Layout& _layout, const std::string& vertex_code, const std::string& fragment_code,
		std::vector<std::string> defines) : layout(_layout)
	{
		AddShaderLocationDefines(layout, defines);
		defines.push_back("FLIP_TEXCOORD_Y");

		auto vertex_shader_spirv = CompileGlslToSpirv(ShaderStage::Vertex, vertex_code, defines);
		auto fragment_shader_spirv = CompileGlslToSpirv(ShaderStage::Fragment, fragment_code, defines);

#if defined(SKYGFX_PLATFORM_APPLE)
		bool es = true;
		uint32_t version = 300;
		// TODO: android can be 320
		// TODO: since 310 we have uniform(std140, binding = 1), 300 have uniform(std140)
#elif defined(SKYGFX_PLATFORM_WINDOWS)
		bool es = false;
		uint32_t version = 450;
#endif

		auto glsl_vert = skygfx::CompileSpirvToGlsl(vertex_shader_spirv, es, version);
		auto glsl_frag = skygfx::CompileSpirvToGlsl(fragment_shader_spirv, es, version);

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
		
		program = glCreateProgram();
		glAttachShader(program, vertexShader);
		glAttachShader(program, fragmentShader);
		glLinkProgram(program);
		
		GLint link_status = 0;
		glGetProgramiv(program, GL_LINK_STATUS, &link_status);
		if (link_status == GL_FALSE)
		{
			GLint maxLength = 0;
			glGetProgramiv(program, GL_INFO_LOG_LENGTH, &maxLength);
			std::string errorLog;
			errorLog.resize(maxLength);
			glGetProgramInfoLog(program, maxLength, &maxLength, &errorLog[0]);
			throw std::runtime_error(errorLog);
		}
		
		glDeleteShader(vertexShader);
		glDeleteShader(fragmentShader);

		glGenVertexArrays(1, &vao);
		glBindVertexArray(vao);

		for (int i = 0; i < layout.attributes.size(); i++)
		{
			const auto& attrib = layout.attributes.at(i);

			glEnableVertexAttribArray(i);
#if defined(SKYGFX_PLATFORM_WINDOWS)
			glVertexAttribFormat(i, SizeMap.at(attrib.format), TypeMap.at(attrib.format), 
				NormalizeMap.at(attrib.format), (GLuint)attrib.offset);
			glVertexAttribBinding(i, 0);
#endif
		}
		
		if (es && version <= 300)
		{
			auto fix_bindings = [&](const ShaderReflection& reflection) {
				for (const auto& descriptor_set : reflection.descriptor_sets)
				{
					if (descriptor_set.type != ShaderReflection::DescriptorSet::Type::UniformBuffer)
						continue;
					
					auto block_index = glGetUniformBlockIndex(program, descriptor_set.type_name.c_str());
					glUniformBlockBinding(program, block_index, descriptor_set.binding);
				}
			};
			fix_bindings(MakeSpirvReflection(vertex_shader_spirv));
			fix_bindings(MakeSpirvReflection(fragment_shader_spirv));
		}
	}

	~ShaderDataGL44()
	{
		glDeleteVertexArrays(1, &vao);
		glDeleteProgram(program);
	}

	void apply()
	{
		glUseProgram(program);
		glBindVertexArray(vao);
	}
	
#if defined(SKYGFX_PLATFORM_APPLE)
	void applyLayout()
	{
		for (int i = 0; i < layout.attributes.size(); i++)
		{
			const auto& attrib = layout.attributes.at(i);

			glVertexAttribPointer(i, SizeMap.at(attrib.format),
				TypeMap.at(attrib.format), NormalizeMap.at(attrib.format),
				(GLsizei)layout.stride, (void*)attrib.offset);
		}
	}
#endif
};

class TextureDataGL44
{
	friend class RenderTargetDataGL44;
	friend class BackendGL44;

private:
	GLuint texture;
	bool mipmap;
	uint32_t width;
	uint32_t height;

public:
	TextureDataGL44(uint32_t _width, uint32_t _height, uint32_t channels, void* memory, bool _mipmap) : 
		mipmap(_mipmap),
		width(_width),
		height(_height)
	{
		GLint last_texture;
		glGetIntegerv(GL_TEXTURE_BINDING_2D, &last_texture);
		glGenTextures(1, &texture);
		glBindTexture(GL_TEXTURE_2D, texture);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

		if (memory)
		{
			auto temp_data = malloc(width * height * 4); // TODO: we should not use magic numbers
			const auto row_size = width * 4;

			for (size_t i = 0; i < (size_t)height; i++)
			{
				auto src = (void*)(size_t(memory) + i * row_size);
				auto dst = (void*)(size_t(temp_data) + size_t(height - 1 - i) * row_size);

				memcpy(dst, src, row_size);
			}

			glBindTexture(GL_TEXTURE_2D, texture);
			glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, temp_data);
			free(temp_data);
			
			if (mipmap)
				glGenerateMipmap(GL_TEXTURE_2D);
		}

		glBindTexture(GL_TEXTURE_2D, last_texture);
	}

	~TextureDataGL44()
	{
		glDeleteTextures(1, &texture);
	}

	void bind(uint32_t binding)
	{
		glActiveTexture(GL_TEXTURE0 + binding);
		glBindTexture(GL_TEXTURE_2D, texture);
	}
};

class RenderTargetDataGL44
{
	friend class BackendGL44;

private:
	GLuint framebuffer;
	GLuint depth_stencil_renderbuffer;
	TextureDataGL44* texture_data;
	uint32_t width;
	uint32_t height;

public:
	RenderTargetDataGL44(uint32_t _width, uint32_t _height, TextureDataGL44* _texture_data) : 
		texture_data(_texture_data), width(_width), height(_height)
	{
		GLint last_fbo;
		GLint last_rbo;

		glGetIntegerv(GL_FRAMEBUFFER_BINDING, &last_fbo);
		glGetIntegerv(GL_RENDERBUFFER_BINDING, &last_rbo);

		glGenFramebuffers(1, &framebuffer);
		glGenRenderbuffers(1, &depth_stencil_renderbuffer);

		glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
		glBindRenderbuffer(GL_RENDERBUFFER, depth_stencil_renderbuffer);

		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture_data->texture, 0);

		glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, width, height);
		glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, depth_stencil_renderbuffer);

		assert(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);

		glBindFramebuffer(GL_FRAMEBUFFER, last_fbo);
		glBindRenderbuffer(GL_RENDERBUFFER, last_rbo);
	}

	~RenderTargetDataGL44()
	{
		glDeleteFramebuffers(1, &framebuffer);
		glDeleteRenderbuffers(1, &depth_stencil_renderbuffer);
	}
};

class BufferDataGL
{
	friend class BackendGL44;

protected:
	GLuint buffer;

public:
	BufferDataGL()
	{
		glGenBuffers(1, &buffer);

		// HOW TO DO MAPPING:
		/*
			auto ptr = glMapBufferRange(GL_ARRAY_BUFFER, 0, size, GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_BUFFER_BIT);
			memcpy(ptr, value.data, value.size);
			glUnmapBuffer(GL_ARRAY_BUFFER);
		*/

		// HOW TO GET SIZE:
		//GLint size = 0;
		//glGetBufferParameteriv(GL_ARRAY_BUFFER, GL_BUFFER_SIZE, &size);

	}

	~BufferDataGL()
	{
		glDeleteBuffers(1, &buffer);
	}
};

class VertexBufferDataGL : public BufferDataGL
{
	friend class BackendGL44;

private:
	size_t stride = 0;

public:
	VertexBufferDataGL(void* memory, size_t size, size_t _stride) : stride(_stride)
	{
		write(memory, size);
	}

	void write(void* memory, size_t size)
	{
		glBindBuffer(GL_ARRAY_BUFFER, buffer);
		glBufferData(GL_ARRAY_BUFFER, size, memory, GL_DYNAMIC_DRAW);
	}
};

class IndexBufferDataGL : public BufferDataGL
{
	friend class BackendGL44;

private:
	size_t stride = 0;

public:
	IndexBufferDataGL(void* memory, size_t size, size_t _stride) : stride(_stride)
	{
		write(memory, size);
	}

	void write(void* memory, size_t size)
	{
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, buffer);
		glBufferData(GL_ELEMENT_ARRAY_BUFFER, size, memory, GL_DYNAMIC_DRAW);
	}
};

class UniformBufferDataGL : public BufferDataGL
{
	friend class BackendGL44;

public:
	UniformBufferDataGL(void* memory, size_t size)
	{
		write(memory, size);
	}

	void write(void* memory, size_t size)
	{
		assert(size % 16 == 0);
		glBindBuffer(GL_UNIFORM_BUFFER, buffer);
		glBufferData(GL_UNIFORM_BUFFER, size, memory, GL_DYNAMIC_DRAW);
	}
};

#if defined(SKYGFX_PLATFORM_WINDOWS)
static HGLRC WglContext;
static HDC gHDC;
#elif defined(SKYGFX_PLATFORM_APPLE)
static GLKView* gGLKView = nullptr;
#endif

static GLenum gTopology;
static GLuint GLPixelBuffer;
static RenderTargetDataGL44* GLCurrentRenderTarget = nullptr;
static ShaderDataGL44* gShader = nullptr;
static bool gShaderDirty = false;

static VertexBufferDataGL* gVertexBuffer = nullptr;
static bool gVertexBufferDirty = false;

static IndexBufferDataGL* gIndexBuffer = nullptr;
static bool gIndexBufferDirty = false;
static GLenum gIndexType;

static ExecuteList gExecuteAfterPresent;

BackendGL44::BackendGL44(void* window, uint32_t width, uint32_t height)
{
#if defined(SKYGFX_PLATFORM_WINDOWS)
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
		WGL_CONTEXT_MINOR_VERSION_ARB, 4,
		WGL_CONTEXT_PROFILE_MASK_ARB, WGL_CONTEXT_CORE_PROFILE_BIT_ARB,
		WGL_CONTEXT_FLAGS_ARB, /*WGL_CONTEXT_DEBUG_BIT_ARB*/0,
		0
	};

	wglMakeCurrent(NULL, NULL);
	wglDeleteContext(WglContext);
	WglContext = wglCreateContextAttribsARB(gHDC, 0, attribs);
	wglMakeCurrent(gHDC, WglContext);

	glEnable(GL_DEBUG_OUTPUT);
	glDebugMessageCallback(MessageCallback, 0);

	bool vsync = false;
	wglSwapIntervalEXT(vsync ? 1 : 0);
#elif defined(SKYGFX_PLATFORM_APPLE)
	auto _window = (UIWindow*)window;
	auto rootView = [[_window rootViewController] view];
	gGLKView = [[GLKView alloc] initWithFrame:[_window frame]];
	[gGLKView setContext:[[EAGLContext alloc]initWithAPI:kEAGLRenderingAPIOpenGLES3]];
	[gGLKView setDrawableColorFormat:GLKViewDrawableColorFormatRGBA8888];
	[gGLKView setDrawableDepthFormat:GLKViewDrawableDepthFormat24];
	[gGLKView setDrawableStencilFormat:GLKViewDrawableStencilFormat8];
	[gGLKView setDrawableMultisample:GLKViewDrawableMultisampleNone];
	[gGLKView setAutoresizingMask:UIViewAutoresizingFlexibleWidth|UIViewAutoresizingFlexibleHeight];
	[EAGLContext setCurrentContext:gGLKView.context];
	[rootView addSubview:gGLKView];
#endif
	glGenBuffers(1, &GLPixelBuffer);

	mBackbufferWidth = width;
	mBackbufferHeight = height;
}

BackendGL44::~BackendGL44()
{
	glDeleteBuffers(1, &GLPixelBuffer);
	
	gExecuteAfterPresent.flush();

#if defined(SKYGFX_PLATFORM_WINDOWS)
	wglDeleteContext(WglContext);
#endif
}

void BackendGL44::resize(uint32_t width, uint32_t height)
{
	mBackbufferWidth = width;
	mBackbufferHeight = height;
	mViewportDirty = true;
}

void BackendGL44::setTopology(Topology topology)
{
	static const std::unordered_map<Topology, GLenum> TopologyMap = {
		{ Topology::PointList, GL_POINTS },
		{ Topology::LineList, GL_LINES },
		{ Topology::LineStrip, GL_LINE_STRIP },
		{ Topology::TriangleList, GL_TRIANGLES },
		{ Topology::TriangleStrip, GL_TRIANGLE_STRIP }
	};

	gTopology = TopologyMap.at(topology);
}

void BackendGL44::setViewport(std::optional<Viewport> viewport)
{
	mViewport = viewport;
	mViewportDirty = true;
}

void BackendGL44::setScissor(std::optional<Scissor> scissor)
{
	if (scissor.has_value())
	{
		auto value = scissor.value();

		glEnable(GL_SCISSOR_TEST);
		glScissor(
			(GLint)glm::round(value.position.x),
			(GLint)glm::round(mBackbufferHeight - value.position.y - value.size.y), // TODO: need different calculations when render target
			(GLint)glm::round(value.size.x),
			(GLint)glm::round(value.size.y));
	}
	else
	{
		glDisable(GL_SCISSOR_TEST);
	}
}

void BackendGL44::setTexture(uint32_t binding, TextureHandle* handle)
{
	auto texture = (TextureDataGL44*)handle;
	texture->bind(binding);
	
	TextureHandle* prev_texture = nullptr;
	
	if (mCurrentTextures.contains(binding))
		prev_texture = mCurrentTextures.at(binding);

	mCurrentTextures[binding] = handle;
	
	if (prev_texture != handle)
		mTexParametersDirty = true;
}

void BackendGL44::setRenderTarget(RenderTargetHandle* handle)
{
	auto render_target = (RenderTargetDataGL44*)handle;
	glBindFramebuffer(GL_FRAMEBUFFER, render_target->framebuffer);
	GLCurrentRenderTarget = render_target;

	if (!mViewport.has_value())
		mViewportDirty = true;
}

void BackendGL44::setRenderTarget(std::nullptr_t value)
{
#if defined(SKYGFX_PLATFORM_WINDOWS)
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
#elif defined(SKYGFX_PLATFORM_APPLE)
	[gGLKView bindDrawable];
#endif
	GLCurrentRenderTarget = nullptr;

	if (!mViewport.has_value())
		mViewportDirty = true;
}

void BackendGL44::setShader(ShaderHandle* handle)
{
	auto shader = (ShaderDataGL44*)handle;

	if (shader == gShader)
		return;
	
	gShader = shader;
	gShaderDirty = true;
}

void BackendGL44::setVertexBuffer(VertexBufferHandle* handle)
{
	auto buffer = (VertexBufferDataGL*)handle;
	gVertexBuffer = buffer;
	gVertexBufferDirty = true;
}

void BackendGL44::setIndexBuffer(IndexBufferHandle* handle)
{
	auto buffer = (IndexBufferDataGL*)handle;
	gIndexBuffer = buffer;
	gIndexBufferDirty = true;
}

void BackendGL44::setUniformBuffer(uint32_t binding, UniformBufferHandle* handle)
{
	auto buffer = (UniformBufferDataGL*)handle;

	glBindBufferBase(GL_UNIFORM_BUFFER, binding, buffer->buffer);
}

void BackendGL44::setBlendMode(const BlendMode& value)
{
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

	glEnable(GL_BLEND);
	glBlendEquationSeparate(BlendOpMap.at(value.colorBlendFunction), BlendOpMap.at(value.alphaBlendFunction));
	glBlendFuncSeparate(BlendMap.at(value.colorSrcBlend), BlendMap.at(value.colorDstBlend), BlendMap.at(value.alphaSrcBlend), BlendMap.at(value.alphaDstBlend));
	glColorMask(value.colorMask.red, value.colorMask.green, value.colorMask.blue, value.colorMask.alpha);
}

void BackendGL44::setDepthMode(std::optional<DepthMode> depth_mode)
{
	if (!depth_mode.has_value())
	{
		glDisable(GL_DEPTH_TEST);
		return;
	}

	glEnable(GL_DEPTH_TEST);
	glDepthFunc(ComparisonFuncMap.at(depth_mode.value().func));
}

void BackendGL44::setStencilMode(std::optional<StencilMode> stencil_mode)
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

void BackendGL44::setCullMode(CullMode cull_mode)
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
	glFrontFace(GL_CW);
	glCullFace(CullMap.at(cull_mode));
}

void BackendGL44::setSampler(Sampler value)
{
	mSampler = value;
	mTexParametersDirty = true;
}

void BackendGL44::setTextureAddress(TextureAddress value)
{
	mTextureAddress = value;
	mTexParametersDirty = true;
}

void BackendGL44::clear(const std::optional<glm::vec4>& color, const std::optional<float>& depth,
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

void BackendGL44::draw(uint32_t vertex_count, uint32_t vertex_offset)
{
	prepareForDrawing();
	glDrawArrays(gTopology, (GLint)vertex_offset, (GLsizei)vertex_count);
}

void BackendGL44::drawIndexed(uint32_t index_count, uint32_t index_offset)
{
	assert(gIndexBuffer);
	prepareForDrawing();
	uint32_t index_size = gIndexType == GL_UNSIGNED_INT ? 4 : 2;
	glDrawElements(gTopology, (GLsizei)index_count, gIndexType, (void*)(size_t)(index_offset * index_size));
}

void BackendGL44::readPixels(const glm::ivec2& pos, const glm::ivec2& size, TextureHandle* dst_texture_handle)
{
	auto dst_texture = (TextureDataGL44*)dst_texture_handle;

	assert(dst_texture->width == size.x);
	assert(dst_texture->height == size.y);

	if (size.x <= 0 || size.y <= 0)
		return;

	auto x = (GLint)pos.x;
	auto y = (GLint)(mBackbufferHeight - pos.y - size.y); // TODO: need different calculations when render target
	auto w = (GLint)size.x;
	auto h = (GLint)size.y;

	glBindBuffer(GL_PIXEL_PACK_BUFFER, GLPixelBuffer);
	glBufferData(GL_PIXEL_PACK_BUFFER, w * h * 4, nullptr, GL_STATIC_READ);
	glReadPixels(x, y, w, h, GL_RGBA, GL_UNSIGNED_BYTE, 0);
	glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);

	glBindTexture(GL_TEXTURE_2D, dst_texture->texture);
	glBindBuffer(GL_PIXEL_UNPACK_BUFFER, GLPixelBuffer);

	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, 0);

	if (dst_texture->mipmap)
		glGenerateMipmap(GL_TEXTURE_2D);

	glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
}

void BackendGL44::present()
{
	CheckErrors();
#if defined(SKYGFX_PLATFORM_WINDOWS)
	SwapBuffers(gHDC);
#elif defined(SKYGFX_PLATFORM_APPLE)
	[gGLKView display];
#endif

	gExecuteAfterPresent.flush();
}

TextureHandle* BackendGL44::createTexture(uint32_t width, uint32_t height, uint32_t channels, void* memory, bool mipmap)
{
	auto texture = new TextureDataGL44(width, height, channels, memory, mipmap);
	return (TextureHandle*)texture;
}

void BackendGL44::destroyTexture(TextureHandle* handle)
{
	auto texture = (TextureDataGL44*)handle;
	delete texture;
}

RenderTargetHandle* BackendGL44::createRenderTarget(uint32_t width, uint32_t height, TextureHandle* texture_handle)
{
	auto texture = (TextureDataGL44*)texture_handle;
	auto render_target = new RenderTargetDataGL44(width, height, texture);
	return (RenderTargetHandle*)render_target;
}

void BackendGL44::destroyRenderTarget(RenderTargetHandle* handle)
{
	auto render_target = (RenderTargetDataGL44*)handle;
	delete render_target;
}

ShaderHandle* BackendGL44::createShader(const Vertex::Layout& layout, const std::string& vertex_code, 
	const std::string& fragment_code, const std::vector<std::string>& defines)
{
	auto shader = new ShaderDataGL44(layout, vertex_code, fragment_code, defines);
	return (ShaderHandle*)shader;
}

void BackendGL44::destroyShader(ShaderHandle* handle)
{
	auto shader = (ShaderDataGL44*)handle;
	delete shader;
}

VertexBufferHandle* BackendGL44::createVertexBuffer(void* memory, size_t size, size_t stride)
{
	auto buffer = new VertexBufferDataGL(memory, size, stride);
	return (VertexBufferHandle*)buffer;
}

void BackendGL44::destroyVertexBuffer(VertexBufferHandle* handle)
{
	gExecuteAfterPresent.add([handle] {
		auto buffer = (VertexBufferDataGL*)handle;

		if (gVertexBuffer == buffer)
			gVertexBuffer = nullptr;

		delete buffer;
	});
}

void BackendGL44::writeVertexBufferMemory(VertexBufferHandle* handle, void* memory, size_t size, size_t stride)
{
	auto buffer = (VertexBufferDataGL*)handle;
	buffer->write(memory, size);
	buffer->stride = stride;
}

IndexBufferHandle* BackendGL44::createIndexBuffer(void* memory, size_t size, size_t stride)
{
	auto buffer = new IndexBufferDataGL(memory, size, stride);
	return (IndexBufferHandle*)buffer;
}

void BackendGL44::destroyIndexBuffer(IndexBufferHandle* handle)
{
	gExecuteAfterPresent.add([handle] {
		auto buffer = (IndexBufferDataGL*)handle;

		if (gIndexBuffer == buffer)
			gIndexBuffer = nullptr;

		delete buffer;
	});
}

void BackendGL44::writeIndexBufferMemory(IndexBufferHandle* handle, void* memory, size_t size, size_t stride)
{
	auto buffer = (VertexBufferDataGL*)handle;
	buffer->write(memory, size);
	buffer->stride = stride;
}

UniformBufferHandle* BackendGL44::createUniformBuffer(void* memory, size_t size)
{
	auto buffer = new UniformBufferDataGL(memory, size);
	return (UniformBufferHandle*)buffer;
}

void BackendGL44::destroyUniformBuffer(UniformBufferHandle* handle)
{
	gExecuteAfterPresent.add([handle] {
		auto buffer = (UniformBufferDataGL*)handle;
		delete buffer;
	});
}

void BackendGL44::writeUniformBufferMemory(UniformBufferHandle* handle, void* memory, size_t size)
{
	auto buffer = (UniformBufferDataGL*)handle;
	buffer->write(memory, size);
}

void BackendGL44::prepareForDrawing()
{
	assert(gShader);
	assert(gVertexBuffer);

	if (gShaderDirty)
	{
		gShader->apply();
		gShaderDirty = false;
	}

	if (gIndexBufferDirty)
	{
		gIndexBufferDirty = false;
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, gIndexBuffer->buffer);
		gIndexType = gIndexBuffer->stride == 2 ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT;
	}

	if (gVertexBufferDirty)
	{
		gVertexBufferDirty = false;
		glBindBuffer(GL_ARRAY_BUFFER, gVertexBuffer->buffer);		
#if defined(SKYGFX_PLATFORM_WINDOWS)
		glBindVertexBuffer(0, gVertexBuffer->buffer, 0, (GLsizei)gVertexBuffer->stride);
#elif defined(SKYGFX_PLATFORM_APPLE)
		gShader->applyLayout();
#endif
	}
	
	if (mTexParametersDirty)
	{
		refreshTexParameters();
		mTexParametersDirty = false;
	}

	if (mViewportDirty)
	{
		mViewportDirty = false;

		float width;
		float height;

		if (GLCurrentRenderTarget == nullptr)
		{
			width = static_cast<float>(mBackbufferWidth);
			height = static_cast<float>(mBackbufferHeight);
		}
		else
		{
			width = static_cast<float>(GLCurrentRenderTarget->width);
			height = static_cast<float>(GLCurrentRenderTarget->height);
		}

		auto viewport = mViewport.value_or(Viewport{ { 0.0f, 0.0f }, { width, height } });

		glViewport(
			(GLint)viewport.position.x,
			(GLint)viewport.position.y,
			(GLint)viewport.size.x,
			(GLint)viewport.size.y);

#if defined(SKYGFX_PLATFORM_WINDOWS)
		glDepthRange((GLclampd)viewport.min_depth, (GLclampd)viewport.max_depth);
#elif defined(SKYGFX_PLATFORM_APPLE)
		glDepthRangef((GLfloat)viewport.min_depth, (GLfloat)viewport.max_depth);
#endif
	}
}

void BackendGL44::refreshTexParameters()
{
	for (auto [binding, texture_handle] : mCurrentTextures)
	{
		glActiveTexture(GL_TEXTURE0 + binding);
	
		bool texture_has_mipmap = ((TextureDataGL44*)texture_handle)->mipmap;

		if (mSampler == Sampler::Linear)
		{
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, texture_has_mipmap ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		}
		else if (mSampler == Sampler::Nearest)
		{
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, texture_has_mipmap ? GL_NEAREST_MIPMAP_NEAREST : GL_NEAREST);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		}

		if (mTextureAddress == TextureAddress::Clamp)
		{
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		}
		else if (mTextureAddress == TextureAddress::Wrap)
		{
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
		}
		else if (mTextureAddress == TextureAddress::MirrorWrap)
		{
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_MIRRORED_REPEAT);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_MIRRORED_REPEAT);
		}
	}
}

#endif
