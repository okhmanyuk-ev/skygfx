#include <skygfx/skygfx.h>
#include <skygfx/vertex.h>
#include "../utils/utils.h"

bool Initialize()
{
	auto available_backends = skygfx::GetAvailableBackends();

	for (auto backend_type : available_backends)
	{
		glfwInit();
		glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

		auto [window, native_window, width, height] = utils::SpawnWindow(800, 600, "Setup");

		skygfx::Initialize(native_window, width, height, backend_type);
		skygfx::Finalize();

		glfwTerminate();
	}

	return true;
}

bool Clear()
{
	auto available_backends = skygfx::GetAvailableBackends();

	for (auto backend_type : available_backends)
	{
		glfwInit();
		glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

		auto [window, native_window, width, height] = utils::SpawnWindow(800, 600, "Triangle");

		skygfx::Initialize(native_window, width, height, backend_type);

		while (!glfwWindowShouldClose(window))
		{
			skygfx::Clear(glm::vec4{ 0.0f, 1.0f, 0.0f, 1.0f });
			skygfx::Present();

			glfwPollEvents();

			glfwSetWindowShouldClose(window, true);
		}

		skygfx::Finalize();

		glfwTerminate();
	}

	return true;
}

bool Triangle()
{
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

	auto available_backends = skygfx::GetAvailableBackends();

	for (auto backend_type : available_backends)
	{
		glfwInit();
		glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

		auto [window, native_window, width, height] = utils::SpawnWindow(800, 600, "Triangle");

		skygfx::Initialize(native_window, width, height, backend_type);

		auto shader = skygfx::Shader(Vertex::Layout, vertex_shader_code, fragment_shader_code);

		skygfx::SetTopology(skygfx::Topology::TriangleList);
		skygfx::SetShader(shader);
		skygfx::SetIndexBuffer(indices);
		skygfx::SetVertexBuffer(vertices);

		while (!glfwWindowShouldClose(window))
		{
			skygfx::Clear(glm::vec4{ 0.0f, 0.0f, 0.0f, 1.0f });
			skygfx::DrawIndexed(static_cast<uint32_t>(indices.size()));
			skygfx::Present();

			glfwPollEvents();

			glfwSetWindowShouldClose(window, true);
		}

		skygfx::Finalize();

		glfwTerminate();
	}

	return true;
}

int main()
{
	#define PUSH(F) { #F, F }
	
	std::vector<std::pair<std::string, std::function<bool()>>> test_cases = {
		PUSH(Initialize),
		PUSH(Clear),
		PUSH(Triangle)
	};

	for (const auto& [name, func] : test_cases)
	{
		bool result = func();

		std::cout << name << " - " << (result ? "SUCCESS" : "FAIL") << std::endl;
	}

	return 0;
}
