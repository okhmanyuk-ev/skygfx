#include <skygfx/skygfx.h>
#include "../utils/utils.h"
#include "../utils/imgui_helper.h"
#include <imgui.h>

int main()
{
	auto backend_type = utils::ChooseBackendTypeViaConsole();

	glfwInit();
	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

	auto [window, native_window, width, height] = utils::SpawnWindow(800, 600, "Imgui");

	skygfx::Initialize(native_window, width, height, backend_type);

	glfwSetFramebufferSizeCallback(window, [](GLFWwindow* window, int width, int height) {
		skygfx::Resize(static_cast<uint32_t>(width), static_cast<uint32_t>(height));
	});

	auto imgui = ImguiHelper(window);

	while (!glfwWindowShouldClose(window))
	{
		imgui.newFrame();

		ImGui::ShowDemoWindow();

		skygfx::Clear();

		imgui.draw();

		skygfx::Present();

		glfwPollEvents();
	}

	skygfx::Finalize();

	glfwTerminate();

	return 0;
}
