#pragma once

#include <vector>
#include <optional>
#include <string>
#include <glm/glm.hpp>

namespace skygfx::Vertex
{
	namespace Location
	{
		constexpr auto Position = "POSITION_LOCATION";
		constexpr auto Color = "COLOR_LOCATION";
		constexpr auto TexCoord = "TEXCOORD_LOCATION";
		constexpr auto Normal = "NORMAL_LOCATION";
	};

	struct Attribute
	{
		enum class Format
		{
			Float1,
			Float2,
			Float3,
			Float4,
			Byte1,
			Byte2,
			Byte3,
			Byte4
		};

		std::optional<std::string> location_define;
		Format format;
		size_t offset;
	};

	struct Layout
	{
		size_t stride;
		std::vector<Attribute> attributes;
	};

	// predefined vertex types:

	struct Position
	{
		glm::vec3 pos = { 0.0f, 0.0f, 0.0f };

		static const Layout Layout;
	};

	struct PositionColor
	{
		glm::vec3 pos = { 0.0f, 0.0f, 0.0f };
		glm::vec4 color = { 0.0f, 0.0f, 0.0f, 0.0f };

		static const Layout Layout;
	};

	struct PositionTexture 
	{
		glm::vec3 pos = { 0.0f, 0.0f, 0.0f };
		glm::vec2 texcoord = { 0.0f, 0.0f };
	
		static const Layout Layout;
	};

	struct PositionNormal
	{
		glm::vec3 pos = { 0.0f, 0.0f, 0.0f };
		glm::vec3 normal = { 0.0f, 0.0f, 0.0f };

		static const Layout Layout;
	};

	struct PositionColorNormal
	{
		glm::vec3 pos = { 0.0f, 0.0f, 0.0f };
		glm::vec4 color = { 0.0f, 0.0f, 0.0f, 0.0f };
		glm::vec3 normal = { 0.0f, 0.0f, 0.0f };

		static const Layout Layout;
	};

	struct PositionColorTexture
	{
		glm::vec3 pos = { 0.0f, 0.0f, 0.0f };
		glm::vec4 color = { 0.0f, 0.0f, 0.0f, 0.0f };
		glm::vec2 texcoord = { 0.0f, 0.0f };

		static const Layout Layout;
	};

	struct PositionTextureNormal
	{
		glm::vec3 pos = { 0.0f, 0.0f, 0.0f };
		glm::vec2 texcoord = { 0.0f, 0.0f };
		glm::vec3 normal = { 0.0f, 0.0f, 0.0f };

		static const Layout Layout;
	};

	struct PositionColorTextureNormal
	{
		glm::vec3 pos = { 0.0f, 0.0f, 0.0f };
		glm::vec4 color = { 0.0f, 0.0f, 0.0f, 0.0f };
		glm::vec2 texcoord = { 0.0f, 0.0f };
		glm::vec3 normal = { 0.0f, 0.0f, 0.0f };

		static const Layout Layout;
	};
}
