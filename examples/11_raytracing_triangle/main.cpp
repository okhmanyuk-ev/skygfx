#include <skygfx/skygfx.h>
#include <skygfx/ext.h>
#include "../utils/utils.h"

const std::string raygen_shader_code = R"(
#version 460

#extension GL_EXT_ray_tracing : require

layout(set = 0, binding = 0) uniform accelerationStructureEXT topLevelAS;
layout(set = 0, binding = 1, rgba8) uniform image2D image;

layout(location = 0) rayPayloadEXT vec3 hitValue;

void main()
{
	const vec2 uv = vec2(gl_LaunchIDEXT.xy) / vec2(gl_LaunchSizeEXT.xy - 1);

	const vec3 origin = vec3(uv.x, 1.0f - uv.y, -1.0f);
	const vec3 direction = vec3(0.0f, 0.0f, 1.0f);

	const uint rayFlags = gl_RayFlagsNoneEXT;
	const uint cullMask = 0xFF;
	const uint sbtRecordOffset = 0;
	const uint sbtRecordStride = 0;
	const uint missIndex = 0;
	const float tmin = 0.0f;
	const float tmax = 10.0f;
	const int payloadLocation = 0;

    hitValue = vec3(0.0);

	traceRayEXT(topLevelAS, rayFlags, cullMask, sbtRecordOffset, sbtRecordStride, missIndex,
		origin, tmin, direction, tmax, payloadLocation);

	imageStore(image, ivec2(gl_LaunchIDEXT.xy), vec4(hitValue, 0.0));
})";

const std::string miss_shader_code = R"(
#version 460
#extension GL_EXT_ray_tracing : enable

layout(location = 0) rayPayloadInEXT vec3 hitValue;

void main()
{
	hitValue = vec3(0.0, 0.0, 0.2);
})";

const std::string closesthit_shader_code = R"(
#version 460
#extension GL_EXT_ray_tracing : enable

layout(location = 0) rayPayloadInEXT vec3 hitValue;
hitAttributeEXT vec3 attribs;

void main()
{
	const vec3 barycentricCoords = vec3(1.0f - attribs.x - attribs.y, attribs.x, attribs.y);
	hitValue = barycentricCoords;
})";

const std::vector<glm::vec3> vertices = {
	{ 0.25f, 0.25f, 0.0f },
	{ 0.75f, 0.25f, 0.0f },
	{ 0.50f, 0.75f, 0.0f },
};

const std::vector<uint32_t> indices = { 0, 1, 2 };

int main()
{
	auto backend_type = skygfx::BackendType::Vulkan;// utils::ChooseBackendTypeViaConsole();

	glfwInit();
	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

	auto [window, native_window, width, height] = utils::SpawnWindow(800, 600, "Raytracing Triangle");

	skygfx::Initialize(native_window, width, height, backend_type);

	glfwSetFramebufferSizeCallback(window, [](GLFWwindow* window, int width, int height) {
		skygfx::Resize(static_cast<uint32_t>(width), static_cast<uint32_t>(height));
	});

	auto shader = skygfx::RaytracingShader(raygen_shader_code, miss_shader_code, closesthit_shader_code);
	auto acceleration_structure = skygfx::AccelerationStructure(vertices, indices);
	auto target = skygfx::RenderTarget(800, 600);

	while (!glfwWindowShouldClose(window))
	{
		skygfx::SetShader(shader);
		skygfx::SetRenderTarget(target);
		skygfx::SetAccelerationStructure(0, acceleration_structure);
		skygfx::Clear(glm::vec4{ 1.0f, 0.0f, 0.0f, 1.0f });
		skygfx::DispatchRays(target.getWidth(), target.getHeight(), 1);

		skygfx::SetRenderTarget(std::nullopt);
		skygfx::Clear(glm::vec4{ 0.0f, 0.0f, 0.0f, 1.0f });
		skygfx::ext::ExecuteCommands({
			skygfx::ext::commands::SetColorTexture{ &target },
			skygfx::ext::commands::Draw()
		});

		skygfx::Present();

		glfwPollEvents();
	}

	skygfx::Finalize();

	glfwTerminate();

	return 0;
}
