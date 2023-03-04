#include "imgui_helper.h"
#include <skygfx/ext.h>
#include <imgui.h>

ImguiHelper::ImguiHelper()
{
	ImGui::CreateContext();
	ImGui::StyleColorsClassic();

	auto& io = ImGui::GetIO();

	io.IniFilename = NULL;
	io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;

	uint8_t* data;
	int32_t width;
	int32_t height;

	io.Fonts->GetTexDataAsRGBA32(&data, &width, &height);
	mFontTexture = std::make_shared<skygfx::Texture>(width, height, 4, data);
	io.Fonts->TexID = mFontTexture.get();
}

ImguiHelper::~ImguiHelper()
{
	ImGui::DestroyContext();
}

void ImguiHelper::draw()
{
	ImGui::Render();

	skygfx::SetSampler(skygfx::Sampler::Nearest);
	skygfx::SetBlendMode(skygfx::BlendStates::NonPremultiplied);
	skygfx::SetDepthMode(std::nullopt);
	skygfx::SetCullMode(skygfx::CullMode::None);

	auto display_scale = ImGui::GetIO().DisplayFramebufferScale;

	auto camera = skygfx::ext::OrthogonalCamera{};
	auto model = glm::scale(glm::mat4(1.0f), { display_scale.x, display_scale.y, 1.0f });

	auto draw_data = ImGui::GetDrawData();
	draw_data->ScaleClipRects(display_scale);

	for (int i = 0; i < draw_data->CmdListsCount; i++)
	{
		const auto cmdlist = draw_data->CmdLists[i];

		uint32_t index_offset = 0;

		for (const auto& cmd : cmdlist->CmdBuffer)
		{
			if (cmd.UserCallback)
			{
				cmd.UserCallback(cmdlist, &cmd);
			}
			else
			{
				skygfx::SetScissor(skygfx::Scissor{
					.position = { cmd.ClipRect.x, cmd.ClipRect.y },
					.size = { cmd.ClipRect.z - cmd.ClipRect.x, cmd.ClipRect.w - cmd.ClipRect.y }
				});

				auto vertices = skygfx::ext::Mesh::Vertices();
				
				for (uint32_t i = 0; i < cmd.ElemCount; i++)
				{
					auto index = cmdlist->IdxBuffer[i + index_offset];
					const auto& vertex = cmdlist->VtxBuffer[index];

					auto color = (uint8_t*)&vertex.col;

					vertices.push_back(skygfx::ext::Mesh::Vertex{
						.pos = { vertex.pos.x, vertex.pos.y, 0.0f },
						.color = {
							color[0] / 255.0f,
							color[1] / 255.0f,
							color[2] / 255.0f,
							color[3] / 255.0f
						},
						.texcoord = { vertex.uv.x, vertex.uv.y }
					});
				}

				mMesh.setVertices(vertices);
				
				skygfx::ext::Commands cmds;
				skygfx::ext::SetMesh(cmds, &mMesh);
				skygfx::ext::SetCamera(cmds, camera);
				skygfx::ext::SetModelMatrix(cmds, model);
				skygfx::ext::SetColorTexture(cmds, (skygfx::Texture*)cmd.TextureId);
				skygfx::ext::Draw(cmds);
				skygfx::ext::ExecuteCommands(cmds);
			}
			index_offset += cmd.ElemCount;
		}
	}

	skygfx::SetScissor(std::nullopt);
}
