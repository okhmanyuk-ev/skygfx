#include <skygfx/skygfx.h>
#include <skygfx/utils.h>
#include <format>
#include <chrono>

#define STB_IMAGE_IMPLEMENTATION
#include "../utils/utils.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

using SavePixelsFunc = std::function<void(uint32_t width, uint32_t height, const std::vector<uint8_t>&)>;

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

bool Clear(skygfx::BackendType backend, SavePixelsFunc save_pixels_func)
{
	skygfx::Initialize(gNativeWindow, gWidth, gHeight, backend);

	glm::vec4 clear_color = { 0.0f, 1.0f, 0.0f, 1.0f };

	skygfx::Clear(clear_color);
			
	auto pixels = skygfx::GetBackbufferPixels();
	save_pixels_func(gWidth, gHeight, pixels);
	auto pixel = BlitPixelsToOne(pixels);

	auto result = pixel == clear_color;

	skygfx::Present();
	skygfx::Finalize();

	return result;
}

bool ClearRenderTarget(skygfx::BackendType backend, SavePixelsFunc save_pixels_func)
{
	skygfx::Initialize(gNativeWindow, gWidth, gHeight, backend);

	auto target = skygfx::RenderTarget(8, 8, skygfx::Format::Byte4);

	glm::vec4 clear_color = { 0.0f, 1.0f, 0.0f, 1.0f };

	skygfx::SetRenderTarget(target);
	skygfx::Clear(clear_color);

	auto pixels = skygfx::GetBackbufferPixels();
	save_pixels_func(target.getWidth(), target.getHeight(), pixels);
	auto pixel = BlitPixelsToOne(pixels);

	auto result = pixel == clear_color;

	skygfx::Present();
	skygfx::Finalize();

	return result;
}

bool Triangle(skygfx::BackendType backend, SavePixelsFunc save_pixels_func)
{
	skygfx::Initialize(gNativeWindow, gWidth, gHeight, backend);
	skygfx::Clear();

	skygfx::utils::scratch::Begin(skygfx::utils::MeshBuilder::Mode::Triangles);
	skygfx::utils::scratch::Vertex({ .pos = {  0.5f, -0.5f, 0.0f }, .color = { 0.0f, 0.0f, 1.0f, 1.0f } });
	skygfx::utils::scratch::Vertex({ .pos = { -0.5f, -0.5f, 0.0f }, .color = { 1.0f, 0.0f, 0.0f, 1.0f } });
	skygfx::utils::scratch::Vertex({ .pos = {  0.0f,  0.5f, 0.0f }, .color = { 0.0f, 1.0f, 0.0f, 1.0f } });
	skygfx::utils::scratch::End();
	skygfx::utils::scratch::Flush();

	auto pixels = skygfx::GetBackbufferPixels();
	save_pixels_func(gWidth, gHeight, pixels);
	auto pixel = BlitPixelsToOne(pixels);

	auto result = pixel == glm::vec4{
		0.0416507758f,
		0.0416276492f,
		0.0416508429f,
		1.00000000f
	};

	skygfx::Present();
	skygfx::utils::ClearContext();
	skygfx::Finalize();

	return result;
}

bool TriangleRenderTarget(skygfx::BackendType backend, SavePixelsFunc save_pixels_func)
{
	skygfx::Initialize(gNativeWindow, gWidth, gHeight, backend);

	auto target = skygfx::RenderTarget(16, 16, skygfx::Format::Byte4);

	skygfx::SetRenderTarget(target);
	skygfx::Clear();

	skygfx::utils::scratch::Begin(skygfx::utils::MeshBuilder::Mode::Triangles);
	skygfx::utils::scratch::Vertex({ .pos = {  0.5f, -0.5f, 0.0f }, .color = { 0.0f, 0.0f, 1.0f, 1.0f } });
	skygfx::utils::scratch::Vertex({ .pos = { -0.5f, -0.5f, 0.0f }, .color = { 1.0f, 0.0f, 0.0f, 1.0f } });
	skygfx::utils::scratch::Vertex({ .pos = {  0.0f,  0.5f, 0.0f }, .color = { 0.0f, 1.0f, 0.0f, 1.0f } });
	skygfx::utils::scratch::End();
	skygfx::utils::scratch::Flush();

	auto pixels = skygfx::GetBackbufferPixels();
	save_pixels_func(target.getWidth(), target.getHeight(), pixels);
	auto pixel = BlitPixelsToOne(pixels);

	auto result = pixel == glm::vec4{
		0.0420343205f,
		0.0410539210f,
		0.0420343131f,
		1.00000000f
	};

	skygfx::utils::passes::Blit(&target, nullptr, {
		.clear = true,
		.sampler = skygfx::Sampler::Nearest
	});

	skygfx::Present();
	skygfx::utils::ClearContext();
	skygfx::Finalize();

	return result;
}

int main()
{
#define PUSH(F) { #F, F }

	std::vector<std::pair<std::string, std::function<bool(skygfx::BackendType, SavePixelsFunc)>>> test_cases = {
		PUSH(Clear),
		PUSH(ClearRenderTarget),
		PUSH(Triangle),
		PUSH(TriangleRenderTarget),
	};

	glfwInit();
	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

	auto [window, native_window, width, height] = utils::SpawnWindow(800, 600, "tests");

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
