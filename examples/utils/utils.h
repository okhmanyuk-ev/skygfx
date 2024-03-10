#pragma once

#include <skygfx/skygfx.h>
#include <skygfx/utils.h>

#include <GLFW/glfw3.h>

#if defined(WIN32)
	#define GLFW_EXPOSE_NATIVE_WIN32
#elif defined(__APPLE__)
	#define GLFW_EXPOSE_NATIVE_COCOA
#endif
#include <GLFW/glfw3native.h>

#include <iostream>

#define HAS_STB_IMAGE __has_include(<stb_image.h>)

#if HAS_STB_IMAGE
	#include <stb_image.h>	
#endif

namespace utils
{
	const std::string& GetBackendName(skygfx::BackendType backend)
	{
		static const std::unordered_map<skygfx::BackendType, std::string> BackendNamesMap = {
			{ skygfx::BackendType::D3D11, "D3D11" },
			{ skygfx::BackendType::D3D12, "D3D12" },
			{ skygfx::BackendType::OpenGL, "OpenGL" },
			{ skygfx::BackendType::Vulkan, "Vulkan" },
			{ skygfx::BackendType::Metal, "Metal" },
			{ skygfx::BackendType::WebGPU, "WebGPU" },
		};

		return BackendNamesMap.at(backend);
	}

	skygfx::BackendType ChooseBackendTypeViaConsole(std::unordered_set<skygfx::Feature> features = {})
	{
		std::cout << "Choose backend type: " << std::endl;

		auto available_backends = skygfx::GetAvailableBackends(features);
		auto backends = std::vector(available_backends.begin(), available_backends.end());

		for (int i = 0; i < backends.size(); i++)
		{
			std::cout << i + 1 << ". " << GetBackendName(backends.at(i)) << std::endl;
		}
		
		int value = 1;
		
		if (backends.size() > 1)
		{
			std::cin >> value;
		}

		auto backend = backends.at(value - 1);

		std::cout << "Backend is " << GetBackendName(backend) << std::endl;

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
		auto [proj, view] = skygfx::utils::MakeCameraMatrices(skygfx::utils::PerspectiveCamera{
			.width = width,
			.height = height,
			.yaw = yaw,
			.pitch = pitch,
			.position = position,
			.world_up = world_up,
			.far_plane = far_plane,
			.near_plane = near_plane,
			.fov = fov
		});
		return { view, proj };
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

#if HAS_STB_IMAGE
	std::tuple<uint32_t, uint32_t, void*> LoadTexture(const std::string& filename)
	{
		int width = 0;
		int height = 0;
		void* memory = stbi_load(filename.c_str(), &width, &height, nullptr, 4);
		return { width, height, memory };
	}
#endif
}
