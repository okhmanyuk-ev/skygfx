#pragma once

#include "skygfx.h"

namespace skygfx
{
	class Backend
	{
	public:
		virtual ~Backend() {}

		virtual void setTexture(TextureHandle* texture) = 0;

		virtual void clear(float r, float g, float b, float a) = 0;

		virtual void present() = 0;

		virtual TextureHandle* createTexture() = 0;
		virtual void destroyTexture(TextureHandle* handle) = 0;
	};
}