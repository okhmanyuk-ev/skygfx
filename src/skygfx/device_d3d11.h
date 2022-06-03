#pragma once

#include "device.h"

namespace skygfx
{
	class DeviceD3D11 : public Device
	{
	public:
		DeviceD3D11(void* window);
		~DeviceD3D11();

		void clear(float r, float g, float b, float a) override;
		void present() override;

	private:
		void createMainRenderTarget(uint32_t width, uint32_t height);
		void destroyMainRenderTarget();
	};
}