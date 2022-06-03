#include <iostream>

#include <GLFW/glfw3.h>
#include <skygfx/skygfx.h>

#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>

int main()
{
	if (!glfwInit())
		return -1;

	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

	uint32_t width = 800;
	uint32_t height = 600;

	auto window = glfwCreateWindow(width, height, "Hello World", NULL, NULL);

	int count = 0;
	auto monitors = glfwGetMonitors(&count);

	auto video_mode = glfwGetVideoMode(monitors[0]);

	auto window_pos_x = (video_mode->width / 2) - (width / 2);
	auto window_pos_y = (video_mode->height / 2) - (height / 2);

	glfwSetWindowPos(window, window_pos_x, window_pos_y);
	glfwMakeContextCurrent(window);

	auto win32_window = glfwGetWin32Window(window);

	auto device = skygfx::Device(skygfx::BackendType::D3D11, win32_window, width, height);

	skygfx::Texture texture;

	device.setTexture(texture);

	while (!glfwWindowShouldClose(window))
	{
		device.clear(0.0f, 1.0f, 0.0f, 1.0f);
		device.present();

		glfwPollEvents();
	}

	glfwTerminate();
	return 0;
}