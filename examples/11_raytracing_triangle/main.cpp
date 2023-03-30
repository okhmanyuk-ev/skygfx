#include <skygfx/skygfx.h>
#include "../utils/utils.h"

int main()
{
	auto backend_type = skygfx::BackendType::Vulkan;// utils::ChooseBackendTypeViaConsole();

	glfwInit();
	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

	auto [window, native_window, width, height] = utils::SpawnWindow(800, 600, "Triangle");

	skygfx::Initialize(native_window, width, height, backend_type);

	glfwSetFramebufferSizeCallback(window, [](GLFWwindow* window, int width, int height) {
		skygfx::Resize(static_cast<uint32_t>(width), static_cast<uint32_t>(height));
	});

	while (!glfwWindowShouldClose(window))
	{
		skygfx::Clear(glm::vec4{ 0.0f, 0.0f, 0.0f, 1.0f });
		skygfx::DispatchRays();
		skygfx::Present();

		glfwPollEvents();
	}

	skygfx::Finalize();

	glfwTerminate();

	return 0;
}
