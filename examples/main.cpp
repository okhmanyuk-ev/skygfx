#include <iostream>

#include <GLFW/glfw3.h>
#include <skygfx/device.h>

#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>

int main()
{
	if (!glfwInit())
		return -1;

	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

	int width = 800;
	int height = 600;

	auto window = glfwCreateWindow(width, height, "Hello World", NULL, NULL);

	int count = 0;
	auto monitors = glfwGetMonitors(&count);

	auto video_mode = glfwGetVideoMode(monitors[0]);

	auto window_pos_x = (video_mode->width / 2) - (width / 2);
	auto window_pos_y = (video_mode->height / 2) - (height / 2);

	glfwSetWindowPos(window, window_pos_x, window_pos_y);
	glfwMakeContextCurrent(window);

	auto win32_window = glfwGetWin32Window(window);

	auto device = skygfx::Device(win32_window);

	while (!glfwWindowShouldClose(window))
	{
		device.clear(0.0f, 1.0f, 0.0f, 1.0f);
		device.present();

		glfwSwapBuffers(window);
		glfwPollEvents();
	}

	glfwTerminate();
	return 0;
}