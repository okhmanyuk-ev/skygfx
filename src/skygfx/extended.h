#pragma once

#include "skygfx.h"

namespace skygfx::extended
{
	class Mesh
	{
	public:
		using Vertex = Vertex::PositionColorTextureNormal;
		using Vertices = std::vector<Vertex>;
		using Indices = std::vector<uint32_t>;
		
		struct DrawVertices
		{
			std::optional<uint32_t> vertex_count = std::nullopt;
			uint32_t vertex_offset = 0;
		};
		
		struct DrawIndexedVertices
		{
			std::optional<uint32_t> index_count = std::nullopt;
			uint32_t index_offset = 0;
		};
		
		using DrawingType = std::variant<DrawVertices, DrawIndexedVertices>;
		
		auto getTopology() const { return mTopology; }
		void setTopology(Topology value) { mTopology = value; }

		const auto& getVertices() const { return mVertices; }
		void setVertices(const Vertices& value) { mVertices = value; }

		const auto& getIndices() const { return mIndices; }
		void setIndices(const Indices& value) { mIndices = value; }

		const auto& getDrawingType() const { return mDrawingType; }
		void setDrawingType(DrawingType value) { mDrawingType = value; }

	private:
		Topology mTopology = Topology::TriangleList;
		Vertices mVertices;
		Indices mIndices;
		DrawingType mDrawingType = DrawVertices{};
	};
	
	struct Matrices
	{
		glm::mat4 projection = glm::mat4(1.0f);
		glm::mat4 view = glm::mat4(1.0f);
		glm::mat4 model = glm::mat4(1.0f);
	};

	struct alignas(16) DirectionalLight
	{
		alignas(16) glm::vec3 direction = { 0.0f, 0.0f, 0.0f };
		alignas(16) glm::vec3 ambient = { 0.0f, 0.0f, 0.0f };
		alignas(16) glm::vec3 diffuse = { 0.0f, 0.0f, 0.0f };
		alignas(16) glm::vec3 specular = { 0.0f, 0.0f, 0.0f };
		float shininess = 0.0f; // TODO: only material has shininess
	};

	struct alignas(16) PointLight
	{
		alignas(16) glm::vec3 position = { 0.0f, 0.0f, 0.0f };
		alignas(16) glm::vec3 ambient = { 0.0f, 0.0f, 0.0f };
		alignas(16) glm::vec3 diffuse = { 0.0f, 0.0f, 0.0f };
		alignas(16) glm::vec3 specular = { 0.0f, 0.0f, 0.0f };
		float constant_attenuation = 0.0f;
		float linear_attenuation = 0.0f;
		float quadratic_attenuation = 0.0f;
		float shininess = 0.0f; // TODO: only material has shininess
	};

	using Light = std::optional<std::variant<DirectionalLight, PointLight>>;

	void DrawMesh(const Mesh& mesh, const Matrices& matrices, Texture* color_texture = nullptr,
		Texture* normal_texture = nullptr, float mipmap_bias = 0.0f, const Light& light = std::nullopt,
		const glm::vec3& eye_position = { 0.0f, 0.0f, 0.0f });

	struct OrthogonalCamera {};

	struct PerspectiveCamera
	{
		float yaw = 0.0f;
		float pitch = 0.0f;
		glm::vec3 position = { 0.0f, 0.0f, 0.0f };
		glm::vec3 world_up = { 0.0f, 1.0f, 0.0f };
		float far_plane = 8192.0f;
		float near_plane = 1.0f;
		float fov = 70.0f;
	};
	
	using Camera = std::variant<OrthogonalCamera, PerspectiveCamera>;

	void DrawMesh(const Mesh& mesh, const Camera& camera, const glm::mat4& model,
		Texture* color_texture = nullptr, Texture* normal_texture = nullptr,
		float mipmap_bias = 0.0f, const Light& light = std::nullopt);
}