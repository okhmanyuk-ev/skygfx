#include "vertex.h"

using namespace skygfx::Vertex;

const Layout Position::Layout = { 
	sizeof(Position), {
		{ Location::Position, Attribute::Format::R32G32B32F, offsetof(Position, pos) }
	}
};

const Layout PositionColor::Layout = { 
	sizeof(PositionColor), {
		{ Location::Position, Attribute::Format::R32G32B32F, offsetof(PositionColor, pos) },
		{ Location::Color, Attribute::Format::R32G32B32A32F, offsetof(PositionColor, color) }
	} 
};

const Layout PositionTexture::Layout = { 
	sizeof(PositionTexture), {
		{ Location::Position, Attribute::Format::R32G32B32F, offsetof(PositionTexture, pos) },
		{ Location::TexCoord, Attribute::Format::R32G32F, offsetof(PositionTexture, texcoord) }
	}
};

const Layout PositionNormal::Layout = { 
	sizeof(PositionNormal), {
		{ Location::Position, Attribute::Format::R32G32B32F, offsetof(PositionNormal, pos) },
		{ Location::Normal, Attribute::Format::R32G32B32F, offsetof(PositionNormal, normal) }
	}
};

const Layout PositionColorNormal::Layout = { 
	sizeof(PositionColorNormal), {
		{ Location::Position, Attribute::Format::R32G32B32F, offsetof(PositionColorNormal, pos) },
		{ Location::Color, Attribute::Format::R32G32B32A32F, offsetof(PositionColorNormal, color) },
		{ Location::Normal, Attribute::Format::R32G32B32F, offsetof(PositionColorNormal, normal) }
	}
};

const Layout PositionColorTexture::Layout = { 
	sizeof(PositionColorTexture), {
		{ Location::Position, Attribute::Format::R32G32B32F, offsetof(PositionColorTexture, pos) },
		{ Location::Color, Attribute::Format::R32G32B32A32F, offsetof(PositionColorTexture, color) },
		{ Location::TexCoord, Attribute::Format::R32G32F, offsetof(PositionColorTexture, texcoord) }
	}
};

const Layout PositionTextureNormal::Layout = { 
	sizeof(PositionTextureNormal), {
		{ Location::Position, Attribute::Format::R32G32B32F, offsetof(PositionTextureNormal, pos) },
		{ Location::TexCoord, Attribute::Format::R32G32F, offsetof(PositionTextureNormal, texcoord) },
		{ Location::Normal, Attribute::Format::R32G32B32F, offsetof(PositionTextureNormal, normal) }
	}
};

const Layout PositionColorTextureNormal::Layout = { 
	sizeof(PositionColorTextureNormal), {
		{ Location::Position, Attribute::Format::R32G32B32F, offsetof(PositionColorTextureNormal, pos) },
		{ Location::Color, Attribute::Format::R32G32B32A32F, offsetof(PositionColorTextureNormal, color) },
		{ Location::TexCoord, Attribute::Format::R32G32F, offsetof(PositionColorTextureNormal, texcoord) },
		{ Location::Normal, Attribute::Format::R32G32B32F, offsetof(PositionColorTextureNormal, normal) }
	}
};
