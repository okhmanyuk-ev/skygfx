#include <skygfx/skygfx.h>
#include <skygfx/ext.h>
#include "../utils/utils.h"

int main()
{
	auto backend_type = skygfx::BackendType::Vulkan;// utils::ChooseBackendTypeViaConsole();

	glfwInit();
	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

	auto [window, native_window, width, height] = utils::SpawnWindow(800, 600, "Raytracing Triangle");

	skygfx::Initialize(native_window, width, height, backend_type);

	glfwSetFramebufferSizeCallback(window, [](GLFWwindow* window, int width, int height) {
		skygfx::Resize(static_cast<uint32_t>(width), static_cast<uint32_t>(height));
	});

	auto target = skygfx::RenderTarget(800, 600);

	while (!glfwWindowShouldClose(window))
	{
		skygfx::SetRenderTarget(target);
		skygfx::Clear(glm::vec4{ 1.0f, 0.0f, 0.0f, 1.0f });
		skygfx::DispatchRays();

		skygfx::SetRenderTarget(std::nullopt);
		skygfx::Clear(glm::vec4{ 0.0f, 0.0f, 0.0f, 1.0f });
		skygfx::ext::ExecuteCommands({
			skygfx::ext::commands::SetColorTexture{ &target },
			skygfx::ext::commands::Draw()
		});

		skygfx::Present();

		glfwPollEvents();
	}

	skygfx::Finalize();

	glfwTerminate();

	return 0;
}
