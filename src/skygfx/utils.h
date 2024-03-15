#pragma once

#include "skygfx.h"
#include "vertex.h"
#include <typeindex>

namespace skygfx::utils
{
	template<typename TVertex, typename TIndex>
	class MeshGeneric
	{
	public:
		using Vertex = TVertex;
		using Index = TIndex;
		using Vertices = std::vector<Vertex>;
		using Indices = std::vector<Index>;

	public:
		MeshGeneric()
		{
		}

		MeshGeneric(const Vertices& vertices)
		{
			setVertices(vertices);
		}

		MeshGeneric(const Vertices& vertices, const Indices& indices)
		{
			setVertices(vertices);
			setIndices(indices);
		}

	public:
		auto getTopology() const { return mTopology; }
		void setTopology(Topology value) { mTopology = value; }

		void setVertices(const Vertex* memory, uint32_t count)
		{
			mVertexCount = count;

			if (count == 0)
				return;

			size_t size = count * sizeof(Vertex);
			size_t stride = sizeof(Vertex);

			if (!mVertexBuffer.has_value() || mVertexBuffer.value().getSize() < size)
				mVertexBuffer.emplace(size, stride);

			mVertexBuffer.value().write(memory, count);
		}

		void setVertices(const Vertices& value)
		{
			setVertices(value.data(), static_cast<uint32_t>(value.size()));
		}

		void setIndices(const Index* memory, uint32_t count)
		{
			mIndexCount = count;

			if (count == 0)
				return;

			size_t size = count * sizeof(Index);
			size_t stride = sizeof(Index);

			if (!mIndexBuffer.has_value() || mIndexBuffer.value().getSize() < size)
				mIndexBuffer.emplace(size, stride);

			mIndexBuffer.value().write(memory, count);
		}

		void setIndices(const Indices& value)
		{
			setIndices(value.data(), static_cast<uint32_t>(value.size()));
		}

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

	using Mesh = MeshGeneric<vertex::PositionColorTextureNormalTangent, uint32_t>;

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
		void vertex(const vertex::PositionColorTextureNormalTangent& value);
		void vertex(const vertex::PositionColorTextureNormal& value);
		void vertex(const vertex::PositionColorTexture& value);
		void vertex(const vertex::PositionColor& value);
		void vertex(const glm::vec3& value);
		void vertex(const glm::vec2& value);
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
		Mesh::Vertices mVertices;
		Mesh::Indices mIndices;
		uint32_t mVertexStart = 0;
		uint32_t mVertexCount = 0;
		uint32_t mIndexCount = 0;
		Mesh::Vertex mVertex;
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

	struct DirectionalLight
	{
		glm::vec3 direction = { 0.5f, 0.5f, 0.5f };
		glm::vec3 ambient = { 1.0f, 1.0f, 1.0f };
		glm::vec3 diffuse = { 1.0f, 1.0f, 1.0f };
		glm::vec3 specular = { 1.0f, 1.0f, 1.0f };
		float shininess = 32.0f;
	};

	struct PointLight
	{
		glm::vec3 position = { 0.0f, 0.0f, 0.0f };
		glm::vec3 ambient = { 1.0f, 1.0f, 1.0f };
		glm::vec3 diffuse = { 1.0f, 1.0f, 1.0f };
		glm::vec3 specular = { 1.0f, 1.0f, 1.0f };
		float constant_attenuation = 0.0f;
		float linear_attenuation = 0.00128f;
		float quadratic_attenuation = 0.0f;
		float shininess = 32.0f;
	};

	using Light = std::variant<
		DirectionalLight,
		PointLight
	>;

	namespace effects
	{
		namespace forward_shading
		{
			struct alignas(16) DirectionalLight
			{
				DirectionalLight() = default;
				DirectionalLight(const utils::DirectionalLight& light);

				alignas(16) glm::vec3 direction = { 0.5f, 0.5f, 0.5f };
				alignas(16) glm::vec3 ambient = { 1.0f, 1.0f, 1.0f };
				alignas(16) glm::vec3 diffuse = { 1.0f, 1.0f, 1.0f };
				alignas(16) glm::vec3 specular = { 1.0f, 1.0f, 1.0f };
				float shininess = 32.0f;

				static const std::string Shader;
				static constexpr bool HasUniform = true;
			};

			struct alignas(16) PointLight
			{
				PointLight() = default;
				PointLight(const utils::PointLight& light);

				alignas(16) glm::vec3 position = { 0.0f, 0.0f, 0.0f };
				alignas(16) glm::vec3 ambient = { 1.0f, 1.0f, 1.0f };
				alignas(16) glm::vec3 diffuse = { 1.0f, 1.0f, 1.0f };
				alignas(16) glm::vec3 specular = { 1.0f, 1.0f, 1.0f };
				float constant_attenuation = 0.0f;
				float linear_attenuation = 0.00128f;
				float quadratic_attenuation = 0.0f;
				float shininess = 32.0f;

				static const std::string Shader;
				static constexpr bool HasUniform = true;
			};
		}

		namespace deferred_shading
		{
			struct alignas(16) DirectionalLight
			{
				DirectionalLight() = default;
				DirectionalLight(const utils::DirectionalLight& light);

				alignas(16) glm::vec3 direction = { 0.5f, 0.5f, 0.5f };
				alignas(16) glm::vec3 ambient = { 1.0f, 1.0f, 1.0f };
				alignas(16) glm::vec3 diffuse = { 1.0f, 1.0f, 1.0f };
				alignas(16) glm::vec3 specular = { 1.0f, 1.0f, 1.0f };
				float shininess = 32.0f;

				static const std::string Shader;
				static constexpr bool HasUniform = true;
			};

			struct alignas(16) PointLight
			{
				PointLight() = default;
				PointLight(const utils::PointLight& light);

				alignas(16) glm::vec3 position = { 0.0f, 0.0f, 0.0f };
				alignas(16) glm::vec3 ambient = { 1.0f, 1.0f, 1.0f };
				alignas(16) glm::vec3 diffuse = { 1.0f, 1.0f, 1.0f };
				alignas(16) glm::vec3 specular = { 1.0f, 1.0f, 1.0f };
				float constant_attenuation = 0.0f;
				float linear_attenuation = 0.00128f;
				float quadratic_attenuation = 0.0f;
				float shininess = 32.0f;

				static const std::string Shader;
				static constexpr bool HasUniform = true;
			};

			struct alignas(16) ExtractGeometryBuffer
			{
				static const std::string Shader;
				static constexpr bool HasUniform = false;
			};
		}

		struct alignas(16) GaussianBlur
		{
			GaussianBlur(glm::vec2 direction);

			glm::vec2 direction;

			static const std::string Shader;
			static constexpr bool HasUniform = true;
		};

		struct alignas(16) BloomDownsample
		{
			BloomDownsample(uint32_t step_number);

			glm::vec2 resolution;
			uint32_t step_number;

			static const std::string Shader;
			static constexpr bool HasUniform = true;
		};

		struct alignas(16) BloomUpsample
		{
			static const std::string Shader;
			static constexpr bool HasUniform = false;
		};

		struct alignas(16) BrightFilter
		{
			BrightFilter(float threshold);

			float threshold = 0.9f;

			static const std::string Shader;
			static constexpr bool HasUniform = true;
		};

		struct alignas(16) Grayscale
		{
			float intensity = 1.0f;

			static const std::string Shader;
			static constexpr bool HasUniform = true;
		};

		struct alignas(16) AlphaTest
		{
			float threshold = 0.0f;

			static const std::string Shader;
			static constexpr bool HasUniform = true;
		};

		struct alignas(16) GammaCorrection
		{
			float gamma = 2.2f;

			static const std::string Shader;
			static constexpr bool HasUniform = true;
		};

		template <typename T>
		concept Effect = requires {
			{ T::Shader } -> std::convertible_to<std::string>;
			{ T::HasUniform } -> std::convertible_to<bool>;
		};
	}

	struct OrthogonalCamera
	{
		std::optional<uint32_t> width = std::nullopt;
		std::optional<uint32_t> height = std::nullopt;
	};

	struct PerspectiveCamera
	{
		std::optional<uint32_t> width = std::nullopt;
		std::optional<uint32_t> height = std::nullopt;

		float yaw = 0.0f;
		float pitch = 0.0f;
		glm::vec3 position = { 0.0f, 0.0f, 0.0f };
		glm::vec3 world_up = { 0.0f, 1.0f, 0.0f };
		float far_plane = 8192.0f;
		float near_plane = 1.0f;
		float fov = 70.0f;
	};

	using Camera = std::variant<OrthogonalCamera, PerspectiveCamera>;

	std::tuple<glm::mat4/*proj*/, glm::mat4/*view*/> MakeCameraMatrices(const OrthogonalCamera& camera);
	std::tuple<glm::mat4/*proj*/, glm::mat4/*view*/> MakeCameraMatrices(const PerspectiveCamera& camera);

	Shader MakeEffectShader(const std::string& effect_shader_func);

	class MeshBuilder;

	namespace scratch
	{
		struct State
		{
			Texture* texture = nullptr;
			Sampler sampler = Sampler::Linear;
			TextureAddress texaddr = TextureAddress::Clamp;
			CullMode cull_mode = CullMode::None;
			FrontFace front_face = FrontFace::Clockwise;
			float mipmap_bias = 0.0f;

			std::optional<Scissor> scissor;
			std::optional<Viewport> viewport;
			std::optional<BlendMode> blend_mode;
			std::optional<DepthMode> depth_mode;
			std::optional<StencilMode> stencil_mode;
			std::optional<DepthBias> depth_bias;
			std::optional<float> alpha_test_threshold;

			glm::mat4 projection_matrix = glm::mat4(1.0f);
			glm::mat4 view_matrix = glm::mat4(1.0f);
			glm::mat4 model_matrix = glm::mat4(1.0f);

			bool operator==(const State& other) const = default;
		};
	}

	struct Context
	{
		Context();

		Shader default_shader;
		std::unordered_map<std::type_index, Shader> shaders;
		Mesh default_mesh;
		Texture white_pixel_texture;

		struct {
			scratch::State state;
			Mesh mesh;
			MeshBuilder mesh_builder;
		} scratch;
	};

	Context& GetContext();
	void ClearContext();

	namespace commands
	{
		struct SetEffect
		{
			SetEffect(std::nullopt_t);
			SetEffect(Shader* shader, void* uniform_data, size_t uniform_size);

			template<effects::Effect T>
			SetEffect(T value)
			{
				auto type_index = std::type_index(typeid(T));
				auto& context = GetContext();

				if (!context.shaders.contains(type_index))
					context.shaders.insert({ type_index, MakeEffectShader(T::Shader) });

				shader = &context.shaders.at(type_index);

				if (T::HasUniform)
				{
					uniform_data.emplace();
					uniform_data.value().resize(sizeof(T));
					std::memcpy(uniform_data.value().data(), &value, sizeof(T));
				}
			}

			Shader* shader = nullptr;
			std::optional<std::vector<uint8_t>> uniform_data;
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

		struct SetCustomTexture
		{
			SetCustomTexture(uint32_t binding, const Texture* texture);
			uint32_t binding;
			const Texture* texture;
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
			SetCamera(Camera camera);
			Camera camera;
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
		commands::SetCustomTexture,
		commands::SetColorTexture,
		commands::SetNormalTexture,
		commands::SetColor,
		commands::SetProjectionMatrix,
		commands::SetViewMatrix,
		commands::SetModelMatrix,
		commands::SetCamera,
		commands::SetEyePosition,
		commands::SetMipmapBias,
		commands::Subcommands,
		commands::Draw
	>;

	namespace commands
	{
		struct Subcommands
		{
			Subcommands(const std::vector<Command>* subcommands);
			const std::vector<Command>* subcommands;
		};
	}

	void ExecuteCommands(const std::vector<Command>& cmds);

	struct RenderPass
	{
		std::vector<RenderTarget*> targets;
		bool clear = false;
		struct {
			std::optional<glm::vec4> color = glm::vec4{ 0.0f, 0.0f, 0.0f, 1.0f };
			std::optional<float> depth = 1.0f;
			std::optional<uint8_t> stencil = 0;
		} clear_value;
		std::vector<Command> commands;
	};

	void ExecuteRenderPass(const RenderPass& render_pass);

	namespace passes
	{
		struct BlitOptions
		{
			bool clear = false;
			Sampler sampler = Sampler::Linear;
			glm::vec4 color = { 1.0f, 1.0f, 1.0f, 1.0f };
			std::optional<BlendMode> blend_mode;
			std::optional<commands::SetEffect> effect;
		};

		void Blit(Texture* src, RenderTarget* dst, const BlitOptions& options = {});

		void GaussianBlur(RenderTarget* src, RenderTarget* dst = nullptr);
		void Grayscale(RenderTarget* src, RenderTarget* dst = nullptr, float intensity = 1.0f);
		void Bloom(RenderTarget* src, RenderTarget* dst = nullptr, float bright_threshold = 1.0f,
			float intensity = 2.0f);
		void BloomGaussian(RenderTarget* src, RenderTarget* dst = nullptr, float bright_threshold = 1.0f,
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
		Sampler sampler = Sampler::Linear;

		static std::vector<Command> Draw(const Model& model, bool use_color_texture = true,
			bool use_normal_texture = true);
	};

	struct DrawSceneOptions
	{
		enum class Technique
		{
			ForwardShading,
			DeferredShading
		};

		struct GrayscalePosteffect
		{
			float intensity = 1.0f;
		};

		struct BloomPosteffect
		{
			float threshold = 1.0f;
			float intensity = 2.0f;
		};

		struct GaussianBlurPosteffect
		{
		};

		using Posteffect = std::variant<
			BloomPosteffect,
			GrayscalePosteffect,
			GaussianBlurPosteffect
		>;

		Technique technique = Technique::DeferredShading;
		bool use_color_textures = true;
		bool use_normal_textures = true;
		bool clear_target = true;
		float mipmap_bias = 0.0f;
		std::vector<Posteffect> posteffects;
	};

	void DrawScene(RenderTarget* target, const PerspectiveCamera& camera,
		const std::vector<Model>& models, const std::vector<Light>& lights = {},
		const DrawSceneOptions& options = {});

	class StageViewer
	{
	public:
		virtual void stage(const std::string& name, Texture* texture) = 0;
	};

	void SetStageViewer(StageViewer* value);
	void ViewStage(const std::string& name, Texture* texture);

	namespace scratch
	{
		void Begin(MeshBuilder::Mode mode, const State& state = {});
		void Vertex(const vertex::PositionColorTextureNormal& value);
		void Vertex(const vertex::PositionColorTexture& value);
		void Vertex(const vertex::PositionColor& value);
		void Vertex(const glm::vec3& value);
		void Vertex(const glm::vec2& value);
		void Color(const glm::vec4& value);
		void Color(const glm::vec3& value);
		void Normal(const glm::vec3& value);
		void TexCoord(const glm::vec2& value);
		void End();
		void Flush();
	}
}
