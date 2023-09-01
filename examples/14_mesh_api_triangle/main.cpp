#include <skygfx/skygfx.h>
#include <skygfx/utils.h>
#include "../utils/utils.h"

int main()
{
	auto backend_type = utils::ChooseBackendTypeViaConsole();

	glfwInit();
	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

	auto [window, native_window, width, height] = utils::SpawnWindow(800, 600, "Mesh Api Triangle");

	skygfx::Initialize(native_window, width, height, backend_type);

	glfwSetFramebufferSizeCallback(window, [](GLFWwindow* window, int width, int height) {
		skygfx::Resize(static_cast<uint32_t>(width), static_cast<uint32_t>(height));
	});

	skygfx::utils::MeshBuilder mesh_builder;
	mesh_builder.begin(skygfx::utils::MeshBuilder::Mode::Triangles);
	mesh_builder.vertex(skygfx::Vertex::PositionColor{ {  0.5f, -0.5f, 0.0f }, { 0.0f, 0.0f, 1.0f, 1.0f } });
	mesh_builder.vertex(skygfx::Vertex::PositionColor{ { -0.5f, -0.5f, 0.0f }, { 1.0f, 0.0f, 0.0f, 1.0f } });
	mesh_builder.vertex(skygfx::Vertex::PositionColor{ {  0.0f,  0.5f, 0.0f }, { 0.0f, 1.0f, 0.0f, 1.0f } });
	mesh_builder.end();

	skygfx::utils::Mesh mesh;
	mesh_builder.setToMesh(mesh);

	while (!glfwWindowShouldClose(window))
	{
		skygfx::Clear();

		skygfx::utils::ExecuteCommands({
			skygfx::utils::commands::SetMesh(&mesh),
			skygfx::utils::commands::Draw()
		});

		skygfx::Present();

		glfwPollEvents();
	}

	skygfx::Finalize();

	glfwTerminate();

	return 0;
}
