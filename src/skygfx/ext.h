#pragma once

#include "skygfx.h"

namespace skygfx::ext
{
	class Mesh
	{
	public:
		using Vertex = Vertex::PositionColorTextureNormal;
		using Index = uint32_t;
		using Vertices = std::vector<Vertex>;
		using Indices = std::vector<Index>;

		auto getTopology() const { return mTopology; }
		void setTopology(Topology value) { mTopology = value; }

		const auto& getVertices() const { return mVertices; }
		void setVertices(const Vertices& value);

		const auto& getIndices() const { return mIndices; }
		void setIndices(const Indices& value);

	private:
		Topology mTopology = Topology::TriangleList;
		Vertices mVertices;
		Indices mIndices;

	public:		
		const auto& getVertexBuffer() const { return mVertexBuffer; }
		const auto& getIndexBuffer() const { return mIndexBuffer; }
		
	private:
		std::optional<VertexBuffer> mVertexBuffer;
		std::optional<IndexBuffer> mIndexBuffer;
	};

	struct DrawVerticesCommand
	{
		std::optional<uint32_t> vertex_count = std::nullopt;
		uint32_t vertex_offset = 0;
	};

	struct DrawIndexedVerticesCommand
	{
		std::optional<uint32_t> index_count = std::nullopt;
		uint32_t index_offset = 0;
	};

	using DrawCommand = std::variant<
		DrawVerticesCommand,
		DrawIndexedVerticesCommand
	>;
	
	struct NoLight {};

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

	using Light = std::variant<
		NoLight,
		DirectionalLight,
		PointLight
	>;

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

	std::tuple<glm::mat4/*proj*/, glm::mat4/*view*/, glm::vec3/*eye_pos*/> MakeCameraMatrices(const Camera& camera, 
		std::optional<uint32_t> width = std::nullopt, std::optional<uint32_t> height = std::nullopt);

	namespace commands
	{
		struct SetMesh { Mesh* mesh; };
		struct SetLight { Light light; };
		struct SetColorTexture { Texture* color_texture; };
		struct SetNormalTexture { Texture* normal_texture; };
		struct SetColor { glm::vec4 color; };
		struct SetProjectionMatrix { glm::mat4 projection_matrix; };
		struct SetViewMatrix { glm::mat4 view_matrix; };
		struct SetModelMatrix { glm::mat4 model_matrix; };
		struct SetCamera { Camera camera; std::optional<uint32_t> width; std::optional<uint32_t> height; };
		struct SetEyePosition { glm::vec3 eye_position; };
		struct SetMipmapBias { float mipmap_bias; };
		struct Callback { std::function<void()> func; };
		struct InsertSubcommands;
		struct Draw { std::optional<DrawCommand> draw_command; };
	}

	using Command = std::variant<
		commands::SetMesh,
		commands::SetLight,
		commands::SetColorTexture,
		commands::SetNormalTexture,
		commands::SetColor,
		commands::SetProjectionMatrix,
		commands::SetViewMatrix,
		commands::SetModelMatrix,
		commands::SetCamera,
		commands::SetEyePosition,
		commands::SetMipmapBias,
		commands::Callback,
		commands::InsertSubcommands,
		commands::Draw
	>;

	using Commands = std::vector<Command>;

	namespace commands
	{
		struct InsertSubcommands { Commands* subcommands; };
	}

	void SetMesh(Commands& cmds, const Mesh* mesh);
	void SetLight(Commands& cmds, Light light);
	void SetColorTexture(Commands& cmds, Texture* color_texture);
	void SetNormalTexture(Commands& cmds, Texture* normal_texture);
	void SetColor(Commands& cmds, glm::vec4 color);
	void SetProjectionMatrix(Commands& cmds, glm::mat4 projection_matrix);
	void SetViewMatrix(Commands& cmds, glm::mat4 view_matrix);
	void SetModelMatrix(Commands& cmds, glm::mat4 model_matrix);
	void SetCamera(Commands& cmds, Camera camera);
	void SetEyePosition(Commands& cmds, glm::vec3 eye_position);
	void SetMipmapBias(Commands& cmds, float mipmap_bias);
	void Callback(Commands& cmds, std::function<void()> func);
	void InsertSubcommands(Commands& cmds, const Commands* subcommands);
	void Draw(Commands& cmds, std::optional<DrawCommand> draw_command = std::nullopt);

	void ExecuteCommands(const Commands& cmds);
}