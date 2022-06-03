#pragma once

#include <cstdint>

namespace skygfx
{
	enum class BackendType
	{
		D3D11,
		OpenGL44
	};

	using TextureHandle = struct TextureHandle;

	class Texture
	{
	public:
		Texture();
		~Texture();

		operator TextureHandle* () { return mTextureHandle; }

	private:
		TextureHandle* mTextureHandle = nullptr;
	};

	class Device
	{
	public:
		Device(BackendType type, void* window);
		~Device();

		void setTexture(Texture& texture);

		// TODO: arguments should be
		// std::optional<glm::vec4> color, std::optional<float> depth, std::optional<uint8_t> stencil
		void clear(float r, float g, float b, float a);
		void present();
	};
}
