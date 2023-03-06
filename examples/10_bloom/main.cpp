#include <skygfx/skygfx.h>
#include <skygfx/ext.h>
#include "../utils/utils.h"

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>	

const auto WhiteColor = glm::vec4{ 1.0f, 1.0f, 1.0f, 1.0f };

const skygfx::ext::Mesh::Vertices vertices = {
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

const skygfx::ext::Mesh::Indices indices = {
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

	int tex_width = 0;
	int tex_height = 0;
	void* tex_memory = stbi_load("assets/bricks.png", &tex_width, &tex_height, nullptr, 4); // TODO: this image has 3 channels, we must can load that type of images

	auto texture = skygfx::Texture(tex_width, tex_height, 4/*TODO: no magic numbers should be*/, tex_memory, true);

	const auto camera = skygfx::ext::PerspectiveCamera{
		.yaw = 0.0f,
		.pitch = glm::radians(-25.0f),
		.position = glm::vec3{ -500.0f, 200.0f, 0.0f }
	};

	const auto light = skygfx::ext::DirectionalLight{
		.direction = { 1.0f, 0.5f, 0.5f },
		.ambient = { 0.25f, 0.25f, 0.25f },
		.diffuse = { 1.0f, 1.0f, 1.0f },
		.specular = { 1.0f, 1.0f, 1.0f },
		.shininess = 32.0f
	};

	skygfx::ext::Mesh cube_mesh;
	cube_mesh.setVertices(vertices);
	cube_mesh.setIndices(indices);

	const auto scale = 100.0f;

	skygfx::SetCullMode(skygfx::CullMode::Back);

	std::optional<skygfx::RenderTarget> src_target;
	std::optional<skygfx::RenderTarget> dst_target;

	skygfx::ext::passes::Bloom bloom_pass;

	auto ensureTargetSizes = [&](auto& target) {
		int win_width;
		int win_height;
		glfwGetWindowSize(window, &win_width, &win_height);

		if (!target.has_value() || target.value().getWidth() != win_width || target.value().getHeight() != win_height)
			target.emplace(win_width, win_height);
	};

	while (!glfwWindowShouldClose(window))
	{
		ensureTargetSizes(src_target);
		ensureTargetSizes(dst_target);

		auto time = (float)glfwGetTime();

		auto model = glm::mat4(1.0f);
		model = glm::scale(model, { scale, scale, scale });
		model = glm::rotate(model, time, { 0.0f, 1.0f, 0.0f });
		
		skygfx::SetRenderTarget(src_target.value());
		skygfx::Clear(glm::vec4{ 0.0f, 0.0f, 0.0f, 1.0f });

		skygfx::ext::ExecuteCommands({
			skygfx::ext::commands::SetMesh{ &cube_mesh },
			skygfx::ext::commands::SetCamera{ camera },
			skygfx::ext::commands::SetLight{ light },
			skygfx::ext::commands::SetColorTexture{ &texture },
			skygfx::ext::commands::SetModelMatrix{ model },
			skygfx::ext::commands::Draw{}
		});

		skygfx::SetRenderTarget(dst_target.value());
		skygfx::Clear(glm::vec4{ 0.0f, 0.0f, 0.0f, 1.0f });

		bloom_pass.execute(src_target.value(), dst_target.value());

		skygfx::SetRenderTarget(std::nullopt);

		skygfx::ext::ExecuteCommands({
			skygfx::ext::commands::SetColorTexture{ &dst_target.value() },
			skygfx::ext::commands::Draw{}
		});
	
		skygfx::Present();

		glfwPollEvents();
	}

	skygfx::Finalize();

	glfwTerminate();
	
	return 0;
}
