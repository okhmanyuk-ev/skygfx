#include <skygfx/skygfx.h>
#include <skygfx/vertex.h>
#include "../utils/utils.h"
#include <format>

glm::vec4 BlitPixelsToOne(const std::vector<uint8_t>& pixels)
{
	glm::vec4 result = { 0.0f, 0.0f, 0.0f, 0.0f };

	const size_t channels_count = 4;

	for (size_t i = 0; i < pixels.size(); i += channels_count)
	{
		for (size_t j = 0; j < channels_count; j++)
		{
			if (pixels.size() <= i + j)
				continue;

			result[j] += glm::unpackUnorm1x8(pixels.at(i + j));
		}
	}

	result /= (pixels.size() / (float)channels_count);

	return result;
}

bool Clear(skygfx::BackendType backend)
{
	glfwInit();
	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

	auto [window, native_window, width, height] = utils::SpawnWindow(800, 600, "test");

	skygfx::Initialize(native_window, width, height, backend);

	glm::vec4 clear_color = { 0.0f, 1.0f, 0.0f, 1.0f };

	skygfx::Clear(clear_color);
			
	auto pixels = skygfx::GetPixels();
	auto pixel = BlitPixelsToOne(pixels);

	auto result = pixel == clear_color;

	skygfx::Present();

	glfwPollEvents();

	skygfx::Finalize();

	glfwTerminate();

	return result;
}

bool ClearRenderTarget(skygfx::BackendType backend)
{
	glfwInit();
	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

	auto [window, native_window, width, height] = utils::SpawnWindow(800, 600, "test");

	skygfx::Initialize(native_window, width, height, backend);

	auto target = skygfx::RenderTarget(8, 8, skygfx::Format::Byte4);

	glm::vec4 clear_color = { 0.0f, 1.0f, 0.0f, 1.0f };

	skygfx::SetRenderTarget(target);
	skygfx::Clear(clear_color);

	auto pixels = skygfx::GetPixels();
	auto pixel = BlitPixelsToOne(pixels);

	auto result = pixel == clear_color;

	skygfx::Present();

	glfwPollEvents();

	skygfx::Finalize();

	glfwTerminate();

	return result;
}

bool Triangle(skygfx::BackendType backend)
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

	glfwInit();
	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

	auto [window, native_window, width, height] = utils::SpawnWindow(800, 600, "test");

	skygfx::Initialize(native_window, width, height, backend);

	auto shader = skygfx::Shader(Vertex::Layout, vertex_shader_code, fragment_shader_code);

	skygfx::SetTopology(skygfx::Topology::TriangleList);
	skygfx::SetShader(shader);
	skygfx::SetIndexBuffer(indices);
	skygfx::SetVertexBuffer(vertices);

	skygfx::Clear(glm::vec4{ 0.0f, 0.0f, 0.0f, 1.0f });
	skygfx::DrawIndexed(static_cast<uint32_t>(indices.size()));

	auto pixels = skygfx::GetPixels();
	auto pixel = BlitPixelsToOne(pixels);

	auto result = pixel == glm::vec4{ 0.0416479930f, 0.0416579768f, 0.0416475832f, 1.00000000f };

	skygfx::Present();

	glfwPollEvents();

	skygfx::Finalize();

	glfwTerminate();

	return result;
}

bool TriangleRenderTarget(skygfx::BackendType backend)
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

	glfwInit();
	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

	auto [window, native_window, width, height] = utils::SpawnWindow(800, 600, "test");

	skygfx::Initialize(native_window, width, height, backend);

	auto shader = skygfx::Shader(Vertex::Layout, vertex_shader_code, fragment_shader_code);
	auto target = skygfx::RenderTarget(16, 16, skygfx::Format::Byte4);

	skygfx::SetTopology(skygfx::Topology::TriangleList);
	skygfx::SetShader(shader);
	skygfx::SetIndexBuffer(indices);
	skygfx::SetVertexBuffer(vertices);
	skygfx::SetRenderTarget(target);
	skygfx::Clear(glm::vec4{ 0.0f, 0.0f, 0.0f, 1.0f });
	skygfx::DrawIndexed(static_cast<uint32_t>(indices.size()));

	auto pixels = skygfx::GetPixels();
	auto pixel = BlitPixelsToOne(pixels);

	auto result = pixel == glm::vec4{ 0.0420343392f, 0.0410539247f, 0.0420343354f, 1.00000000f };

	skygfx::Present();

	glfwPollEvents();

	skygfx::Finalize();

	glfwTerminate();

	return result;
}

int main()
{
	#define PUSH(F) { #F, F }
	
	std::vector<std::pair<std::string, std::function<bool(skygfx::BackendType)>>> test_cases = {
		PUSH(Clear),
		PUSH(ClearRenderTarget),
		PUSH(Triangle),
		PUSH(TriangleRenderTarget),
	};

	auto available_backends = skygfx::GetAvailableBackends();

	size_t total = available_backends.size() * test_cases.size();
	size_t current = 0;

	for (const auto& [name, func] : test_cases)
	{
		for (auto backend : available_backends)
		{
			current += 1;

			const auto& backend_name = utils::GetBackendName(backend);
			auto before = std::chrono::high_resolution_clock::now();
			bool result = func(backend);
			auto duration = std::chrono::high_resolution_clock::now() - before;
			auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
			auto result_str = result ? "SUCCESS" : "FAIL";
			
			auto log_str = std::format("[{}/{}]	{}	{}	{} ms	{}", current, total, result_str, backend_name, duration_ms, name);
			std::cout << log_str << std::endl;
		}
	}

	std::cout << "Press ENTER to continue..." << std::endl;
	std::getchar();

	return 0;
}
