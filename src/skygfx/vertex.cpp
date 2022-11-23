#include "vertex.h"

using namespace skygfx::Vertex;

const Layout Position::Layout = { 
	sizeof(Position), {
		{ Location::Position, Attribute::Format::Float3, offsetof(Position, pos) }
	}
};

const Layout PositionColor::Layout = { 
	sizeof(PositionColor), {
		{ Location::Position, Attribute::Format::Float3, offsetof(PositionColor, pos) },
		{ Location::Color, Attribute::Format::Float4, offsetof(PositionColor, color) }
	} 
};

const Layout PositionTexture::Layout = { 
	sizeof(PositionTexture), {
		{ Location::Position, Attribute::Format::Float3, offsetof(PositionTexture, pos) },
		{ Location::TexCoord, Attribute::Format::Float2, offsetof(PositionTexture, texcoord) }
	}
};

const Layout PositionNormal::Layout = { 
	sizeof(PositionNormal), {
		{ Location::Position, Attribute::Format::Float3, offsetof(PositionNormal, pos) },
		{ Location::Normal, Attribute::Format::Float3, offsetof(PositionNormal, normal) }
	}
};

const Layout PositionColorNormal::Layout = { 
	sizeof(PositionColorNormal), {
		{ Location::Position, Attribute::Format::Float3, offsetof(PositionColorNormal, pos) },
		{ Location::Color, Attribute::Format::Float4, offsetof(PositionColorNormal, color) },
		{ Location::Normal, Attribute::Format::Float3, offsetof(PositionColorNormal, normal) }
	}
};

const Layout PositionColorTexture::Layout = { 
	sizeof(PositionColorTexture), {
		{ Location::Position, Attribute::Format::Float3, offsetof(PositionColorTexture, pos) },
		{ Location::Color, Attribute::Format::Float4, offsetof(PositionColorTexture, color) },
		{ Location::TexCoord, Attribute::Format::Float2, offsetof(PositionColorTexture, texcoord) }
	}
};

const Layout PositionTextureNormal::Layout = { 
	sizeof(PositionTextureNormal), {
		{ Location::Position, Attribute::Format::Float3, offsetof(PositionTextureNormal, pos) },
		{ Location::TexCoord, Attribute::Format::Float2, offsetof(PositionTextureNormal, texcoord) },
		{ Location::Normal, Attribute::Format::Float3, offsetof(PositionTextureNormal, normal) }
	}
};

const Layout PositionColorTextureNormal::Layout = { 
	sizeof(PositionColorTextureNormal), {
		{ Location::Position, Attribute::Format::Float3, offsetof(PositionColorTextureNormal, pos) },
		{ Location::Color, Attribute::Format::Float4, offsetof(PositionColorTextureNormal, color) },
		{ Location::TexCoord, Attribute::Format::Float2, offsetof(PositionColorTextureNormal, texcoord) },
		{ Location::Normal, Attribute::Format::Float3, offsetof(PositionColorTextureNormal, normal) }
	}
};
