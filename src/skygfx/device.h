#pragma once

#include <cstdint>
#include <vector>

namespace skygfx
{
	class Device
	{
	public:
		virtual ~Device() {}

		// TODO: arguments should be
		// std::optional<glm::vec4> color, std::optional<float> depth, std::optional<uint8_t> stencil
		virtual void clear(float r, float g, float b, float a) = 0;

		virtual void present() = 0;
	};
}