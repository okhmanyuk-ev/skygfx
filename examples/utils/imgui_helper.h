#pragma once

#include <skygfx/skygfx.h>
#include <GLFW/glfw3.h>

class ImguiHelper : skygfx::noncopyable
{
public:
	ImguiHelper();
	~ImguiHelper();

	void draw();

private:
	std::shared_ptr<skygfx::Texture> mFontTexture = nullptr;
};
