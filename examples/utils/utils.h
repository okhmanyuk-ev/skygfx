#pragma once

#include <skygfx/skygfx.h>

#include <GLFW/glfw3.h>

#if defined(WIN32)
	#define GLFW_EXPOSE_NATIVE_WIN32
#elif defined(__APPLE__)
	#define GLFW_EXPOSE_NATIVE_COCOA
#endif
#include <GLFW/glfw3native.h>

#include <iostream>

namespace utils
{
	skygfx::BackendType ChooseBackendTypeViaConsole(std::unordered_set<skygfx::Feature> features = {})
	{
		static const std::map<skygfx::BackendType, std::string> backend_names = {
			{ skygfx::BackendType::D3D11, "D3D11" },
			{ skygfx::BackendType::D3D12, "D3D12" },
			{ skygfx::BackendType::OpenGL, "OpenGL" },
			{ skygfx::BackendType::Vulkan, "Vulkan" },
			{ skygfx::BackendType::Metal, "Metal" },
		};

		std::cout << "Choose backend type: " << std::endl;

		auto available_backends = skygfx::GetAvailableBackends(features);
		auto backends = std::vector(available_backends.begin(), available_backends.end());

		for (int i = 0; i < backends.size(); i++)
		{
			std::cout << i + 1 << ". " << backend_names.at(backends.at(i)) << std::endl;
		}
		
		int value = 1;
		
		if (backends.size() > 1)
		{
			std::cin >> value;
		}

		auto backend = backends.at(value - 1);

		std::cout << "Backend is " << backend_names.at(backend) << std::endl;

		return backend;
	}

	void* GetNativeWindow(GLFWwindow* window)
	{
#if defined(GLFW_EXPOSE_NATIVE_WIN32)
		return glfwGetWin32Window(window);
#elif defined(GLFW_EXPOSE_NATIVE_COCOA)
		return glfwGetCocoaWindow(window);
#endif
	}

	std::tuple<glm::mat4/*view*/, glm::mat4/*projection*/> CalculatePerspectiveViewProjection(float yaw, 
		float pitch, const glm::vec3& position, uint32_t width = skygfx::GetBackbufferWidth(),
		uint32_t height = skygfx::GetBackbufferHeight(), float fov = 70.0f, 
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
	
	std::tuple<GLFWwindow*, void*, uint32_t, uint32_t> SpawnWindow(uint32_t width, uint32_t height, const std::string& title)
	{
		auto window = glfwCreateWindow((int)width, (int)height, title.c_str(), NULL, NULL);

		auto monitor = glfwGetPrimaryMonitor();
		auto video_mode = glfwGetVideoMode(monitor);

		auto window_pos_x = (video_mode->width / 2) - (width / 2);
		auto window_pos_y = (video_mode->height / 2) - (height / 2);

		glfwSetWindowPos(window, window_pos_x, window_pos_y);
		
		int fb_width;
		int fb_height;
		glfwGetFramebufferSize(window, &fb_width, &fb_height);

		auto native_window = GetNativeWindow(window);

		return { window, native_window, fb_width, fb_height };
	}
}
