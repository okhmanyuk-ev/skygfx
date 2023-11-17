#pragma once

#include <vector>
#include <optional>
#include <string>
#include <glm/glm.hpp>
#include "skygfx.h"

namespace skygfx::vertex
{
	namespace location
	{
		constexpr auto Position = "POSITION_LOCATION";
		constexpr auto Color = "COLOR_LOCATION";
		constexpr auto TexCoord = "TEXCOORD_LOCATION";
		constexpr auto Normal = "NORMAL_LOCATION";
		constexpr auto Tangent = "TANGENT_LOCATION";
		constexpr auto Bitangent = "BITANGENT_LOCATION";
	};

	namespace defaults
	{
		constexpr glm::vec3 Position = { 0.0f, 0.0f, 0.0f };
		constexpr glm::vec4 Color = { 1.0f, 1.0f, 1.0f, 1.0f };
		constexpr glm::vec2 TexCoord = { 0.0f, 0.0f };
		constexpr glm::vec3 Normal = { 0.0f, 0.0f, 0.0f };
		constexpr glm::vec3 Tangent = { 0.0f, 1.0f, 0.0f };
		constexpr glm::vec3 Bitangent = { 0.0f, 1.0f, 0.0f };
	}

	// predefined vertex types:

	struct Position
	{
		glm::vec3 pos = defaults::Position;

		static const VertexLayout Layout;
	};

	struct PositionColor
	{
		glm::vec3 pos = defaults::Position;
		glm::vec4 color = defaults::Color;

		static const VertexLayout Layout;
	};

	struct PositionTexture 
	{
		glm::vec3 pos = defaults::Position;
		glm::vec2 texcoord = defaults::TexCoord;
	
		static const VertexLayout Layout;
	};

	struct PositionNormal
	{
		glm::vec3 pos = defaults::Position;
		glm::vec3 normal = defaults::Normal;

		static const VertexLayout Layout;
	};

	struct PositionColorNormal
	{
		glm::vec3 pos = defaults::Position;
		glm::vec4 color = defaults::Color;
		glm::vec3 normal = defaults::Normal;

		static const VertexLayout Layout;
	};

	struct PositionColorTexture
	{
		glm::vec3 pos = defaults::Position;
		glm::vec4 color = defaults::Color;
		glm::vec2 texcoord = defaults::TexCoord;

		static const VertexLayout Layout;
	};

	struct PositionTextureNormal
	{
		glm::vec3 pos = defaults::Position;
		glm::vec2 texcoord = defaults::TexCoord;
		glm::vec3 normal = defaults::Normal;

		static const VertexLayout Layout;
	};

	struct PositionColorTextureNormal
	{
		glm::vec3 pos = defaults::Position;
		glm::vec4 color = defaults::Color;
		glm::vec2 texcoord = defaults::TexCoord;
		glm::vec3 normal = defaults::Normal;

		static const VertexLayout Layout;
	};

	struct PositionColorTextureNormalTangent
	{
		glm::vec3 pos = defaults::Position;
		glm::vec4 color = defaults::Color;
		glm::vec2 texcoord = defaults::TexCoord;
		glm::vec3 normal = defaults::Normal;
		glm::vec3 tangent = defaults::Tangent;

		static const VertexLayout Layout;
	};

	struct PositionColorTextureNormalTangentBitangent
	{
		glm::vec3 pos = defaults::Position;
		glm::vec4 color = defaults::Color;
		glm::vec2 texcoord = defaults::TexCoord;
		glm::vec3 normal = defaults::Normal;
		glm::vec3 tangent = defaults::Tangent;
		glm::vec3 bitangent = defaults::Bitangent;

		static const VertexLayout Layout;
	};
}
