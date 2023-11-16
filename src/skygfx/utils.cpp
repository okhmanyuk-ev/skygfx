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
#ifdef EFFECT_FUNC
	EFFECT_FUNC(result);
#else
	result = In.color;
	result *= settings.color;
	result *= texture(sColorTexture, In.tex_coord, settings.mipmap_bias);
#endif
})";

const std::string utils::effects::forward_shading::DirectionalLight::Shader = R"(
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
	result = In.color;
	result *= settings.color;
	result *= texture(sColorTexture, In.tex_coord, settings.mipmap_bias);

	vec3 normal = normalize(In.normal * vec3(texture(sNormalTexture, In.tex_coord, settings.mipmap_bias)));

	vec3 view_dir = normalize(settings.eye_position - In.frag_position);
	vec3 light_dir = normalize(light.direction);

	float diff = max(dot(normal, -light_dir), 0.0);
	vec3 reflect_dir = reflect(light_dir, normal);
	float spec = pow(max(dot(view_dir, reflect_dir), 0.0), light.shininess);

	vec3 intensity = light.ambient + (light.diffuse * diff) + (light.specular * spec);

	result *= vec4(intensity, 1.0);
})";

const std::string utils::effects::forward_shading::PointLight::Shader = R"(
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
	result = In.color;
	result *= settings.color;
	result *= texture(sColorTexture, In.tex_coord, settings.mipmap_bias);

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

const std::string utils::effects::deferred_shading::DirectionalLight::Shader = R"(
layout(binding = EFFECT_UNIFORM_BINDING) uniform _light
{
	vec3 direction;
	vec3 ambient;
	vec3 diffuse;
	vec3 specular;
	float shininess;
} light;

layout(binding = 5) uniform sampler2D sColorBufferTexture;
layout(binding = 6) uniform sampler2D sNormalBufferTexture;
layout(binding = 7) uniform sampler2D sPositionsBufferTexture;

void effect(inout vec4 result)
{
	result = In.color;
	result *= settings.color;
	result *= texture(sColorBufferTexture, In.tex_coord);

	vec3 pixel_normal = vec3(texture(sNormalBufferTexture, In.tex_coord));
	vec3 pixel_position = vec3(texture(sPositionsBufferTexture, In.tex_coord));

	vec3 view_dir = normalize(settings.eye_position - pixel_position);
	vec3 light_dir = normalize(light.direction);

	float diff = max(dot(pixel_normal, -light_dir), 0.0);
	vec3 reflect_dir = reflect(light_dir, pixel_normal);
	float spec = pow(max(dot(view_dir, reflect_dir), 0.0), light.shininess);

	vec3 intensity = light.ambient + (light.diffuse * diff) + (light.specular * spec);

	result *= vec4(intensity, 1.0);
})";

const std::string utils::effects::deferred_shading::PointLight::Shader = R"(
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

layout(binding = 5) uniform sampler2D sColorBufferTexture;
layout(binding = 6) uniform sampler2D sNormalBufferTexture;
layout(binding = 7) uniform sampler2D sPositionsBufferTexture;

void effect(inout vec4 result)
{
	result = In.color;
	result *= settings.color;
	result *= texture(sColorBufferTexture, In.tex_coord);

	vec3 pixel_normal = vec3(texture(sNormalBufferTexture, In.tex_coord));
	vec3 pixel_position = vec3(texture(sPositionsBufferTexture, In.tex_coord));

	vec3 light_offset = light.position - pixel_position;

	float distance = length(light_offset);
	float linear_attn = light.linear_attenuation * distance;
	float quadratic_attn = light.quadratic_attenuation * (distance * distance);
	float attenuation = 1.0 / (light.constant_attenuation + linear_attn + quadratic_attn);

	vec3 light_dir = normalize(light_offset);
	float diff = max(dot(pixel_normal, light_dir), 0.0);
	vec3 reflect_dir = reflect(-light_dir, pixel_normal);
	vec3 view_dir = normalize(settings.eye_position - pixel_position);
	float spec = pow(max(dot(view_dir, reflect_dir), 0.0), light.shininess);

	vec3 intensity = light.ambient + (light.diffuse * diff) + (light.specular * spec);

	intensity *= attenuation;

	result *= vec4(intensity, 1.0);
})";

const std::string utils::effects::deferred_shading::ExtractGeometryBuffer::Shader = R"(
layout(binding = EFFECT_UNIFORM_BINDING) uniform _effect_settings
{
	float unused;
} effect_settings;

//layout(location = 0) out vec4 result; // color_buffer
layout(location = 1) out vec4 normal_buffer;
layout(location = 2) out vec4 positions_buffer;

void effect(inout vec4 result)
{
	result = In.color;
	result *= settings.color;
	result *= texture(sColorTexture, In.tex_coord, settings.mipmap_bias);

	vec3 normal = normalize(In.normal * vec3(texture(sNormalTexture, In.tex_coord, settings.mipmap_bias)));
	normal_buffer = vec4(normal, 1.0);
	positions_buffer = vec4(In.frag_position, 1.0);
})";

const std::string utils::effects::GaussianBlur::Shader = R"(
layout(binding = EFFECT_UNIFORM_BINDING) uniform _blur
{
	vec2 direction;
} blur;

void effect(inout vec4 result)
{
	result = vec4(0.0);

	vec2 tex_size = textureSize(sColorTexture, 0);
	vec2 off1 = vec2(1.3846153846) * blur.direction / tex_size;
	vec2 off2 = vec2(3.2307692308) * blur.direction / tex_size;

	result += texture(sColorTexture, In.tex_coord) * 0.2270270270;

	result += texture(sColorTexture, In.tex_coord + off1) * 0.3162162162;
	result += texture(sColorTexture, In.tex_coord - off1) * 0.3162162162;

	result += texture(sColorTexture, In.tex_coord + off2) * 0.0702702703;
	result += texture(sColorTexture, In.tex_coord - off2) * 0.0702702703;
})";

const std::string utils::effects::BloomDownsample::Shader = R"(
layout(binding = EFFECT_UNIFORM_BINDING) uniform _downsample
{
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
	const vec2 pixelSize = vec2(1.0) / textureSize(srcSampler, 0);
	const vec3 taps[] = {
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
	int padding;
} upsample;

void effect(inout vec4 result)
{
	const vec2 pixelSize = vec2(1.0) / textureSize(sColorTexture, 0);

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
	result = In.color;
	result *= settings.color;
	result *= texture(sColorTexture, In.tex_coord, settings.mipmap_bias);

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
	result = In.color;
	result *= settings.color;
	result *= texture(sColorTexture, In.tex_coord, settings.mipmap_bias);

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
	result = In.color;
	result *= settings.color;
	result *= texture(sColorTexture, In.tex_coord, settings.mipmap_bias);

	if (result.a <= alphatest.threshold)
		discard;
})";

static std::optional<utils::Context> gContext;

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

utils::effects::forward_shading::DirectionalLight::DirectionalLight(const utils::DirectionalLight& light) :
	direction(light.direction),
	ambient(light.ambient),
	diffuse(light.diffuse),
	specular(light.specular),
	shininess(light.shininess)
{
}

utils::effects::forward_shading::PointLight::PointLight(const utils::PointLight& light) :
	position(light.position),
	ambient(light.ambient),
	diffuse(light.diffuse),
	specular(light.specular),
	constant_attenuation(light.constant_attenuation),
	linear_attenuation(light.linear_attenuation),
	quadratic_attenuation(light.quadratic_attenuation),
	shininess(light.shininess)
{
}

utils::effects::deferred_shading::DirectionalLight::DirectionalLight(const utils::DirectionalLight& light) :
	direction(light.direction),
	ambient(light.ambient),
	diffuse(light.diffuse),
	specular(light.specular),
	shininess(light.shininess)
{
}

utils::effects::deferred_shading::PointLight::PointLight(const utils::PointLight& light) :
	position(light.position),
	ambient(light.ambient),
	diffuse(light.diffuse),
	specular(light.specular),
	constant_attenuation(light.constant_attenuation),
	linear_attenuation(light.linear_attenuation),
	quadratic_attenuation(light.quadratic_attenuation),
	shininess(light.shininess)
{
}

utils::effects::GaussianBlur::GaussianBlur(glm::vec2 _direction) :
	direction(std::move(_direction))
{
}

utils::effects::BloomDownsample::BloomDownsample(uint32_t _step_number) :
	step_number(_step_number)
{
}

utils::effects::BrightFilter::BrightFilter(float _threshold) :
	threshold(_threshold)
{
}

std::tuple<glm::mat4/*proj*/, glm::mat4/*view*/> utils::MakeOrthogonalCameraMatrices(const OrthogonalCamera& camera,
	std::optional<uint32_t> _width, std::optional<uint32_t> _height)
{
	auto width = (float)_width.value_or(GetBackbufferWidth());
	auto height = (float)_height.value_or(GetBackbufferHeight());
	auto proj = glm::orthoLH(0.0f, width, height, 0.0f, -1.0f, 1.0f);
	auto view = glm::lookAtLH(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f),
		glm::vec3(0.0f, 1.0f, 0.0f));
	return { proj, view };
}

std::tuple<glm::mat4/*proj*/, glm::mat4/*view*/, glm::vec3/*eye_pos*/> utils::MakePerspectiveCameraMatrices(
	const PerspectiveCamera& camera, std::optional<uint32_t> _width, std::optional<uint32_t> _height)
{
	auto sin_yaw = glm::sin(camera.yaw);
	auto sin_pitch = glm::sin(camera.pitch);

	auto cos_yaw = glm::cos(camera.yaw);
	auto cos_pitch = glm::cos(camera.pitch);

	auto front = glm::normalize(glm::vec3(cos_yaw * cos_pitch, sin_pitch, sin_yaw * cos_pitch));
	auto right = glm::normalize(glm::cross(front, camera.world_up));
	auto up = glm::normalize(glm::cross(right, front));

	auto width = (float)_width.value_or(GetBackbufferWidth());
	auto height = (float)_height.value_or(GetBackbufferHeight());

	auto proj = glm::perspectiveFov(camera.fov, width, height, camera.near_plane, camera.far_plane);
	auto view = glm::lookAtRH(camera.position, camera.position + front, up);

	return { proj, view, camera.position };
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

void utils::ClearContext()
{
	gContext.reset();
}

utils::Context& utils::GetContext()
{
	if (!gContext.has_value())
		gContext.emplace();

	return gContext.value();
}

static const uint32_t white_pixel = 0xFFFFFFFF;

utils::Context::Context() :
	default_shader(utils::Mesh::Vertex::Layout, vertex_shader_code, fragment_shader_code, std::vector<std::string>{
		"COLOR_TEXTURE_BINDING 0",
		"NORMAL_TEXTURE_BINDING 1",
		"SETTINGS_UNIFORM_BINDING 2"
	}),
	default_mesh({
		{ { -1.0f, -1.0f, 0.0f }, { 1.0f, 1.0f, 1.0f, 1.0f }, { 0.0f, 1.0f } },
		{ { -1.0f,  1.0f, 0.0f }, { 1.0f, 1.0f, 1.0f, 1.0f }, { 0.0f, 0.0f } },
		{ {  1.0f,  1.0f, 0.0f }, { 1.0f, 1.0f, 1.0f, 1.0f }, { 1.0f, 0.0f } },
		{ {  1.0f, -1.0f, 0.0f }, { 1.0f, 1.0f, 1.0f, 1.0f }, { 1.0f, 1.0f } },
	}, { 0, 1, 2, 0, 2, 3 }),
	white_pixel_texture(1, 1, skygfx::Format::Byte4, (void*)&white_pixel)
{
}

// commands

utils::commands::SetEffect::SetEffect(std::nullopt_t)
{
}

utils::commands::SetEffect::SetEffect(Shader* _shader, void* _uniform_data, size_t uniform_size) :
	shader(_shader)
{
	uniform_data.resize(uniform_size);
	std::memcpy(uniform_data.data(), _uniform_data, uniform_size);
}

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

utils::commands::SetFrontFace::SetFrontFace(FrontFace _front_face) :
	front_face(_front_face)
{
}

utils::commands::SetDepthBias::SetDepthBias(std::optional<DepthBias> _depth_bias) :
	depth_bias(_depth_bias)
{
}

utils::commands::SetDepthMode::SetDepthMode(std::optional<DepthMode> _depth_mode) :
	depth_mode(_depth_mode)
{
}

utils::commands::SetStencilMode::SetStencilMode(std::optional<StencilMode> _stencil_mode) :
	stencil_mode(_stencil_mode)
{
}

utils::commands::SetMesh::SetMesh(const Mesh* _mesh) :
	mesh(_mesh)
{
}

utils::commands::SetCustomTexture::SetCustomTexture(uint32_t _binding, const Texture* _texture) :
	binding(_binding),
	texture(_texture)
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

utils::commands::Subcommands::Subcommands(const std::vector<Command>* _subcommands) :
	subcommands(_subcommands)
{
}

utils::commands::Subcommands::Subcommands(std::vector<Command>&& _subcommands) :
	subcommands(_subcommands)
{
}

utils::commands::Subcommands::Subcommands(std::function<std::vector<Command>()> _subcommands) :
	subcommands(_subcommands)
{
}

utils::commands::Draw::Draw(std::optional<DrawCommand> _draw_command) :
	draw_command(std::move(_draw_command))
{
}

void utils::ExecuteCommands(const std::vector<Command>& cmds)
{
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

	auto front_face = FrontFace::Clockwise;
	bool front_face_dirty = true;

	std::optional<DepthBias> depth_bias;
	bool depth_bias_dirty = true;

	std::optional<DepthMode> depth_mode;
	bool depth_mode_dirty = true;

	std::optional<StencilMode> stencil_mode;
	bool stencil_mode_dirty = true;

	const Mesh* mesh = nullptr;
	bool mesh_dirty = true;

	Shader* shader = nullptr;
	bool shader_dirty = true;

	const std::vector<uint8_t>* uniform_data = nullptr;
	bool uniform_dirty = true;

	const Texture* color_texture = nullptr;
	const Texture* normal_texture = nullptr;
	bool textures_dirty = true;

	std::unordered_map<uint32_t, const Texture*> custom_textures;
	bool custom_textures_dirty = true;

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
				viewport = cmd.viewport;
				viewport_dirty = true;
			},
			[&](const commands::SetScissor& cmd) {
				scissor = cmd.scissor;
				scissor_dirty = true;
			},
			[&](const commands::SetBlendMode& cmd) {
				blend_mode = cmd.blend_mode;
				blend_mode_dirty = true;
			},
			[&](const commands::SetSampler& cmd) {
				sampler = cmd.sampler;
				sampler_dirty = true;
			},
			[&](const commands::SetCullMode& cmd) {
				cull_mode = cmd.cull_mode;
				cull_mode_dirty = true;
			},
			[&](const commands::SetTextureAddress& cmd) {
				texture_address = cmd.texture_address;
				texture_address_dirty = true;
			},
			[&](const commands::SetFrontFace& cmd) {
				front_face = cmd.front_face;
				front_face_dirty = true;
			},
			[&](const commands::SetDepthBias& cmd) {
				depth_bias = cmd.depth_bias;
				depth_bias_dirty = true;
			},
			[&](const commands::SetDepthMode& cmd) {
				depth_mode = cmd.depth_mode;
				depth_mode_dirty = true;
			},
			[&](const commands::SetStencilMode& cmd) {
				stencil_mode = cmd.stencil_mode;
				stencil_mode_dirty = true;
			},
			[&](const commands::SetMesh& cmd) {
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
			[&](const commands::SetCustomTexture& cmd) {
				custom_textures[cmd.binding] = cmd.texture;
				custom_textures_dirty = true;
			},
			[&](const commands::SetColorTexture& cmd) {
				color_texture = cmd.color_texture;
				textures_dirty = true;
			},
			[&](const commands::SetNormalTexture& cmd) {
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
				std::visit(cases{
					[&](const OrthogonalCamera& camera) {
						std::tie(settings.projection, settings.view) = MakeOrthogonalCameraMatrices(
							camera, cmd.height, cmd.width);
						settings.eye_position = { 0.0f, 0.0f, 0.0f };
					},
					[&](const PerspectiveCamera& camera) {
						std::tie(settings.projection, settings.view, settings.eye_position) = 
							MakePerspectiveCameraMatrices(camera, cmd.height, cmd.width);
					},
				}, cmd.camera);
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
					[&](const std::vector<Command>* subcommands) {
						for (const auto& subcommand : *subcommands)
						{
							execute_command(subcommand);
						}
					},
					[&](const std::vector<Command>& subcommands) {
						for (const auto& subcommand : subcommands)
						{
							execute_command(subcommand);
						}
					},
					[&](const std::function<std::vector<Command>()>& subcommands) {
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

				if (front_face_dirty)
				{
					SetFrontFace(front_face);
					front_face_dirty = false;
				}

				if (depth_bias_dirty)
				{
					SetDepthBias(depth_bias);
					depth_bias_dirty = false;
				}

				if (depth_mode_dirty)
				{
					SetDepthMode(depth_mode);
					depth_mode_dirty = false;
				}

				if (stencil_mode_dirty)
				{
					SetStencilMode(stencil_mode);
					stencil_mode_dirty = false;
				}

				if (mesh == nullptr)
					mesh = &GetContext().default_mesh;

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
					SetShader(shader == nullptr ? GetContext().default_shader : *shader);
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
					const auto& _color_texture = color_texture != nullptr ? *color_texture : GetContext().white_pixel_texture;
					const auto& _normal_texture = normal_texture != nullptr ? *normal_texture : GetContext().white_pixel_texture;

					SetTexture(0, _color_texture);
					SetTexture(1, _normal_texture);
					
					textures_dirty = false;
				}

				if (custom_textures_dirty)
				{
					for (auto [binding, texture] : custom_textures)
					{
						SetTexture(binding, *texture);
					}
					custom_textures_dirty = false;
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

void utils::ExecuteCommands(const RenderTarget* target, bool clear, const std::vector<Command>& cmds)
{
	if (target == nullptr)
		SetRenderTarget(std::nullopt);
	else
		SetRenderTarget(*target);

	if (clear)
		Clear();

	ExecuteCommands(cmds);
}

void utils::ExecuteCommands(const std::string& name, const RenderTarget* target, bool clear,
	const std::vector<Command>& cmds)
{
	ExecuteCommands(target, clear, cmds);
	DebugStage(name, target);
}

void utils::passes::Blit(const Texture* src, const RenderTarget* dst, bool clear, std::vector<Command>&& commands)
{
	commands.insert(commands.end(), {
		commands::SetColorTexture(src),
		commands::Draw()
	});
	ExecuteCommands(dst, clear, commands);
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
	auto blur_target = skygfx::AcquireTransientRenderTarget(src->getWidth(), src->getHeight());
	Blit("gaussian horizontal", src, blur_target, effects::GaussianBlur({ 1.0f, 0.0f }), true);
	Blit("gaussian vertical", blur_target, dst, effects::GaussianBlur({ 0.0f, 1.0f }));
	skygfx::ReleaseTransientRenderTarget(blur_target);
}

void utils::passes::Grayscale(const RenderTarget* src, const RenderTarget* dst, float intensity)
{
	Blit("grayscale", src, dst, effects::Grayscale{ intensity });
}

void utils::passes::Bloom(const RenderTarget* src, const RenderTarget* dst, float bright_threshold, float intensity)
{
	Blit(src, dst);

	if (intensity <= 0.0f)
		return;

	constexpr int ChainSize = 8;

	// acquire targets

	auto bright = skygfx::AcquireTransientRenderTarget(src->getWidth(), src->getHeight());

	std::array<RenderTarget*, ChainSize> tex_chain;

	for (int i = 0; i < ChainSize; i++)
	{
		auto w = src->getWidth() >> (i + 1);
		auto h = src->getHeight() >> (i + 1);

		w = glm::max(w, 1u);
		h = glm::max(h, 1u);

		tex_chain[i] = skygfx::AcquireTransientRenderTarget(w, h);
	}

	// extract bright

	auto downsample_src = src;

	if (bright_threshold > 0.0f)
	{
		Blit("bright", src, bright, effects::BrightFilter(bright_threshold), true);
		downsample_src = bright;
	}

	// downsample

	uint32_t step_number = 0;

	for (auto target : tex_chain)
	{
		Blit("downsample", downsample_src, target, effects::BloomDownsample(step_number));
		downsample_src = target;
		step_number += 1;
	}

	// upsample

	for (auto it = std::next(tex_chain.rbegin()); it != tex_chain.rend(); ++it)
	{
		Blit("upsample", *std::prev(it), *it, effects::BloomUpsample(), false, BlendStates::Additive);
	}

	// combine

	Blit(*tex_chain.begin(), dst, effects::BloomUpsample(), false, BlendStates::Additive, glm::vec4(intensity));

	// release targets

	skygfx::ReleaseTransientRenderTarget(bright);

	for (auto target : tex_chain)
	{
		skygfx::ReleaseTransientRenderTarget(target);
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

	auto bright = skygfx::AcquireTransientRenderTarget(width, height);
	auto blur_dst = skygfx::AcquireTransientRenderTarget(width, height);
	auto blur_src = src;

	if (bright_threshold > 0.0f)
	{
		Blit("bright", src, bright, effects::BrightFilter(bright_threshold), true);
		blur_src = bright;
	}

	GaussianBlur(blur_src, blur_dst);

	Blit(blur_dst, dst, false, BlendStates::Additive, glm::vec4(intensity));

	skygfx::ReleaseTransientRenderTarget(bright);
	skygfx::ReleaseTransientRenderTarget(blur_dst);
}

utils::commands::Subcommands utils::Model::Draw(const Model& model, bool use_color_texture, bool use_normal_texture)
{
	std::vector<Command> cmds;

	if (use_color_texture)
		cmds.push_back(commands::SetColorTexture(model.color_texture));

	if (use_normal_texture)
		cmds.push_back(commands::SetNormalTexture(model.normal_texture));

	cmds.insert(cmds.end(), {
		commands::SetMesh(model.mesh),
		commands::SetModelMatrix(model.matrix),
		commands::SetCullMode(model.cull_mode),
		commands::SetTextureAddress(model.texture_address),
		commands::SetDepthMode(model.depth_mode),
		commands::SetColor(model.color),
		commands::SetSampler(model.sampler),
		commands::Draw(model.draw_command)
	});

	return commands::Subcommands(std::move(cmds));
}

static void DrawSceneForwardShading(const RenderTarget* target, const utils::PerspectiveCamera& camera,
	const std::vector<utils::Model>& models, const std::vector<utils::Light>& lights,
	const utils::DrawSceneOptions& options)
{
	using namespace utils;

	if (models.empty())
		return;

	std::vector<Command> draw_models;

	for (const auto& model : models)
	{
		draw_models.push_back(Model::Draw(model, options.textures, options.normal_mapping));
	}

	std::vector<Command> cmds = {
		commands::SetCamera(camera),
	};

	if (lights.empty())
	{
		cmds.push_back(commands::Subcommands(&draw_models));
		ExecuteCommands(cmds);
		(target, options.clear_target, cmds);
		return;
	}

	bool first_light_done = false;

	for (const auto& light : lights)
	{
		cmds.insert(cmds.end(), {
			std::visit(cases{
				[](const DirectionalLight& light) {
					return commands::SetEffect(effects::forward_shading::DirectionalLight(light));
				},
				[](const PointLight& light) {
					return commands::SetEffect(effects::forward_shading::PointLight(light));
				}
			}, light),
			commands::Subcommands(&draw_models)
		});

		if (!first_light_done)
		{
			first_light_done = true;
			cmds.push_back(commands::SetBlendMode(skygfx::BlendStates::Additive));
		}
	}

	ExecuteCommands(target, options.clear_target, cmds);
}

static void DrawSceneDeferredShading(const RenderTarget* target, const utils::PerspectiveCamera& camera,
	const std::vector<utils::Model>& models, const std::vector<utils::Light>& lights,
	const utils::DrawSceneOptions& options)
{
	using namespace utils;

	if (models.empty())
		return;

	std::vector<Command> draw_models;

	for (const auto& model : models)
	{
		draw_models.push_back(Model::Draw(model, options.textures, options.normal_mapping));
	}

	// extract g-buffer
	// TODO: use acquire transient for mrt

	auto color_buffer = AcquireTransientRenderTarget();
	auto normal_buffer = AcquireTransientRenderTarget();
	auto positions_buffer = AcquireTransientRenderTarget();

	SetRenderTarget({ color_buffer, normal_buffer, positions_buffer });
	Clear();
	ExecuteCommands({
		commands::SetCamera(camera),
		commands::SetEffect(effects::deferred_shading::ExtractGeometryBuffer{}),
		commands::Subcommands(&draw_models)
	});

	DebugStage("color_buffer", color_buffer);
	DebugStage("normal_buffer", normal_buffer);
	DebugStage("positions_buffer", positions_buffer);

	std::vector<Command> cmds = {
		commands::SetEyePosition(camera.position),
		commands::SetBlendMode(BlendStates::Additive),
		commands::SetCustomTexture(5, color_buffer),
		commands::SetCustomTexture(6, normal_buffer),
		commands::SetCustomTexture(7, positions_buffer)
	};

	for (const auto& light : lights)
	{
		cmds.insert(cmds.end(), {
			std::visit(cases{
				[](const DirectionalLight& light) {
					return commands::SetEffect(effects::deferred_shading::DirectionalLight(light));
				},
				[](const PointLight& light) {
					return commands::SetEffect(effects::deferred_shading::PointLight(light));
				}
			}, light),
			commands::Draw()
		});
	}

	ExecuteCommands(target, options.clear_target, cmds);

	ReleaseTransientRenderTarget(color_buffer);
	ReleaseTransientRenderTarget(normal_buffer);
	ReleaseTransientRenderTarget(positions_buffer);
}

void utils::DrawScene(DrawSceneTechnique technique, const RenderTarget* target, const PerspectiveCamera& camera,
	const std::vector<Model>& models, const std::vector<Light>& lights, const DrawSceneOptions& options)
{
	if (technique == DrawSceneTechnique::ForwardShading)
		DrawSceneForwardShading(target, camera, models, lights, options);
	else if (technique == DrawSceneTechnique::DeferredShading)
		DrawSceneDeferredShading(target, camera, models, lights, options);
}

// mesh builder

template<typename T>
static void AddItem(std::vector<T>& items, uint32_t& count, const T& item)
{
	count++;
	if (items.size() < count)
		items.push_back(item);
	else
		items[count - 1] = item;
}

static void ExtractOrderedIndexSequence(const skygfx::utils::Mesh::Vertices& vertices, uint32_t vertex_start,
	uint32_t vertex_count, skygfx::utils::Mesh::Indices& indices, uint32_t& index_count)
{
	for (uint32_t i = vertex_start; i < vertex_count; i++)
	{
		AddItem(indices, index_count, i);
	}
}

static void ExtractLineListIndicesFromLineLoop(const skygfx::utils::Mesh::Vertices& vertices, uint32_t vertex_start,
	uint32_t vertex_count, skygfx::utils::Mesh::Indices& indices, uint32_t& index_count)
{
	if (vertex_count == vertex_start)
		return;

	for (uint32_t i = vertex_start + 1; i < vertex_count; i++)
	{
		AddItem(indices, index_count, i - 1);
		AddItem(indices, index_count, i);
	}
	AddItem(indices, index_count, vertex_start + vertex_count - 1);
	AddItem(indices, index_count, vertex_start);
}

static void ExtractLineListIndicesFromLineStrip(const skygfx::utils::Mesh::Vertices& vertices, uint32_t vertex_start,
	uint32_t vertex_count, skygfx::utils::Mesh::Indices& indices, uint32_t& index_count)
{
	for (uint32_t i = vertex_start + 1; i < vertex_count; i++)
	{
		AddItem(indices, index_count, i - 1);
		AddItem(indices, index_count, i);
	}
}

static void ExtractTrianglesIndicesFromTriangleFan(const skygfx::utils::Mesh::Vertices& vertices, uint32_t vertex_start,
	uint32_t vertex_count, skygfx::utils::Mesh::Indices& indices, uint32_t& index_count)
{
	for (uint32_t i = vertex_start + 2; i < vertex_count; i++)
	{
		AddItem(indices, index_count, vertex_start);
		AddItem(indices, index_count, i - 1);
		AddItem(indices, index_count, i);
	}
}

static void ExtractTrianglesIndicesFromPolygons(const skygfx::utils::Mesh::Vertices& vertices, uint32_t vertex_start,
	uint32_t vertex_count, skygfx::utils::Mesh::Indices& indices, uint32_t& index_count)
{
	ExtractTrianglesIndicesFromTriangleFan(vertices, vertex_start, vertex_count, indices, index_count);
}

static void ExtractTrianglesIndicesFromQuads(const skygfx::utils::Mesh::Vertices& vertices, uint32_t vertex_start,
	uint32_t vertex_count, skygfx::utils::Mesh::Indices& indices, uint32_t& index_count)
{
	for (uint32_t i = vertex_start + 3; i < vertex_count; i += 4) {
		// first triangle
		AddItem(indices, index_count, i - 3);
		AddItem(indices, index_count, i - 2);
		AddItem(indices, index_count, i - 1);
		// second triangle
		AddItem(indices, index_count, i - 3);
		AddItem(indices, index_count, i - 1);
		AddItem(indices, index_count, i);
	}
}

static void ExtractTrianglesIndicesFromTriangleStrip(const skygfx::utils::Mesh::Vertices& vertices, uint32_t vertex_start,
	uint32_t vertex_count, skygfx::utils::Mesh::Indices& indices, uint32_t& index_count)
{
	for (uint32_t i = vertex_start + 2; i < vertex_count; i++)
	{
		if ((i - vertex_start) % 2 == 0)
		{
			AddItem(indices, index_count, i - 2);
			AddItem(indices, index_count, i - 1);
			AddItem(indices, index_count, i);
		}
		else
		{
			AddItem(indices, index_count, i - 1);
			AddItem(indices, index_count, i - 2);
			AddItem(indices, index_count, i);
		}
	}
}

Topology utils::MeshBuilder::ConvertModeToTopology(Mode mode)
{
	static const std::unordered_map<Mode, Topology> TopologyMap = {
		{ Mode::Points, Topology::PointList },
		{ Mode::Lines, Topology::LineList },
		{ Mode::LineLoop, Topology::LineList },
		{ Mode::LineStrip, Topology::LineList },
		{ Mode::Triangles, Topology::TriangleList },
		{ Mode::TriangleStrip, Topology::TriangleList },
		{ Mode::TriangleFan, Topology::TriangleList },
		{ Mode::Quads, Topology::TriangleList },
		{ Mode::Polygon, Topology::TriangleList }
	};

	return TopologyMap.at(mode);
}

void utils::MeshBuilder::reset(bool reset_vertex)
{
	assert(!mBegan);
	mIndexCount = 0;
	mVertexCount = 0;
	mMode.reset();
	mTopology.reset();

	if (reset_vertex)
		mVertex = Mesh::Vertex{};
}

void utils::MeshBuilder::begin(Mode mode)
{
	assert(!mBegan);
	mBegan = true;

	auto topology = ConvertModeToTopology(mode);

	if (mTopology.has_value())
	{
		assert(topology == mTopology.value());
	}
	else
	{
		mTopology = topology;
	}

	mMode = mode;
	mVertexStart = mVertexCount;
}

void utils::MeshBuilder::vertex(const Vertex::PositionColorTextureNormal& value)
{
	assert(mBegan);
	AddItem(mVertices, mVertexCount, value);
}

void utils::MeshBuilder::vertex(const Vertex::PositionColorTexture& value)
{
	vertex(Vertex::PositionColorTextureNormal{
		.pos = value.pos,
		.color = value.color,
		.texcoord = value.texcoord,
		.normal = { 0.0f, 0.0f, 0.0f }
	});
}

void utils::MeshBuilder::vertex(const Vertex::PositionColor& value)
{
	vertex(Vertex::PositionColorTexture{
		.pos = value.pos,
		.color = value.color,
		.texcoord = { 0.0f, 0.0f }
	});
}

void utils::MeshBuilder::vertex(const glm::vec3& value)
{
	mVertex.pos = value;
	vertex(mVertex);
}

void utils::MeshBuilder::vertex(const glm::vec2& value)
{
	vertex({ value.x, value.y, 0.0f });
}

void utils::MeshBuilder::color(const glm::vec4& value)
{
	mVertex.color = value;
}

void utils::MeshBuilder::color(const glm::vec3& value)
{
	color(glm::vec4{ value.r, value.g, value.b, mVertex.color.a });
}

void utils::MeshBuilder::normal(const glm::vec3& value)
{
	mVertex.normal = value;
}

void utils::MeshBuilder::texcoord(const glm::vec2& value)
{
	mVertex.texcoord = value;
}

void utils::MeshBuilder::end()
{
	assert(mBegan);
	mBegan = false;

	using ExtractIndicesFunc = std::function<void(const Mesh::Vertices& vertices,
		uint32_t vertex_start, uint32_t vertex_count, Mesh::Indices& indices, uint32_t& index_count)>;

	static const std::unordered_map<Mode, ExtractIndicesFunc> ExtractIndicesFuncs = {
		{ Mode::Points, ExtractOrderedIndexSequence },
		{ Mode::Lines, ExtractOrderedIndexSequence },
		{ Mode::LineLoop, ExtractLineListIndicesFromLineLoop },
		{ Mode::LineStrip, ExtractLineListIndicesFromLineStrip },
		{ Mode::Polygon, ExtractTrianglesIndicesFromPolygons },
		{ Mode::TriangleFan, ExtractTrianglesIndicesFromTriangleFan },
		{ Mode::Quads, ExtractTrianglesIndicesFromQuads },
		{ Mode::TriangleStrip, ExtractTrianglesIndicesFromTriangleStrip },
		{ Mode::Triangles, ExtractOrderedIndexSequence }
	};

	ExtractIndicesFuncs.at(mMode.value())(mVertices, mVertexStart, mVertexCount, mIndices, mIndexCount);
}

void utils::MeshBuilder::setToMesh(Mesh& mesh)
{
	assert(!mBegan);
	mesh.setTopology(mTopology.value());
	mesh.setVertices(mVertices.data(), mVertexCount);
	mesh.setIndices(mIndices.data(), mIndexCount);
}

bool utils::MeshBuilder::isBeginAllowed(Mode mode) const
{
	if (!mTopology.has_value())
		return true;

	auto topology = ConvertModeToTopology(mode);
	return topology == mTopology.value();
}

static utils::StageDebugger* gStageDebugger = nullptr;

void utils::SetStageDebugger(StageDebugger* value)
{
	gStageDebugger = value;
}

void utils::DebugStage(const std::string& name, const Texture* texture)
{
	if (gStageDebugger == nullptr)
		return;

	gStageDebugger->stage(name, texture);
}

void utils::ScratchRasterizer::begin(MeshBuilder::Mode mode, const State& state)
{
	if (!mMeshBuilder.isBeginAllowed(mode))
		flush();

	if (mState != state)
		flush();

	mState = state;
	mMeshBuilder.begin(mode);
}

void utils::ScratchRasterizer::vertex(const Vertex::PositionColorTextureNormal& value)
{
	mMeshBuilder.vertex(value);
}

void utils::ScratchRasterizer::vertex(const Vertex::PositionColorTexture& value)
{
	mMeshBuilder.vertex(value);
}

void utils::ScratchRasterizer::vertex(const Vertex::PositionColor& value)
{
	mMeshBuilder.vertex(value);
}

void utils::ScratchRasterizer::vertex(const glm::vec3& value)
{
	mMeshBuilder.vertex(value);
}

void utils::ScratchRasterizer::vertex(const glm::vec2& value)
{
	mMeshBuilder.vertex(value);
}

void utils::ScratchRasterizer::color(const glm::vec4& value)
{
	mMeshBuilder.color(value);
}

void utils::ScratchRasterizer::color(const glm::vec3& value)
{
	mMeshBuilder.color(value);
}

void utils::ScratchRasterizer::normal(const glm::vec3& value)
{
	mMeshBuilder.normal(value);
}

void utils::ScratchRasterizer::texcoord(const glm::vec2& value)
{
	mMeshBuilder.texcoord(value);
}

void utils::ScratchRasterizer::end()
{
	mMeshBuilder.end();
}

void utils::ScratchRasterizer::flush()
{
	if (mMeshBuilder.getVertexCount() == 0)
	{
		mMeshBuilder.reset(false);
		return;
	}

	mMeshBuilder.setToMesh(mMesh);

	std::vector<Command> cmds;

	if (mState.alpha_test_threshold.has_value())
	{
		cmds.push_back(commands::SetEffect(effects::AlphaTest{ mState.alpha_test_threshold.value()}));
	}

	cmds.insert(cmds.end(), {
		commands::SetViewport(mState.viewport),
		commands::SetScissor(mState.scissor),
		commands::SetBlendMode(mState.blend_mode),
		commands::SetDepthBias(mState.depth_bias),
		commands::SetDepthMode(mState.depth_mode),
		commands::SetStencilMode(mState.stencil_mode),
		commands::SetCullMode(mState.cull_mode),
		commands::SetFrontFace(mState.front_face),
		commands::SetSampler(mState.sampler),
		commands::SetTextureAddress(mState.texaddr),
		commands::SetProjectionMatrix(mState.projection_matrix),
		commands::SetViewMatrix(mState.view_matrix),
		commands::SetModelMatrix(mState.model_matrix),
		commands::SetMesh(&mMesh),
		commands::SetColorTexture(mState.texture),
		commands::Draw()
	});

	ExecuteCommands(cmds);

	mMeshBuilder.reset(false);
}
