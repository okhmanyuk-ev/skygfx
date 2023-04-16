#include <skygfx/skygfx.h>
#include <skygfx/vertex.h>
#include "../utils/utils.h"

const std::string triangle_vertex_shader_code = R"(
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

const std::string triangle_fragment_shader_code = R"(
#version 450 core

layout(location = 0) out vec4 result;
layout(location = 0) in struct { vec4 Color; } In;

void main() 
{ 
	result = In.Color;
})";

using TriangleVertex = skygfx::Vertex::PositionColor;

const std::vector<TriangleVertex> triangle_vertices = {
	{ {  0.75f, -0.75f, 0.0f }, { 0.0f, 0.0f, 1.0f, 1.0f } },
	{ { -0.75f, -0.75f, 0.0f }, { 1.0f, 0.0f, 0.0f, 1.0f } },
	{ {  0.0f,   0.75f, 0.0f }, { 0.0f, 1.0f, 0.0f, 1.0f } },
};

const std::vector<uint32_t> triangle_indices = { 0, 1, 2 };

const std::string cube_vertex_shader_code = R"(
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
	Out.Normal = mat3(transpose(inverse(ubo.model))) * aNormal;
	Out.TexCoord = aTexCoord;
#ifdef FLIP_TEXCOORD_Y
	Out.TexCoord.y = 1.0 - Out.TexCoord.y;
#endif
	gl_Position = ubo.projection * ubo.view * ubo.model * vec4(aPosition, 1.0);
})";

const std::string cube_fragment_shader_code = R"(
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

const std::vector<uint32_t> cube_indices = {
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

struct alignas(16) Light
{
	alignas(16) glm::vec3 direction;
	alignas(16) glm::vec3 ambient;
	alignas(16) glm::vec3 diffuse;
	alignas(16) glm::vec3 specular;
	alignas(16) glm::vec3 eye_position;
	float shininess;
} light;

int main()
{
	auto backend_type = utils::ChooseBackendTypeViaConsole();

	glfwInit();
	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

	auto [window, native_window, width, height] = utils::SpawnWindow(800, 600, "Render Target");

	skygfx::Initialize(native_window, width, height, backend_type);

	glfwSetFramebufferSizeCallback(window, [](GLFWwindow* window, int width, int height) {
		skygfx::Resize(static_cast<uint32_t>(width), static_cast<uint32_t>(height));
	});
	
	auto cube_shader = skygfx::Shader(CubeVertex::Layout, cube_vertex_shader_code, cube_fragment_shader_code);
	auto triangle_shader = skygfx::Shader(TriangleVertex::Layout, triangle_vertex_shader_code, triangle_fragment_shader_code);

	const auto yaw = 0.0f;
	const auto pitch = glm::radians(-25.0f);
	const auto position = glm::vec3{ -5.0f, 2.0f, 0.0f };

	light.eye_position = position;
	light.ambient = { 0.25f, 0.25f, 0.25f };
	light.diffuse = { 1.0f, 1.0f, 1.0f };
	light.specular = { 1.0f, 1.0f, 1.0f };
	light.direction = { 1.0f, 0.5f, 0.5f };
	light.shininess = 32.0f;

	auto target = skygfx::RenderTarget(800, 600);

	// draw triangle to target

	skygfx::SetRenderTarget(target);
	skygfx::SetTopology(skygfx::Topology::TriangleList);
	skygfx::SetCullMode(skygfx::CullMode::None);
	skygfx::SetVertexBuffer(triangle_vertices);
	skygfx::SetIndexBuffer(triangle_indices);
	skygfx::SetShader(triangle_shader);
	skygfx::Clear(glm::vec4{ 0.25f, 0.25f, 0.25f, 1.0f });
	skygfx::DrawIndexed(static_cast<uint32_t>(triangle_indices.size()));

	// prepare steps for drawing target to cube and later to screen

	skygfx::SetRenderTarget(std::nullopt);
	skygfx::SetTopology(skygfx::Topology::TriangleList);
	skygfx::SetCullMode(skygfx::CullMode::Back);
	skygfx::SetVertexBuffer(cube_vertices);
	skygfx::SetIndexBuffer(cube_indices);
	skygfx::SetUniformBuffer(2, light);
	skygfx::SetTexture(0, target); // render targets can be pushed as textures
	skygfx::SetShader(cube_shader);

	while (!glfwWindowShouldClose(window))
	{
		std::tie(matrices.view, matrices.projection) = utils::CalculatePerspectiveViewProjection(yaw, pitch, position);

		auto time = (float)glfwGetTime();

		matrices.model = glm::mat4(1.0f);
		matrices.model = glm::rotate(matrices.model, time, { 0.0f, 1.0f, 0.0f });

		skygfx::SetUniformBuffer(1, matrices);

		skygfx::Clear(glm::vec4{ 0.0f, 0.0f, 0.0f, 1.0f });
		skygfx::DrawIndexed(static_cast<uint32_t>(cube_indices.size()));
		skygfx::Present();

		glfwPollEvents();
	}

	skygfx::Finalize();

	glfwTerminate();

	return 0;
}
