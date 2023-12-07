#include "vertex.h"

using namespace skygfx::vertex;

std::vector<std::string> skygfx::vertex::MakeSequentialLocationDefines(const std::vector<std::string>& locations)
{
	std::vector<std::string> result;

	for (size_t i = 0; i < locations.size(); i++)
	{
		const auto& location = locations.at(i);
		result.push_back(location + " " + std::to_string(i));
	}

	return result;
}

const skygfx::InputLayout Position::Layout = skygfx::InputLayout(skygfx::InputLayout::Rate::Vertex, {
	{ Format::Float3, offsetof(Position, pos) }
});

const skygfx::InputLayout PositionColor::Layout = skygfx::InputLayout(skygfx::InputLayout::Rate::Vertex, {
	{ Format::Float3, offsetof(PositionColor, pos) },
	{ Format::Float4, offsetof(PositionColor, color) }
});

const skygfx::InputLayout PositionTexture::Layout = skygfx::InputLayout(skygfx::InputLayout::Rate::Vertex, {
	{ Format::Float3, offsetof(PositionTexture, pos) },
	{ Format::Float2, offsetof(PositionTexture, texcoord) }
});

const skygfx::InputLayout PositionNormal::Layout = skygfx::InputLayout(skygfx::InputLayout::Rate::Vertex, {
	{ Format::Float3, offsetof(PositionNormal, pos) },
	{ Format::Float3, offsetof(PositionNormal, normal) }
});

const skygfx::InputLayout PositionColorNormal::Layout = skygfx::InputLayout(skygfx::InputLayout::Rate::Vertex, {
	{ Format::Float3, offsetof(PositionColorNormal, pos) },
	{ Format::Float4, offsetof(PositionColorNormal, color) },
	{ Format::Float3, offsetof(PositionColorNormal, normal) }
});

const skygfx::InputLayout PositionColorTexture::Layout = skygfx::InputLayout(skygfx::InputLayout::Rate::Vertex, {
	{ Format::Float3, offsetof(PositionColorTexture, pos) },
	{ Format::Float4, offsetof(PositionColorTexture, color) },
	{ Format::Float2, offsetof(PositionColorTexture, texcoord) }
});

const skygfx::InputLayout PositionTextureNormal::Layout = skygfx::InputLayout(skygfx::InputLayout::Rate::Vertex, {
	{ Format::Float3, offsetof(PositionTextureNormal, pos) },
	{ Format::Float2, offsetof(PositionTextureNormal, texcoord) },
	{ Format::Float3, offsetof(PositionTextureNormal, normal) }
});

const skygfx::InputLayout PositionColorTextureNormal::Layout = skygfx::InputLayout(skygfx::InputLayout::Rate::Vertex, {
	{ Format::Float3, offsetof(PositionColorTextureNormal, pos) },
	{ Format::Float4, offsetof(PositionColorTextureNormal, color) },
	{ Format::Float2, offsetof(PositionColorTextureNormal, texcoord) },
	{ Format::Float3, offsetof(PositionColorTextureNormal, normal) }
});

const skygfx::InputLayout PositionColorTextureNormalTangent::Layout = skygfx::InputLayout(skygfx::InputLayout::Rate::Vertex, {
	{ Format::Float3, offsetof(PositionColorTextureNormalTangent, pos) },
	{ Format::Float4, offsetof(PositionColorTextureNormalTangent, color) },
	{ Format::Float2, offsetof(PositionColorTextureNormalTangent, texcoord) },
	{ Format::Float3, offsetof(PositionColorTextureNormalTangent, normal) },
	{ Format::Float3, offsetof(PositionColorTextureNormalTangent, tangent) }
});

const skygfx::InputLayout PositionColorTextureNormalTangentBitangent::Layout = skygfx::InputLayout(skygfx::InputLayout::Rate::Vertex, {
	{ Format::Float3, offsetof(PositionColorTextureNormalTangentBitangent, pos) },
	{ Format::Float4, offsetof(PositionColorTextureNormalTangentBitangent, color) },
	{ Format::Float2, offsetof(PositionColorTextureNormalTangentBitangent, texcoord) },
	{ Format::Float3, offsetof(PositionColorTextureNormalTangentBitangent, normal) },
	{ Format::Float3, offsetof(PositionColorTextureNormalTangentBitangent, tangent) },
	{ Format::Float3, offsetof(PositionColorTextureNormalTangentBitangent, bitangent) }
});
