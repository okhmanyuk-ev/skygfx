#include "utils.h"
#include <ranges>

using namespace skygfx;

const std::string vertex_shader_code = R"(
#version 450 core

layout(location = POSITION_LOCATION) in vec3 aPosition;
layout(location = COLOR_LOCATION) in vec4 aColor;
layout(location = TEXCOORD_LOCATION) in vec2 aTexCoord;
layout(location = NORMAL_LOCATION) in vec3 aNormal;

layout(binding = SETTINGS_UNIFORM_BINDING) uniform _settings
{
	mat4 projection;
	mat4 view;
	mat4 model;
	vec3 eye_position;
	float mipmap_bias;
	vec4 color;
} settings;

layout(location = 0) out struct
{
	vec3 frag_position;
	vec4 color;
	vec2 tex_coord;
	vec3 normal;
} Out;

out gl_PerVertex { vec4 gl_Position; };

void main()
{
	Out.frag_position = vec3(settings.model * vec4(aPosition, 1.0));
	Out.normal = mat3(transpose(inverse(settings.model))) * aNormal;
	Out.color = aColor;
	Out.tex_coord = aTexCoord;
#ifdef FLIP_TEXCOORD_Y
	Out.tex_coord.y = 1.0 - Out.tex_coord.y;
#endif
	gl_Position = settings.projection * settings.view * settings.model * vec4(aPosition, 1.0);
})";

const std::string fragment_shader_code = R"(
#version 450 core

layout(binding = SETTINGS_UNIFORM_BINDING) uniform _settings
{
	mat4 projection;
	mat4 view;
	mat4 model;
	vec3 eye_position;
	float mipmap_bias;
	vec4 color;
} settings;

layout(location = 0) out vec4 result;

layout(location = 0) in struct
{
	vec3 frag_position;
	vec4 color;
	vec2 tex_coord;
	vec3 normal;
} In;

layout(binding = COLOR_TEXTURE_BINDING) uniform sampler2D sColorTexture;
layout(binding = NORMAL_TEXTURE_BINDING) uniform sampler2D sNormalTexture;

#ifdef EFFECT_FUNC
void EFFECT_FUNC(inout vec4);
#endif

void main()
{
	result = In.color;
	result *= settings.color;
	result *= texture(sColorTexture, In.tex_coord, settings.mipmap_bias);
#ifdef EFFECT_FUNC
	EFFECT_FUNC(result);
#endif
})";

const std::string utils::effects::DirectionalLight::Shader = R"(
layout(binding = EFFECT_UNIFORM_BINDING) uniform _light
{
	vec3 direction;
	vec3 ambient;
	vec3 diffuse;
	vec3 specular;
	float shininess;
} light;

void effect(inout vec4 result)
{
	vec3 normal = normalize(In.normal * vec3(texture(sNormalTexture, In.tex_coord, settings.mipmap_bias)));
	
	vec3 view_dir = normalize(settings.eye_position - In.frag_position);
	vec3 light_dir = normalize(light.direction);

	float diff = max(dot(normal, -light_dir), 0.0);
	vec3 reflect_dir = reflect(light_dir, normal);
	float spec = pow(max(dot(view_dir, reflect_dir), 0.0), light.shininess);

	vec3 intensity = light.ambient + (light.diffuse * diff) + (light.specular * spec);

	result *= vec4(intensity, 1.0);
})";

const std::string utils::effects::PointLight::Shader = R"(
layout(binding = EFFECT_UNIFORM_BINDING) uniform _light
{
	vec3 position;
	vec3 ambient;
	vec3 diffuse;
	vec3 specular;
	float constant_attenuation;
	float linear_attenuation;
	float quadratic_attenuation;
	float shininess;
} light;

void effect(inout vec4 result)
{
	vec3 normal = normalize(In.normal * vec3(texture(sNormalTexture, In.tex_coord, settings.mipmap_bias)));

	vec3 light_offset = light.position - In.frag_position;

	float distance = length(light_offset);
	float linear_attn = light.linear_attenuation * distance;
	float quadratic_attn = light.quadratic_attenuation * (distance * distance);
	float attenuation = 1.0 / (light.constant_attenuation + linear_attn + quadratic_attn);

	vec3 light_dir = normalize(light_offset);
	float diff = max(dot(normal, light_dir), 0.0);
	vec3 reflect_dir = reflect(-light_dir, normal);
	vec3 view_dir = normalize(settings.eye_position - In.frag_position);
	float spec = pow(max(dot(view_dir, reflect_dir), 0.0), light.shininess);

	vec3 intensity = light.ambient + (light.diffuse * diff) + (light.specular * spec);

	intensity *= attenuation;

	result *= vec4(intensity, 1.0);
})";

const std::string utils::effects::GaussianBlur::Shader = R"(
layout(binding = EFFECT_UNIFORM_BINDING) uniform _blur
{
	vec2 direction;
	vec2 resolution;
} blur;

void effect(inout vec4 result)
{
	result = vec4(0.0);

	vec2 off1 = vec2(1.3846153846) * blur.direction / blur.resolution;
	vec2 off2 = vec2(3.2307692308) * blur.direction / blur.resolution;

	result += texture(sColorTexture, In.tex_coord) * 0.2270270270;

	result += texture(sColorTexture, In.tex_coord + off1) * 0.3162162162;
	result += texture(sColorTexture, In.tex_coord - off1) * 0.3162162162;

	result += texture(sColorTexture, In.tex_coord + off2) * 0.0702702703;
	result += texture(sColorTexture, In.tex_coord - off2) * 0.0702702703;
})";

const std::string utils::effects::BrightFilter::Shader = R"(
layout(binding = EFFECT_UNIFORM_BINDING) uniform _bright
{
	float threshold;
} bright;

void effect(inout vec4 result)
{
	float luminance = dot(vec3(0.2125, 0.7154, 0.0721), result.xyz);
	luminance = max(0.0, luminance - bright.threshold);
	result *= sign(luminance);
})";

const std::string utils::effects::Grayscale::Shader = R"(
layout(binding = EFFECT_UNIFORM_BINDING) uniform _grayscale
{
	float intensity;
} grayscale;

void effect(inout vec4 result)
{
	float gray = dot(result.rgb, vec3(0.299, 0.587, 0.114));
	result.rgb = mix(result.rgb, vec3(gray), grayscale.intensity);
})";


utils::Mesh::Mesh()
{
}

utils::Mesh::Mesh(Vertices vertices)
{
	setVertices(std::move(vertices));
}

utils::Mesh::Mesh(Vertices vertices, Indices indices)
{
	setVertices(std::move(vertices));
	setIndices(std::move(indices));
}

void utils::Mesh::setVertices(Vertices value)
{
	if (value.empty())
		return;

	size_t size = value.size() * sizeof(Vertices::value_type);
	size_t stride = sizeof(Vertices::value_type);

	if (!mVertexBuffer.has_value() || mVertexBuffer.value().getSize() < size)
		mVertexBuffer.emplace(size, stride);

	mVertexBuffer.value().write(value);
	mVertices = std::move(value);
}

void utils::Mesh::setIndices(Indices value)
{
	if (value.empty())
		return;

	size_t size = value.size() * sizeof(Indices::value_type);
	size_t stride = sizeof(Indices::value_type);

	if (!mIndexBuffer.has_value() || mIndexBuffer.value().getSize() < size)
		mIndexBuffer.emplace(size, stride);

	mIndexBuffer.value().write(value);
	mIndices = std::move(value);
}

std::tuple<glm::mat4/*proj*/, glm::mat4/*view*/, glm::vec3/*eye_pos*/> utils::MakeCameraMatrices(const Camera& camera,
	std::optional<uint32_t> _width, std::optional<uint32_t> _height)
{
	auto width = (float)_width.value_or(GetBackbufferWidth());
	auto height = (float)_height.value_or(GetBackbufferHeight());

	return std::visit(cases{
		[&](const OrthogonalCamera& camera) {
			auto proj = glm::orthoLH(0.0f, width, height, 0.0f, -1.0f, 1.0f);
			auto view = glm::lookAtLH(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f), 
				glm::vec3(0.0f, 1.0f, 0.0f));
			auto eye_pos = glm::vec3{ 0.0f, 0.0f, 0.0f };
			return std::make_tuple(proj, view, eye_pos);
		},
		[&](const PerspectiveCamera& camera) {
			auto sin_yaw = glm::sin(camera.yaw);
			auto sin_pitch = glm::sin(camera.pitch);

			auto cos_yaw = glm::cos(camera.yaw);
			auto cos_pitch = glm::cos(camera.pitch);

			auto front = glm::normalize(glm::vec3(cos_yaw * cos_pitch, sin_pitch, sin_yaw * cos_pitch));
			auto right = glm::normalize(glm::cross(front, camera.world_up));
			auto up = glm::normalize(glm::cross(right, front));

			auto proj = glm::perspectiveFov(camera.fov, width, height, camera.near_plane, camera.far_plane);
			auto view = glm::lookAtRH(camera.position, camera.position + front, up);

			return std::make_tuple(proj, view, camera.position);
		}
	}, camera);
}

Shader utils::MakeEffectShader(const std::string& effect_shader_func)
{
	return Shader(Mesh::Vertex::Layout, vertex_shader_code, fragment_shader_code + effect_shader_func, {
		"COLOR_TEXTURE_BINDING 0",
		"NORMAL_TEXTURE_BINDING 1",
		"SETTINGS_UNIFORM_BINDING 2",
		"EFFECT_UNIFORM_BINDING 3",
		"EFFECT_FUNC effect"
	});
}

void utils::SetBlendMode(Commands& cmds, BlendMode blend_mode)
{
	cmds.push_back(commands::SetBlendMode{
		.blend_mode = std::move(blend_mode)
	});
}

void utils::SetSampler(Commands& cmds, Sampler sampler)
{
	cmds.push_back(commands::SetSampler{
		.sampler = sampler
	});
}

void utils::SetMesh(Commands& cmds, const Mesh* mesh)
{
	cmds.push_back(commands::SetMesh{
		.mesh = mesh
	});
}

void utils::SetColorTexture(Commands& cmds, const Texture* color_texture)
{
	cmds.push_back(commands::SetColorTexture{
		.color_texture = color_texture
	});
}

void utils::SetNormalTexture(Commands& cmds, const Texture* normal_texture)
{
	cmds.push_back(commands::SetNormalTexture{
		.normal_texture = normal_texture
	});
}

void utils::SetColor(Commands& cmds, glm::vec4 color)
{
	cmds.push_back(commands::SetColor{
		.color = std::move(color)
	});
}

void utils::SetProjectionMatrix(Commands& cmds, glm::mat4 projection_matrix)
{
	cmds.push_back(commands::SetProjectionMatrix{
		.projection_matrix = std::move(projection_matrix)
	});
}

void utils::SetViewMatrix(Commands& cmds, glm::mat4 view_matrix)
{
	cmds.push_back(commands::SetViewMatrix{
		.view_matrix = std::move(view_matrix)
	});
}

void utils::SetModelMatrix(Commands& cmds, glm::mat4 model_matrix)
{
	cmds.push_back(commands::SetModelMatrix{
		.model_matrix = std::move(model_matrix)
	});
}

void utils::SetCamera(Commands& cmds, Camera camera, std::optional<uint32_t> width,
		std::optional<uint32_t> height)
{
	cmds.push_back(commands::SetCamera{
		.camera = std::move(camera),
		.width = width,
		.height = height
	});
}

void utils::SetEyePosition(Commands& cmds, glm::vec3 eye_position)
{
	cmds.push_back(commands::SetEyePosition{
		.eye_position = std::move(eye_position)
	});
}

void utils::SetMipmapBias(Commands& cmds, float mipmap_bias)
{
	cmds.push_back(commands::SetMipmapBias{
		.mipmap_bias = mipmap_bias
	});
}

void utils::Callback(Commands& cmds, std::function<void()> func)
{
	cmds.push_back(commands::Callback{
		.func = func
	});
}

void utils::InsertSubcommands(Commands& cmds, const Commands* subcommands)
{
	cmds.push_back(commands::InsertSubcommands{
		.subcommands = const_cast<Commands*>(subcommands)
	});
}

void utils::Draw(Commands& cmds, std::optional<DrawCommand> draw_command)
{
	cmds.push_back(commands::Draw{
		.draw_command = std::move(draw_command)
	});
}

void utils::ExecuteCommands(const Commands& cmds)
{
	static auto default_mesh = Mesh({
		{ { -1.0f, -1.0f, 0.0f }, { 1.0f, 1.0f, 1.0f, 1.0f }, { 0.0f, 1.0f } },
		{ { -1.0f,  1.0f, 0.0f }, { 1.0f, 1.0f, 1.0f, 1.0f }, { 0.0f, 0.0f } },
		{ {  1.0f,  1.0f, 0.0f }, { 1.0f, 1.0f, 1.0f, 1.0f }, { 1.0f, 0.0f } },
		{ {  1.0f, -1.0f, 0.0f }, { 1.0f, 1.0f, 1.0f, 1.0f }, { 1.0f, 1.0f } },
	}, { 0, 1, 2, 0, 2, 3 });

	auto blend_mode = BlendStates::NonPremultiplied;
	bool blend_mode_dirty = true;

	auto sampler = Sampler::Linear;
	bool sampler_dirty = true;

	Mesh* mesh = nullptr;
	bool mesh_dirty = true;

	Shader* shader = nullptr;
	bool shader_dirty = true;

	std::vector<uint8_t>* uniform_data = nullptr;
	bool uniform_dirty = true;

	Texture* color_texture = nullptr;
	Texture* normal_texture = nullptr;
	bool textures_dirty = true;

	struct alignas(16) Settings
	{
		glm::mat4 projection = glm::mat4(1.0f);
		glm::mat4 view = glm::mat4(1.0f);
		glm::mat4 model = glm::mat4(1.0f);
		alignas(16) glm::vec3 eye_position = { 0.0f, 0.0f, 0.0f };
		float mipmap_bias = 0.0f;
		alignas(16) glm::vec4 color = { 1.0f, 1.0f, 1.0f, 1.0f };
	} settings;

	bool settings_dirty = true;

	std::function<void(const Command&)> execute_command = [&](const Command& _cmd) {
		std::visit(cases{
			[&](const commands::SetBlendMode& cmd) {
				if (blend_mode == cmd.blend_mode)
					return;

				blend_mode = cmd.blend_mode;
				blend_mode_dirty = true;
			},
			[&](const commands::SetSampler& cmd) {
				if (sampler == cmd.sampler)
					return;

				sampler = cmd.sampler;
				sampler_dirty = true;
			},
			[&](const commands::SetMesh& cmd) {
				if (mesh == cmd.mesh)
					return;

				mesh = const_cast<Mesh*>(cmd.mesh);
				mesh_dirty = true;
			},
			[&](const commands::SetEffect& cmd) {
				shader = cmd.shader;
				if (shader)
					uniform_data = const_cast<std::vector<uint8_t>*>(&cmd.uniform_data);
				else
					uniform_data = nullptr;

				shader_dirty = true;
				uniform_dirty = true;
			},
			[&](const commands::SetColorTexture& cmd) {
				if (color_texture == cmd.color_texture)
					return;

				color_texture = const_cast<Texture*>(cmd.color_texture);
				textures_dirty = true;
			},
			[&](const commands::SetNormalTexture& cmd) {
				if (normal_texture == cmd.normal_texture)
					return;
				
				normal_texture = const_cast<Texture*>(cmd.normal_texture);
				textures_dirty = true;
			},
			[&](const commands::SetColor& cmd) {
				settings.color = cmd.color;
				settings_dirty = true;
			},
			[&](const commands::SetProjectionMatrix& cmd) {
				settings.projection = cmd.projection_matrix;
				settings_dirty = true;
			},
			[&](const commands::SetViewMatrix& cmd) {
				settings.view = cmd.view_matrix;
				settings_dirty = true;
			},
			[&](const commands::SetModelMatrix& cmd) {
				settings.model = cmd.model_matrix;
				settings_dirty = true;
			},
			[&](const commands::SetCamera& cmd) {
				std::tie(settings.projection, settings.view, settings.eye_position) = MakeCameraMatrices(cmd.camera, cmd.height, cmd.width);
				settings_dirty = true;
			},
			[&](const commands::SetEyePosition& cmd) {
				settings.eye_position = cmd.eye_position;
				settings_dirty = true;
			},
			[&](const commands::SetMipmapBias& cmd) {
				settings.mipmap_bias = cmd.mipmap_bias;
				settings_dirty = true;
			},
			[&](const commands::Callback& cmd) {
				cmd.func();
			},
			[&](const commands::InsertSubcommands& cmd) {
				for (const auto& subcommand : *cmd.subcommands)
				{
					execute_command(subcommand);
				}
			},
			[&](const commands::Draw& cmd) {
				if (blend_mode_dirty)
				{
					SetBlendMode(blend_mode);
					blend_mode_dirty = false;
				}

				if (sampler_dirty)
				{
					SetSampler(sampler);
					sampler_dirty = false;
				}

				if (mesh == nullptr)
					mesh = &default_mesh;

				if (mesh_dirty)
				{
					auto topology = mesh->getTopology();
					const auto& vertex_buffer = mesh->getVertexBuffer();
					const auto& index_buffer = mesh->getIndexBuffer();

					SetTopology(topology);
					
					if (vertex_buffer.has_value())
						SetVertexBuffer(vertex_buffer.value());
					
					if (index_buffer.has_value())
						SetIndexBuffer(index_buffer.value());

					mesh_dirty = false;
				}

				if (shader_dirty)
				{
					static auto default_shader = Shader(Mesh::Vertex::Layout, vertex_shader_code, fragment_shader_code, {
						"COLOR_TEXTURE_BINDING 0",
						"NORMAL_TEXTURE_BINDING 1",
						"SETTINGS_UNIFORM_BINDING 2"
					});

					SetShader(shader == nullptr ? default_shader : *shader);
					shader_dirty = false;
				}

				if (uniform_dirty)
				{
					if (uniform_data != nullptr)
					{
						SetUniformBuffer(3, uniform_data->data(), uniform_data->size());
					}
					uniform_dirty = false;
				}

				if (textures_dirty)
				{
					uint32_t white_pixel = 0xFFFFFFFF;
					static auto white_pixel_texture = Texture(1, 1, 4, &white_pixel);

					const auto& _color_texture = color_texture != nullptr ? *color_texture : white_pixel_texture;
					const auto& _normal_texture = normal_texture != nullptr ? *normal_texture : white_pixel_texture;

					SetTexture(0, _color_texture);
					SetTexture(1, _normal_texture);
					
					textures_dirty = false;
				}

				if (settings_dirty)
				{
					SetUniformBuffer(2, settings);
					settings_dirty = false;
				}

				auto draw_command = cmd.draw_command;
			
				if (!draw_command.has_value())
				{
					if (mesh->getIndices().empty())
						draw_command = DrawVerticesCommand{};
					else
						draw_command = DrawIndexedVerticesCommand{};
				}

				std::visit(cases{
					[&](const DrawVerticesCommand& draw) {
						const auto& vertices = mesh->getVertices();
						auto vertex_count = draw.vertex_count.value_or(static_cast<uint32_t>(vertices.size()));
						auto vertex_offset = draw.vertex_offset;

						::Draw(vertex_count, vertex_offset);
					},
					[&](const DrawIndexedVerticesCommand& draw) {
						const auto& indices = mesh->getIndices();			
						auto index_count = draw.index_count.value_or(static_cast<uint32_t>(indices.size()));
						auto index_offset = draw.index_offset;

						DrawIndexed(index_count, index_offset);
					}
				}, draw_command.value());
			}
		}, _cmd);
	};
	
	for (const auto& cmd : cmds)
	{
		execute_command(cmd);
	}
}

void utils::passes::GaussianBlur::execute(const RenderTarget& src, const RenderTarget& dst)
{
	if (!mBlurTarget.has_value() || mBlurTarget.value().getWidth() != src.getWidth() || mBlurTarget.value().getHeight() != src.getHeight())
		mBlurTarget.emplace(src.getWidth(), src.getHeight());

	auto resolution = glm::vec2{ static_cast<float>(src.getWidth()), static_cast<float>(src.getHeight()) };

	SetRenderTarget(mBlurTarget.value());
	Clear(glm::vec4{ 0.0f, 0.0f, 0.0f, 1.0f });

	ExecuteCommands({
		commands::SetBlendMode{ BlendStates::AlphaBlend },
		commands::SetColorTexture{ &src },
		commands::SetEffect{
			effects::GaussianBlur{
				.direction = { 1.0f, 0.0f },
				.resolution = resolution
			}
		},
		commands::Draw{},
		commands::SetEffect{
			effects::GaussianBlur{
				.direction = { 0.0f, 1.0f },
				.resolution = resolution
			}
		},
		commands::Callback{ [&] {
			SetRenderTarget(dst);		
		} },
		commands::SetColorTexture{ &mBlurTarget.value() },
		commands::Draw{}
	});
}

void utils::passes::BrightFilter::execute(const RenderTarget& src, const RenderTarget& dst)
{
	SetRenderTarget(dst);

	ExecuteCommands({
		commands::SetBlendMode{ BlendStates::AlphaBlend },
		commands::SetColorTexture{ &src },
		commands::SetEffect{ effects::BrightFilter{
			.threshold = mThreshold
		} },
		commands::Draw{},
	});
}

void utils::passes::Bloom::execute(const RenderTarget& src, const RenderTarget& dst)
{
	glm::u32vec2 src_size = { src.getWidth(), src.getHeight() };

	constexpr int ChainSize = 8;

	if (!mPrevSize.has_value() || mPrevSize != src_size)
	{
		mPrevSize = src_size;

		mBrightTarget.emplace(src_size.x, src_size.y);
		mTexChain.clear();

		for (int i = 0; i < ChainSize; i++)
		{
			auto w = src_size.x >> i;
			auto h = src_size.y >> i;

			mTexChain.emplace_back(w, h);
		}
	}
	
	SetRenderTarget(mBrightTarget.value());
	Clear(glm::vec4{ 0.0f, 0.0f, 0.0f, 1.0f });

	mBrightFilter.execute(src, mBrightTarget.value());

	// downsampling

	Texture* prev_texture = &mBrightTarget.value();

	for (auto& tex_chain_cell : mTexChain)
	{
		SetRenderTarget(tex_chain_cell.downsampled);
		Clear(glm::vec4{ 0.0f, 0.0f, 0.0f, 1.0f }); 
		ExecuteCommands({
			commands::SetBlendMode{ BlendStates::AlphaBlend },
			commands::SetColorTexture{ prev_texture },
			commands::Draw{}
		});

		prev_texture = &tex_chain_cell.downsampled;
	}

	// upsample and blur

	Texture* prev_blurred_target = nullptr;

	for (auto& tex_chain_cell : std::ranges::reverse_view(mTexChain))
	{
		if (prev_blurred_target != nullptr)
		{
			SetRenderTarget(tex_chain_cell.downsampled);
			ExecuteCommands({
				commands::SetBlendMode{ BlendStates::Additive },
				commands::SetColorTexture{ prev_blurred_target },
				commands::Draw{}
			});
		}

		SetRenderTarget(tex_chain_cell.blurred);
		Clear(glm::vec4{ 0.0f, 0.0f, 0.0f, 1.0f });
		tex_chain_cell.gaussian_blur.execute(tex_chain_cell.downsampled, tex_chain_cell.blurred);
		prev_blurred_target = &tex_chain_cell.blurred;
	}

	// combine

	SetRenderTarget(dst);

	ExecuteCommands({
		commands::SetColorTexture{ &src },
		commands::Draw{},
		commands::SetBlendMode{ BlendStates::Additive },
		commands::SetColorTexture{ prev_blurred_target },
		commands::SetColor{ glm::vec4(mIntensity) },
		commands::Draw{}
	});
}

void utils::passes::Grayscale::execute(const RenderTarget& src, const RenderTarget& dst)
{
	SetRenderTarget(dst);

	ExecuteCommands({
		commands::SetColorTexture{ &src },
		commands::SetEffect{ effects::Grayscale{} },
		commands::Draw{},
	});
}
