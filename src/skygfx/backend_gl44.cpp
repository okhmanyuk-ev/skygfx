#include "backend_gl44.h"

#include <unordered_map>
#include <stdexcept>
#include <iostream>

#define GLEW_STATIC
#include <GL/glew.h>
#include <GL/GL.h>
#include <GL/wglew.h>

#pragma comment(lib, "opengl32")
#pragma comment(lib, "glu32")

using namespace skygfx;

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

const std::unordered_map<Vertex::Attribute::Format, GLboolean> static NormalizeMap = {
	{ Vertex::Attribute::Format::R32F, GL_FALSE },
	{ Vertex::Attribute::Format::R32G32F, GL_FALSE },
	{ Vertex::Attribute::Format::R32G32B32F, GL_FALSE },
	{ Vertex::Attribute::Format::R32G32B32A32F, GL_FALSE },
	{ Vertex::Attribute::Format::R8UN, GL_TRUE },
	{ Vertex::Attribute::Format::R8G8UN, GL_TRUE },
	{ Vertex::Attribute::Format::R8G8B8UN, GL_TRUE },
	{ Vertex::Attribute::Format::R8G8B8A8UN, GL_TRUE }
};

class ShaderDataGL44
{
private:
	Vertex::Layout layout;
	GLuint program;
	GLuint vao;

public:
	ShaderDataGL44(const Vertex::Layout& layout, const std::string& vertex_code, const std::string& fragment_code)
	{
		std::vector<std::string> defines;
		AddShaderLocationDefines(layout, defines);
		defines.push_back("FLIP_TEXCOORD_Y");

		auto vertex_shader_spirv = CompileGlslToSpirv(ShaderStage::Vertex, vertex_code, defines);
		auto fragment_shader_spirv = CompileGlslToSpirv(ShaderStage::Fragment, fragment_code, defines);

		auto glsl_vert = CompileSpirvToGlsl(vertex_shader_spirv);
		auto glsl_frag = CompileSpirvToGlsl(fragment_shader_spirv);

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
		glDeleteShader(vertexShader);
		glDeleteShader(fragmentShader);

		glGenVertexArrays(1, &vao);
		glBindVertexArray(vao);

		for (int i = 0; i < layout.attributes.size(); i++)
		{
			const auto& attrib = layout.attributes.at(i);

			glEnableVertexAttribArray(i);
			glVertexAttribFormat(i, SizeMap.at(attrib.format), TypeMap.at(attrib.format), 
				NormalizeMap.at(attrib.format), (GLuint)attrib.offset);
			glVertexAttribBinding(i, 0);
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
};

class TextureDataGL44
{
private:
	GLuint texture;
	bool is_mipmap;

public:
	TextureDataGL44(uint32_t width, uint32_t height, uint32_t channels, void* memory, bool mipmap) : is_mipmap(mipmap)
	{
		GLint last_texture;
		glGetIntegerv(GL_TEXTURE_BINDING_2D, &last_texture);
		glGenTextures(1, &texture);
		glBindTexture(GL_TEXTURE_2D, texture);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
		glBindTexture(GL_TEXTURE_2D, last_texture);

		writePixels(width, height, channels, memory, mipmap);
	}

	void writePixels(int width, int height, int channels, void* memory, bool mipmap)
	{
	//	assert(width == mWidth);
	//	assert(height == mHeight);
		assert(memory);

		auto temp_data = malloc(width * height * 4); // TODO: we should not use magic numbers

		const auto row_size = width * 4;

		for (int i = 0; i < height; i++)
		{
			auto src = (void*)(size_t(memory) + size_t(i) * row_size);
			auto dst = (void*)(size_t(temp_data) + size_t(height - 1 - i) * row_size);

			memcpy(dst, src, row_size);
		}

		GLint last_texture;
		glGetIntegerv(GL_TEXTURE_BINDING_2D, &last_texture);
		glBindTexture(GL_TEXTURE_2D, texture);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, temp_data);

		if (mipmap)
			glGenerateMipmap(GL_TEXTURE_2D);

		glBindTexture(GL_TEXTURE_2D, last_texture);

		free(temp_data);
	}

	~TextureDataGL44()
	{
		glDeleteTextures(1, &texture);
	}

	void bind(int slot)
	{
		glActiveTexture(GL_TEXTURE0 + slot);
		glBindTexture(GL_TEXTURE_2D, texture);

		// sampler state
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, is_mipmap ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
	}
};

static HGLRC WglContext;
static HDC gHDC;

static GLenum GLTopology;
static GLenum GLIndexType;
static GLuint GLVertexBuffer;
static GLuint GLIndexBuffer;
static GLuint GLUniformBuffer;

BackendGL44::BackendGL44(void* window)
{
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

	glGenBuffers(1, &GLVertexBuffer);
	glGenBuffers(1, &GLIndexBuffer);
	glGenBuffers(1, &GLUniformBuffer);
}

BackendGL44::~BackendGL44()
{
	glDeleteBuffers(1, &GLVertexBuffer);
	glDeleteBuffers(1, &GLIndexBuffer);
	glDeleteBuffers(1, &GLUniformBuffer);
	
	wglDeleteContext(WglContext);
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

	GLTopology = TopologyMap.at(topology);
}

void BackendGL44::setViewport(const Viewport& viewport)
{
	glViewport(
		(GLint)viewport.position.x,
		(GLint)viewport.position.y,
		(GLint)viewport.size.x,
		(GLint)viewport.size.y);

	glDepthRange((GLclampd)viewport.min_depth, (GLclampd)viewport.max_depth);
}

void BackendGL44::setTexture(TextureHandle* handle)
{
	int slot = 0;
	auto texture = (TextureDataGL44*)handle;
	texture->bind(slot);
}

void BackendGL44::setShader(ShaderHandle* handle)
{
	auto shader = (ShaderDataGL44*)handle;
	shader->apply();
}

void BackendGL44::setVertexBuffer(const Buffer& buffer)
{
	glBindBuffer(GL_ARRAY_BUFFER, GLVertexBuffer);

	GLint size = 0;
	glGetBufferParameteriv(GL_ARRAY_BUFFER, GL_BUFFER_SIZE, &size);

	if ((size_t)size < buffer.size)
	{
		glBufferData(GL_ARRAY_BUFFER, buffer.size, buffer.data, GL_DYNAMIC_DRAW);
	}
	else
	{
		auto ptr = glMapBufferRange(GL_ARRAY_BUFFER, 0, size, GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_BUFFER_BIT);
		memcpy(ptr, buffer.data, buffer.size);
		glUnmapBuffer(GL_ARRAY_BUFFER);
	}

	glBindVertexBuffer(0, GLVertexBuffer, 0, (GLsizei)buffer.stride);
}

void BackendGL44::setIndexBuffer(const Buffer& buffer)
{
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, GLIndexBuffer);

	GLint size = 0;
	glGetBufferParameteriv(GL_ELEMENT_ARRAY_BUFFER, GL_BUFFER_SIZE, &size);

	if ((size_t)size < buffer.size)
	{
		glBufferData(GL_ELEMENT_ARRAY_BUFFER, buffer.size, buffer.data, GL_DYNAMIC_DRAW);
	}
	else
	{
		auto ptr = glMapBufferRange(GL_ELEMENT_ARRAY_BUFFER, 0, size, GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_BUFFER_BIT);
		memcpy(ptr, buffer.data, buffer.size);
		glUnmapBuffer(GL_ELEMENT_ARRAY_BUFFER);

	}

	GLIndexType = buffer.stride == 2 ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT;
}

void BackendGL44::setUniformBuffer(int slot, void* memory, size_t size)
{
	assert(size % 16 == 0);

	glBindBufferBase(GL_UNIFORM_BUFFER, slot, GLUniformBuffer);
	glBufferData(GL_UNIFORM_BUFFER, size, memory, GL_DYNAMIC_DRAW);
}

void BackendGL44::setBlendMode(const BlendMode& value)
{
	//
}

void BackendGL44::setDepthMode(const DepthMode& value)
{
	//
}

void BackendGL44::setStencilMode(const StencilMode& value)
{
	//
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

void BackendGL44::drawIndexed(uint32_t index_count, uint32_t index_offset)
{
	uint32_t index_size = GLIndexType == GL_UNSIGNED_INT ? 4 : 2;
	glDrawElements(GLTopology, (GLsizei)index_count, GLIndexType, (void*)(size_t)(index_offset * index_size));
}

void BackendGL44::present()
{
	SwapBuffers(gHDC);
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

ShaderHandle* BackendGL44::createShader(const Vertex::Layout& layout, const std::string& vertex_code, const std::string& fragment_code)
{
	auto shader = new ShaderDataGL44(layout, vertex_code, fragment_code);
	return (ShaderHandle*)shader;
}

void BackendGL44::destroyShader(ShaderHandle* handle)
{
	auto shader = (ShaderDataGL44*)handle;
	delete shader;
}
