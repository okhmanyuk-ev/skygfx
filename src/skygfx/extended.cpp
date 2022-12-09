#include "extended.h"

using namespace skygfx;

static const std::string vertex_shader_code = R"(
#version 450 core

layout(location = POSITION_LOCATION) in vec3 aPosition;
layout(location = COLOR_LOCATION) in vec4 aColor;
layout(location = TEXCOORD_LOCATION) in vec2 aTexCoord;

layout(binding = 1) uniform _matrices
{
	mat4 projection;
	mat4 view;
	mat4 model;
} matrices;

layout(location = 0) out struct {
	vec4 color;
	vec2 tex_coord;
} Out;

out gl_PerVertex { vec4 gl_Position; };

void main()
{
	Out.tex_coord = aTexCoord;
	Out.color = aColor;
#ifdef FLIP_TEXCOORD_Y
	Out.tex_coord.y = 1.0 - Out.tex_coord.y;
#endif
	gl_Position = matrices.projection * matrices.view * matrices.model * vec4(aPosition, 1.0);
})";

static const std::string fragment_shader_code = R"(
#version 450 core

layout(location = 0) in struct {
	vec4 color;
	vec2 tex_coord;
} In;

layout(location = 0) out vec4 result;

layout(binding = 0) uniform sampler2D sColorTexture;

void main()
{
	result = texture(sColorTexture, In.tex_coord) * In.color;
})";

void extended::DrawMesh(const Mesh& mesh, const Matrices& matrices, const Texture& texture,
		const Scissor& scissor)
{
	static auto shader = std::make_shared<skygfx::Shader>(Mesh::Vertex::Layout, vertex_shader_code, fragment_shader_code);

	auto topology = mesh.getTopology();
	const auto& vertices = mesh.getVertices();
	const auto& indices = mesh.getIndices();
	
	skygfx::SetTopology(topology);
	skygfx::SetDynamicVertexBuffer(vertices);
	skygfx::SetDynamicUniformBuffer(1, matrices);
	skygfx::SetTexture(0, texture);
	skygfx::SetScissor(scissor);
	skygfx::SetShader(*shader);
	
	if (indices.empty())
	{
		skygfx::Draw(static_cast<uint32_t>(vertices.size()));
	}
	else
	{
		skygfx::DrawIndexed(static_cast<uint32_t>(indices.size()));
	}
}
