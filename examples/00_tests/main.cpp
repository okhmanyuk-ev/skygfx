#include <skygfx/skygfx.h>
#include <skygfx/vertex.h>
#include "../utils/utils.h"
#include <format>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

using PixelsFunc = std::function<void(uint32_t width, uint32_t height, const std::vector<uint8_t>&)>;

static void* gNativeWindow = nullptr;
static uint32_t gWidth = 0;
static uint32_t gHeight = 0;

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

bool Clear(skygfx::BackendType backend, PixelsFunc pixels_func)
{
	skygfx::Initialize(gNativeWindow, gWidth, gHeight, backend);

	glm::vec4 clear_color = { 0.0f, 1.0f, 0.0f, 1.0f };

	skygfx::Clear(clear_color);
			
	auto pixels = skygfx::GetPixels();
	pixels_func(gWidth, gHeight, pixels);
	auto pixel = BlitPixelsToOne(pixels);

	auto result = pixel == clear_color;

	skygfx::Present();
	skygfx::Finalize();

	return result;
}

bool ClearRenderTarget(skygfx::BackendType backend, PixelsFunc pixels_func)
{
	skygfx::Initialize(gNativeWindow, gWidth, gHeight, backend);

	auto target = skygfx::RenderTarget(8, 8, skygfx::Format::Byte4);

	glm::vec4 clear_color = { 0.0f, 1.0f, 0.0f, 1.0f };

	skygfx::SetRenderTarget(target);
	skygfx::Clear(clear_color);

	auto pixels = skygfx::GetPixels();
	pixels_func(target.getWidth(), target.getHeight(), pixels);
	auto pixel = BlitPixelsToOne(pixels);

	auto result = pixel == clear_color;

	skygfx::Present();
	skygfx::Finalize();

	return result;
}

bool Triangle(skygfx::BackendType backend, PixelsFunc pixels_func)
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

	skygfx::Initialize(gNativeWindow, gWidth, gHeight, backend);

	auto shader = skygfx::Shader(Vertex::Layout, vertex_shader_code, fragment_shader_code);

	skygfx::SetTopology(skygfx::Topology::TriangleList);
	skygfx::SetShader(shader);
	skygfx::SetIndexBuffer(indices);
	skygfx::SetVertexBuffer(vertices);

	skygfx::Clear(glm::vec4{ 0.0f, 0.0f, 0.0f, 1.0f });
	skygfx::DrawIndexed(static_cast<uint32_t>(indices.size()));

	auto pixels = skygfx::GetPixels();
	pixels_func(gWidth, gHeight, pixels);
	auto pixel = BlitPixelsToOne(pixels);

	auto result = pixel == glm::vec4{
		0.0416530818f,
		0.0416318141f,
		0.0416527465f,
		1.00000000f
	};

	skygfx::Present();
	skygfx::Finalize();

	return result;
}

bool TriangleRenderTarget(skygfx::BackendType backend, PixelsFunc pixels_func)
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

	skygfx::Initialize(gNativeWindow, gWidth, gHeight, backend);

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
	pixels_func(target.getWidth(), target.getHeight(), pixels);
	auto pixel = BlitPixelsToOne(pixels);

	auto result = pixel == glm::vec4{
		0.0420343205f,
		0.0410539210f,
		0.0420343131f,
		1.00000000f
	};

	skygfx::Present();
	skygfx::Finalize();

	return result;
}

int main()
{
	#define PUSH(F) { #F, F }
	
	std::vector<std::pair<std::string, std::function<bool(skygfx::BackendType, PixelsFunc)>>> test_cases = {
		PUSH(Clear),
		PUSH(ClearRenderTarget),
		PUSH(Triangle),
		PUSH(TriangleRenderTarget),
	};

	glfwInit();
	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

	auto [window, native_window, width, height] = utils::SpawnWindow(800, 600, "test");

	gNativeWindow = native_window;
	gWidth = width;
	gHeight = height;

	auto available_backends = skygfx::GetAvailableBackends();

	size_t total = available_backends.size() * test_cases.size();
	size_t current = 0;
	size_t passed = 0;

	for (auto backend : available_backends)
	{
		for (const auto& [name, func] : test_cases) 
		{
			current += 1;

			const auto& backend_name = utils::GetBackendName(backend);

			auto pixels_save_func = [&](uint32_t width, uint32_t height, const std::vector<uint8_t>& pixels) {
				int channels = 4;
				
				if (pixels.size() != width * height * channels)
					return;

				auto filename = name + "_" + backend_name + ".png";
				stbi_write_png(filename.c_str(), width, height, channels, pixels.data(), width * channels);
			};

			auto before = std::chrono::high_resolution_clock::now();
			bool result = func(backend, pixels_save_func);
			auto duration = std::chrono::high_resolution_clock::now() - before;
			auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
			auto result_str = result ? "SUCCESS" : "FAIL";
			
			if (result)
				passed += 1;

			auto log_str = std::format("[{}/{}]	{}	{}	{} ms	{}", current, total, result_str, backend_name, duration_ms, name);
			std::cout << log_str << std::endl;
		}
	}

	glfwTerminate();

	std::cout << "---------------------" << std::endl;
	std::cout << std::format("{}/{} tests passed!", passed, total) << std::endl;
	std::cout << "Press ENTER to continue..." << std::endl;
	std::getchar();

	return 0;
}
