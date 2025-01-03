#pragma once

#include "skygfx.h"
#include "vertex.h"
#include <typeindex>

namespace skygfx::utils
{
	class Mesh
	{
	public:
		using Vertex = vertex::PositionColorTextureNormalTangent;
		using Index = uint32_t;
		using Vertices = std::vector<Vertex>;
		using Indices = std::vector<Index>;

	public:
		Mesh() = default;
		Mesh(const Vertices& vertices);
		Mesh(const Vertices& vertices, const Indices& indices);

		void setVertices(const Vertex* memory, uint32_t count);
		void setVertices(const Vertices& value);

		void setIndices(const Index* memory, uint32_t count);
		void setIndices(const Indices& value);

		auto getVertexCount() const { return mVertexCount; }
		auto getIndexCount() const { return mIndexCount; }

		const auto& getVertexBuffer() const { return mVertexBuffer; }
		const auto& getIndexBuffer() const { return mIndexBuffer; }

	private:
		uint32_t mVertexCount = 0;
		uint32_t mIndexCount = 0;
		std::optional<VertexBuffer> mVertexBuffer;
		std::optional<IndexBuffer> mIndexBuffer;
	};

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
		struct BasicEffect
		{
			static const std::string VertexShaderCode;
			static const std::string FragmentShaderCode;
			static constexpr bool HasEffectUniform = false;
			static const std::vector<std::string> Defines;
		};

		struct alignas(16) BaseDirectionalLightEffect
		{
			BaseDirectionalLightEffect() = default;
			BaseDirectionalLightEffect(const utils::DirectionalLight& light);

			alignas(16) glm::vec3 direction = { 0.5f, 0.5f, 0.5f };
			alignas(16) glm::vec3 ambient = { 1.0f, 1.0f, 1.0f };
			alignas(16) glm::vec3 diffuse = { 1.0f, 1.0f, 1.0f };
			alignas(16) glm::vec3 specular = { 1.0f, 1.0f, 1.0f };
			float shininess = 32.0f;
		
			static constexpr bool HasEffectUniform = true;
			static constexpr uint32_t EffectUniformBinding = 3;
		};

		struct alignas(16) BasePointLightEffect
		{
			BasePointLightEffect() = default;
			BasePointLightEffect(const utils::PointLight& light);

			alignas(16) glm::vec3 position = { 0.0f, 0.0f, 0.0f };
			alignas(16) glm::vec3 ambient = { 1.0f, 1.0f, 1.0f };
			alignas(16) glm::vec3 diffuse = { 1.0f, 1.0f, 1.0f };
			alignas(16) glm::vec3 specular = { 1.0f, 1.0f, 1.0f };
			float constant_attenuation = 0.0f;
			float linear_attenuation = 0.00128f;
			float quadratic_attenuation = 0.0f;
			float shininess = 32.0f;

			static constexpr bool HasEffectUniform = true;
			static constexpr uint32_t EffectUniformBinding = 3;
		};

		namespace forward_shading
		{
			struct alignas(16) DirectionalLight : BaseDirectionalLightEffect
			{
				using BaseDirectionalLightEffect::BaseDirectionalLightEffect;

				static const std::string VertexShaderCode;
				static const std::string FragmentShaderCode;
				static const std::vector<std::string> Defines;
			};

			struct alignas(16) PointLight : BasePointLightEffect
			{
				using BasePointLightEffect::BasePointLightEffect;

				static const std::string VertexShaderCode;
				static const std::string FragmentShaderCode;
				static const std::vector<std::string> Defines;
			};
		}

		namespace deferred_shading
		{
			struct alignas(16) DirectionalLight : BaseDirectionalLightEffect
			{
				using BaseDirectionalLightEffect::BaseDirectionalLightEffect;

				static const std::string VertexShaderCode;
				static const std::string FragmentShaderCode;
				static const std::vector<std::string> Defines;
			};

			struct alignas(16) PointLight : BasePointLightEffect
			{
				using BasePointLightEffect::BasePointLightEffect;

				static const std::string VertexShaderCode;
				static const std::string FragmentShaderCode;
				static const std::vector<std::string> Defines;
			};

			struct alignas(16) ExtractGeometryBuffer
			{
				static const std::string VertexShaderCode;
				static const std::string FragmentShaderCode;
				static constexpr bool HasEffectUniform = false;
				static const std::vector<std::string> Defines;
			};
		}

		struct alignas(16) GaussianBlur
		{
			GaussianBlur(glm::vec2 direction);

			glm::vec2 direction;

			static const std::string VertexShaderCode;
			static const std::string FragmentShaderCode;
			static constexpr bool HasEffectUniform = true;
			static constexpr uint32_t EffectUniformBinding = 3;
			static const std::vector<std::string> Defines;
		};

		struct alignas(16) BloomDownsample
		{
			BloomDownsample(uint32_t step_number);

			glm::vec2 resolution;
			uint32_t step_number;

			static const std::string VertexShaderCode;
			static const std::string FragmentShaderCode;
			static constexpr bool HasEffectUniform = true;
			static constexpr uint32_t EffectUniformBinding = 3;
			static const std::vector<std::string> Defines;
		};

		struct alignas(16) BloomUpsample
		{
			static const std::string VertexShaderCode;
			static const std::string FragmentShaderCode;
			static constexpr bool HasEffectUniform = false;
			static const std::vector<std::string> Defines;
		};

		struct alignas(16) BrightFilter
		{
			BrightFilter(float threshold);

			float threshold = 0.9f;

			static const std::string VertexShaderCode;
			static const std::string FragmentShaderCode;
			static constexpr bool HasEffectUniform = true;
			static constexpr uint32_t EffectUniformBinding = 3;
			static const std::vector<std::string> Defines;
		};

		struct alignas(16) Grayscale
		{
			float intensity = 1.0f;

			static const std::string VertexShaderCode;
			static const std::string FragmentShaderCode;
			static constexpr bool HasEffectUniform = true;
			static constexpr uint32_t EffectUniformBinding = 3;
			static const std::vector<std::string> Defines;
		};

		struct alignas(16) AlphaTest
		{
			float threshold = 0.0f;

			static const std::string VertexShaderCode;
			static const std::string FragmentShaderCode;
			static constexpr bool HasEffectUniform = true;
			static constexpr uint32_t EffectUniformBinding = 3;
			static const std::vector<std::string> Defines;
		};

		struct alignas(16) GammaCorrection
		{
			float gamma = 2.2f;

			static const std::string VertexShaderCode;
			static const std::string FragmentShaderCode;
			static constexpr bool HasEffectUniform = true;
			static constexpr uint32_t EffectUniformBinding = 3;
			static const std::vector<std::string> Defines;
		};

		template <typename T>
		concept Effect = requires {
			{ T::VertexShaderCode } -> std::convertible_to<std::string>;
			{ T::FragmentShaderCode } -> std::convertible_to<std::string>;
			{ T::HasEffectUniform } -> std::convertible_to<bool>;
			{ T::Defines } -> std::convertible_to<std::vector<std::string>>;
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

	struct Context
	{
		Context();

		std::unordered_map<std::type_index, Shader> shaders;
		Mesh default_mesh;
		Texture white_pixel_texture;
	};

	Context& GetContext();
	void ClearContext();

	namespace commands
	{
		struct SetEffect
		{
			struct Uniform
			{
				uint32_t binding;
				std::vector<uint8_t> data;
			};

			SetEffect(std::nullopt_t);
			SetEffect(Shader* shader, uint32_t uniform_binding, void* uniform_data, size_t uniform_size);

			template<effects::Effect T>
			SetEffect(T value)
			{
				auto type_index = std::type_index(typeid(T));
				auto& context = GetContext();

				if (!context.shaders.contains(type_index))
					context.shaders.insert({ type_index, Shader(T::VertexShaderCode, T::FragmentShaderCode, T::Defines) });

				shader = &context.shaders.at(type_index);

				if constexpr (T::HasEffectUniform)
				{
					uniform.emplace();
					uniform->binding = T::EffectUniformBinding;
					uniform->data.resize(sizeof(T));
					std::memcpy(uniform->data.data(), &value, sizeof(T));
				}
			}

			Shader* shader = nullptr;
			std::optional<Uniform> uniform;
		};

		struct SetTopology
		{
			SetTopology(Topology topology);
			Topology topology;
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

		struct SetShader
		{
			SetShader(const Shader* shader);
			const Shader* shader;
		};

		struct SetVertexBuffer
		{
			SetVertexBuffer(const VertexBuffer* buffer);
			const VertexBuffer* buffer;
		};

		struct SetIndexBuffer
		{
			SetIndexBuffer(const IndexBuffer* buffer);
			const IndexBuffer* buffer;
		};

		struct SetUniformBuffer
		{
			SetUniformBuffer(uint32_t binding, const void* memory, size_t size);
			uint32_t binding;
			const void* memory;
			size_t size;
		};

		struct SetTexture
		{
			SetTexture(uint32_t binding, const Texture* texture);
			uint32_t binding;
			const Texture* texture;
		};

		struct Draw
		{
			Draw(uint32_t vertex_count, uint32_t vertex_offset = 0, uint32_t instance_count = 1);
			uint32_t vertex_count;
			uint32_t vertex_offset;
			uint32_t instance_count;
		};

		struct DrawIndexed
		{
			DrawIndexed(uint32_t index_count, uint32_t index_offset = 0, uint32_t instance_count = 1);
			uint32_t index_count;
			uint32_t index_offset;
			uint32_t instance_count;
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

		struct DrawMesh
		{
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

			DrawMesh(std::optional<DrawCommand> draw_command = std::nullopt);
			std::optional<DrawCommand> draw_command;
		};

		struct Subcommands;
	}

	using Command = std::variant<
		commands::SetTopology,
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
		commands::SetShader,
		commands::SetVertexBuffer,
		commands::SetIndexBuffer,
		commands::SetUniformBuffer,
		commands::SetTexture,
		commands::Draw,
		commands::DrawIndexed,
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
		commands::Subcommands,
		commands::DrawMesh
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
		std::vector<const RenderTarget*> targets;
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
		Topology topology = Topology::TriangleList;
		Mesh* mesh = nullptr;
		Texture* color_texture = nullptr;
		Texture* normal_texture = nullptr;
		glm::vec4 color = { 1.0f, 1.0f, 1.0f, 1.0f };
		std::optional<commands::DrawMesh::DrawCommand> draw_command = std::nullopt;
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
		void reset();
		void begin(Mode mode, std::function<void()> onFlush = nullptr);
		void vertex(const Mesh::Vertex& value);
		void end();
		void setToMesh(Mesh& mesh);

	public:
		bool isBegan() const { return mBegan; }
		const auto& getTopology() const { return mTopology; }
		const auto& getVertices() const { return mVertices; }
		const auto& getIndices() const { return mIndices; }

	private:
		bool mBegan = false;
		std::optional<Mode> mMode;
		std::optional<Topology> mTopology;
		Mesh::Vertices mVertices;
		Mesh::Indices mIndices;
		uint32_t mVertexStart = 0;
	};

	class Scratch
	{
	public:
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

	public:
		void begin(MeshBuilder::Mode mode, const State& state);
		void begin(MeshBuilder::Mode mode);
		void vertex(const Mesh::Vertex& value);
		void end();
		void flush(bool sort_textures = false);
		bool isBegan() const;

	private:
		void pushCommand();

	private:
		State mState;
		Mesh mMesh;
		MeshBuilder mMeshBuilder;

		struct ScratchCommand
		{
			State state;
			skygfx::Topology topology;
			uint32_t index_count;
			uint32_t index_offset;
		};

		std::vector<ScratchCommand> mScratchCommands;
		std::vector<Command> mCommands;
	};
}
