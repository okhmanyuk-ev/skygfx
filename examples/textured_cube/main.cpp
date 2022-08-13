#include <iostream>

#include <GLFW/glfw3.h>
#include <skygfx/skygfx.h>
#include "../utils/utils.h"

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>	

static std::string vertex_shader_code = R"(
#version 450 core

layout(location = POSITION_LOCATION) in vec3 aPosition;
layout(location = TEXCOORD_LOCATION) in vec2 aTexCoord;

layout(binding = 1) uniform _ubo
{
	mat4 projection;
	mat4 view;
	mat4 model;
} ubo;

layout(location = 0) out struct { vec2 TexCoord; } Out;
out gl_PerVertex { vec4 gl_Position; };

void main()
{
	Out.TexCoord = aTexCoord;
#ifdef FLIP_TEXCOORD_Y
	Out.TexCoord.y = 1.0 - Out.TexCoord.y;
#endif
	gl_Position = ubo.projection * ubo.view * ubo.model * vec4(aPosition, 1.0);
})";

static std::string fragment_shader_code = R"(
#version 450 core

layout(location = 0) out vec4 result;
layout(location = 0) in struct { vec2 TexCoord; } In;
layout(binding = 0) uniform sampler2D sTexture;

void main() 
{ 
	result = texture(sTexture, In.TexCoord);
})";

using Vertex = skygfx::Vertex::PositionTexture;

const std::vector<Vertex> vertices = {
	/* front */
	/* 0  */ { { -1.0f,  1.0f,  1.0f }, { 0.0f, 0.0f } },
	/* 1  */ { {  1.0f,  1.0f,  1.0f }, { 1.0f, 0.0f } },
	/* 2  */ { { -1.0f, -1.0f,  1.0f }, { 0.0f, 1.0f } },
	/* 3  */ { {  1.0f, -1.0f,  1.0f }, { 1.0f, 1.0f } },

	/* top */
	/* 4  */ { { -1.0f,  1.0f,  1.0f }, { 0.0f, 0.0f } },
	/* 5  */ { { -1.0f,  1.0f, -1.0f }, { 0.0f, 1.0f } },
	/* 6  */ { {  1.0f,  1.0f,  1.0f }, { 1.0f, 0.0f } },
	/* 7  */ { {  1.0f,  1.0f, -1.0f }, { 1.0f, 1.0f } },

	/* left */
	/* 8  */ { { -1.0f,  1.0f, -1.0f }, { 0.0f, 0.0f } },
	/* 9  */ { { -1.0f,  1.0f,  1.0f }, { 1.0f, 0.0f } },
	/* 10 */ { { -1.0f, -1.0f, -1.0f }, { 0.0f, 1.0f } },
	/* 11 */ { { -1.0f, -1.0f,  1.0f }, { 1.0f, 1.0f } },

	/* back */
	/* 12 */ { { -1.0f,  1.0f, -1.0f }, { 1.0f, 0.0f } },
	/* 13 */ { { -1.0f, -1.0f, -1.0f }, { 1.0f, 1.0f } },
	/* 14 */ { {  1.0f,  1.0f, -1.0f }, { 0.0f, 0.0f } },
	/* 15 */ { {  1.0f, -1.0f, -1.0f }, { 0.0f, 1.0f } },

	/* bottom */
	/* 16 */ { { -1.0f, -1.0f,  1.0f }, { 0.0f, 0.0f } },
	/* 17 */ { {  1.0f, -1.0f,  1.0f }, { 0.0f, 1.0f } },
	/* 18 */ { { -1.0f, -1.0f, -1.0f }, { 1.0f, 0.0f } },
	/* 19 */ { {  1.0f, -1.0f, -1.0f }, { 1.0f, 1.0f } },

	/* right */
	/* 20 */ { { 1.0f, -1.0f, -1.0f }, { 1.0f, 1.0f } },
	/* 21 */ { { 1.0f, -1.0f,  1.0f }, { 0.0f, 1.0f } },
	/* 22 */ { { 1.0f,  1.0f, -1.0f }, { 1.0f, 0.0f } },
	/* 23 */ { { 1.0f,  1.0f,  1.0f }, { 0.0f, 0.0f } },
};

static std::vector<uint32_t> indices = {
	0, 1, 2, 1, 3, 2, // front
	4, 5, 6, 5, 7, 6, // top
	8, 9, 10, 9, 11, 10, // left
	12, 13, 14, 13, 15, 14, // back
	16, 17, 18, 17, 19, 18, // bottom
	20, 21, 22, 21, 23, 22, // right
};

static struct alignas(16) Matrices
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

	uint32_t width = 800;
	uint32_t height = 600;

	auto window = glfwCreateWindow(width, height, "Textured Cube", NULL, NULL);

	int count = 0;
	auto monitors = glfwGetMonitors(&count);

	auto video_mode = glfwGetVideoMode(monitors[0]);

	auto window_pos_x = (video_mode->width / 2) - (width / 2);
	auto window_pos_y = (video_mode->height / 2) - (height / 2);

	glfwSetWindowPos(window, window_pos_x, window_pos_y);
	glfwMakeContextCurrent(window);

	auto native_window = utils::GetNativeWindow(window);

	auto device = skygfx::Device(backend_type, native_window, width, height);
	auto shader = skygfx::Shader(Vertex::Layout, vertex_shader_code, fragment_shader_code);

	int tex_width = 0;
	int tex_height = 0;
	void* tex_memory = stbi_load("assets/bricks.png", &tex_width, &tex_height, nullptr, 4); // TODO: this image has 3 channels, we must can load that type of images

	auto texture = skygfx::Texture(tex_width, tex_height, 4/*TODO: no magic numbers should be*/, tex_memory, true);

	auto vertex_buffer = skygfx::VertexBuffer(vertices);
	auto index_buffer = skygfx::IndexBuffer(indices);
	auto uniform_buffer = skygfx::UniformBuffer(matrices);

	const auto yaw = 0.0f;
	const auto pitch = glm::radians(-25.0f);
	const auto position = glm::vec3{ -500.0f, 200.0f, 0.0f };

	std::tie(matrices.view, matrices.projection) = utils::CalculatePerspectiveViewProjection(yaw, pitch, position, width, height);

	const auto scale = 100.0f;

	while (!glfwWindowShouldClose(window))
	{
		auto time = (float)glfwGetTime();

		matrices.model = glm::mat4(1.0f);
		matrices.model = glm::scale(matrices.model, { scale, scale, scale });
		matrices.model = glm::rotate(matrices.model, time, { 0.0f, 1.0f, 0.0f });

		uniform_buffer.write(matrices);

		device.clear(glm::vec4{ 0.0f, 0.0f, 0.0f, 1.0f });
		device.setTopology(skygfx::Topology::TriangleList);
		device.setShader(shader);
		device.setVertexBuffer(vertex_buffer);
		device.setIndexBuffer(index_buffer);
		device.setUniformBuffer(1, uniform_buffer);
		device.setCullMode(skygfx::CullMode::Back);
		device.setTexture(0, texture);
		device.drawIndexed(static_cast<uint32_t>(indices.size()));
		device.present();

		glfwPollEvents();
	}

	glfwTerminate();
	return 0;
}