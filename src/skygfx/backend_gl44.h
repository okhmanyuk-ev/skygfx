#pragma once

#include "backend.h"

namespace skygfx
{
	class BackendGL44 : public Backend
	{
	public:
		BackendGL44(void* window);
		~BackendGL44();

		void setTexture(TextureHandle* texture) override {};

		void clear(float r, float g, float b, float a) override;
		void present() override;

		TextureHandle* createTexture() override;
		void destroyTexture(TextureHandle* handle) override;
	};
}