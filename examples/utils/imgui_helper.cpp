#include "imgui_helper.h"
#include <skygfx/utils.h>
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

	skygfx::SetDepthMode(std::nullopt);
	skygfx::SetCullMode(skygfx::CullMode::None);

	auto display_scale = ImGui::GetIO().DisplayFramebufferScale;
	auto model = glm::scale(glm::mat4(1.0f), { display_scale.x, display_scale.y, 1.0f });

	auto draw_data = ImGui::GetDrawData();
	draw_data->ScaleClipRects(display_scale);

	skygfx::utils::Mesh::Vertices vertices;
	skygfx::utils::Mesh::Indices indices;

	for (int i = 0; i < draw_data->CmdListsCount; i++)
	{
		const auto& cmdlist = *draw_data->CmdLists[i];
		auto base_vertex = vertices.size();

		for (const auto& vertex : cmdlist.VtxBuffer)
		{
			vertices.push_back(skygfx::utils::Mesh::Vertex{
				.pos = { vertex.pos.x, vertex.pos.y, 0.0f },
				.color = glm::unpackUnorm4x8(vertex.col),
				.texcoord = { vertex.uv.x, vertex.uv.y }
			});
		}

		for (auto index : cmdlist.IdxBuffer)
		{
			indices.push_back(static_cast<skygfx::utils::Mesh::Index>(index + base_vertex));
		}
	}

	mMesh.setVertices(std::move(vertices));
	mMesh.setIndices(std::move(indices));

	skygfx::utils::Commands draw_cmds;
	skygfx::utils::SetSampler(draw_cmds, skygfx::Sampler::Nearest);
	skygfx::utils::SetMesh(draw_cmds, &mMesh);
	skygfx::utils::SetCamera(draw_cmds, skygfx::utils::OrthogonalCamera{});
	skygfx::utils::SetModelMatrix(draw_cmds, model);

	uint32_t index_offset = 0;

	for (int i = 0; i < draw_data->CmdListsCount; i++)
	{
		const auto& cmdlist = *draw_data->CmdLists[i];

		for (const auto& cmd : cmdlist.CmdBuffer)
		{
			if (cmd.UserCallback)
			{
				cmd.UserCallback(&cmdlist, &cmd);
			}
			else
			{
				skygfx::utils::Callback(draw_cmds, [cmd]{
					skygfx::SetScissor(skygfx::Scissor{
						.position = { cmd.ClipRect.x, cmd.ClipRect.y },
						.size = { cmd.ClipRect.z - cmd.ClipRect.x, cmd.ClipRect.w - cmd.ClipRect.y }
					});
				});
				skygfx::utils::SetColorTexture(draw_cmds, (skygfx::Texture*)cmd.TextureId);
				skygfx::utils::Draw(draw_cmds, skygfx::utils::DrawIndexedVerticesCommand{
					.index_count = cmd.ElemCount,
					.index_offset = index_offset
				});
			}

			index_offset += cmd.ElemCount;
		}
	}

	skygfx::utils::ExecuteCommands(draw_cmds);

	skygfx::SetScissor(std::nullopt);
}
