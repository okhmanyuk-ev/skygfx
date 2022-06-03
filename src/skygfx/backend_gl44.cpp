#include "backend_gl44.h"

#define GLEW_STATIC
#include <GL/glew.h>
#include <GL/GL.h>
#include <GL/wglew.h>
#pragma comment(lib, "opengl32")
#pragma comment(lib, "glu32")

using namespace skygfx;

static HGLRC WglContext;
static HDC gHDC;

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

	// auto version = glGetString(GL_VERSION);

}

BackendGL44::~BackendGL44()
{

}

void BackendGL44::setTopology(Topology topology)
{
	//
}

void BackendGL44::setViewport(const Viewport& viewport)
{
	//
}

void BackendGL44::setTexture(TextureHandle* handle)
{
	//
}

void BackendGL44::setShader(ShaderHandle* handle)
{
	//
}

void BackendGL44::setVertexBuffer(const Buffer& buffer)
{
	//
}

void BackendGL44::setIndexBuffer(const Buffer& buffer)
{
	//
}

void BackendGL44::setBlendMode(const BlendMode& value)
{
	//
}

void BackendGL44::clear(std::optional<glm::vec4> color, std::optional<float> depth, std::optional<uint8_t> stencil)
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
	//glDrawElementsBaseVertex(mGLTopology, (GLsizei)indexCount, mGLIndexType, (void*)(indexOffset * indexSize), (GLint)vertexOffset);
}

void BackendGL44::present()
{
	SwapBuffers(gHDC);
}

TextureHandle* BackendGL44::createTexture()
{
	return nullptr;
}

void BackendGL44::destroyTexture(TextureHandle* handle)
{
	//
}

ShaderHandle* BackendGL44::createShader(const Vertex::Layout& layout, const std::string& vertex_code, const std::string& fragment_code)
{
	return nullptr;
}

void BackendGL44::destroyShader(ShaderHandle* handle)
{
	//
}
