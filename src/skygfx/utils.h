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
			glm::vec2 direction;
			glm::vec2 resolution;

			static const std::string Shader;
		};

		struct alignas(16) BloomDownsample
		{
			BloomDownsample(const glm::vec2& resolution, uint32_t step_number);
			BloomDownsample(const Texture& texture, uint32_t step_number);

			glm::vec2 resolution;
			uint32_t step_number;

			static const std::string Shader;
		};

		struct alignas(16) BloomUpsample
		{
			BloomUpsample(const glm::vec2& resolution);
			BloomUpsample(const Texture& texture);

			glm::vec2 resolution;

			static const std::string Shader;
		};

		struct alignas(16) BrightFilter
		{
			float threshold = 0.9f;

			static const std::string Shader;
		};

		struct alignas(16) Grayscale
		{
			float intensity = 1.0f;

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
			SetEffect() {}

			template<effects::Effect T>
			SetEffect(T value) {
				static auto _shader = MakeEffectShader(T::Shader);
				shader = &_shader;
				uniform_data.insert(uniform_data.end(), reinterpret_cast<uint8_t*>(&value), 
					reinterpret_cast<uint8_t*>(&value) + sizeof(T));
			}
			Shader* shader = nullptr;
			std::vector<uint8_t> uniform_data;
		};

		struct SetBlendMode { std::optional<BlendMode> blend_mode; };
		struct SetSampler { Sampler sampler = Sampler::Linear; };
		struct SetMesh { const Mesh* mesh; };
		struct SetColorTexture { const Texture* color_texture; };
		struct SetNormalTexture { const Texture* normal_texture; };
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
		commands::SetBlendMode,
		commands::SetSampler,
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
		commands::InsertSubcommands,
		commands::Draw
	>;

	using Commands = std::vector<Command>;

	namespace commands
	{
		struct InsertSubcommands { Commands* subcommands; };
	}

	void SetBlendMode(Commands& cmds, std::optional<BlendMode> blend_mode);
	void SetSampler(Commands& cmds, Sampler sampler);
	void SetMesh(Commands& cmds, const Mesh* mesh);

	template<class T>
	concept EffectCommandConstructible = std::is_constructible_v<commands::SetEffect, T>;

	void SetEffect(Commands& cmds, EffectCommandConstructible auto effect)
	{
		cmds.push_back(commands::SetEffect{ std::move(effect) });
	}

	void SetColorTexture(Commands& cmds, const Texture* color_texture);
	void SetNormalTexture(Commands& cmds, const Texture* normal_texture);
	void SetColor(Commands& cmds, glm::vec4 color);
	void SetProjectionMatrix(Commands& cmds, glm::mat4 projection_matrix);
	void SetViewMatrix(Commands& cmds, glm::mat4 view_matrix);
	void SetModelMatrix(Commands& cmds, glm::mat4 model_matrix);
	void SetCamera(Commands& cmds, Camera camera, std::optional<uint32_t> width = std::nullopt,
		std::optional<uint32_t> height = std::nullopt);
	void SetEyePosition(Commands& cmds, glm::vec3 eye_position);
	void SetMipmapBias(Commands& cmds, float mipmap_bias);
	void Callback(Commands& cmds, std::function<void()> func);
	void InsertSubcommands(Commands& cmds, const Commands* subcommands);
	void Draw(Commands& cmds, std::optional<DrawCommand> draw_command = std::nullopt);

	void ExecuteCommands(const Commands& cmds);

	namespace passes
	{
		class Pass
		{
		public:
			virtual void execute(const RenderTarget& src, const RenderTarget& dst) = 0;
		};

		class GaussianBlur : public Pass
		{
		public:
			void execute(const RenderTarget& src, const RenderTarget& dst) override;

		private:
			std::optional<RenderTarget> mBlurTarget;
		};

		class Bloom : public Pass
		{
		public:
			void execute(const RenderTarget& src, const RenderTarget& dst) override;

		public:
			auto getIntensity() const { return mIntensity; }
			void setIntensity(float value) { mIntensity = value; }

			auto getBrightThreshold() const { return mBrightThreshold; }
			void setBrightThreshold(float value) { mBrightThreshold = value; }

		private:
			std::optional<RenderTarget> mBrightTarget;
			std::vector<RenderTarget> mTexChain;
			std::optional<glm::u32vec2> mPrevSize;
			float mBrightThreshold = 1.0f;
			float mIntensity = 2.0f;
		};

		class Grayscale : public Pass
		{
		public:
			void execute(const RenderTarget& src, const RenderTarget& dst) override;
		};
	}
}
