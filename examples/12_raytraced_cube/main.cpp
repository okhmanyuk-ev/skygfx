#include <skygfx/skygfx.h>
#include <skygfx/utils.h>
#define STB_IMAGE_IMPLEMENTATION
#include "../utils/utils.h"

const std::string raygen_shader_code = R"(
#version 460

#extension GL_EXT_ray_tracing : require

layout(binding = 2) uniform _settings
{
	mat4 viewInverse;
	mat4 projInverse;
} settings;

layout(binding = 0) uniform accelerationStructureEXT topLevelAS;
layout(binding = 1, rgba8) uniform image2D image;

layout(location = 0) rayPayloadEXT vec3 hitValue;

void main()
{
	vec2 pixelCenter = vec2(gl_LaunchIDEXT.xy) + vec2(0.5);
	vec2 uv = pixelCenter / vec2(gl_LaunchSizeEXT.xy);
	vec2 d = uv * 2.0 - 1.0;
	
	vec4 origin = settings.viewInverse * vec4(0, 0, 0, 1);
	vec4 target = settings.projInverse * vec4(d.x, d.y, 1, 1);
	vec4 direction = settings.viewInverse * vec4(normalize(target.xyz), 0);

	uint rayFlags = gl_RayFlagsNoneEXT;
	uint cullMask = 0xFF;
	uint sbtRecordOffset = 0;
	uint sbtRecordStride = 0;
	uint missIndex = 0;
	float tmin = 0.001f;
	float tmax = 8192.0f;
	const int payloadLocation = 0;

    hitValue = vec3(0.0);

	traceRayEXT(topLevelAS, rayFlags, cullMask, sbtRecordOffset, sbtRecordStride, missIndex,
		origin.xyz, tmin, direction.xyz, tmax, payloadLocation);

	imageStore(image, ivec2(gl_LaunchSizeEXT.xy) - ivec2(gl_LaunchIDEXT.xy), vec4(hitValue, 0.0));
})";

const std::string miss_shader_code = R"(
#version 460
#extension GL_EXT_ray_tracing : enable

layout(location = 0) rayPayloadInEXT vec3 hitValue;

void main()
{
	hitValue = vec3(0.0, 0.0, 0.0);
})";

const std::string closesthit_shader_code = R"(
#version 460
#extension GL_EXT_ray_tracing : enable

layout(location = 0) rayPayloadInEXT vec3 hitValue;
hitAttributeEXT vec3 attribs;

void main()
{
	//const vec3 barycentricCoords = vec3(1.0f - attribs.x - attribs.y, attribs.x, attribs.y);
	//hitValue = barycentricCoords;

	hitValue = vec3(1.0, 1.0, 1.0);
})";

const std::vector<glm::vec3> vertices = {
	/* front */
	/* 0  */ { -1.0f,  1.0f,  1.0f },
	/* 1  */ {  1.0f,  1.0f,  1.0f },
	/* 2  */ { -1.0f, -1.0f,  1.0f },
	/* 3  */ {  1.0f, -1.0f,  1.0f },

	/* top */
	/* 4  */ { -1.0f,  1.0f,  1.0f },
	/* 5  */ { -1.0f,  1.0f, -1.0f },
	/* 6  */ {  1.0f,  1.0f,  1.0f },
	/* 7  */ {  1.0f,  1.0f, -1.0f },

	/* left */
	/* 8  */ { -1.0f,  1.0f, -1.0f },
	/* 9  */ { -1.0f,  1.0f,  1.0f },
	/* 10 */ { -1.0f, -1.0f, -1.0f },
	/* 11 */ { -1.0f, -1.0f,  1.0f },

	/* back */
	/* 12 */ { -1.0f,  1.0f, -1.0f },
	/* 13 */ { -1.0f, -1.0f, -1.0f },
	/* 14 */ {  1.0f,  1.0f, -1.0f },
	/* 15 */ {  1.0f, -1.0f, -1.0f },

	/* bottom */
	/* 16 */ { -1.0f, -1.0f,  1.0f },
	/* 17 */ {  1.0f, -1.0f,  1.0f },
	/* 18 */ { -1.0f, -1.0f, -1.0f },
	/* 19 */ {  1.0f, -1.0f, -1.0f },

	/* right */
	/* 20 */ { 1.0f, -1.0f, -1.0f },
	/* 21 */ { 1.0f, -1.0f,  1.0f },
	/* 22 */ { 1.0f,  1.0f, -1.0f },
	/* 23 */ { 1.0f,  1.0f,  1.0f },
};

const std::vector<uint32_t> indices = {
	0, 1, 2, 1, 3, 2, // front
	4, 5, 6, 5, 7, 6, // top
	8, 9, 10, 9, 11, 10, // left
	12, 13, 14, 13, 15, 14, // back
	16, 17, 18, 17, 19, 18, // bottom
	20, 21, 22, 21, 23, 22, // right
};

struct alignas(16) Settings
{
	glm::mat4 viewInverse;
	glm::mat4 projInverse;
};

int main()
{
	auto features = { skygfx::Feature::Raytracing };
	auto backend_type = utils::ChooseBackendTypeViaConsole(features);

	glfwInit();
	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

	auto [window, native_window, width, height] = utils::SpawnWindow(800, 600, "Raytraced cube");

	skygfx::Initialize(native_window, width, height, backend_type, skygfx::Adapter::HighPerformance, features);

	glfwSetFramebufferSizeCallback(window, [](GLFWwindow* window, int width, int height) {
		skygfx::Resize(static_cast<uint32_t>(width), static_cast<uint32_t>(height));
	});

	auto shader = skygfx::RaytracingShader(raygen_shader_code, miss_shader_code, closesthit_shader_code);
	
	const auto yaw = 0.0f;
	const auto pitch = glm::radians(-25.0f);
	const auto position = glm::vec3{ -5.0f, 2.0f, 0.0f };

	while (!glfwWindowShouldClose(window))
	{
		auto [view, projection] = utils::CalculatePerspectiveViewProjection(yaw, pitch, position);

		auto time = (float)glfwGetTime();

		auto model = glm::mat4(1.0f);
		model = glm::rotate(model, time, { 0.0f, 1.0f, 0.0f });

		auto acceleration_structure = skygfx::AccelerationStructure(vertices, indices, model);
	
		skygfx::SetUniformBuffer(2, Settings{
			.viewInverse = glm::inverse(view),
			.projInverse = glm::inverse(projection)
		});

		auto target = skygfx::GetTemporaryRenderTarget();

		skygfx::SetShader(shader);
		skygfx::SetRenderTarget(*target);
		skygfx::SetAccelerationStructure(0, acceleration_structure);
		skygfx::DispatchRays(target->getWidth(), target->getHeight(), 1);

		skygfx::utils::passes::Blit(target, nullptr, true);

		skygfx::Present();

		glfwPollEvents();
	}

	skygfx::Finalize();

	glfwTerminate();

	return 0;
}
