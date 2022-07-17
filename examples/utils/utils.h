#pragma once

#include <skygfx/skygfx.h>

#include <GLFW/glfw3.h>

#if defined(WIN32)
	#define GLFW_EXPOSE_NATIVE_WIN32
#elif defined(__APPLE__)
	#define GLFW_EXPOSE_NATIVE_COCOA
#endif
#include <GLFW/glfw3native.h>

namespace utils
{
	skygfx::BackendType ChooseBackendTypeViaConsole()
	{
		std::cout << "Choose backend type: " << std::endl;
		std::cout << "1. D3D11" << std::endl;
		std::cout << "2. OpenGL 4.4" << std::endl;
		std::cout << "3. Vulkan" << std::endl;
		std::cout << "4. Metal" << std::endl;

		int value = 0;
		std::cin >> value;

		if (value == 1)
			return skygfx::BackendType::D3D11;
		else if (value == 2)
			return skygfx::BackendType::OpenGL44;
		else if (value == 3)
			return skygfx::BackendType::Vulkan;
		else if (value == 4)
			return skygfx::BackendType::Metal;
		else
			throw std::runtime_error("unknown type");
	}

	void* GetNativeWindow(GLFWwindow* window)
	{
#if defined(GLFW_EXPOSE_NATIVE_WIN32)
		return glfwGetWin32Window(window);
#elif defined(GLFW_EXPOSE_NATIVE_COCOA)
		return glfwGetCocoaWindow(window);
#endif
		return nullptr;
	}

	std::tuple<glm::mat4/*view*/, glm::mat4/*projection*/> CalculatePerspectiveViewProjection(float yaw, 
		float pitch, const glm::vec3& position, uint32_t width, uint32_t height, float fov = 70.0f, 
		float near_plane = 1.0f, float far_plane = 8192.0f, const glm::vec3& world_up = { 0.0f, 1.0f, 0.0f })
	{
		auto sin_yaw = glm::sin(yaw);
		auto sin_pitch = glm::sin(pitch);

		auto cos_yaw = glm::cos(yaw);
		auto cos_pitch = glm::cos(pitch);

		auto front = glm::normalize(glm::vec3(cos_yaw * cos_pitch, sin_pitch, sin_yaw * cos_pitch));
		auto right = glm::normalize(glm::cross(front, world_up));
		auto up = glm::normalize(glm::cross(right, front));

		auto view = glm::lookAtRH(position, position + front, up);
		auto projection = glm::perspectiveFov(fov, (float)width, (float)height, near_plane, far_plane);

		return { view, projection };
	}
}
