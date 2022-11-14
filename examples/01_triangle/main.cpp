#include <skygfx/skygfx.h>
#include "../utils/utils.h"

const std::string vertex_shader_code = R"(
#version 450 core

layout(location = POSITION_LOCATION) in vec3 aPosition;
layout(location = COLOR_LOCATION) in vec4 aColor;

layout(location = 0) out struct { vec4 Color; } Out;
out gl_PerVertex { vec4 gl_Position; };

void main()
{
	Out.Color = aColor;
	gl_Position = vec4(aPosition, 1.0);
})";

const std::string fragment_shader_code = R"(
#version 450 core

layout(location = 0) out vec4 result;
layout(location = 0) in struct { vec4 Color; } In;

void main() 
{ 
	result = In.Color;
})";

using Vertex = skygfx::Vertex::PositionColor;

const std::vector<Vertex> vertices = {
	{ {  0.5f, -0.5f, 0.0f }, { 0.0f, 0.0f, 1.0f, 1.0f } },
	{ { -0.5f, -0.5f, 0.0f }, { 1.0f, 0.0f, 0.0f, 1.0f } },
	{ {  0.0f,  0.5f, 0.0f }, { 0.0f, 1.0f, 0.0f, 1.0f } },
};

const std::vector<uint32_t> indices = { 0, 1, 2 };

int main()
{
	auto backend_type = utils::ChooseBackendTypeViaConsole();

	glfwInit();
	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

	uint32_t width = 800;
	uint32_t height = 600;

	auto window = glfwCreateWindow(width, height, "Triangle", NULL, NULL);

	int count = 0;
	auto monitors = glfwGetMonitors(&count);

	auto video_mode = glfwGetVideoMode(monitors[0]);

	auto window_pos_x = (video_mode->width / 2) - (width / 2);
	auto window_pos_y = (video_mode->height / 2) - (height / 2);

	glfwSetWindowPos(window, window_pos_x, window_pos_y);

	auto native_window = utils::GetNativeWindow(window);

	skygfx::Initialize(native_window, width, height, backend_type);

	auto shader = skygfx::Shader(Vertex::Layout, vertex_shader_code, fragment_shader_code);

	skygfx::SetTopology(skygfx::Topology::TriangleList);
	skygfx::SetShader(shader);
	skygfx::SetDynamicIndexBuffer(indices);
	skygfx::SetDynamicVertexBuffer(vertices);

	while (!glfwWindowShouldClose(window))
	{
		skygfx::Clear(glm::vec4{ 0.0f, 0.0f, 0.0f, 1.0f });
		skygfx::DrawIndexed(static_cast<uint32_t>(indices.size()));
		skygfx::Present();

		glfwPollEvents();
	}

	skygfx::Finalize();
	
	glfwTerminate();
	
	return 0;
}
