#include <skygfx/skygfx.h>
#include <skygfx/utils.h>
#include "../utils/utils.h"

int main()
{
	auto backend_type = utils::ChooseBackendTypeViaConsole();

	glfwInit();
	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

	auto [window, native_window, width, height] = utils::SpawnWindow(800, 600, "Scratch Api Triangle");

	skygfx::Initialize(native_window, width, height, backend_type);

	glfwSetFramebufferSizeCallback(window, [](GLFWwindow* window, int width, int height) {
		skygfx::Resize(static_cast<uint32_t>(width), static_cast<uint32_t>(height));
	});

	while (!glfwWindowShouldClose(window))
	{
		skygfx::Clear();

		skygfx::utils::scratch::Begin(skygfx::utils::MeshBuilder::Mode::Triangles);
		skygfx::utils::scratch::Vertex(skygfx::vertex::PositionColor{ {  0.5f, -0.5f, 0.0f }, { 0.0f, 0.0f, 1.0f, 1.0f } });
		skygfx::utils::scratch::Vertex(skygfx::vertex::PositionColor{ { -0.5f, -0.5f, 0.0f }, { 1.0f, 0.0f, 0.0f, 1.0f } });
		skygfx::utils::scratch::Vertex(skygfx::vertex::PositionColor{ {  0.0f,  0.5f, 0.0f }, { 0.0f, 1.0f, 0.0f, 1.0f } });
		skygfx::utils::scratch::End();
		skygfx::utils::scratch::Flush();

		skygfx::Present();

		glfwPollEvents();
	}

	skygfx::Finalize();

	glfwTerminate();

	return 0;
}
