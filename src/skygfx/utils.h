#pragma once

#include "skygfx.h"
#include "vertex.h"

namespace skygfx::utils
{
	class Mesh
	{
	public:
		using Vertex = Vertex::PositionColorTextureNormal;
		using Index = uint32_t;
		using Vertices = std::vector<Vertex>;
		using Indices = std::vector<Index>;

	public:
		Mesh();
		Mesh(const Vertices& vertices);
		Mesh(const Vertices& vertices, const Indices& indices);

	public:
		auto getTopology() const { return mTopology; }
		void setTopology(Topology value) { mTopology = value; }

		void setVertices(const Vertex* memory, uint32_t count);
		void setVertices(const Vertices& value);

		void setIndices(const Index* memory, uint32_t count);
		void setIndices(const Indices& value);

		auto getVertexCount() const { return mVertexCount; }
		auto getIndexCount() const { return mIndexCount; }

	private:
		Topology mTopology = Topology::TriangleList;
		uint32_t mVertexCount = 0;
		uint32_t mIndexCount = 0;

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

	namespace effects
	{
		struct alignas(16) DirectionalLight
		{
			alignas(16) glm::vec3 direction = { 0.5f, 0.5f, 0.5f };
			alignas(16) glm::vec3 ambient = { 1.0f, 1.0f, 1.0f };
			alignas(16) glm::vec3 diffuse = { 1.0f, 1.0f, 1.0f };
			alignas(16) glm::vec3 specular = { 1.0f, 1.0f, 1.0f };
			float shininess = 32.0f;

			static const std::string Shader;
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
			float shininess = 32.0f;

			static const std::string Shader;
		};

		struct alignas(16) GaussianBlur
		{
			GaussianBlur(glm::vec2 direction);

			glm::vec2 direction;

			static const std::string Shader;
		};

		struct alignas(16) BloomDownsample
		{
			BloomDownsample(uint32_t step_number);

			glm::vec2 resolution;
			uint32_t step_number;

			static const std::string Shader;
		};

		struct alignas(16) BloomUpsample
		{
			static const std::string Shader;
		};

		struct alignas(16) BrightFilter
		{
			BrightFilter(float threshold);

			float threshold = 0.9f;

			static const std::string Shader;
		};

		struct alignas(16) Grayscale
		{
			float intensity = 1.0f;

			static const std::string Shader;
		};

		struct alignas(16) AlphaTest
		{
			float threshold = 0.0f;

			static const std::string Shader;
		};

		template <typename T>
		concept Effect = requires { T::Shader; } && std::is_same<std::remove_const_t<decltype(T::Shader)>, std::string>::value;
	}

	using Light = std::variant<
		effects::DirectionalLight,
		effects::PointLight
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

	Shader MakeEffectShader(const std::string& effect_shader_func);

	void EnsureDefaultShader();
	const Shader& GetDefaultShader();

	namespace commands
	{
		struct SetEffect
		{
			SetEffect(std::nullopt_t);
			SetEffect(Shader* shader, void* uniform_data, size_t uniform_size);

			template<effects::Effect T>
			SetEffect(T value)
			{
				static auto _shader = MakeEffectShader(T::Shader);
				shader = &_shader;
				uniform_data.resize(sizeof(T));
				std::memcpy(uniform_data.data(), &value, sizeof(T));
			}

			Shader* shader = nullptr;
			std::vector<uint8_t> uniform_data;
		};

		struct SetViewport
		{
			SetViewport(std::optional<Viewport> viewport);
			std::optional<Viewport> viewport;
		};

		struct SetScissor
		{
			SetScissor(std::optional<Scissor> scissor);
			std::optional<Scissor> scissor;
		};

		struct SetBlendMode
		{
			SetBlendMode(std::optional<BlendMode> blend_mode);
			std::optional<BlendMode> blend_mode;
		};

		struct SetSampler
		{
			SetSampler(Sampler sampler);
			Sampler sampler;
		};

		struct SetCullMode
		{
			SetCullMode(CullMode cull_mode);
			CullMode cull_mode;
		};

		struct SetTextureAddress
		{
			SetTextureAddress(TextureAddress texture_address);
			TextureAddress texture_address;
		};

		struct SetFrontFace
		{
			SetFrontFace(FrontFace front_face);
			FrontFace front_face;
		};

		struct SetDepthBias
		{
			SetDepthBias(std::optional<DepthBias> depth_bias);
			std::optional<DepthBias> depth_bias;
		};

		struct SetDepthMode
		{
			SetDepthMode(std::optional<DepthMode> depth_mode);
			std::optional<DepthMode> depth_mode;
		};

		struct SetStencilMode
		{
			SetStencilMode(std::optional<StencilMode> stencil_mode);
			std::optional<StencilMode> stencil_mode;
		};

		struct SetMesh
		{
			SetMesh(const Mesh* mesh);
			const Mesh* mesh;
		};

		struct SetColorTexture
		{
			SetColorTexture(const Texture* color_texture);
			const Texture* color_texture;
		};

		struct SetNormalTexture
		{
			SetNormalTexture(const Texture* normal_texture);
			const Texture* normal_texture;
		};

		struct SetColor
		{
			SetColor(glm::vec4 color);
			glm::vec4 color;
		};

		struct SetProjectionMatrix
		{
			SetProjectionMatrix(glm::mat4 projection_matrix);
			glm::mat4 projection_matrix;
		};

		struct SetViewMatrix
		{
			SetViewMatrix(glm::mat4 view_matrix);
			glm::mat4 view_matrix;
		};

		struct SetModelMatrix
		{
			SetModelMatrix(glm::mat4 model_matrix);
			glm::mat4 model_matrix;
		};

		struct SetCamera
		{
			SetCamera(Camera camera, std::optional<uint32_t> width = std::nullopt,
				std::optional<uint32_t> height = std::nullopt);
			Camera camera;
			std::optional<uint32_t> width;
			std::optional<uint32_t> height;
		};

		struct SetEyePosition
		{
			SetEyePosition(glm::vec3 eye_position);
			glm::vec3 eye_position;
		};

		struct SetMipmapBias
		{
			SetMipmapBias(float mipmap_bias);
			float mipmap_bias;
		};

		struct Callback
		{
			Callback(std::function<void()> func);
			std::function<void()> func;
		};

		struct Draw
		{
			Draw(std::optional<DrawCommand> draw_command = std::nullopt);
			std::optional<DrawCommand> draw_command;
		};

		struct Subcommands;
	}

	using Command = std::variant<
		commands::SetViewport,
		commands::SetScissor,
		commands::SetBlendMode,
		commands::SetSampler,
		commands::SetCullMode,
		commands::SetTextureAddress,
		commands::SetFrontFace,
		commands::SetDepthBias,
		commands::SetDepthMode,
		commands::SetStencilMode,
		commands::SetMesh,
		commands::SetEffect,
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
		commands::Subcommands,
		commands::Draw
	>;

	using Commands = std::vector<Command>;

	namespace commands
	{
		struct Subcommands
		{
			Subcommands(const Commands* subcommands);
			Subcommands(Commands&& subcommands);
			Subcommands(std::function<Commands()> subcommands);
			std::variant<const Commands*, Commands, std::function<Commands()>> subcommands;
		};
	}

	void AddCommands(Commands& cmdlist, Commands&& cmds);
	void ExecuteCommands(const Commands& cmds);

	namespace passes
	{
		void Blit(const Texture* src, const RenderTarget* dst, bool clear,
			Commands&& commands);

		void Blit(const Texture* src, const RenderTarget* dst = nullptr, bool clear = false,
			const std::optional<BlendMode>& blend_mode = std::nullopt, glm::vec4 color = { 1.0f, 1.0f, 1.0f, 1.0f });

		void Blit(const Texture* src, const RenderTarget* dst, effects::Effect auto effect, bool clear = false,
			const std::optional<BlendMode>& blend_mode = std::nullopt, glm::vec4 color = { 1.0f, 1.0f, 1.0f, 1.0f })
		{
			Blit(src, dst, clear, {
				commands::SetEffect(std::move(effect)),
				commands::SetBlendMode(std::move(blend_mode)),
				commands::SetColor(std::move(color))
			});
		}

		void GaussianBlur(const RenderTarget* src, const RenderTarget* dst = nullptr);
		void Grayscale(const RenderTarget* src, const RenderTarget* dst = nullptr, float intensity = 1.0f);
		void Bloom(const RenderTarget* src, const RenderTarget* dst = nullptr, float bright_threshold = 1.0f,
			float intensity = 2.0f);
		void BloomGaussian(const RenderTarget* src, const RenderTarget* dst = nullptr, float bright_threshold = 1.0f,
			float intensity = 2.0f);
	}

	struct Model
	{
		Mesh* mesh = nullptr;
		Texture* color_texture = nullptr;
		Texture* normal_texture = nullptr;
		glm::vec4 color = { 1.0f, 1.0f, 1.0f, 1.0f };
		std::optional<DrawCommand> draw_command = std::nullopt;
		glm::mat4 matrix = glm::mat4(1.0f);
		CullMode cull_mode = CullMode::None;
		TextureAddress texture_address = TextureAddress::Clamp;
		DepthMode depth_mode;
	};

	struct DrawSceneOptions
	{
		bool textures = true;
		bool normal_mapping = true;
	};

	void DrawScene(const Camera& camera, const std::vector<Model>& models, const std::vector<Light>& lights = {},
		const DrawSceneOptions& options = {});

	class MeshBuilder
	{
	public:
		enum class Mode
		{
			Points,
			Lines,
			LineLoop,
			LineStrip,
			Triangles,
			TriangleStrip,
			TriangleFan,
			Quads,
			Polygon
		};

	public:
		static Topology ConvertModeToTopology(Mode mode);

	public:
		void reset(bool reset_vertex = true);
		void begin(Mode mode);
		void vertex(const Vertex::PositionColorTextureNormal& vertex);
		void vertex(const Vertex::PositionColorTexture& vertex);
		void vertex(const Vertex::PositionColor& vertex);
		void vertex(const glm::vec3& pos);
		void vertex(const glm::vec2& pos);
		void color(const glm::vec4& value);
		void color(const glm::vec3& value);
		void normal(const glm::vec3& value);
		void texcoord(const glm::vec2& value);
		void end();

		void setToMesh(Mesh& mesh);

		bool isBeginAllowed(Mode mode) const;

	public:
		bool isBegan() const { return mBegan; }

		const auto& getVertices() const { return mVertices; }
		const auto& getIndices() const { return mIndices; }

		auto getVertexCount() const { return mVertexCount; }
		auto getIndexCount() const { return mIndexCount; }

		const auto& getTopology() const { return mTopology; }

	private:
		bool mBegan = false;
		std::optional<Mode> mMode;
		std::optional<Topology> mTopology;
		skygfx::utils::Mesh::Vertices mVertices;
		skygfx::utils::Mesh::Indices mIndices;
		uint32_t mVertexStart = 0;
		uint32_t mVertexCount = 0;
		uint32_t mIndexCount = 0;
		skygfx::utils::Mesh::Vertex mVertex;
	};
}
