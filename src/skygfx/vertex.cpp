#include "vertex.h"

using namespace skygfx::Vertex;

const skygfx::VertexLayout Position::Layout = {
	sizeof(Position), {
		{ Location::Position, Format::Float3, offsetof(Position, pos) }
	}
};

const skygfx::VertexLayout PositionColor::Layout = {
	sizeof(PositionColor), {
		{ Location::Position, Format::Float3, offsetof(PositionColor, pos) },
		{ Location::Color, Format::Float4, offsetof(PositionColor, color) }
	} 
};

const skygfx::VertexLayout PositionTexture::Layout = {
	sizeof(PositionTexture), {
		{ Location::Position, Format::Float3, offsetof(PositionTexture, pos) },
		{ Location::TexCoord, Format::Float2, offsetof(PositionTexture, texcoord) }
	}
};

const skygfx::VertexLayout PositionNormal::Layout = {
	sizeof(PositionNormal), {
		{ Location::Position, Format::Float3, offsetof(PositionNormal, pos) },
		{ Location::Normal, Format::Float3, offsetof(PositionNormal, normal) }
	}
};

const skygfx::VertexLayout PositionColorNormal::Layout = {
	sizeof(PositionColorNormal), {
		{ Location::Position, Format::Float3, offsetof(PositionColorNormal, pos) },
		{ Location::Color, Format::Float4, offsetof(PositionColorNormal, color) },
		{ Location::Normal, Format::Float3, offsetof(PositionColorNormal, normal) }
	}
};

const skygfx::VertexLayout PositionColorTexture::Layout = {
	sizeof(PositionColorTexture), {
		{ Location::Position, Format::Float3, offsetof(PositionColorTexture, pos) },
		{ Location::Color, Format::Float4, offsetof(PositionColorTexture, color) },
		{ Location::TexCoord, Format::Float2, offsetof(PositionColorTexture, texcoord) }
	}
};

const skygfx::VertexLayout PositionTextureNormal::Layout = {
	sizeof(PositionTextureNormal), {
		{ Location::Position, Format::Float3, offsetof(PositionTextureNormal, pos) },
		{ Location::TexCoord, Format::Float2, offsetof(PositionTextureNormal, texcoord) },
		{ Location::Normal, Format::Float3, offsetof(PositionTextureNormal, normal) }
	}
};

const skygfx::VertexLayout PositionColorTextureNormal::Layout = {
	sizeof(PositionColorTextureNormal), {
		{ Location::Position, Format::Float3, offsetof(PositionColorTextureNormal, pos) },
		{ Location::Color, Format::Float4, offsetof(PositionColorTextureNormal, color) },
		{ Location::TexCoord, Format::Float2, offsetof(PositionColorTextureNormal, texcoord) },
		{ Location::Normal, Format::Float3, offsetof(PositionColorTextureNormal, normal) }
	}
};
