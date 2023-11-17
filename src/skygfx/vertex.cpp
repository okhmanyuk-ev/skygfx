#include "vertex.h"

using namespace skygfx::vertex;

const skygfx::VertexLayout Position::Layout = {
	sizeof(Position), {
		{ location::Position, Format::Float3, offsetof(Position, pos) }
	}
};

const skygfx::VertexLayout PositionColor::Layout = {
	sizeof(PositionColor), {
		{ location::Position, Format::Float3, offsetof(PositionColor, pos) },
		{ location::Color, Format::Float4, offsetof(PositionColor, color) }
	} 
};

const skygfx::VertexLayout PositionTexture::Layout = {
	sizeof(PositionTexture), {
		{ location::Position, Format::Float3, offsetof(PositionTexture, pos) },
		{ location::TexCoord, Format::Float2, offsetof(PositionTexture, texcoord) }
	}
};

const skygfx::VertexLayout PositionNormal::Layout = {
	sizeof(PositionNormal), {
		{ location::Position, Format::Float3, offsetof(PositionNormal, pos) },
		{ location::Normal, Format::Float3, offsetof(PositionNormal, normal) }
	}
};

const skygfx::VertexLayout PositionColorNormal::Layout = {
	sizeof(PositionColorNormal), {
		{ location::Position, Format::Float3, offsetof(PositionColorNormal, pos) },
		{ location::Color, Format::Float4, offsetof(PositionColorNormal, color) },
		{ location::Normal, Format::Float3, offsetof(PositionColorNormal, normal) }
	}
};

const skygfx::VertexLayout PositionColorTexture::Layout = {
	sizeof(PositionColorTexture), {
		{ location::Position, Format::Float3, offsetof(PositionColorTexture, pos) },
		{ location::Color, Format::Float4, offsetof(PositionColorTexture, color) },
		{ location::TexCoord, Format::Float2, offsetof(PositionColorTexture, texcoord) }
	}
};

const skygfx::VertexLayout PositionTextureNormal::Layout = {
	sizeof(PositionTextureNormal), {
		{ location::Position, Format::Float3, offsetof(PositionTextureNormal, pos) },
		{ location::TexCoord, Format::Float2, offsetof(PositionTextureNormal, texcoord) },
		{ location::Normal, Format::Float3, offsetof(PositionTextureNormal, normal) }
	}
};

const skygfx::VertexLayout PositionColorTextureNormal::Layout = {
	sizeof(PositionColorTextureNormal), {
		{ location::Position, Format::Float3, offsetof(PositionColorTextureNormal, pos) },
		{ location::Color, Format::Float4, offsetof(PositionColorTextureNormal, color) },
		{ location::TexCoord, Format::Float2, offsetof(PositionColorTextureNormal, texcoord) },
		{ location::Normal, Format::Float3, offsetof(PositionColorTextureNormal, normal) }
	}
};

const skygfx::VertexLayout PositionColorTextureNormalTangent::Layout = {
	sizeof(PositionColorTextureNormalTangent), {
		{ location::Position, Format::Float3, offsetof(PositionColorTextureNormalTangent, pos) },
		{ location::Color, Format::Float4, offsetof(PositionColorTextureNormalTangent, color) },
		{ location::TexCoord, Format::Float2, offsetof(PositionColorTextureNormalTangent, texcoord) },
		{ location::Normal, Format::Float3, offsetof(PositionColorTextureNormalTangent, normal) },
		{ location::Tangent, Format::Float3, offsetof(PositionColorTextureNormalTangent, tangent) }
	}
};

const skygfx::VertexLayout PositionColorTextureNormalTangentBitangent::Layout = {
	sizeof(PositionColorTextureNormalTangentBitangent), {
		{ location::Position, Format::Float3, offsetof(PositionColorTextureNormalTangentBitangent, pos) },
		{ location::Color, Format::Float4, offsetof(PositionColorTextureNormalTangentBitangent, color) },
		{ location::TexCoord, Format::Float2, offsetof(PositionColorTextureNormalTangentBitangent, texcoord) },
		{ location::Normal, Format::Float3, offsetof(PositionColorTextureNormalTangentBitangent, normal) },
		{ location::Tangent, Format::Float3, offsetof(PositionColorTextureNormalTangentBitangent, tangent) },
		{ location::Bitangent, Format::Float3, offsetof(PositionColorTextureNormalTangentBitangent, bitangent) }
	}
};
