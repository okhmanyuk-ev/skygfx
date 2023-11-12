#pragma once

#include <skygfx/skygfx.h>
#include <skygfx/utils.h>
#include <GLFW/glfw3.h>

class ImguiHelper : skygfx::noncopyable
{
public:
	ImguiHelper();
	~ImguiHelper();

	void draw();

private:
	std::shared_ptr<skygfx::Texture> mFontTexture = nullptr;
	skygfx::utils::ScratchRasterizer mScratchRasterizer;
};

class StageDebugger : public skygfx::utils::StageDebugger
{
private:
	struct Stage
	{
		std::string name;
		skygfx::RenderTarget* target;
	};

	std::vector<Stage> mStages;

public:
	void stage(const std::string& name, const skygfx::Texture* texture) override;
	void show();
};