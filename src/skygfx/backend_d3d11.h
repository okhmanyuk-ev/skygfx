#pragma once

#include "backend.h"

namespace skygfx
{
	class BackendD3D11 : public Backend
	{
	public:
		BackendD3D11(void* window);
		~BackendD3D11();

		void setTexture(TextureHandle* texture) override {};

		void clear(float r, float g, float b, float a) override;
		void present() override;

		TextureHandle* createTexture() override;
		void destroyTexture(TextureHandle* handle) override;

	private:
		void createMainRenderTarget(uint32_t width, uint32_t height);
		void destroyMainRenderTarget();
	};
}