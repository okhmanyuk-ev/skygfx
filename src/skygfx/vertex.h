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

	std::vector<std::string> MakeSequentialLocationDefines(const std::vector<std::string>& locations);

	struct Position
	{
		glm::vec3 pos = defaults::Position;

		static const InputLayout Layout;

		static inline const auto Defines = MakeSequentialLocationDefines({
			location::Position
		});
	};

	struct PositionColor
	{
		glm::vec3 pos = defaults::Position;
		glm::vec4 color = defaults::Color;

		static const InputLayout Layout;

		static inline const auto Defines = MakeSequentialLocationDefines({
			location::Position,
			location::Color
		});
	};

	struct PositionTexture 
	{
		glm::vec3 pos = defaults::Position;
		glm::vec2 texcoord = defaults::TexCoord;
	
		static const InputLayout Layout;

		static inline const auto Defines = MakeSequentialLocationDefines({
			location::Position,
			location::TexCoord
		});
	};

	struct PositionNormal
	{
		glm::vec3 pos = defaults::Position;
		glm::vec3 normal = defaults::Normal;

		static const InputLayout Layout;

		static inline const auto Defines = MakeSequentialLocationDefines({
			location::Position,
			location::Normal
		});
	};

	struct PositionColorNormal
	{
		glm::vec3 pos = defaults::Position;
		glm::vec4 color = defaults::Color;
		glm::vec3 normal = defaults::Normal;

		static const InputLayout Layout;

		static inline const auto Defines = MakeSequentialLocationDefines({
			location::Position,
			location::Color,
			location::Normal
		});
	};

	struct PositionColorTexture
	{
		glm::vec3 pos = defaults::Position;
		glm::vec4 color = defaults::Color;
		glm::vec2 texcoord = defaults::TexCoord;

		static const InputLayout Layout;

		static inline const auto Defines = MakeSequentialLocationDefines({
			location::Position,
			location::Color,
			location::TexCoord
		});
	};

	struct PositionTextureNormal
	{
		glm::vec3 pos = defaults::Position;
		glm::vec2 texcoord = defaults::TexCoord;
		glm::vec3 normal = defaults::Normal;

		static const InputLayout Layout;

		static inline const auto Defines = MakeSequentialLocationDefines({
			location::Position,
			location::TexCoord,
			location::Normal
		});
	};

	struct PositionColorTextureNormal
	{
		glm::vec3 pos = defaults::Position;
		glm::vec4 color = defaults::Color;
		glm::vec2 texcoord = defaults::TexCoord;
		glm::vec3 normal = defaults::Normal;

		static const InputLayout Layout;

		static inline const auto Defines = MakeSequentialLocationDefines({
			location::Position,
			location::Color,
			location::TexCoord,
			location::Normal
		});
	};

	struct PositionColorTextureNormalTangent
	{
		glm::vec3 pos = defaults::Position;
		glm::vec4 color = defaults::Color;
		glm::vec2 texcoord = defaults::TexCoord;
		glm::vec3 normal = defaults::Normal;
		glm::vec3 tangent = defaults::Tangent;

		static const InputLayout Layout;

		static inline const auto Defines = MakeSequentialLocationDefines({
			location::Position,
			location::Color,
			location::TexCoord,
			location::Normal,
			location::Tangent
		});
	};

	struct PositionColorTextureNormalTangentBitangent
	{
		glm::vec3 pos = defaults::Position;
		glm::vec4 color = defaults::Color;
		glm::vec2 texcoord = defaults::TexCoord;
		glm::vec3 normal = defaults::Normal;
		glm::vec3 tangent = defaults::Tangent;
		glm::vec3 bitangent = defaults::Bitangent;

		static const InputLayout Layout;

		static inline const auto Defines = MakeSequentialLocationDefines({
			location::Position,
			location::Color,
			location::TexCoord,
			location::Normal,
			location::Tangent,
			location::Bitangent
		});
	};
}
