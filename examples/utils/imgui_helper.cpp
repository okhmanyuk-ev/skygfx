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
	auto model = glm::scale(glm::mat4(1.0f), { display_scale.x, display_scale.y, 1.0f });

	auto draw_data = ImGui::GetDrawData();
	draw_data->ScaleClipRects(display_scale);

	skygfx::ext::Mesh::Vertices vertices;
	skygfx::ext::Mesh::Indices indices;

	for (int i = 0; i < draw_data->CmdListsCount; i++)
	{
		const auto& cmdlist = *draw_data->CmdLists[i];
		auto base_vertex = vertices.size();

		for (const auto& vertex : cmdlist.VtxBuffer)
		{
			vertices.push_back(skygfx::ext::Mesh::Vertex{
				.pos = { vertex.pos.x, vertex.pos.y, 0.0f },
				.color = glm::unpackUnorm4x8(vertex.col),
				.texcoord = { vertex.uv.x, vertex.uv.y }
			});
		}

		for (auto index : cmdlist.IdxBuffer)
		{
			indices.push_back(static_cast<skygfx::ext::Mesh::Index>(index + base_vertex));
		}
	}

	mMesh.setVertices(std::move(vertices));
	mMesh.setIndices(std::move(indices));

	skygfx::ext::Commands draw_cmds;
	skygfx::ext::SetMesh(draw_cmds, &mMesh);
	skygfx::ext::SetCamera(draw_cmds, skygfx::ext::OrthogonalCamera{});
	skygfx::ext::SetModelMatrix(draw_cmds, model);

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
				skygfx::ext::Callback(draw_cmds, [cmd]{
					skygfx::SetScissor(skygfx::Scissor{
						.position = { cmd.ClipRect.x, cmd.ClipRect.y },
						.size = { cmd.ClipRect.z - cmd.ClipRect.x, cmd.ClipRect.w - cmd.ClipRect.y }
					});
				});
				skygfx::ext::SetColorTexture(draw_cmds, (skygfx::Texture*)cmd.TextureId);
				skygfx::ext::Draw(draw_cmds, skygfx::ext::DrawIndexedVerticesCommand{
					.index_count = cmd.ElemCount,
					.index_offset = index_offset
				});
			}

			index_offset += cmd.ElemCount;
		}
	}

	skygfx::ext::ExecuteCommands(draw_cmds);

	skygfx::SetScissor(std::nullopt);
}
