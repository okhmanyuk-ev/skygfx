#pragma once

#include "skygfx.h"

namespace skygfx::utils
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
		void setVertices(const Vertices& value);

		const auto& getIndices() const { return mIndices; }
		void setIndices(const Indices& value);

		const auto& getDrawingType() const { return mDrawingType; }
		void setDrawingType(DrawingType value) { mDrawingType = value; }

	private:
		Topology mTopology = Topology::TriangleList;
		Vertices mVertices;
		Indices mIndices;
		DrawingType mDrawingType = DrawVertices{};

	public:		
		const auto& getVertexBuffer() const { return *mVertexBuffer; }
		const auto& getIndexBuffer() const { return *mIndexBuffer; }
		
	private:
		std::shared_ptr<skygfx::VertexBuffer> mVertexBuffer = nullptr;
		std::shared_ptr<skygfx::IndexBuffer> mIndexBuffer = nullptr;
	};
	
	struct Material
	{
		glm::vec4 color = { 1.0f, 1.0f, 1.0f, 1.0f };
		Texture* color_texture = nullptr;
		Texture* normal_texture = nullptr;
	};
	
	struct Matrices
	{
		glm::mat4 projection = glm::mat4(1.0f);
		glm::mat4 view = glm::mat4(1.0f);
		glm::mat4 model = glm::mat4(1.0f);
	};

	struct alignas(16) DirectionalLight
	{
		alignas(16) glm::vec3 direction = { 0.5f, 0.5f, 0.5f };
		alignas(16) glm::vec3 ambient = { 1.0f, 1.0f, 1.0f };
		alignas(16) glm::vec3 diffuse = { 1.0f, 1.0f, 1.0f };
		alignas(16) glm::vec3 specular = { 1.0f, 1.0f, 1.0f };
		float shininess = 32.0f; // TODO: only material has shininess
	};

	struct alignas(16) PointLight
	{
		alignas(16) glm::vec3 position = { 0.0f, 0.0f, 0.0f };
		alignas(16) glm::vec3 ambient = { 1.0f, 1.0f, 1.0f };
		alignas(16) glm::vec3 diffuse = { 1.0f, 1.0f, 1.0f };
		alignas(16) glm::vec3 specular = { 1.0f, 1.0f, 1.0f };
		float constant_attenuation = 0.0f;
		float linear_attenuation = 0.00128f;
		float quadratic_attenuation = 0.0f;
		float shininess = 32.0f; // TODO: only material has shininess
	};

	using Light = std::optional<std::variant<DirectionalLight, PointLight>>;

	void DrawMesh(const Mesh& mesh, const Matrices& matrices, const Material& material = {},
		float mipmap_bias = 0.0f, const Light& light = std::nullopt,
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
		const Material& material = {}, float mipmap_bias = 0.0f, const Light& light = std::nullopt);

	// buffer will be recreated if it doesnt have enough space
	// TODO: make with move semantics instead of shared_ptrs
	// TODO: try to remove stride argument
	// TODO: try to make templated version when stride argument will be removed
	std::shared_ptr<skygfx::VertexBuffer> EnsureBufferSpace(std::shared_ptr<skygfx::VertexBuffer> buffer, size_t size, size_t stride);
	std::shared_ptr<skygfx::IndexBuffer> EnsureBufferSpace(std::shared_ptr<skygfx::IndexBuffer> buffer, size_t size, size_t stride);
	std::shared_ptr<skygfx::UniformBuffer> EnsureBufferSpace(std::shared_ptr<skygfx::UniformBuffer> buffer, size_t size);
}
