#include <skygfx/skygfx.h>
#include <skygfx/utils.h>
#define STB_IMAGE_IMPLEMENTATION
#include "../utils/utils.h"
#include "../utils/imgui_helper.h"
#include <imgui.h>
#include <imgui_impl_glfw.h>

const auto WhiteColor = glm::vec4{ 1.0f, 1.0f, 1.0f, 1.0f };

const skygfx::utils::Mesh::Vertices vertices = {
	/* front */
	/* 0  */ { { -1.0f,  1.0f,  1.0f }, WhiteColor, { 0.0f, 0.0f }, { 0.0f, 0.0f, 1.0f } },
	/* 1  */ { {  1.0f,  1.0f,  1.0f }, WhiteColor, { 1.0f, 0.0f }, { 0.0f, 0.0f, 1.0f } },
	/* 2  */ { { -1.0f, -1.0f,  1.0f }, WhiteColor, { 0.0f, 1.0f }, { 0.0f, 0.0f, 1.0f } },
	/* 3  */ { {  1.0f, -1.0f,  1.0f }, WhiteColor, { 1.0f, 1.0f }, { 0.0f, 0.0f, 1.0f } },

	/* top */
	/* 4  */ { { -1.0f,  1.0f,  1.0f }, WhiteColor, { 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f } },
	/* 5  */ { { -1.0f,  1.0f, -1.0f }, WhiteColor, { 0.0f, 1.0f }, { 0.0f, 1.0f, 0.0f } },
	/* 6  */ { {  1.0f,  1.0f,  1.0f }, WhiteColor, { 1.0f, 0.0f }, { 0.0f, 1.0f, 0.0f } },
	/* 7  */ { {  1.0f,  1.0f, -1.0f }, WhiteColor, { 1.0f, 1.0f }, { 0.0f, 1.0f, 0.0f } },

	/* left */
	/* 8  */ { { -1.0f,  1.0f, -1.0f }, WhiteColor, { 0.0f, 0.0f }, { -1.0f, 0.0f, 0.0f } },
	/* 9  */ { { -1.0f,  1.0f,  1.0f }, WhiteColor, { 1.0f, 0.0f }, { -1.0f, 0.0f, 0.0f } },
	/* 10 */ { { -1.0f, -1.0f, -1.0f }, WhiteColor, { 0.0f, 1.0f }, { -1.0f, 0.0f, 0.0f } },
	/* 11 */ { { -1.0f, -1.0f,  1.0f }, WhiteColor, { 1.0f, 1.0f }, { -1.0f, 0.0f, 0.0f } },

	/* back */
	/* 12 */ { { -1.0f,  1.0f, -1.0f }, WhiteColor, { 1.0f, 0.0f }, { 0.0f, 0.0f, -1.0f } },
	/* 13 */ { { -1.0f, -1.0f, -1.0f }, WhiteColor, { 1.0f, 1.0f }, { 0.0f, 0.0f, -1.0f } },
	/* 14 */ { {  1.0f,  1.0f, -1.0f }, WhiteColor, { 0.0f, 0.0f }, { 0.0f, 0.0f, -1.0f } },
	/* 15 */ { {  1.0f, -1.0f, -1.0f }, WhiteColor, { 0.0f, 1.0f }, { 0.0f, 0.0f, -1.0f } },

	/* bottom */
	/* 16 */ { { -1.0f, -1.0f,  1.0f }, WhiteColor, { 0.0f, 0.0f }, { 0.0f, -1.0f, 0.0f } },
	/* 17 */ { {  1.0f, -1.0f,  1.0f }, WhiteColor, { 0.0f, 1.0f }, { 0.0f, -1.0f, 0.0f } },
	/* 18 */ { { -1.0f, -1.0f, -1.0f }, WhiteColor, { 1.0f, 0.0f }, { 0.0f, -1.0f, 0.0f } },
	/* 19 */ { {  1.0f, -1.0f, -1.0f }, WhiteColor, { 1.0f, 1.0f }, { 0.0f, -1.0f, 0.0f } },

	/* right */
	/* 20 */ { { 1.0f, -1.0f, -1.0f }, WhiteColor, { 1.0f, 1.0f }, { 1.0f, 0.0f, 0.0f } },
	/* 21 */ { { 1.0f, -1.0f,  1.0f }, WhiteColor, { 0.0f, 1.0f }, { 1.0f, 0.0f, 0.0f } },
	/* 22 */ { { 1.0f,  1.0f, -1.0f }, WhiteColor, { 1.0f, 0.0f }, { 1.0f, 0.0f, 0.0f } },
	/* 23 */ { { 1.0f,  1.0f,  1.0f }, WhiteColor, { 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f } },
};

const skygfx::utils::Mesh::Indices indices = {
	0, 1, 2, 1, 3, 2, // front
	4, 5, 6, 5, 7, 6, // top
	8, 9, 10, 9, 11, 10, // left
	12, 13, 14, 13, 15, 14, // back
	16, 17, 18, 17, 19, 18, // bottom
	20, 21, 22, 21, 23, 22, // right
};

int main()
{
	auto backend_type = utils::ChooseBackendTypeViaConsole();

	glfwInit();
	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

	auto [window, native_window, width, height] = utils::SpawnWindow(800, 600, "Bloom");

	skygfx::Initialize(native_window, width, height, backend_type);

	glfwSetFramebufferSizeCallback(window, [](GLFWwindow* window, int width, int height) {
		skygfx::Resize(static_cast<uint32_t>(width), static_cast<uint32_t>(height));
	});

	auto [tex_width, tex_height, tex_memory] = utils::LoadTexture("assets/bricks.jpg");

	auto texture = skygfx::Texture(tex_width, tex_height, skygfx::PixelFormat::RGBA8UNorm, tex_memory, true);

	const auto camera = skygfx::utils::PerspectiveCamera{
		.yaw = 0.0f,
		.pitch = glm::radians(-25.0f),
		.position = { -5.0f, 2.0f, 0.0f }
	};

	const auto light = skygfx::utils::DirectionalLight{
		.direction = { 1.0f, 0.5f, 0.5f },
		.ambient = { 0.25f, 0.25f, 0.25f },
		.diffuse = { 1.0f, 1.0f, 1.0f },
		.specular = { 1.0f, 1.0f, 1.0f },
		.shininess = 32.0f
	};

	skygfx::utils::Mesh cube_mesh;
	cube_mesh.setVertices(vertices);
	cube_mesh.setIndices(indices);

	auto imgui = ImguiHelper();

	ImGui_ImplGlfw_InitForOpenGL(window, true);

	float angle = 1.0f;
	float speed = 1.0f;
	bool animated = true;

	float threshold = 1.0f;
	float intensity = 2.0f;
	bool gaussian = false;

	auto model = skygfx::utils::Model{
		.mesh = &cube_mesh,
		.color_texture = &texture,
		.cull_mode = skygfx::CullMode::Back
	};

	StageViewer stage_viewer;
	skygfx::utils::SetStageViewer(&stage_viewer);

	while (!glfwWindowShouldClose(window))
	{
		ImGui_ImplGlfw_NewFrame();

		ImGui::NewFrame();

		ImGui::Begin("Settings", NULL, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove);
		ImGui::SetWindowPos({ 16.0f, 16.0f });
		ImGui::SliderFloat("Intensity", &intensity, 0.0f, 4.0f);
		ImGui::SliderFloat("Threshold", &threshold, 0.0f, 1.0f);
		ImGui::Checkbox("Gaussian", &gaussian);
		ImGui::Checkbox("Animated", &animated);
		ImGui::SliderFloat("Speed", &speed, 0.0f, 2.0f);
		ImGui::SliderAngle("Angle", &angle, 0.0f);
		ImGui::End();

		auto src_target = skygfx::AcquireTransientRenderTarget();

		if (animated)
			angle = glm::wrapAngle((float)glfwGetTime() * speed);

		model.matrix = glm::rotate(glm::mat4(1.0f), angle, { 0.0f, 1.0f, 0.0f });

		skygfx::utils::DrawScene(src_target, camera, { model }, { light });

		if (gaussian)
			skygfx::utils::passes::BloomGaussian(src_target, nullptr, threshold, intensity);
		else
			skygfx::utils::passes::Bloom(src_target, nullptr, threshold, intensity);

		stage_viewer.show();
		imgui.draw();

		skygfx::Present();

		glfwPollEvents();
	}

	skygfx::Finalize();

	glfwTerminate();

	return 0;
}
