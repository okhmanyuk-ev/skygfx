#include <iostream>

#include <GLFW/glfw3.h>
#include <skygfx/skygfx.h>
#include "../utils/utils.h"

static std::string triangle_vertex_shader_code = R"(
#version 450 core

layout(location = POSITION_LOCATION) in vec3 aPosition;
layout(location = COLOR_LOCATION) in vec4 aColor;

layout(location = 0) out struct { vec4 Color; } Out;
out gl_PerVertex { vec4 gl_Position; };

void main()
{
	Out.Color = aColor;
	gl_Position = vec4(aPosition, 1.0);
})";

static std::string triangle_fragment_shader_code = R"(
#version 450 core

layout(location = 0) out vec4 result;
layout(location = 0) in struct { vec4 Color; } In;

void main() 
{ 
	result = In.Color;
})";

using TriangleVertex = skygfx::Vertex::PositionColor;

static std::vector<TriangleVertex> triangle_vertices = {
	{ {  0.75f, -0.75f, 0.0f }, { 0.0f, 0.0f, 1.0f, 1.0f } },
	{ { -0.75f, -0.75f, 0.0f }, { 1.0f, 0.0f, 0.0f, 1.0f } },
	{ {  0.0f,   0.75f, 0.0f }, { 0.0f, 1.0f, 0.0f, 1.0f } },
};

static std::vector<uint32_t> triangle_indices = { 0, 1, 2 };

static std::string cube_vertex_shader_code = R"(
#version 450 core

layout(location = POSITION_LOCATION) in vec3 aPosition;
layout(location = TEXCOORD_LOCATION) in vec2 aTexCoord;
layout(location = NORMAL_LOCATION) in vec3 aNormal;

layout(binding = 1) uniform _ubo
{
	mat4 projection;
	mat4 view;
	mat4 model;
} ubo;

layout(location = 0) out struct { vec3 Position; vec3 Normal; vec2 TexCoord; } Out;
out gl_PerVertex { vec4 gl_Position; };

void main()
{
	Out.Position = vec3(ubo.model * vec4(aPosition, 1.0));
	Out.Normal = vec3(ubo.model * vec4(aNormal, 1.0));
	Out.TexCoord = aTexCoord;
#ifdef FLIP_TEXCOORD_Y
	Out.TexCoord.y = 1.0 - Out.TexCoord.y;
#endif
	gl_Position = ubo.projection * ubo.view * ubo.model * vec4(aPosition, 1.0);
})";

static std::string cube_fragment_shader_code = R"(
#version 450 core

layout(binding = 2) uniform _light
{
	vec3 direction;
	vec3 ambient;
	vec3 diffuse;
	vec3 specular;
	vec3 eye_position;
	float shininess;
} light;

layout(location = 0) out vec4 result;
layout(location = 0) in struct { vec3 Position; vec3 Normal; vec2 TexCoord; } In;
layout(binding = 0) uniform sampler2D sTexture;

void main() 
{ 
	result = texture(sTexture, In.TexCoord);

	vec3 normal = normalize(In.Normal);
	vec3 view_dir = normalize(light.eye_position - In.Position);
	vec3 light_dir = normalize(light.direction);

	float diff = max(dot(normal, -light_dir), 0.0);
	vec3 reflectDir = reflect(light_dir, normal);
	float spec = pow(max(dot(view_dir, reflectDir), 0.0), light.shininess);

	vec3 intensity = light.ambient + (light.diffuse * diff) + (light.specular * spec);

	result *= vec4(intensity, 1.0);
})";

using CubeVertex = skygfx::Vertex::PositionTextureNormal;

const std::vector<CubeVertex> cube_vertices = {
	/* front */
	/* 0  */ { { -1.0f,  1.0f,  1.0f }, { 0.0f, 0.0f }, { 0.0f, 0.0f, 1.0f } },
	/* 1  */ { {  1.0f,  1.0f,  1.0f }, { 1.0f, 0.0f }, { 0.0f, 0.0f, 1.0f } },
	/* 2  */ { { -1.0f, -1.0f,  1.0f }, { 0.0f, 1.0f }, { 0.0f, 0.0f, 1.0f } },
	/* 3  */ { {  1.0f, -1.0f,  1.0f }, { 1.0f, 1.0f }, { 0.0f, 0.0f, 1.0f } },

	/* top */
	/* 4  */ { { -1.0f,  1.0f,  1.0f }, { 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f } },
	/* 5  */ { { -1.0f,  1.0f, -1.0f }, { 0.0f, 1.0f }, { 0.0f, 1.0f, 0.0f } },
	/* 6  */ { {  1.0f,  1.0f,  1.0f }, { 1.0f, 0.0f }, { 0.0f, 1.0f, 0.0f } },
	/* 7  */ { {  1.0f,  1.0f, -1.0f }, { 1.0f, 1.0f }, { 0.0f, 1.0f, 0.0f } },

	/* left */
	/* 8  */ { { -1.0f,  1.0f, -1.0f }, { 0.0f, 0.0f }, { -1.0f, 0.0f, 0.0f } },
	/* 9  */ { { -1.0f,  1.0f,  1.0f }, { 1.0f, 0.0f }, { -1.0f, 0.0f, 0.0f } },
	/* 10 */ { { -1.0f, -1.0f, -1.0f }, { 0.0f, 1.0f }, { -1.0f, 0.0f, 0.0f } },
	/* 11 */ { { -1.0f, -1.0f,  1.0f }, { 1.0f, 1.0f }, { -1.0f, 0.0f, 0.0f } },

	/* back */
	/* 12 */ { { -1.0f,  1.0f, -1.0f }, { 1.0f, 0.0f }, { 0.0f, 0.0f, -1.0f } },
	/* 13 */ { { -1.0f, -1.0f, -1.0f }, { 1.0f, 1.0f }, { 0.0f, 0.0f, -1.0f } },
	/* 14 */ { {  1.0f,  1.0f, -1.0f }, { 0.0f, 0.0f }, { 0.0f, 0.0f, -1.0f } },
	/* 15 */ { {  1.0f, -1.0f, -1.0f }, { 0.0f, 1.0f }, { 0.0f, 0.0f, -1.0f } },

	/* bottom */
	/* 16 */ { { -1.0f, -1.0f,  1.0f }, { 0.0f, 0.0f }, { 0.0f, -1.0f, 0.0f } },
	/* 17 */ { {  1.0f, -1.0f,  1.0f }, { 0.0f, 1.0f }, { 0.0f, -1.0f, 0.0f } },
	/* 18 */ { { -1.0f, -1.0f, -1.0f }, { 1.0f, 0.0f }, { 0.0f, -1.0f, 0.0f } },
	/* 19 */ { {  1.0f, -1.0f, -1.0f }, { 1.0f, 1.0f }, { 0.0f, -1.0f, 0.0f } },

	/* right */
	/* 20 */ { { 1.0f, -1.0f, -1.0f }, { 1.0f, 1.0f }, { 1.0f, 0.0f, 0.0f } },
	/* 21 */ { { 1.0f, -1.0f,  1.0f }, { 0.0f, 1.0f }, { 1.0f, 0.0f, 0.0f } },
	/* 22 */ { { 1.0f,  1.0f, -1.0f }, { 1.0f, 0.0f }, { 1.0f, 0.0f, 0.0f } },
	/* 23 */ { { 1.0f,  1.0f,  1.0f }, { 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f } },
};

static std::vector<uint32_t> cube_indices = {
	0, 1, 2, 1, 3, 2, // front
	4, 5, 6, 5, 7, 6, // top
	8, 9, 10, 9, 11, 10, // left
	12, 13, 14, 13, 15, 14, // back
	16, 17, 18, 17, 19, 18, // bottom
	20, 21, 22, 21, 23, 22, // right
};

static struct alignas(16) UniformBuffer
{
	glm::mat4 projection = glm::mat4(1.0f);
	glm::mat4 view = glm::mat4(1.0f);
	glm::mat4 model = glm::mat4(1.0f);
} cube_ubo;

static struct alignas(16) Light
{
	alignas(16) glm::vec3 direction;
	alignas(16) glm::vec3 ambient;
	alignas(16) glm::vec3 diffuse;
	alignas(16) glm::vec3 specular;
	alignas(16) glm::vec3 eye_position;
	float shininess;
} cube_light;

int main()
{
	auto backend_type = utils::ChooseBackendTypeViaConsole();

	glfwInit();
	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

	uint32_t width = 800;
	uint32_t height = 600;

	auto window = glfwCreateWindow(width, height, "Render Target", NULL, NULL);

	int count = 0;
	auto monitors = glfwGetMonitors(&count);

	auto video_mode = glfwGetVideoMode(monitors[0]);

	auto window_pos_x = (video_mode->width / 2) - (width / 2);
	auto window_pos_y = (video_mode->height / 2) - (height / 2);

	glfwSetWindowPos(window, window_pos_x, window_pos_y);
	glfwMakeContextCurrent(window);

	auto native_window = utils::GetNativeWindow(window);

	auto device = skygfx::Device(backend_type, native_window, width, height);
	auto cube_shader = skygfx::Shader(CubeVertex::Layout, cube_vertex_shader_code, cube_fragment_shader_code);
	auto triangle_shader = skygfx::Shader(TriangleVertex::Layout, triangle_vertex_shader_code, triangle_fragment_shader_code);

	auto window_viewport = skygfx::Viewport();
	window_viewport.size = { static_cast<float>(width), static_cast<float>(height) };

	const auto yaw = 0.0f;
	const auto pitch = glm::radians(-25.0f);

	const auto world_up = glm::vec3{ 0.0f, 1.0f, 0.0f };
	const auto position = glm::vec3{ -500.0f, 200.0f, 0.0f };

	const float fov = 70.0f;
	const float near_plane = 1.0f;
	const float far_plane = 8192.0f;

	const auto scale = 100.0f;

	const float sin_yaw = glm::sin(yaw);
	const float sin_pitch = glm::sin(pitch);

	const float cos_yaw = glm::cos(yaw);
	const float cos_pitch = glm::cos(pitch);

	const auto front = glm::normalize(glm::vec3(cos_yaw * cos_pitch, sin_pitch, sin_yaw * cos_pitch));
	const auto right = glm::normalize(glm::cross(front, world_up));
	const auto up = glm::normalize(glm::cross(right, front));

	cube_ubo.view = glm::lookAtRH(position, position + front, up);
	cube_ubo.projection = glm::perspectiveFov(fov, (float)width, (float)height, near_plane, far_plane);

	cube_light.eye_position = position;
	cube_light.ambient = { 0.25f, 0.25f, 0.25f };
	cube_light.diffuse = { 1.0f, 1.0f, 1.0f };
	cube_light.specular = { 1.0f, 1.0f, 1.0f };
	cube_light.direction = { 1.0f, 0.5f, 0.5f };
	cube_light.shininess = 32.0f;

	auto target = skygfx::RenderTarget(800, 600);

	auto target_viewport = skygfx::Viewport();
	target_viewport.size = { static_cast<float>(target.getWidth()), static_cast<float>(target.getHeight()) };

	while (!glfwWindowShouldClose(window))
	{
		// draw triangle to target

		device.setRenderTarget(target);
		device.clear(glm::vec4{ 0.25f, 0.25f, 0.25f, 1.0f });
		device.setTopology(skygfx::Topology::TriangleList);
		device.setViewport(target_viewport);
		device.setShader(triangle_shader);
		device.setVertexBuffer(triangle_vertices);
		device.setIndexBuffer(triangle_indices);
		device.setCullMode(skygfx::CullMode::None);
		device.drawIndexed(static_cast<uint32_t>(triangle_indices.size()));

		// draw cube textured by our 'target'

		auto time = (float)glfwGetTime();

		cube_ubo.model = glm::mat4(1.0f);
		cube_ubo.model = glm::scale(cube_ubo.model, { scale, scale, scale });
		cube_ubo.model = glm::rotate(cube_ubo.model, time, { 0.0f, 1.0f, 0.0f });

		device.setRenderTarget(nullptr);
		device.clear(glm::vec4{ 0.0f, 0.0f, 0.0f, 1.0f });
		device.setTopology(skygfx::Topology::TriangleList);
		device.setViewport(window_viewport);
		device.setShader(cube_shader);
		device.setVertexBuffer(cube_vertices);
		device.setIndexBuffer(cube_indices);
		device.setUniformBuffer(1, cube_ubo);
		device.setUniformBuffer(2, cube_light);
		device.setCullMode(skygfx::CullMode::Back);
		device.setTexture(target); // render targets can be pushed as textures
		device.drawIndexed(static_cast<uint32_t>(cube_indices.size()));
		device.present();

		glfwPollEvents();
	}

	glfwTerminate();
	return 0;
}