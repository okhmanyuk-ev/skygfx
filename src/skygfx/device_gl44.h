#pragma once

#include "device.h"

namespace skygfx
{
	class DeviceGL44 : public Device
	{
	public:
		DeviceGL44(void* window);
		~DeviceGL44();

		void clear(float r, float g, float b, float a) override;
		void present() override;
	};
}