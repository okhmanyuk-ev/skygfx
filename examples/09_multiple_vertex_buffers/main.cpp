#include <skygfx/skygfx.h>
#include <skygfx/vertex.h>
#include "../utils/utils.h"

const std::string vertex_shader_code = R"(
#version 450 core

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec4 aColor;

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

const std::vector<glm::vec3> positions = {
	{  0.5f, -0.5f, 0.0f },
	{ -0.5f, -0.5f, 0.0f },
	{  0.0f,  0.5f, 0.0f }
};

const std::vector<glm::vec4> colors = {
	{ 0.0f, 0.0f, 1.0f, 1.0f },
	{ 1.0f, 0.0f, 0.0f, 1.0f },
	{ 0.0f, 1.0f, 0.0f, 1.0f }
};

const std::vector<uint32_t> indices = { 0, 1, 2 };

int main()
{
	auto backend_type = utils::ChooseBackendTypeViaConsole();

	glfwInit();
	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

	auto [window, native_window, width, height] = utils::SpawnWindow(800, 600, "Multiple vertex buffers");

	skygfx::Initialize(native_window, width, height, backend_type);

	glfwSetFramebufferSizeCallback(window, [](GLFWwindow* window, int width, int height) {
		skygfx::Resize(static_cast<uint32_t>(width), static_cast<uint32_t>(height));
	});

	auto shader = skygfx::Shader(vertex_shader_code, fragment_shader_code);

	auto positions_buffer = skygfx::VertexBuffer(positions);
	auto colors_buffer = skygfx::VertexBuffer(colors);

	auto positions_layout = skygfx::InputLayout(skygfx::InputLayout::Rate::Vertex, {
		{ 0, { skygfx::VertexFormat::Float3, 0 } } 
	});

	auto colors_layout = skygfx::InputLayout(skygfx::InputLayout::Rate::Vertex, {
		{ 1, { skygfx::VertexFormat::Float4, 0 } }
	});

	skygfx::SetTopology(skygfx::Topology::TriangleList);
	skygfx::SetShader(shader);
	skygfx::SetIndexBuffer(indices);
	skygfx::SetVertexBuffer({ &positions_buffer, &colors_buffer });
	skygfx::SetInputLayout({ positions_layout, colors_layout });

	while (!glfwWindowShouldClose(window))
	{
		skygfx::Clear();
		skygfx::DrawIndexed(static_cast<uint32_t>(indices.size()));
		skygfx::Present();

		glfwPollEvents();
	}

	skygfx::Finalize();

	glfwTerminate();

	return 0;
}
