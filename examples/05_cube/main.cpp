#include <skygfx/skygfx.h>
#include <skygfx/vertex.h>
#include "../utils/utils.h"

const std::string vertex_shader_code = R"(
#version 450 core

layout(location = POSITION_LOCATION) in vec3 aPosition;
layout(location = COLOR_LOCATION) in vec4 aColor;

layout(binding = 0) uniform _ubo
{
	mat4 projection;
	mat4 view;
	mat4 model;
} ubo;

layout(location = 0) out struct { vec4 Color; } Out;
out gl_PerVertex { vec4 gl_Position; };

void main()
{
	Out.Color = aColor;
	gl_Position = ubo.projection * ubo.view * ubo.model * vec4(aPosition, 1.0);
})";

const std::string fragment_shader_code = R"(
#version 450 core

layout(location = 0) out vec4 result;
layout(location = 0) in struct { vec4 Color; } In;

void main()
{
	result = In.Color;
})";

using Vertex = skygfx::vertex::PositionColor;

const glm::vec4 Red = { 1.0f, 0.0f, 0.0f, 1.0f };
const glm::vec4 Green = { 0.0f, 1.0f, 0.0f, 1.0f };
const glm::vec4 Blue = { 0.0f, 0.0f, 1.0f, 1.0f };
const glm::vec4 Yellow = { 1.0f, 1.0f, 0.0f, 1.0f };
const glm::vec4 Cyan = { 0.0f, 1.0f, 1.0f, 1.0f };
const glm::vec4 Magenta = { 1.0f, 0.0f, 1.0f, 1.0f };

const std::vector<Vertex> vertices = {
	/* front */
	/* 0  */ { { -1.0f,  1.0f,  1.0f }, Red },
	/* 1  */ { {  1.0f,  1.0f,  1.0f }, Red },
	/* 2  */ { { -1.0f, -1.0f,  1.0f }, Red },
	/* 3  */ { {  1.0f, -1.0f,  1.0f }, Red },

	/* top */
	/* 4  */ { { -1.0f,  1.0f,  1.0f }, Green },
	/* 5  */ { { -1.0f,  1.0f, -1.0f }, Green },
	/* 6  */ { {  1.0f,  1.0f,  1.0f }, Green },
	/* 7  */ { {  1.0f,  1.0f, -1.0f }, Green },

	/* left */
	/* 8  */ { { -1.0f,  1.0f, -1.0f }, Blue },
	/* 9  */ { { -1.0f,  1.0f,  1.0f }, Blue },
	/* 10 */ { { -1.0f, -1.0f, -1.0f }, Blue },
	/* 11 */ { { -1.0f, -1.0f,  1.0f }, Blue },

	/* back */
	/* 12 */ { { -1.0f,  1.0f, -1.0f }, Yellow },
	/* 13 */ { { -1.0f, -1.0f, -1.0f }, Yellow },
	/* 14 */ { {  1.0f,  1.0f, -1.0f }, Yellow },
	/* 15 */ { {  1.0f, -1.0f, -1.0f }, Yellow },

	/* bottom */
	/* 16 */ { { -1.0f, -1.0f,  1.0f }, Cyan },
	/* 17 */ { {  1.0f, -1.0f,  1.0f }, Cyan },
	/* 18 */ { { -1.0f, -1.0f, -1.0f }, Cyan },
	/* 19 */ { {  1.0f, -1.0f, -1.0f }, Cyan },

	/* right */
	/* 20 */ { { 1.0f, -1.0f, -1.0f }, Magenta },
	/* 21 */ { { 1.0f, -1.0f,  1.0f }, Magenta },
	/* 22 */ { { 1.0f,  1.0f, -1.0f }, Magenta },
	/* 23 */ { { 1.0f,  1.0f,  1.0f }, Magenta },
};

const std::vector<uint32_t> indices = {
	0, 1, 2, 1, 3, 2, // front
	4, 5, 6, 5, 7, 6, // top
	8, 9, 10, 9, 11, 10, // left
	12, 13, 14, 13, 15, 14, // back
	16, 17, 18, 17, 19, 18, // bottom
	20, 21, 22, 21, 23, 22, // right
};

struct alignas(16) Matrices
{
	glm::mat4 projection = glm::mat4(1.0f);
	glm::mat4 view = glm::mat4(1.0f);
	glm::mat4 model = glm::mat4(1.0f);
} matrices;

int main()
{
	auto backend_type = utils::ChooseBackendTypeViaConsole();

	glfwInit();
	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

	auto [window, native_window, width, height] = utils::SpawnWindow(800, 600, "Cube");

	skygfx::Initialize(native_window, width, height, backend_type);

	glfwSetFramebufferSizeCallback(window, [](GLFWwindow* window, int width, int height) {
		skygfx::Resize(static_cast<uint32_t>(width), static_cast<uint32_t>(height));
	});

	auto shader = skygfx::Shader(vertex_shader_code, fragment_shader_code, Vertex::Defines);

	const auto yaw = 0.0f;
	const auto pitch = glm::radians(-25.0f);
	const auto position = glm::vec3{ -5.0f, 2.0f, 0.0f };

	skygfx::SetTopology(skygfx::Topology::TriangleList);
	skygfx::SetShader(shader);
	skygfx::SetVertexBuffer(vertices);
	skygfx::SetIndexBuffer(indices);
	skygfx::SetCullMode(skygfx::CullMode::Back);
	skygfx::SetInputLayout(Vertex::Layout);

	while (!glfwWindowShouldClose(window))
	{
		std::tie(matrices.view, matrices.projection) = utils::CalculatePerspectiveViewProjection(yaw, pitch, position);

		auto time = (float)glfwGetTime();

		matrices.model = glm::mat4(1.0f);
		matrices.model = glm::rotate(matrices.model, time, { 0.0f, 1.0f, 0.0f });

		skygfx::SetUniformBuffer(0, matrices);

		skygfx::Clear();
		skygfx::DrawIndexed(static_cast<uint32_t>(indices.size()));
		skygfx::Present();

		glfwPollEvents();
	}

	skygfx::Finalize();

	glfwTerminate();

	return 0;
}
