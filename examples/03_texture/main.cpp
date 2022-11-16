#include <skygfx/skygfx.h>
#include "../utils/utils.h"

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>	

const std::string vertex_shader_code = R"(
#version 450 core

layout(location = POSITION_LOCATION) in vec3 aPosition;
layout(location = COLOR_LOCATION) in vec4 aColor;
layout(location = TEXCOORD_LOCATION) in vec2 aTexCoord;

layout(location = 0) out struct { vec4 Color; vec2 TexCoord; } Out;
out gl_PerVertex { vec4 gl_Position; };

void main()
{
	Out.Color = aColor;
	Out.TexCoord = aTexCoord;
#ifdef FLIP_TEXCOORD_Y
	Out.TexCoord.y = 1.0 - Out.TexCoord.y;
#endif
	gl_Position = vec4(aPosition, 1.0);
})";

const std::string fragment_shader_code = R"(
#version 450 core

layout(location = 0) out vec4 result;
layout(location = 0) in struct { vec4 Color; vec2 TexCoord; } In;

layout(binding = 0) uniform sampler2D sTexture;

void main() 
{ 
	result = In.Color * texture(sTexture, In.TexCoord);
})";

using Vertex = skygfx::Vertex::PositionColorTexture;

const std::vector<Vertex> vertices = {
	{ {  0.5f, -0.5f, 0.0f }, { 1.0f, 1.0f, 1.0f, 1.0f }, { 1.0f, 1.0f } }, // bottom right
	{ { -0.5f, -0.5f, 0.0f }, { 1.0f, 1.0f, 1.0f, 1.0f }, { 0.0f, 1.0f } }, // bottom left
	{ {  0.5f,  0.5f, 0.0f }, { 1.0f, 1.0f, 1.0f, 1.0f }, { 1.0f, 0.0f } }, // top right
	{ { -0.5f,  0.5f, 0.0f }, { 1.0f, 1.0f, 1.0f, 1.0f }, { 0.0f, 0.0f } }, // top left
};

const std::vector<uint32_t> indices = { 0, 1, 2, 2, 1, 3 };

int main()
{
	auto backend_type = utils::ChooseBackendTypeViaConsole();

	glfwInit();
	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

	uint32_t width = 800;
	uint32_t height = 600;

	auto window = glfwCreateWindow(width, height, "Texture", NULL, NULL);

	int count = 0;
	auto monitors = glfwGetMonitors(&count);

	auto video_mode = glfwGetVideoMode(monitors[0]);

	auto window_pos_x = (video_mode->width / 2) - (width / 2);
	auto window_pos_y = (video_mode->height / 2) - (height / 2);

	glfwSetWindowPos(window, window_pos_x, window_pos_y);

	auto native_window = utils::GetNativeWindow(window);

	skygfx::Initialize(native_window, width, height, backend_type);

	glfwSetWindowSizeCallback(window, [](GLFWwindow* window, int width, int height) {
		skygfx::Resize(static_cast<uint32_t>(width), static_cast<uint32_t>(height));
	});

	auto shader = skygfx::Shader(Vertex::Layout, vertex_shader_code, fragment_shader_code);

	int tex_width = 0;
	int tex_height = 0;
	void* tex_memory = stbi_load("assets/bricks.png", &tex_width, &tex_height, nullptr, 4); // TODO: this image has 3 channels, we must can load that type of images

	auto texture = skygfx::Texture(tex_width, tex_height, 4/*TODO: no magic numbers should be*/, tex_memory);

	skygfx::SetTopology(skygfx::Topology::TriangleList);
	skygfx::SetShader(shader);
	skygfx::SetTexture(0, texture);
	skygfx::SetDynamicVertexBuffer(vertices);
	skygfx::SetDynamicIndexBuffer(indices);

	while (!glfwWindowShouldClose(window))
	{
		skygfx::Clear(glm::vec4{ 0.0f, 0.0f, 0.0f, 1.0f });
		skygfx::DrawIndexed(static_cast<uint32_t>(indices.size()));
		skygfx::Present();

		glfwPollEvents();
	}

	skygfx::Finalize();

	glfwTerminate();
	
	return 0;
}
