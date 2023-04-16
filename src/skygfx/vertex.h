#pragma once

#include <vector>
#include <optional>
#include <string>
#include <glm/glm.hpp>
#include "skygfx.h"

namespace skygfx::Vertex
{
	namespace Location
	{
		constexpr auto Position = "POSITION_LOCATION";
		constexpr auto Color = "COLOR_LOCATION";
		constexpr auto TexCoord = "TEXCOORD_LOCATION";
		constexpr auto Normal = "NORMAL_LOCATION";
	};

	// predefined vertex types:

	struct Position
	{
		glm::vec3 pos = { 0.0f, 0.0f, 0.0f };

		static const VertexLayout Layout;
	};

	struct PositionColor
	{
		glm::vec3 pos = { 0.0f, 0.0f, 0.0f };
		glm::vec4 color = { 0.0f, 0.0f, 0.0f, 0.0f };

		static const VertexLayout Layout;
	};

	struct PositionTexture 
	{
		glm::vec3 pos = { 0.0f, 0.0f, 0.0f };
		glm::vec2 texcoord = { 0.0f, 0.0f };
	
		static const VertexLayout Layout;
	};

	struct PositionNormal
	{
		glm::vec3 pos = { 0.0f, 0.0f, 0.0f };
		glm::vec3 normal = { 0.0f, 0.0f, 0.0f };

		static const VertexLayout Layout;
	};

	struct PositionColorNormal
	{
		glm::vec3 pos = { 0.0f, 0.0f, 0.0f };
		glm::vec4 color = { 0.0f, 0.0f, 0.0f, 0.0f };
		glm::vec3 normal = { 0.0f, 0.0f, 0.0f };

		static const VertexLayout Layout;
	};

	struct PositionColorTexture
	{
		glm::vec3 pos = { 0.0f, 0.0f, 0.0f };
		glm::vec4 color = { 0.0f, 0.0f, 0.0f, 0.0f };
		glm::vec2 texcoord = { 0.0f, 0.0f };

		static const VertexLayout Layout;
	};

	struct PositionTextureNormal
	{
		glm::vec3 pos = { 0.0f, 0.0f, 0.0f };
		glm::vec2 texcoord = { 0.0f, 0.0f };
		glm::vec3 normal = { 0.0f, 0.0f, 0.0f };

		static const VertexLayout Layout;
	};

	struct PositionColorTextureNormal
	{
		glm::vec3 pos = { 0.0f, 0.0f, 0.0f };
		glm::vec4 color = { 0.0f, 0.0f, 0.0f, 0.0f };
		glm::vec2 texcoord = { 0.0f, 0.0f };
		glm::vec3 normal = { 0.0f, 0.0f, 0.0f };

		static const VertexLayout Layout;
	};
}
