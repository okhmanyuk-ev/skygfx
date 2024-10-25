#include "imgui_helper.h"
#include <skygfx/utils.h>
#include <imgui.h>
#include <span>

ImguiHelper::ImguiHelper()
{
	ImGui::CreateContext();
	ImGui::StyleColorsClassic();
	auto& style = ImGui::GetStyle();
	style.Colors[ImGuiCol_Border] = { 0.0f, 0.0f, 0.0f, 0.0f };

	auto& io = ImGui::GetIO();

	io.IniFilename = NULL;
	io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;

	uint8_t* data;
	int32_t width;
	int32_t height;

	io.Fonts->GetTexDataAsRGBA32(&data, &width, &height);
	mFontTexture = std::make_shared<skygfx::Texture>(width, height, skygfx::PixelFormat::RGBA8UNorm, data);
	io.Fonts->TexID = mFontTexture.get();
}

ImguiHelper::~ImguiHelper()
{
	ImGui::DestroyContext();
}

void ImguiHelper::draw()
{
	ImGui::Render();

	auto display_scale = ImGui::GetIO().DisplayFramebufferScale;

	auto draw_data = ImGui::GetDrawData();
	draw_data->ScaleClipRects(display_scale);

	auto [view, proj] = skygfx::utils::MakeCameraMatrices(skygfx::utils::OrthogonalCamera{});

	skygfx::utils::Scratch::State state;
	state.sampler = skygfx::Sampler::Nearest;
	state.blend_mode = skygfx::BlendStates::NonPremultiplied;
	state.view_matrix = view;
	state.projection_matrix = proj;
	state.model_matrix = glm::scale(glm::mat4(1.0f), { display_scale.x, display_scale.y, 1.0f });

	for (auto cmdlist : std::span{ draw_data->CmdLists, static_cast<std::size_t>(draw_data->CmdListsCount) })
	{
		uint32_t index_offset = 0;

		for (const auto& cmd : cmdlist->CmdBuffer)
		{
			if (cmd.UserCallback)
			{
				cmd.UserCallback(cmdlist, &cmd);
			}
			else
			{
				state.texture = (skygfx::Texture*)cmd.TextureId;
				state.scissor = skygfx::Scissor{
					.position = { cmd.ClipRect.x, cmd.ClipRect.y },
					.size = { cmd.ClipRect.z - cmd.ClipRect.x, cmd.ClipRect.w - cmd.ClipRect.y }
				};

				mScratch.begin(skygfx::utils::MeshBuilder::Mode::Triangles, state);

				for (uint32_t i = 0; i < cmd.ElemCount; i++)
				{
					auto index = cmdlist->IdxBuffer[i + index_offset];
					const auto& vertex = cmdlist->VtxBuffer[index];
					mScratch.vertex({
						.pos = { vertex.pos.x, vertex.pos.y, 0.0f },
						.color = glm::unpackUnorm4x8(vertex.col),
						.texcoord = { vertex.uv.x, vertex.uv.y }
					});
				}

				mScratch.end();
			}

			index_offset += cmd.ElemCount;
		}
	}

	mScratch.flush();
}

void StageViewer::stage(const std::string& name, skygfx::Texture* texture)
{
	if (texture == nullptr)
		return;

	Stage stage;
	stage.name = name;
	stage.target = skygfx::AcquireTransientRenderTarget(texture->getWidth(), texture->getHeight());
	skygfx::utils::passes::Blit(texture, stage.target, {
		.clear = true
	});
	mStages.push_back(stage);
}

void StageViewer::show()
{
	ImGui::Begin("Stage Viewer");
	auto max_size = ImGui::GetContentRegionAvail().x;
	for (const auto& stage : mStages)
	{
		glm::vec2 size = { (float)stage.target->getWidth(), (float)stage.target->getHeight() };
		size *= max_size / std::fmaxf(size.x, size.y);
		ImGui::Text("%s", stage.name.c_str());
		ImGui::Image((ImTextureID)stage.target, { size.x, size.y });
	}
	ImGui::End();
	mStages.clear();
}