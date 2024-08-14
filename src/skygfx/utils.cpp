#include "utils.h"
#include <ranges>
#include <array>

using namespace skygfx;

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

const std::string vertex_shader_code = R"(
#version 450 core

layout(location = POSITION_LOCATION) in vec3 aPosition;
layout(location = COLOR_LOCATION) in vec4 aColor;
layout(location = TEXCOORD_LOCATION) in vec2 aTexCoord;
layout(location = NORMAL_LOCATION) in vec3 aNormal;
layout(location = TANGENT_LOCATION) in vec3 aTangent;

layout(binding = SETTINGS_UNIFORM_BINDING) uniform _settings
{
	mat4 projection;
	mat4 view;
	mat4 model;
	vec3 eye_position;
	float mipmap_bias;
	vec4 color;
	uint has_normal_texture;
} settings;

layout(location = 0) out struct
{
	vec3 world_position;
	vec4 color;
	vec2 tex_coord;
	vec3 normal;
	vec3 tangent;
	vec3 bitangent;
} Out;

out gl_PerVertex { vec4 gl_Position; };

void main()
{
	Out.world_position = vec3(settings.model * vec4(aPosition, 1.0));
	Out.normal = transpose(inverse(mat3(settings.model))) * aNormal;
	Out.tangent = transpose(inverse(mat3(settings.model))) * aTangent;
	Out.bitangent = cross(Out.normal, Out.tangent);
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
	uint has_normal_texture;
} settings;

layout(location = 0) out vec4 result;

layout(location = 0) in struct
{
	vec3 world_position;
	vec4 color;
	vec2 tex_coord;
	vec3 normal;
	vec3 tangent;
	vec3 bitangent;
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

	vec3 normal;

	if (settings.has_normal_texture != 0)
	{
		normal = vec3(texture(sNormalTexture, In.tex_coord, settings.mipmap_bias));
		normal = normal * 2.0 - 1.0;
		normal = mat3(In.tangent, In.bitangent, In.normal) * normal;
		normal = normalize(normal);
	}
	else
	{
		normal = normalize(In.normal);
	}

	vec3 view_dir = normalize(settings.eye_position - In.world_position);
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

	vec3 normal;

	if (settings.has_normal_texture != 0)
	{
		normal = vec3(texture(sNormalTexture, In.tex_coord, settings.mipmap_bias));
		normal = normal * 2.0 - 1.0;
		normal = mat3(In.tangent, In.bitangent, In.normal) * normal;
		normal = normalize(normal);
	}
	else
	{
		normal = normalize(In.normal);
	}

	vec3 light_offset = light.position - In.world_position;

	float distance = length(light_offset);
	float linear_attn = light.linear_attenuation * distance;
	float quadratic_attn = light.quadratic_attenuation * (distance * distance);
	float attenuation = 1.0 / (light.constant_attenuation + linear_attn + quadratic_attn);

	vec3 light_dir = normalize(light_offset);
	float diff = max(dot(normal, light_dir), 0.0);
	vec3 reflect_dir = reflect(-light_dir, normal);
	vec3 view_dir = normalize(settings.eye_position - In.world_position);
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

	vec3 pixel_normal = vec3(texture(sNormalBufferTexture, In.tex_coord)) * 2.0 - 1.0;
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

	vec3 pixel_normal = vec3(texture(sNormalBufferTexture, In.tex_coord)) * 2.0 - 1.0;
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
//layout(location = 0) out vec4 result; // color_buffer
layout(location = 1) out vec4 normal_buffer;
layout(location = 2) out vec4 positions_buffer;

void effect(inout vec4 result)
{
	result = In.color;
	result *= settings.color;
	result *= texture(sColorTexture, In.tex_coord, settings.mipmap_bias);

	if (settings.has_normal_texture != 0)
	{
		vec3 normal = vec3(texture(sNormalTexture, In.tex_coord, settings.mipmap_bias));
		normal = normal * 2.0 - 1.0;
		normal = mat3(In.tangent, In.bitangent, In.normal) * normal;
		normal = normalize(normal);
		normal_buffer = vec4(normal * 0.5 + 0.5, 1.0);
	}
	else
	{
		vec3 normal = normalize(In.normal);
		normal_buffer = vec4(normal * 0.5 + 0.5, 1.0);
	}

	positions_buffer = vec4(In.world_position, 1.0);
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

const std::string utils::effects::GammaCorrection::Shader = R"(
layout(binding = EFFECT_UNIFORM_BINDING) uniform _gamma
{
	float value;
} gamma;

void effect(inout vec4 result)
{
	result = In.color;
	result *= settings.color;
	result *= texture(sColorTexture, In.tex_coord, settings.mipmap_bias);
    result.rgb = pow(result.rgb, vec3(1.0 / gamma.value));
})";

static std::optional<utils::Context> gContext;

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

std::tuple<glm::mat4/*proj*/, glm::mat4/*view*/> utils::MakeCameraMatrices(const OrthogonalCamera& camera)
{
	auto width = (float)camera.width.value_or(GetBackbufferWidth());
	auto height = (float)camera.height.value_or(GetBackbufferHeight());
	auto proj = glm::orthoLH(0.0f, width, height, 0.0f, -1.0f, 1.0f);
	auto view = glm::lookAtLH(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f),
		glm::vec3(0.0f, 1.0f, 0.0f));
	return { proj, view };
}

std::tuple<glm::mat4/*proj*/, glm::mat4/*view*/> utils::MakeCameraMatrices(const PerspectiveCamera& camera)
{
	auto sin_yaw = glm::sin(camera.yaw);
	auto sin_pitch = glm::sin(camera.pitch);

	auto cos_yaw = glm::cos(camera.yaw);
	auto cos_pitch = glm::cos(camera.pitch);

	auto front = glm::normalize(glm::vec3(cos_yaw * cos_pitch, sin_pitch, sin_yaw * cos_pitch));
	auto right = glm::normalize(glm::cross(front, camera.world_up));
	auto up = glm::normalize(glm::cross(right, front));

	auto width = (float)camera.width.value_or(GetBackbufferWidth());
	auto height = (float)camera.height.value_or(GetBackbufferHeight());

	auto proj = glm::perspectiveFov(camera.fov, width, height, camera.near_plane, camera.far_plane);
	auto view = glm::lookAtRH(camera.position, camera.position + front, up);

	return { proj, view };
}

Shader utils::MakeEffectShader(const std::string& effect_shader_func)
{
	auto defines = utils::Mesh::Vertex::Defines;
	defines.insert(defines.end(), {
		"COLOR_TEXTURE_BINDING 0",
		"NORMAL_TEXTURE_BINDING 1",
		"SETTINGS_UNIFORM_BINDING 2",
		"EFFECT_UNIFORM_BINDING 3",
		"EFFECT_FUNC effect"
	});
	return Shader(vertex_shader_code, fragment_shader_code + effect_shader_func, defines);
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

static std::vector<std::string> MakeDefaultShaderDefines()
{
	auto result = utils::Mesh::Vertex::Defines;
	result.insert(result.end(), {
		"COLOR_TEXTURE_BINDING 0",
		"NORMAL_TEXTURE_BINDING 1",
		"SETTINGS_UNIFORM_BINDING 2"
	});
	return result;
}

utils::Context::Context() :
	default_shader(vertex_shader_code, fragment_shader_code, MakeDefaultShaderDefines()),
	default_mesh({
		{ { -1.0f, -1.0f, 0.0f }, { 1.0f, 1.0f, 1.0f, 1.0f }, { 0.0f, 1.0f } },
		{ { -1.0f,  1.0f, 0.0f }, { 1.0f, 1.0f, 1.0f, 1.0f }, { 0.0f, 0.0f } },
		{ {  1.0f,  1.0f, 0.0f }, { 1.0f, 1.0f, 1.0f, 1.0f }, { 1.0f, 0.0f } },
		{ {  1.0f, -1.0f, 0.0f }, { 1.0f, 1.0f, 1.0f, 1.0f }, { 1.0f, 1.0f } },
	}, { 0, 1, 2, 0, 2, 3 }),
	white_pixel_texture(1, 1, Format::Byte4, (void*)&white_pixel)
{
}

// commands

utils::commands::SetEffect::SetEffect(std::nullopt_t)
{
}

utils::commands::SetEffect::SetEffect(Shader* _shader, void* _uniform_data, size_t uniform_size) :
	shader(_shader)
{
	uniform_data.emplace();
	uniform_data.value().resize(uniform_size);
	std::memcpy(uniform_data.value().data(), _uniform_data, uniform_size);
}

utils::commands::SetTopology::SetTopology(Topology _topology) :
	topology(_topology)
{
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

utils::commands::SetCamera::SetCamera(Camera _camera) :
	camera(std::move(_camera))
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

utils::commands::Subcommands::Subcommands(const std::vector<Command>* _subcommands) :
	subcommands(_subcommands)
{
}

utils::commands::Draw::Draw(std::optional<DrawCommand> _draw_command) :
	draw_command(std::move(_draw_command))
{
}

void utils::ExecuteCommands(const std::vector<Command>& cmds)
{
	constexpr uint32_t ColorTextureBinding = 0;
	constexpr uint32_t NormalTextureBinding = 1;
	constexpr uint32_t SettingsUniformBinding = 2;
	constexpr uint32_t EffectUniformBinding = 3;

	auto& context = GetContext();

	auto set_texture = [&](uint32_t binding, const Texture* texture) {
		SetTexture(binding, texture ? *texture : context.white_pixel_texture);
	};

	auto set_shader = [&](Shader* shader) {
		SetShader(shader == nullptr ? context.default_shader : *shader);
	};

	SetTopology(Topology::TriangleList);
	SetViewport(std::nullopt);
	SetScissor(std::nullopt);
	SetBlendMode(std::nullopt);
	SetSampler(Sampler::Linear);
	SetCullMode(CullMode::None);
	SetTextureAddress(TextureAddress::Clamp);
	SetFrontFace(FrontFace::Clockwise);
	SetDepthBias(std::nullopt);
	SetDepthMode(std::nullopt);
	SetStencilMode(std::nullopt);
	SetInputLayout(Mesh::Vertex::Layout);
	set_texture(ColorTextureBinding, nullptr);
	set_texture(NormalTextureBinding, nullptr);
	set_shader(nullptr);

	const Mesh* mesh = &context.default_mesh;
	bool mesh_dirty = true;

	struct alignas(16) Settings
	{
		glm::mat4 projection = glm::mat4(1.0f);
		glm::mat4 view = glm::mat4(1.0f);
		glm::mat4 model = glm::mat4(1.0f);
		alignas(16) glm::vec3 eye_position = { 0.0f, 0.0f, 0.0f };
		float mipmap_bias = 0.0f;
		alignas(16) glm::vec4 color = { 1.0f, 1.0f, 1.0f, 1.0f };
		uint32_t has_normal_texture = 0;
	} settings;

	bool settings_dirty = true;

	std::function<void(const Command&)> execute_command;

	auto execute_commands = [&](const std::vector<Command>& _cmds) {
		for (const auto& cmd : _cmds)
		{
			execute_command(cmd);
		}
	};

	execute_command = [&](const Command& _cmd) {
		std::visit(cases{
			[&](const commands::SetTopology& cmd) {
				SetTopology(cmd.topology);
			},
			[&](const commands::SetViewport& cmd) {
				SetViewport(cmd.viewport);
			},
			[&](const commands::SetScissor& cmd) {
				SetScissor(cmd.scissor);
			},
			[&](const commands::SetBlendMode& cmd) {
				SetBlendMode(cmd.blend_mode);
			},
			[&](const commands::SetSampler& cmd) {
				SetSampler(cmd.sampler);
			},
			[&](const commands::SetCullMode& cmd) {
				SetCullMode(cmd.cull_mode);
			},
			[&](const commands::SetTextureAddress& cmd) {
				SetTextureAddress(cmd.texture_address);
			},
			[&](const commands::SetFrontFace& cmd) {
				SetFrontFace(cmd.front_face);
			},
			[&](const commands::SetDepthBias& cmd) {
				SetDepthBias(cmd.depth_bias);
			},
			[&](const commands::SetDepthMode& cmd) {
				SetDepthMode(cmd.depth_mode);
			},
			[&](const commands::SetStencilMode& cmd) {
				SetStencilMode(cmd.stencil_mode);
			},
			[&](const commands::SetMesh& cmd) {
				mesh = cmd.mesh ? cmd.mesh : &context.default_mesh;
				mesh_dirty = true;
			},
			[&](const commands::SetEffect& cmd) {
				set_shader(cmd.shader);

				if (cmd.uniform_data.has_value())
				{
					const auto& uniform = cmd.uniform_data.value();
					SetUniformBuffer(EffectUniformBinding, (void*)uniform.data(), uniform.size());
				}
			},
			[&](const commands::SetCustomTexture& cmd) {
				set_texture(cmd.binding, cmd.texture);
			},
			[&](const commands::SetColorTexture& cmd) {
				execute_command(commands::SetCustomTexture(ColorTextureBinding, cmd.color_texture));
			},
			[&](const commands::SetNormalTexture& cmd) {
				execute_command(commands::SetCustomTexture(NormalTextureBinding, cmd.normal_texture));
				settings.has_normal_texture = cmd.normal_texture != nullptr;
				settings_dirty = true;
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
				std::tie(settings.projection, settings.view, settings.eye_position) = std::visit(cases{
					[&](const OrthogonalCamera& camera) {
						return std::tuple_cat(MakeCameraMatrices(camera), std::make_tuple(glm::vec3{ 0.0f, 0.0f, 0.0f }));
					},
					[&](const PerspectiveCamera& camera) {
						return std::tuple_cat(MakeCameraMatrices(camera), std::make_tuple(camera.position));
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
			[&](const commands::Subcommands& cmd) {
				execute_commands(*cmd.subcommands);
			},
			[&](const commands::Draw& cmd) {
				if (mesh_dirty)
				{
					const auto& vertex_buffer = mesh->getVertexBuffer();
					const auto& index_buffer = mesh->getIndexBuffer();

					if (vertex_buffer.has_value())
						SetVertexBuffer(vertex_buffer.value());
					
					if (index_buffer.has_value())
						SetIndexBuffer(index_buffer.value());

					mesh_dirty = false;
				}

				if (settings_dirty)
				{
					SetUniformBuffer(SettingsUniformBinding, settings);
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
						Draw(vertex_count, vertex_offset);
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

	execute_commands(cmds);
}

void utils::ExecuteRenderPass(const RenderPass& render_pass)
{
	if (render_pass.targets.empty() || render_pass.targets == std::vector<const RenderTarget*>{ nullptr })
		SetRenderTarget(std::nullopt);
	else
		SetRenderTarget(render_pass.targets);

	if (render_pass.clear)
		Clear(render_pass.clear_value.color, render_pass.clear_value.depth, render_pass.clear_value.stencil);

	ExecuteCommands(render_pass.commands);
}

void utils::passes::Blit(Texture* src, RenderTarget* dst, const BlitOptions& options)
{
	auto render_pass = RenderPass{
		.targets = { dst },
		.clear = options.clear
	};

	if (options.effect.has_value())
		render_pass.commands.push_back(options.effect.value());

	render_pass.commands.insert(render_pass.commands.end(), {
		commands::SetSampler(options.sampler),
		commands::SetColor(options.color),
		commands::SetBlendMode(options.blend_mode),
		commands::SetColorTexture(src),
		commands::Draw()
	});

	ExecuteRenderPass(render_pass);
}

void utils::passes::GaussianBlur(RenderTarget* src, RenderTarget* dst)
{
	auto blur_target = AcquireTransientRenderTarget(src->getWidth(), src->getHeight());
	Blit(src, blur_target, {
		.clear = true,
		.effect = effects::GaussianBlur({ 1.0f, 0.0f })
	});
	ViewStage("gaussian horizontal", blur_target);
	Blit(blur_target, dst, {
		.effect = effects::GaussianBlur({ 0.0f, 1.0f })
	});
	ViewStage("gaussian vertical", dst);
	ReleaseTransientRenderTarget(blur_target);
}

void utils::passes::Grayscale(RenderTarget* src, RenderTarget* dst, float intensity)
{
	Blit(src, dst, {
		.effect = effects::Grayscale{ intensity }
	});
	ViewStage("grayscale", dst);
}

void utils::passes::Bloom(RenderTarget* src, RenderTarget* dst, float bright_threshold, float intensity)
{
	Blit(src, dst);

	if (intensity <= 0.0f)
		return;

	constexpr int ChainSize = 8;

	// acquire targets

	auto bright = AcquireTransientRenderTarget(src->getWidth(), src->getHeight());

	std::array<RenderTarget*, ChainSize> tex_chain;

	for (int i = 0; i < ChainSize; i++)
	{
		auto w = src->getWidth() >> (i + 1);
		auto h = src->getHeight() >> (i + 1);

		w = glm::max(w, 1u);
		h = glm::max(h, 1u);

		tex_chain[i] = AcquireTransientRenderTarget(w, h);
	}

	// extract bright

	auto downsample_src = src;

	if (bright_threshold > 0.0f)
	{
		Blit(src, bright, {
			.clear = true,
			.effect = effects::BrightFilter(bright_threshold)
		});
		ViewStage("bright", bright);
		downsample_src = bright;
	}

	// downsample

	uint32_t step_number = 0;

	for (auto target : tex_chain)
	{
		Blit(downsample_src, target, {
			.effect = effects::BloomDownsample(step_number)
		});
		ViewStage("downsample", target);
		downsample_src = target;
		step_number += 1;
	}

	// upsample

	for (auto it = std::next(tex_chain.rbegin()); it != tex_chain.rend(); ++it)
	{
		Blit(*std::prev(it), *it, {
			.blend_mode = BlendStates::Additive,
			.effect = effects::BloomUpsample()
		});
		ViewStage("upsample", *it);
	}

	// combine

	Blit(*tex_chain.begin(), dst, {
		.color = glm::vec4(intensity),
		.blend_mode = BlendStates::Additive,
		.effect = effects::BloomUpsample()
	});

	// release targets

	ReleaseTransientRenderTarget(bright);

	for (auto target : tex_chain)
	{
		ReleaseTransientRenderTarget(target);
	}
}

void utils::passes::BloomGaussian(RenderTarget* src, RenderTarget* dst, float bright_threshold,
	float intensity)
{
	Blit(src, dst);

	if (intensity <= 0.0f)
		return;

	constexpr auto DownsampleCount = 8;

	auto width = static_cast<uint32_t>(glm::floor(static_cast<float>(src->getWidth()) / static_cast<float>(DownsampleCount)));
	auto height = static_cast<uint32_t>(glm::floor(static_cast<float>(src->getHeight()) / static_cast<float>(DownsampleCount)));

	auto bright = AcquireTransientRenderTarget(width, height);
	auto blur_dst = AcquireTransientRenderTarget(width, height);
	auto blur_src = src;

	if (bright_threshold > 0.0f)
	{
		Blit(src, bright, {
			.clear = true,
			.effect = effects::BrightFilter(bright_threshold)
		});
		ViewStage("bright", bright);
		blur_src = bright;
	}

	GaussianBlur(blur_src, blur_dst);

	Blit(blur_dst, dst, {
		.color = glm::vec4(intensity),
		.blend_mode = BlendStates::Additive
	});

	ReleaseTransientRenderTarget(bright);
	ReleaseTransientRenderTarget(blur_dst);
}

std::vector<utils::Command> utils::Model::Draw(const Model& model, bool use_color_texture, bool use_normal_texture)
{
	return {
		commands::SetColorTexture(use_color_texture ? model.color_texture : nullptr),
		commands::SetNormalTexture(use_normal_texture ? model.normal_texture : nullptr),
		commands::SetMesh(model.mesh),
		commands::SetModelMatrix(model.matrix),
		commands::SetCullMode(model.cull_mode),
		commands::SetTextureAddress(model.texture_address),
		commands::SetDepthMode(model.depth_mode),
		commands::SetColor(model.color),
		commands::SetSampler(model.sampler),
		commands::Draw(model.draw_command)
	};
}

static void DrawSceneForwardShading(RenderTarget* target, const utils::PerspectiveCamera& camera,
	const std::vector<utils::Model>& models, const std::vector<utils::Light>& lights,
	const utils::DrawSceneOptions& options)
{
	using namespace utils;

	if (models.empty())
		return;

	if (lights.empty())
		return;

	auto render_pass = RenderPass{
		.targets = { target },
		.clear = options.clear_target,
		.commands = {
			commands::SetCamera(camera),
			commands::SetMipmapBias(options.mipmap_bias)
		}
	};

	std::vector<Command> draw_models;

	for (const auto& model : models)
	{
		auto cmds = Model::Draw(model, options.use_color_textures, options.use_normal_textures);
		std::ranges::move(cmds, std::back_inserter(draw_models));
	}

	bool first_light_done = false;

	for (const auto& light : lights)
	{
		render_pass.commands.insert(render_pass.commands.end(), {
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
			render_pass.commands.push_back(commands::SetBlendMode(BlendStates::Additive));
		}
	}

	ExecuteRenderPass(render_pass);
}

static void DrawSceneDeferredShading(RenderTarget* target, const utils::PerspectiveCamera& camera,
	const std::vector<utils::Model>& models, const std::vector<utils::Light>& lights,
	const utils::DrawSceneOptions& options)
{
	using namespace utils;

	if (models.empty())
		return;

	if (lights.empty())
		return;

	// g-buffer pass

	auto color_buffer = AcquireTransientRenderTarget();
	auto normal_buffer = AcquireTransientRenderTarget();
	auto positions_buffer = AcquireTransientRenderTarget();

	auto gbuffer_pass = RenderPass{
		.targets = { color_buffer, normal_buffer, positions_buffer },
		.clear = true,
		.commands = {
			commands::SetMipmapBias(options.mipmap_bias),
			commands::SetCamera(camera),
			commands::SetEffect(effects::deferred_shading::ExtractGeometryBuffer{}),
		}
	};

	for (const auto& model : models)
	{
		auto cmds = Model::Draw(model, options.use_color_textures, options.use_normal_textures);
		std::ranges::move(cmds, std::back_inserter(gbuffer_pass.commands));
	}

	// light pass

	auto light_pass = RenderPass{
		.targets = { target },
		.clear = options.clear_target,
		.commands = {
			commands::SetEyePosition(camera.position),
			commands::SetBlendMode(BlendStates::Additive),
			commands::SetCustomTexture(5, color_buffer),
			commands::SetCustomTexture(6, normal_buffer),
			commands::SetCustomTexture(7, positions_buffer)
		}
	};

	for (const auto& light : lights)
	{
		light_pass.commands.insert(light_pass.commands.end(), {
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

	ExecuteRenderPass(gbuffer_pass);
	ExecuteRenderPass(light_pass);

	ViewStage("color_buffer", color_buffer);
	ViewStage("normal_buffer", normal_buffer);
	ViewStage("positions_buffer", positions_buffer);

	ReleaseTransientRenderTarget(color_buffer);
	ReleaseTransientRenderTarget(normal_buffer);
	ReleaseTransientRenderTarget(positions_buffer);
}

void utils::DrawScene(RenderTarget* target, const PerspectiveCamera& camera,
	const std::vector<Model>& models, const std::vector<Light>& lights, const DrawSceneOptions& options)
{
	std::optional<RenderTarget*> scene_target;

	auto get_same_transient_target = [](const RenderTarget* target) {
		if (target != nullptr)
			return AcquireTransientRenderTarget(target->getWidth(), target->getHeight());
		else
			return AcquireTransientRenderTarget();
	};

	if (!options.posteffects.empty())
	{
		scene_target = get_same_transient_target(target);
	}

	using DrawSceneFunc = std::function<void(RenderTarget* target, const PerspectiveCamera& camera,
		const std::vector<Model>& models, const std::vector<Light>& lights,
		const DrawSceneOptions& options)>;

	static const std::unordered_map<DrawSceneOptions::Technique, DrawSceneFunc> DrawSceneFuncs = {
		{ DrawSceneOptions::Technique::ForwardShading, DrawSceneForwardShading },
		{ DrawSceneOptions::Technique::DeferredShading, DrawSceneDeferredShading },
	};

	DrawSceneFuncs.at(options.technique)(scene_target.value_or((RenderTarget*)target), camera, models, lights, options);

	if (options.posteffects.empty())
		return;

	auto src = scene_target.value();

	for (size_t i = 0; i < options.posteffects.size(); i++)
	{
		const auto& posteffect = options.posteffects.at(i);
		auto dst = i == options.posteffects.size() - 1 ? (RenderTarget*)target : get_same_transient_target(target);

		std::visit(cases{
			[&](const DrawSceneOptions::BloomPosteffect& bloom) {
				passes::Bloom(src, dst, bloom.threshold, bloom.intensity);
			},
			[&](const DrawSceneOptions::GrayscalePosteffect& grayscale) {
				passes::Grayscale(src, dst, grayscale.intensity);
			},
			[&](const DrawSceneOptions::GaussianBlurPosteffect& blur) {
				passes::GaussianBlur(src, dst);
			}
		}, posteffect);

		ReleaseTransientRenderTarget(src);
		src = dst;
	}
}

static utils::StageViewer* gStageViewer = nullptr;

void utils::SetStageViewer(StageViewer* value)
{
	gStageViewer = value;
}

void utils::ViewStage(const std::string& name, Texture* texture)
{
	if (gStageViewer == nullptr)
		return;

	gStageViewer->stage(name, texture);
}

void utils::MeshBuilder::reset()
{
	assert(!mBegan);
	mIndices.clear();
	mVertices.clear();
	mMode.reset();
	mTopology.reset();
}

void utils::MeshBuilder::begin(Mode mode, std::function<void()> onFlush)
{
	assert(!mBegan);
	mBegan = true;

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

	auto topology = TopologyMap.at(mode);

	if (mTopology.has_value() && topology != mTopology.value() && onFlush)
		onFlush();

	mTopology = topology;
	mMode = mode;
	mVertexStart = (uint32_t)mVertices.size();
}

void utils::MeshBuilder::vertex(const Mesh::Vertex& value)
{
	assert(mBegan);
	mVertices.push_back(value);
}

static void AddIndicesForVertexArray(utils::MeshBuilder::Mode mode, skygfx::Topology topology,
	uint32_t vertex_start, uint32_t vertex_count, std::vector<uint32_t>& indices)
{
	auto extract_ordered_index_sequence = [](uint32_t vertex_start, uint32_t vertex_count, auto& indices) {
		indices.reserve(indices.size() + (size_t)(vertex_count - vertex_start));

		for (uint32_t i = vertex_start; i < vertex_count; i++)
			indices.push_back(i);
	};

	auto add_indices = [](auto& indices, auto... elements) {
		(indices.push_back(elements), ...);
	};

	auto extract_line_list_indices_from_line_strip = [&add_indices](uint32_t vertex_start, uint32_t vertex_count, auto& indices) {
		for (uint32_t i = vertex_start + 1; i < vertex_count; i++)
			add_indices(indices, i - 1, i);
	};

	auto extract_line_list_indices_from_line_loop = [&extract_line_list_indices_from_line_strip, &add_indices](uint32_t vertex_start, uint32_t vertex_count, auto& indices) {
		if (vertex_count == vertex_start)
			return;

		extract_line_list_indices_from_line_strip(vertex_start, vertex_count, indices);
		add_indices(indices, vertex_start + vertex_count - 1, vertex_start);
	};

	auto extract_triangles_indices_from_triangle_fan = [&add_indices](uint32_t vertex_start, uint32_t vertex_count, auto& indices) {
		for (uint32_t i = vertex_start + 2; i < vertex_count; i++)
			add_indices(indices, vertex_start, i - 1, i);
	};

	auto extract_triangles_indices_from_polygons = extract_triangles_indices_from_triangle_fan;

	auto extract_triangles_indices_from_quads = [&add_indices](uint32_t vertex_start, uint32_t vertex_count, auto& indices) {
		for (uint32_t i = vertex_start + 3; i < vertex_count; i += 4)
			add_indices(indices, i - 3, i - 2, i - 1, i - 3, i - 1, i);
	};

	auto extract_triangles_indices_from_triangle_strip = [&add_indices](uint32_t vertex_start, uint32_t vertex_count, auto& indices) {
		for (uint32_t i = vertex_start + 2; i < vertex_count; i++)
		{
			bool even = (i - vertex_start) % 2 == 0;
			add_indices(indices, i - (even ? 2 : 1), i - (even ? 1 : 2), i);
		}
	};

	using ExtractIndicesFunc = std::function<void(uint32_t vertex_start,
		uint32_t vertex_count, std::vector<uint32_t>& indices)>;

	static const std::unordered_map<utils::MeshBuilder::Mode, std::unordered_map<skygfx::Topology, ExtractIndicesFunc>> ExtractIndicesFuncs = {
		{ utils::MeshBuilder::Mode::Points, { { skygfx::Topology::PointList, extract_ordered_index_sequence } } },
		{ utils::MeshBuilder::Mode::Lines, { { skygfx::Topology::LineList, extract_ordered_index_sequence } } },
		{ utils::MeshBuilder::Mode::LineLoop, { { skygfx::Topology::LineList, extract_line_list_indices_from_line_loop } } },
		{ utils::MeshBuilder::Mode::LineStrip, { { skygfx::Topology::LineList, extract_line_list_indices_from_line_strip } } },
		{ utils::MeshBuilder::Mode::Polygon, { { skygfx::Topology::TriangleList, extract_triangles_indices_from_polygons } } },
		{ utils::MeshBuilder::Mode::TriangleFan, { { skygfx::Topology::TriangleList, extract_triangles_indices_from_triangle_fan } } },
		{ utils::MeshBuilder::Mode::Quads, { { skygfx::Topology::TriangleList, extract_triangles_indices_from_quads } } },
		{ utils::MeshBuilder::Mode::TriangleStrip, { { skygfx::Topology::TriangleList, extract_triangles_indices_from_triangle_strip } } },
		{ utils::MeshBuilder::Mode::Triangles, { { skygfx::Topology::TriangleList, extract_ordered_index_sequence } } }
	};

	ExtractIndicesFuncs.at(mode).at(topology)(vertex_start, vertex_count, indices);
}

void utils::MeshBuilder::end()
{
	assert(mBegan);
	mBegan = false;
	AddIndicesForVertexArray(mMode.value(), mTopology.value(), mVertexStart, (uint32_t)mVertices.size(), mIndices);
}

void utils::MeshBuilder::setToMesh(Mesh& mesh)
{
	assert(!mBegan);
	mesh.setVertices(mVertices);
	mesh.setIndices(mIndices);
}

void utils::Scratch::begin(MeshBuilder::Mode mode, const State& state)
{
	if (!mMeshBuilder.getVertices().empty() && mState != state)
		pushCommand();

	mMeshBuilder.begin(mode, [&] {
		pushCommand();
	});

	mState = state;
}

void utils::Scratch::begin(MeshBuilder::Mode mode)
{
	begin(mode, {});
}

void utils::Scratch::vertex(const Mesh::Vertex& value)
{
	mMeshBuilder.vertex(value);
}

void utils::Scratch::end()
{
	mMeshBuilder.end();
}

void utils::Scratch::flush(bool sort_textures)
{
	if (mMeshBuilder.getVertices().empty())
	{
		mMeshBuilder.reset();
		return;
	}

	pushCommand();

	mMeshBuilder.setToMesh(mMesh);
	mMeshBuilder.reset();

	if (sort_textures)
	{
		std::sort(mScratchCommands.begin(), mScratchCommands.end(), [](const ScratchCommand& left, const ScratchCommand& right) {
			return left.state.texture < right.state.texture;
		});
	}

	mCommands.push_back(commands::SetMesh(&mMesh));

	for (const auto& command : mScratchCommands)
	{
		mCommands.insert(mCommands.end(), {
			command.state.alpha_test_threshold.has_value() ?
				commands::SetEffect(effects::AlphaTest{ command.state.alpha_test_threshold.value() }) :
				commands::SetEffect(std::nullopt),
			commands::SetViewport(command.state.viewport),
			commands::SetScissor(command.state.scissor),
			commands::SetBlendMode(command.state.blend_mode),
			commands::SetDepthBias(command.state.depth_bias),
			commands::SetDepthMode(command.state.depth_mode),
			commands::SetStencilMode(command.state.stencil_mode),
			commands::SetCullMode(command.state.cull_mode),
			commands::SetFrontFace(command.state.front_face),
			commands::SetSampler(command.state.sampler),
			commands::SetTextureAddress(command.state.texaddr),
			commands::SetMipmapBias(command.state.mipmap_bias),
			commands::SetProjectionMatrix(command.state.projection_matrix),
			commands::SetViewMatrix(command.state.view_matrix),
			commands::SetModelMatrix(command.state.model_matrix),
			commands::SetColorTexture(command.state.texture),
			commands::SetTopology(command.topology),
			commands::Draw(DrawIndexedVerticesCommand{
				.index_count = command.index_count,
				.index_offset = command.index_offset
			})
		});
	}

	mScratchCommands.clear();

	ExecuteCommands(mCommands);

	mCommands.clear();
}

bool utils::Scratch::isBegan() const
{
	return mMeshBuilder.isBegan();
}

void utils::Scratch::pushCommand()
{
	uint32_t index_offset = 0;

	if (!mScratchCommands.empty())
	{
		const auto& prev_command = mScratchCommands.at(mScratchCommands.size() - 1);
		index_offset = prev_command.index_offset + prev_command.index_count;
	}

	uint32_t index_count = static_cast<uint32_t>(mMeshBuilder.getIndices().size()) - index_offset;

	auto topology = mMeshBuilder.getTopology().value();

	mScratchCommands.push_back(ScratchCommand{
		.state = mState,
		.topology = topology,
		.index_count = index_count,
		.index_offset = index_offset
	});
}
