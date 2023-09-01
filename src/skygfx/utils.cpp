#include "utils.h"
#include <ranges>
#include <array>

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
	vec2 resolution;
	vec2 direction;
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

const std::string utils::effects::BloomDownsample::Shader = R"(
layout(binding = EFFECT_UNIFORM_BINDING) uniform _downsample
{
	vec2 resolution;
	uint step_number;
} downsample;

vec3 getSample(sampler2D srcSampler, const vec2 uv)
{
	return texture(srcSampler, uv, 0).rgb;
}

float getLuminance(vec3 c)
{
	return 0.2125 * c.r + 0.7154 * c.g + 0.0721 * c.b;
}

float getKarisWeight(const vec3 box4x4)
{
	return 1.0 / (1.0 + getLuminance(box4x4));
}

vec3 downsample13tap(sampler2D srcSampler, const vec2 centerUV)
{
	const vec2 pixelSize = vec2(1.0) / downsample.resolution;

	const vec3 taps[] = 
	{
		getSample(srcSampler, centerUV + vec2(-2,-2) * pixelSize),
		getSample(srcSampler, centerUV + vec2( 0,-2) * pixelSize),
		getSample(srcSampler, centerUV + vec2( 2,-2) * pixelSize),

		getSample(srcSampler, centerUV + vec2(-1,-1) * pixelSize),
		getSample(srcSampler, centerUV + vec2( 1,-1) * pixelSize),

		getSample(srcSampler, centerUV + vec2(-2, 0) * pixelSize),
		getSample(srcSampler, centerUV + vec2( 0, 0) * pixelSize),
		getSample(srcSampler, centerUV + vec2( 2, 0) * pixelSize),

		getSample(srcSampler, centerUV + vec2(-1, 1) * pixelSize),
		getSample(srcSampler, centerUV + vec2( 1, 1) * pixelSize),

		getSample(srcSampler, centerUV + vec2(-2, 2) * pixelSize),
		getSample(srcSampler, centerUV + vec2( 0, 2) * pixelSize),
		getSample(srcSampler, centerUV + vec2( 2, 2) * pixelSize),
	};

	// on the first downsample use Karis average

	if (downsample.step_number == 0)
	{
		const vec3 box[] =
		{
			0.25 * (taps[3] + taps[4] + taps[8]  + taps[9]), 
			0.25 * (taps[0] + taps[1] + taps[5]  + taps[6]), 
			0.25 * (taps[1] + taps[2] + taps[6]  + taps[7]), 
			0.25 * (taps[5] + taps[6] + taps[10] + taps[11]), 
			0.25 * (taps[6] + taps[7] + taps[11] + taps[12]), 
		};

		// weight by partial Karis average to reduce fireflies
		return 
			0.5   * getKarisWeight(box[0]) * box[0] + 
			0.125 * getKarisWeight(box[1]) * box[1] + 
			0.125 * getKarisWeight(box[2]) * box[2] + 
			0.125 * getKarisWeight(box[3]) * box[3] + 
			0.125 * getKarisWeight(box[4]) * box[4];
	}
	else
	{
		return 
			0.5   * (0.25 * (taps[3] + taps[4] + taps[8]  + taps[9]))  + 
			0.125 * (0.25 * (taps[0] + taps[1] + taps[5]  + taps[6]))  + 
			0.125 * (0.25 * (taps[1] + taps[2] + taps[6]  + taps[7]))  + 
			0.125 * (0.25 * (taps[5] + taps[6] + taps[10] + taps[11])) + 
			0.125 * (0.25 * (taps[6] + taps[7] + taps[11] + taps[12]));
	}
}

void effect(inout vec4 result)
{
	result = vec4(downsample13tap(sColorTexture, In.tex_coord), 1.0);
})";

const std::string utils::effects::BloomUpsample::Shader = R"(
layout(binding = EFFECT_UNIFORM_BINDING) uniform _upsample
{
	vec2 resolution;
} upsample;

void effect(inout vec4 result)
{
	const vec2 pixelSize = vec2(1.0) / upsample.resolution;

	const vec2 offsets[] = 
	{
		vec2(-1,-1), vec2(0,-1), vec2(1,-1),
		vec2(-1, 0), vec2(0, 0), vec2(1, 0),
		vec2(-1, 1), vec2(0, 1), vec2(1, 1),
	};

	const float weights[] = 
	{
		1.0 / 16.0, 2.0 / 16.0, 1.0 / 16.0,
		2.0 / 16.0, 4.0 / 16.0, 2.0 / 16.0,
		1.0 / 16.0, 2.0 / 16.0, 1.0 / 16.0,
	};

	vec3 r = vec3(0.0);

	for (int i = 0; i < 9; i++)
	{
		r += weights[i] * texture(sColorTexture, In.tex_coord + offsets[i] * pixelSize).rgb;
	}

	result = vec4(r, 1.0) * settings.color;
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

const std::string utils::effects::AlphaTest::Shader = R"(
layout(binding = EFFECT_UNIFORM_BINDING) uniform _alphatest
{
	float threshold;
} alphatest;

void effect(inout vec4 result)
{
	if (result.a <= alphatest.threshold)
		discard;
})";

static std::optional<skygfx::Shader> gDefaultShader;

utils::Mesh::Mesh()
{
}

utils::Mesh::Mesh(const Vertices& vertices)
{
	setVertices(vertices);
}

utils::Mesh::Mesh(const Vertices& vertices, const Indices& indices)
{
	setVertices(vertices);
	setIndices(indices);
}

void utils::Mesh::setVertices(const Vertex* memory, uint32_t count)
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

void utils::Mesh::setVertices(const Vertices& value)
{
	setVertices(value.data(), static_cast<uint32_t>(value.size()));
}

void utils::Mesh::setIndices(const Index* memory, uint32_t count)
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

void utils::Mesh::setIndices(const Indices& value)
{
	setIndices(value.data(), static_cast<uint32_t>(value.size()));
}

utils::effects::GaussianBlur::GaussianBlur(glm::vec2 _resolution, glm::vec2 _direction) :
	resolution(std::move(_resolution)),
	direction(std::move(_direction))
{
}

utils::effects::GaussianBlur::GaussianBlur(const Texture& texture, glm::vec2 _direction) :
	GaussianBlur({ static_cast<float>(texture.getWidth()), static_cast<float>(texture.getHeight()) }, std::move(_direction))
{
}

utils::effects::BloomDownsample::BloomDownsample(glm::vec2 _resolution, uint32_t _step_number) :
	resolution(std::move(_resolution)),
	step_number(_step_number)
{
}

utils::effects::BloomDownsample::BloomDownsample(const Texture& texture, uint32_t _step_number) :
	BloomDownsample({ static_cast<float>(texture.getWidth()), static_cast<float>(texture.getHeight()) }, _step_number)
{
}

utils::effects::BloomUpsample::BloomUpsample(const glm::vec2& _resolution) :
	resolution(_resolution)
{
}

utils::effects::BloomUpsample::BloomUpsample(const Texture& texture) :
	BloomUpsample({ static_cast<float>(texture.getWidth()), static_cast<float>(texture.getHeight()) })
{
}

utils::effects::BrightFilter::BrightFilter(float _threshold) :
	threshold(_threshold)
{
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

void utils::EnsureDefaultShader()
{
	if (gDefaultShader.has_value())
		return;

	gDefaultShader.emplace(Mesh::Vertex::Layout, vertex_shader_code, fragment_shader_code, std::vector<std::string>{
		"COLOR_TEXTURE_BINDING 0",
		"NORMAL_TEXTURE_BINDING 1",
		"SETTINGS_UNIFORM_BINDING 2"
	});
}

const Shader& utils::GetDefaultShader()
{
	EnsureDefaultShader();
	return gDefaultShader.value();
}

// commands

utils::commands::SetViewport::SetViewport(std::optional<Viewport> _viewport) :
	viewport(std::move(_viewport))
{
}

utils::commands::SetScissor::SetScissor(std::optional<Scissor> _scissor) :
	scissor(_scissor)
{
}

utils::commands::SetBlendMode::SetBlendMode(std::optional<BlendMode> _blend_mode) :
	blend_mode(std::move(_blend_mode))
{
}

utils::commands::SetSampler::SetSampler(Sampler _sampler) :
	sampler(_sampler)
{
}

utils::commands::SetCullMode::SetCullMode(CullMode _cull_mode) :
	cull_mode(_cull_mode)
{
}

utils::commands::SetTextureAddress::SetTextureAddress(TextureAddress _texture_address) :
	texture_address(_texture_address)
{
}

utils::commands::SetDepthMode::SetDepthMode(std::optional<DepthMode> _depth_mode) :
	depth_mode(_depth_mode)
{
}

utils::commands::SetMesh::SetMesh(const Mesh* _mesh) :
	mesh(_mesh)
{
}

utils::commands::SetColorTexture::SetColorTexture(const Texture* _color_texture) :
	color_texture(_color_texture)
{
}

utils::commands::SetNormalTexture::SetNormalTexture(const Texture* _normal_texture) :
	normal_texture(_normal_texture)
{
}

utils::commands::SetColor::SetColor(glm::vec4 _color) :
	color(std::move(_color))
{
}

utils::commands::SetProjectionMatrix::SetProjectionMatrix(glm::mat4 _projection_matrix) :
	projection_matrix(std::move(_projection_matrix))
{
}

utils::commands::SetViewMatrix::SetViewMatrix(glm::mat4 _view_matrix) :
	view_matrix(std::move(_view_matrix))
{
}

utils::commands::SetModelMatrix::SetModelMatrix(glm::mat4 _model_matrix) :
	model_matrix(std::move(_model_matrix))
{
}

utils::commands::SetCamera::SetCamera(Camera _camera, std::optional<uint32_t> _width, std::optional<uint32_t> _height) :
	camera(std::move(_camera)), width(_width), height(_height)
{
}

utils::commands::SetEyePosition::SetEyePosition(glm::vec3 _eye_position) :
	eye_position(std::move(_eye_position))
{
}

utils::commands::SetMipmapBias::SetMipmapBias(float _mipmap_bias) :
	mipmap_bias(_mipmap_bias)
{
}

utils::commands::Callback::Callback(std::function<void()> _func) :
	func(_func)
{
}

utils::commands::Subcommands::Subcommands(const Commands* _subcommands) :
	subcommands(_subcommands)
{
}

utils::commands::Subcommands::Subcommands(Commands&& _subcommands) :
	subcommands(_subcommands)
{
}

utils::commands::Subcommands::Subcommands(std::function<Commands()> _subcommands) :
	subcommands(_subcommands)
{
}

utils::commands::Draw::Draw(std::optional<DrawCommand> _draw_command) :
	draw_command(std::move(_draw_command))
{
}

void utils::AddCommands(Commands& cmdlist, Commands&& cmds)
{
	for (auto&& cmd : cmds)
	{
		cmdlist.push_back(std::move(cmd));
	}
}

void utils::ExecuteCommands(const Commands& cmds)
{
	static const auto default_mesh = Mesh({
		{ { -1.0f, -1.0f, 0.0f }, { 1.0f, 1.0f, 1.0f, 1.0f }, { 0.0f, 1.0f } },
		{ { -1.0f,  1.0f, 0.0f }, { 1.0f, 1.0f, 1.0f, 1.0f }, { 0.0f, 0.0f } },
		{ {  1.0f,  1.0f, 0.0f }, { 1.0f, 1.0f, 1.0f, 1.0f }, { 1.0f, 0.0f } },
		{ {  1.0f, -1.0f, 0.0f }, { 1.0f, 1.0f, 1.0f, 1.0f }, { 1.0f, 1.0f } },
	}, { 0, 1, 2, 0, 2, 3 });

	std::optional<Viewport> viewport;
	bool viewport_dirty = true;

	std::optional<Scissor> scissor;
	bool scissor_dirty = true;

	std::optional<BlendMode> blend_mode;
	bool blend_mode_dirty = true;

	auto sampler = Sampler::Linear;
	bool sampler_dirty = true;

	auto cull_mode = CullMode::None;
	bool cull_mode_dirty = true;

	auto texture_address = TextureAddress::Clamp;
	bool texture_address_dirty = true;

	std::optional<DepthMode> depth_mode;
	bool depth_mode_dirty = true;

	const Mesh* mesh = nullptr;
	bool mesh_dirty = true;

	Shader* shader = nullptr;
	bool shader_dirty = true;

	const std::vector<uint8_t>* uniform_data = nullptr;
	bool uniform_dirty = true;

	const Texture* color_texture = nullptr;
	const Texture* normal_texture = nullptr;
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
			[&](const commands::SetViewport& cmd) {
				if (viewport == cmd.viewport)
					return;

				viewport = cmd.viewport;
				viewport_dirty = true;
			},
			[&](const commands::SetScissor& cmd) {
				if (scissor == cmd.scissor)
					return;

				scissor = cmd.scissor;
				scissor_dirty = true;
			},
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
			[&](const commands::SetCullMode& cmd) {
				if (cull_mode == cmd.cull_mode)
					return;

				cull_mode = cmd.cull_mode;
				cull_mode_dirty = true;
			},
			[&](const commands::SetTextureAddress& cmd) {
				if (texture_address == cmd.texture_address)
					return;

				texture_address = cmd.texture_address;
				texture_address_dirty = true;
			},
			[&](const commands::SetDepthMode& cmd) {
				if (depth_mode == cmd.depth_mode)
					return;

				depth_mode = cmd.depth_mode;
				depth_mode_dirty = true;
			},
			[&](const commands::SetMesh& cmd) {
				if (mesh == cmd.mesh)
					return;

				mesh = cmd.mesh;
				mesh_dirty = true;
			},
			[&](const commands::SetEffect& cmd) {
				shader = cmd.shader;
				if (shader)
					uniform_data = &cmd.uniform_data;
				else
					uniform_data = nullptr;

				shader_dirty = true;
				uniform_dirty = true;
			},
			[&](const commands::SetColorTexture& cmd) {
				if (color_texture == cmd.color_texture)
					return;

				color_texture = cmd.color_texture;
				textures_dirty = true;
			},
			[&](const commands::SetNormalTexture& cmd) {
				if (normal_texture == cmd.normal_texture)
					return;
				
				normal_texture = cmd.normal_texture;
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
			[&](const commands::Subcommands& cmd) {
				std::visit(cases{
					[&](const Commands* subcommands) {
						for (const auto& subcommand : *subcommands)
						{
							execute_command(subcommand);
						}
					},
					[&](const Commands& subcommands) {
						for (const auto& subcommand : subcommands)
						{
							execute_command(subcommand);
						}
					},
					[&](const std::function<Commands()>& subcommands) {
						for (const auto& subcommand : subcommands())
						{
							execute_command(subcommand);
						}
					}
				}, cmd.subcommands);
			},
			[&](const commands::Draw& cmd) {
				if (viewport_dirty)
				{
					SetViewport(viewport);
					viewport_dirty = false;
				}

				if (scissor_dirty)
				{
					SetScissor(scissor);
					scissor_dirty = false;
				}

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

				if (cull_mode_dirty)
				{
					SetCullMode(cull_mode);
					cull_mode_dirty = false;
				}

				if (texture_address_dirty)
				{
					SetTextureAddress(texture_address);
					texture_address_dirty = false;
				}

				if (depth_mode_dirty)
				{
					SetDepthMode(depth_mode);
					depth_mode_dirty = false;
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
					SetShader(shader == nullptr ? GetDefaultShader() : *shader);
					shader_dirty = false;
				}

				if (uniform_dirty)
				{
					if (uniform_data != nullptr)
					{
						SetUniformBuffer(3, (void*)uniform_data->data(), uniform_data->size());
					}
					uniform_dirty = false;
				}

				if (textures_dirty)
				{
					uint32_t white_pixel = 0xFFFFFFFF;
					static const auto white_pixel_texture = Texture(1, 1, skygfx::Format::Byte4, &white_pixel);

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
					if (mesh->getIndexCount() == 0)
						draw_command = DrawVerticesCommand{};
					else
						draw_command = DrawIndexedVerticesCommand{};
				}

				std::visit(cases{
					[&](const DrawVerticesCommand& draw) {
						auto vertex_count = draw.vertex_count.value_or(mesh->getVertexCount());
						auto vertex_offset = draw.vertex_offset;

						::Draw(vertex_count, vertex_offset);
					},
					[&](const DrawIndexedVerticesCommand& draw) {
						auto index_count = draw.index_count.value_or(mesh->getIndexCount());
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

void utils::passes::Blit(const Texture* src, const RenderTarget* dst, bool clear, Commands&& commands)
{
	if (dst == nullptr)
		SetRenderTarget(std::nullopt);
	else
		SetRenderTarget(*dst);

	if (clear)
		Clear();

	AddCommands(commands, {
		commands::SetColorTexture(src),
		commands::Draw()
	});
	ExecuteCommands(commands);
}

void utils::passes::Blit(const Texture* src, const RenderTarget* dst, bool clear,
	const std::optional<BlendMode>& blend_mode, glm::vec4 color)
{
	Blit(src, dst, clear, {
		commands::SetBlendMode(std::move(blend_mode)),
		commands::SetColor(std::move(color))
	});
}

void utils::passes::GaussianBlur(const RenderTarget* src, const RenderTarget* dst)
{
	auto blur_target = skygfx::GetTemporaryRenderTarget(src->getWidth(), src->getHeight());
	Blit(src, blur_target, effects::GaussianBlur(*src, { 1.0f, 0.0f }), true);
	Blit(blur_target, dst, effects::GaussianBlur(*src, { 0.0f, 1.0f }));
	skygfx::ReleaseTemporaryRenderTarget(blur_target);
}

void utils::passes::Grayscale(const RenderTarget* src, const RenderTarget* dst, float intensity)
{
	Blit(src, dst, effects::Grayscale{ intensity });
}

void utils::passes::Bloom(const RenderTarget* src, const RenderTarget* dst, float bright_threshold, float intensity)
{
	Blit(src, dst);

	if (intensity <= 0.0f)
		return;

	constexpr int ChainSize = 8;

	// get targets

	auto bright = skygfx::GetTemporaryRenderTarget(src->getWidth(), src->getHeight());

	std::array<RenderTarget*, ChainSize> tex_chain;

	for (int i = 0; i < ChainSize; i++)
	{
		auto w = src->getWidth() >> (i + 1);
		auto h = src->getHeight() >> (i + 1);

		w = glm::max(w, 1u);
		h = glm::max(h, 1u);

		tex_chain[i] = skygfx::GetTemporaryRenderTarget(w, h);
	}

	// extract bright

	auto downsample_src = src;

	if (bright_threshold > 0.0f)
	{
		Blit(src, bright, effects::BrightFilter(bright_threshold), true);
		downsample_src = bright;
	}

	// downsample

	uint32_t step_number = 0;

	for (auto target : tex_chain)
	{
		Blit(downsample_src, target, effects::BloomDownsample(*downsample_src, step_number));
		downsample_src = target;
		step_number += 1;
	}

	// upsample and blur

	Texture* prev_downsampled = nullptr;

	for (auto it = tex_chain.rbegin(); it != tex_chain.rend(); ++it)
	{
		if (prev_downsampled != nullptr)
		{
			Blit(prev_downsampled, *it, effects::BloomUpsample(*prev_downsampled), false, BlendStates::Additive);
		}

		prev_downsampled = *it;
	}

	// apply

	Blit(prev_downsampled, dst, effects::BloomUpsample(*prev_downsampled), false, BlendStates::Additive, glm::vec4(intensity));

	// release targets

	skygfx::ReleaseTemporaryRenderTarget(bright);

	for (auto target : tex_chain)
	{
		skygfx::ReleaseTemporaryRenderTarget(target);
	}
}

void utils::passes::BloomGaussian(const RenderTarget* src, const RenderTarget* dst, float bright_threshold,
	float intensity)
{
	Blit(src, dst);

	if (intensity <= 0.0f)
		return;

	constexpr auto DownsampleCount = 8;

	auto width = static_cast<uint32_t>(glm::floor(static_cast<float>(src->getWidth()) / static_cast<float>(DownsampleCount)));
	auto height = static_cast<uint32_t>(glm::floor(static_cast<float>(src->getHeight()) / static_cast<float>(DownsampleCount)));

	auto bright = skygfx::GetTemporaryRenderTarget(width, height);
	auto blur_dst = skygfx::GetTemporaryRenderTarget(width, height);
	auto blur_src = src;

	if (bright_threshold > 0.0f)
	{
		Blit(src, bright, effects::BrightFilter(bright_threshold), true);
		blur_src = bright;
	}

	GaussianBlur(blur_src, blur_dst);

	Blit(blur_dst, dst, false, BlendStates::Additive, glm::vec4(intensity));

	skygfx::ReleaseTemporaryRenderTarget(bright);
	skygfx::ReleaseTemporaryRenderTarget(blur_dst);
}

void utils::DrawScene(const Camera& camera, const std::vector<Model>& models, const std::vector<Light>& lights)
{
	assert(!models.empty());

	Commands model_cmds;

	for (const auto& model : models)
	{
		AddCommands(model_cmds, {
			commands::SetMesh(model.mesh),
			commands::SetModelMatrix(model.matrix),
			commands::SetColorTexture(model.color_texture),
			commands::SetNormalTexture(model.normal_texture),
			commands::SetCullMode(model.cull_mode),
			commands::SetTextureAddress(model.texture_address),
			commands::SetDepthMode(model.depth_mode),
			commands::SetColor(model.color),
			commands::Draw(model.draw_command)
		});
	}

	Commands cmds = {
		commands::SetCamera(camera),
	};

	if (lights.empty())
	{
		AddCommands(cmds, {
			commands::Subcommands(&model_cmds)
		});
		ExecuteCommands(cmds);
		return;
	}

	bool first_light_done = false;

	for (const auto& light : lights)
	{
		std::visit(cases{
			[&](const auto& value) {
				AddCommands(cmds, {
					commands::SetEffect(value)
				});
			}
		}, light);

		AddCommands(cmds, {
			commands::Subcommands(&model_cmds)
		});

		if (!first_light_done)
		{
			first_light_done = true;
			AddCommands(cmds, {
				commands::SetBlendMode(skygfx::BlendStates::Additive)
			});
		}
	}

	ExecuteCommands(cmds);
}
