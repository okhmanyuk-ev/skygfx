#pragma once

#include <cstdint>
#include <vector>

namespace skygfx
{
	class Device
	{
	public:
		Device(void* window);
		~Device();

		// TODO: arguments should be
		// std::optional<glm::vec4> color, std::optional<float> depth, std::optional<uint8_t> stencil
		void clear(float r, float g, float b, float a); 

		void present();

	private:
		void createMainRenderTarget(uint32_t width, uint32_t height);
		void destroyMainRenderTarget();
	};
}