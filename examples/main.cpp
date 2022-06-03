#include <iostream>

#include <GLFW/glfw3.h>
#include <skygfx/skygfx.h>

#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>

static std::string vertex_shader_code = R"(
#version 450 core

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec4 aColor;

layout(location = 0) out struct { vec4 Color; } Out;
out gl_PerVertex { vec4 gl_Position; };

void main()
{
	Out.Color = aColor;
	gl_Position = vec4(aPosition, 1.0);
}
)";

static std::string fragment_shader_code = R"(
#version 450 core

layout(location = 0) out vec4 result;
layout(location = 0) in struct { vec4 Color; } In;

void main() 
{ 
	result = In.Color;
}
)";

static std::vector<skygfx::Vertex::PositionColor> vertices = {
	{ { 0.0f, -0.5f, 0.0f }, { 0.0f, 1.0f, 0.0f, 1.0f } },
	{ { -0.5f, 0.5f, 0.0f }, { 1.0f, 0.0f, 0.0f, 1.0f } },
	{ { 0.5f, 0.5f, 0.0f }, { 0.0f, 0.0f, 1.0f, 1.0f } },
};

static std::vector<uint32_t> indices = { 0, 1, 2 };

int main()
{
	if (!glfwInit())
		return -1;

	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

	uint32_t width = 800;
	uint32_t height = 600;

	auto window = glfwCreateWindow(width, height, "Hello Triangle", NULL, NULL);

	int count = 0;
	auto monitors = glfwGetMonitors(&count);

	auto video_mode = glfwGetVideoMode(monitors[0]);

	auto window_pos_x = (video_mode->width / 2) - (width / 2);
	auto window_pos_y = (video_mode->height / 2) - (height / 2);

	glfwSetWindowPos(window, window_pos_x, window_pos_y);
	glfwMakeContextCurrent(window);

	auto win32_window = glfwGetWin32Window(window);

	auto device = skygfx::Device(skygfx::BackendType::D3D11, win32_window, width, height);
	auto shader = skygfx::Shader(skygfx::Vertex::PositionColor::Layout, vertex_shader_code, fragment_shader_code);

	auto viewport = skygfx::Viewport();
	viewport.size = { static_cast<float>(width), static_cast<float>(height) };

	while (!glfwWindowShouldClose(window))
	{
		device.clear(glm::vec4{ 0.0f, 0.0f, 0.0f, 1.0f });
		device.setViewport(viewport);
		device.setTopology(skygfx::Topology::TriangleList);
		device.setVertexBuffer(vertices);
		device.setIndexBuffer(indices);
		device.setShader(shader);
		device.drawIndexed(3);
		device.present();

		glfwPollEvents();
	}

	glfwTerminate();
	return 0;
}