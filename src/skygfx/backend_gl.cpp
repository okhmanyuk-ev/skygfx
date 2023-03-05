#include "backend_gl.h"

#ifdef SKYGFX_HAS_OPENGL

#include <unordered_map>
#include <unordered_set>
#include <stdexcept>
#include <iostream>

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
	{ Vertex::Attribute::Format::Float1, 1 },
	{ Vertex::Attribute::Format::Float2, 2 },
	{ Vertex::Attribute::Format::Float3, 3 },
	{ Vertex::Attribute::Format::Float4, 4 },
	{ Vertex::Attribute::Format::Byte1, 1 },
	{ Vertex::Attribute::Format::Byte2, 2 },
	{ Vertex::Attribute::Format::Byte3, 3 },
	{ Vertex::Attribute::Format::Byte4, 4 }
};

static const std::unordered_map<Vertex::Attribute::Format, GLenum> TypeMap = {
	{ Vertex::Attribute::Format::Float1, GL_FLOAT },
	{ Vertex::Attribute::Format::Float2, GL_FLOAT },
	{ Vertex::Attribute::Format::Float3, GL_FLOAT },
	{ Vertex::Attribute::Format::Float4, GL_FLOAT },
	{ Vertex::Attribute::Format::Byte1, GL_UNSIGNED_BYTE },
	{ Vertex::Attribute::Format::Byte2, GL_UNSIGNED_BYTE },
	{ Vertex::Attribute::Format::Byte3, GL_UNSIGNED_BYTE },
	{ Vertex::Attribute::Format::Byte4, GL_UNSIGNED_BYTE }
};

static const std::unordered_map<Vertex::Attribute::Format, GLboolean> NormalizeMap = {
	{ Vertex::Attribute::Format::Float1, GL_FALSE },
	{ Vertex::Attribute::Format::Float2, GL_FALSE },
	{ Vertex::Attribute::Format::Float3, GL_FALSE },
	{ Vertex::Attribute::Format::Float4, GL_FALSE },
	{ Vertex::Attribute::Format::Byte1, GL_TRUE },
	{ Vertex::Attribute::Format::Byte2, GL_TRUE },
	{ Vertex::Attribute::Format::Byte3, GL_TRUE },
	{ Vertex::Attribute::Format::Byte4, GL_TRUE }
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

class ShaderGL
{
private:
	Vertex::Layout mLayout;
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
	ShaderGL(const Vertex::Layout& layout, const std::string& vertex_code, const std::string& fragment_code,
		std::vector<std::string> defines) : mLayout(layout)
	{
		AddShaderLocationDefines(layout, defines);
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
		for (int i = 0; i < mLayout.attributes.size(); i++)
		{
			const auto& attrib = mLayout.attributes.at(i);

			glVertexAttribPointer(i, SizeMap.at(attrib.format),
				TypeMap.at(attrib.format), NormalizeMap.at(attrib.format),
				(GLsizei)mLayout.stride, (void*)attrib.offset);
		}
	}
#endif
};

class TextureGL
{
public:
	auto getGLTexture() const { return mTexture; }
	auto isMipmap() const { return mMipmap; }
	auto getWidth() const { return mWidth; }
	auto getHeight() const { return mHeight; }

private:
	GLuint mTexture = 0;
	bool mMipmap = false;
	uint32_t mWidth = 0;
	uint32_t mHeight = 0;

public:
	TextureGL(uint32_t width, uint32_t height, uint32_t channels, void* memory, bool mipmap) :
		mMipmap(mipmap),
		mWidth(width),
		mHeight(height)
	{
		GLint last_texture;
		glGetIntegerv(GL_TEXTURE_BINDING_2D, &last_texture);
		glGenTextures(1, &mTexture);
		glBindTexture(GL_TEXTURE_2D, mTexture);
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

			glBindTexture(GL_TEXTURE_2D, mTexture);
			glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, temp_data);
			free(temp_data);
			
			if (mipmap)
				glGenerateMipmap(GL_TEXTURE_2D);
		}

		glBindTexture(GL_TEXTURE_2D, last_texture);
	}

	~TextureGL()
	{
		glDeleteTextures(1, &mTexture);
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

#if defined(SKYGFX_PLATFORM_WINDOWS)
static HGLRC WglContext;
static HDC gHDC;
#elif defined(SKYGFX_PLATFORM_IOS)
static GLKView* gGLKView = nullptr;
#elif defined(SKYGFX_PLATFORM_MACOS)
NSOpenGLView* glView;
NSOpenGLContext *glContext;
#elif defined(SKYGFX_PLATFORM_EMSCRIPTEN)
EGLDisplay gDisplay;
EGLSurface gSurface;
EGLContext gContext;
EGLConfig gConfig;
#endif

static GLenum gTopology;
static GLuint gPixelBuffer;
static RenderTargetGL* gRenderTarget = nullptr;
static ShaderGL* gShader = nullptr;
static bool gShaderDirty = false;

static VertexBufferGL* gVertexBuffer = nullptr;
static bool gVertexBufferDirty = false;

static IndexBufferGL* gIndexBuffer = nullptr;
static bool gIndexBufferDirty = false;
static GLenum gIndexType;

static ExecuteList gExecuteAfterPresent;

static bool gTexParametersDirty = true;
static Sampler gSampler = Sampler::Linear;
static TextureAddress gTextureAddress = TextureAddress::Wrap;

static std::unordered_set<uint32_t> gDirtyTextures;
static std::unordered_map<uint32_t, TextureGL*> gTextures;

static bool gViewportDirty = true;
static std::optional<Viewport> gViewport;

static bool gScissorDirty = true;
static std::optional<Scissor> gScissor;

static uint32_t gBackbufferWidth = 0;
static uint32_t gBackbufferHeight = 0;

BackendGL::BackendGL(void* window, uint32_t width, uint32_t height)
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
	glDebugMessageCallback(MessageCallback, 0);

	bool vsync = false;
	wglSwapIntervalEXT(vsync ? 1 : 0);
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
	gDisplay = eglGetDisplay(EGL_DEFAULT_DISPLAY);
	eglInitialize(gDisplay, NULL, NULL);
	EGLint num_configs;
	eglChooseConfig(gDisplay, attribs, &gConfig, 1, &num_configs);
	gSurface = eglCreateWindowSurface(gDisplay, gConfig, (EGLNativeWindowType)window, NULL);
	gContext = eglCreateContext(gDisplay, gConfig, NULL, context_attribs);
	eglMakeCurrent(gDisplay, gSurface, gSurface, gContext);
#endif

	glGenBuffers(1, &gPixelBuffer);

	gBackbufferWidth = width;
	gBackbufferHeight = height;
}

BackendGL::~BackendGL()
{
	glDeleteBuffers(1, &gPixelBuffer);
	
	gExecuteAfterPresent.flush();

#if defined(SKYGFX_PLATFORM_WINDOWS)
	wglDeleteContext(WglContext);
#elif defined(SKYGFX_PLATFORM_MACOS)
	[glView release];
#endif
}

void BackendGL::resize(uint32_t width, uint32_t height)
{
	gBackbufferWidth = width;
	gBackbufferHeight = height;
	gViewportDirty = true;
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

	gTopology = TopologyMap.at(topology);
}

void BackendGL::setViewport(std::optional<Viewport> viewport)
{
	if (gViewport == viewport)
		return;
		
	gViewport = viewport;
	gViewportDirty = true;
}

void BackendGL::setScissor(std::optional<Scissor> scissor)
{
	if (gScissor == scissor)
		return;
		
	gScissor = scissor;
	gScissorDirty = true;
}

void BackendGL::setTexture(uint32_t binding, TextureHandle* handle)
{
	auto texture = (TextureGL*)handle;
	
	if (gTextures[binding] == texture)
		return;
		
	gTextures[binding] = texture;
	gDirtyTextures.insert(binding);
	gTexParametersDirty = true;
}

void BackendGL::setRenderTarget(RenderTargetHandle* handle)
{
	auto render_target = (RenderTargetGL*)handle;
	
	if (gRenderTarget == render_target)
		return;
	
	glBindFramebuffer(GL_FRAMEBUFFER, render_target->getGLFramebuffer());
	gRenderTarget = render_target;

	if (!gViewport.has_value())
		gViewportDirty = true;
}

void BackendGL::setRenderTarget(std::nullopt_t value)
{
	if (gRenderTarget == nullptr)
		return;
		
#if defined(SKYGFX_PLATFORM_WINDOWS) | defined(SKYGFX_PLATFORM_MACOS) | defined(SKYGFX_PLATFORM_EMSCRIPTEN)
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
#elif defined(SKYGFX_PLATFORM_IOS)
	[gGLKView bindDrawable];
#endif
	gRenderTarget = nullptr;

	if (!gViewport.has_value())
		gViewportDirty = true;
}

void BackendGL::setShader(ShaderHandle* handle)
{
	auto shader = (ShaderGL*)handle;

	if (shader == gShader)
		return;
	
	gShader = shader;
	gShaderDirty = true;
}

void BackendGL::setVertexBuffer(VertexBufferHandle* handle)
{
	auto buffer = (VertexBufferGL*)handle;

	//if (gVertexBuffer == buffer) // TODO: gl emit errors when this code uncommented
	//	return;

	gVertexBuffer = buffer;
	gVertexBufferDirty = true;
}

void BackendGL::setIndexBuffer(IndexBufferHandle* handle)
{
	auto buffer = (IndexBufferGL*)handle;
	
	//if (gIndexBuffer == buffer) // TODO: gl emit errors when this code uncommented
	//	return;
	
	gIndexBuffer = buffer;
	gIndexBufferDirty = true;
}

void BackendGL::setUniformBuffer(uint32_t binding, UniformBufferHandle* handle)
{
	auto buffer = (UniformBufferGL*)handle;
	glBindBufferBase(GL_UNIFORM_BUFFER, binding, buffer->getGLBuffer());
}

void BackendGL::setBlendMode(const BlendMode& value)
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
	glBlendEquationSeparate(BlendOpMap.at(value.color_blend_func), BlendOpMap.at(value.alpha_blend_func));
	glBlendFuncSeparate(BlendMap.at(value.color_src_blend), BlendMap.at(value.color_dst_blend), 
		BlendMap.at(value.alpha_src_blend), BlendMap.at(value.alpha_dst_blend));
	glColorMask(value.color_mask.red, value.color_mask.green, value.color_mask.blue, value.color_mask.alpha);
}

void BackendGL::setDepthMode(std::optional<DepthMode> depth_mode)
{
	if (!depth_mode.has_value())
	{
		glDisable(GL_DEPTH_TEST);
		return;
	}

	glEnable(GL_DEPTH_TEST);
	glDepthFunc(ComparisonFuncMap.at(depth_mode.value().func));
}

void BackendGL::setStencilMode(std::optional<StencilMode> stencil_mode)
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
	glFrontFace(GL_CW);
	glCullFace(CullMap.at(cull_mode));
}

void BackendGL::setSampler(Sampler value)
{
	if (gSampler == value)
		return;

	gSampler = value;
	gTexParametersDirty = true;
}

void BackendGL::setTextureAddress(TextureAddress value)
{
	if (gTextureAddress == value)
		return;
		
	gTextureAddress = value;
	gTexParametersDirty = true;
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
	glDrawArrays(gTopology, (GLint)vertex_offset, (GLsizei)vertex_count);
}

void BackendGL::drawIndexed(uint32_t index_count, uint32_t index_offset)
{
	assert(gIndexBuffer);
	prepareForDrawing();
	uint32_t index_size = gIndexType == GL_UNSIGNED_INT ? 4 : 2;
	glDrawElements(gTopology, (GLsizei)index_count, gIndexType, (void*)(size_t)(index_offset * index_size));
}

void BackendGL::readPixels(const glm::i32vec2& pos, const glm::i32vec2& size, TextureHandle* dst_texture_handle)
{
	auto dst_texture = (TextureGL*)dst_texture_handle;

	assert(dst_texture->getWidth() == size.x);
	assert(dst_texture->getHeight() == size.y);

	if (size.x <= 0 || size.y <= 0)
		return;

	auto x = (GLint)pos.x;
	auto y = (GLint)(gBackbufferHeight - pos.y - size.y); // TODO: need different calculations when render target
	auto w = (GLint)size.x;
	auto h = (GLint)size.y;

	glBindBuffer(GL_PIXEL_PACK_BUFFER, gPixelBuffer);
	glBufferData(GL_PIXEL_PACK_BUFFER, w * h * 4, nullptr, GL_STATIC_READ);
	glReadPixels(x, y, w, h, GL_RGBA, GL_UNSIGNED_BYTE, 0);
	glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);

	glBindTexture(GL_TEXTURE_2D, dst_texture->getGLTexture());
	glBindBuffer(GL_PIXEL_UNPACK_BUFFER, gPixelBuffer);

	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, 0);

	if (dst_texture->isMipmap())
		glGenerateMipmap(GL_TEXTURE_2D);

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
	eglSwapBuffers(gDisplay, gSurface);
#endif
	gExecuteAfterPresent.flush();
}

TextureHandle* BackendGL::createTexture(uint32_t width, uint32_t height, uint32_t channels, void* memory, bool mipmap)
{
	auto texture = new TextureGL(width, height, channels, memory, mipmap);
	return (TextureHandle*)texture;
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

ShaderHandle* BackendGL::createShader(const Vertex::Layout& layout, const std::string& vertex_code, 
	const std::string& fragment_code, const std::vector<std::string>& defines)
{
	auto shader = new ShaderGL(layout, vertex_code, fragment_code, defines);
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
	gExecuteAfterPresent.add([handle] {
		auto buffer = (VertexBufferGL*)handle;

		if (gVertexBuffer == buffer)
			gVertexBuffer = nullptr;

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
	gExecuteAfterPresent.add([handle] {
		auto buffer = (IndexBufferGL*)handle;

		if (gIndexBuffer == buffer)
			gIndexBuffer = nullptr;

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
	gExecuteAfterPresent.add([handle] {
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
	assert(gShader);
	assert(gVertexBuffer);

	if (gShaderDirty)
	{
		gShader->apply();
		gVertexBufferDirty = true;
		gShaderDirty = false;
	}

	if (gIndexBufferDirty)
	{
		gIndexBufferDirty = false;
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, gIndexBuffer->getGLBuffer());
		gIndexType = gIndexBuffer->getStride() == 2 ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT;
	}

	if (gVertexBufferDirty)
	{
		gVertexBufferDirty = false;
		glBindBuffer(GL_ARRAY_BUFFER, gVertexBuffer->getGLBuffer());
#if defined(SKYGFX_PLATFORM_WINDOWS)
		glBindVertexBuffer(0, gVertexBuffer->getGLBuffer(), 0, (GLsizei)gVertexBuffer->getStride());
#elif defined(SKYGFX_PLATFORM_IOS) | defined(SKYGFX_PLATFORM_MACOS) | defined(SKYGFX_PLATFORM_EMSCRIPTEN)
		gShader->applyLayout();
#endif
	}

	for (auto binding : gDirtyTextures)
	{
		auto texture = gTextures.at(binding);
		
		glActiveTexture(GL_TEXTURE0 + binding);
		glBindTexture(GL_TEXTURE_2D, texture->getGLTexture());
	}

	gDirtyTextures.clear();

	if (gTexParametersDirty)
	{
		for (auto [binding, texture_handle] : gTextures)
		{
			glActiveTexture(GL_TEXTURE0 + binding);

			bool texture_has_mipmap = ((TextureGL*)texture_handle)->isMipmap();

			if (gSampler == Sampler::Linear)
			{
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, texture_has_mipmap ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
			}
			else if (gSampler == Sampler::Nearest)
			{
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, texture_has_mipmap ? GL_NEAREST_MIPMAP_NEAREST : GL_NEAREST);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
			}

			if (gTextureAddress == TextureAddress::Clamp)
			{
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
			}
			else if (gTextureAddress == TextureAddress::Wrap)
			{
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
			}
			else if (gTextureAddress == TextureAddress::MirrorWrap)
			{
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_MIRRORED_REPEAT);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_MIRRORED_REPEAT);
			}
		}
		gTexParametersDirty = false;
	}

	if (gViewportDirty)
	{
		gViewportDirty = false;

		float width;
		float height;

		if (gRenderTarget == nullptr)
		{
			width = static_cast<float>(gBackbufferWidth);
			height = static_cast<float>(gBackbufferHeight);
		}
		else
		{
			width = static_cast<float>(gRenderTarget->getTexture()->getWidth());
			height = static_cast<float>(gRenderTarget->getTexture()->getHeight());
		}

		auto viewport = gViewport.value_or(Viewport{ { 0.0f, 0.0f }, { width, height } });
		
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
	
	if (gScissorDirty)
	{
		gScissorDirty = false;
		
		if (gScissor.has_value())
		{
			auto value = gScissor.value();
			
			glEnable(GL_SCISSOR_TEST);
			glScissor(
				(GLint)glm::round(value.position.x),
				(GLint)glm::round(gBackbufferHeight - value.position.y - value.size.y), // TODO: need different calculations when render target
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
