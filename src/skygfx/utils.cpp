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
	Out.world_position = mat3(settings.model) * aPosition;
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

template<typename T>
static void AddItem(std::vector<T>& items, uint32_t& count, const T& item)
{
	count++;
	if (items.size() < count)
		items.push_back(item);
	else
		items[count - 1] = item;
}

static void ExtractOrderedIndexSequence(const utils::Mesh::Vertices& vertices, uint32_t vertex_start,
	uint32_t vertex_count, utils::Mesh::Indices& indices, uint32_t& index_count)
{
	for (uint32_t i = vertex_start; i < vertex_count; i++)
	{
		AddItem(indices, index_count, i);
	}
}

static void ExtractLineListIndicesFromLineLoop(const utils::Mesh::Vertices& vertices, uint32_t vertex_start,
	uint32_t vertex_count, utils::Mesh::Indices& indices, uint32_t& index_count)
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

static void ExtractLineListIndicesFromLineStrip(const utils::Mesh::Vertices& vertices, uint32_t vertex_start,
	uint32_t vertex_count, utils::Mesh::Indices& indices, uint32_t& index_count)
{
	for (uint32_t i = vertex_start + 1; i < vertex_count; i++)
	{
		AddItem(indices, index_count, i - 1);
		AddItem(indices, index_count, i);
	}
}

static void ExtractTrianglesIndicesFromTriangleFan(const utils::Mesh::Vertices& vertices, uint32_t vertex_start,
	uint32_t vertex_count, utils::Mesh::Indices& indices, uint32_t& index_count)
{
	for (uint32_t i = vertex_start + 2; i < vertex_count; i++)
	{
		AddItem(indices, index_count, vertex_start);
		AddItem(indices, index_count, i - 1);
		AddItem(indices, index_count, i);
	}
}

static void ExtractTrianglesIndicesFromPolygons(const utils::Mesh::Vertices& vertices, uint32_t vertex_start,
	uint32_t vertex_count, utils::Mesh::Indices& indices, uint32_t& index_count)
{
	ExtractTrianglesIndicesFromTriangleFan(vertices, vertex_start, vertex_count, indices, index_count);
}

static void ExtractTrianglesIndicesFromQuads(const utils::Mesh::Vertices& vertices, uint32_t vertex_start,
	uint32_t vertex_count, utils::Mesh::Indices& indices, uint32_t& index_count)
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

static void ExtractTrianglesIndicesFromTriangleStrip(const utils::Mesh::Vertices& vertices, uint32_t vertex_start,
	uint32_t vertex_count, utils::Mesh::Indices& indices, uint32_t& index_count)
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

void utils::MeshBuilder::vertex(const vertex::PositionColorTextureNormalTangent& value)
{
	assert(mBegan);
	AddItem(mVertices, mVertexCount, value);
}

void utils::MeshBuilder::vertex(const vertex::PositionColorTextureNormal& value)
{
	vertex(vertex::PositionColorTextureNormalTangent{
		.pos = value.pos,
		.color = value.color,
		.texcoord = value.texcoord,
		.normal = value.normal,
		.tangent = vertex::defaults::Tangent
	});
}

void utils::MeshBuilder::vertex(const vertex::PositionColorTexture& value)
{
	vertex(vertex::PositionColorTextureNormal{
		.pos = value.pos,
		.color = value.color,
		.texcoord = value.texcoord,
		.normal = vertex::defaults::Normal
	});
}

void utils::MeshBuilder::vertex(const vertex::PositionColor& value)
{
	vertex(vertex::PositionColorTexture{
		.pos = value.pos,
		.color = value.color,
		.texcoord = vertex::defaults::TexCoord
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
	if (render_pass.targets.empty() || render_pass.targets == std::vector<RenderTarget*>{ nullptr })
		SetRenderTarget(std::nullopt);
	else
		SetRenderTarget(render_pass.targets);

	if (render_pass.clear)
		Clear(render_pass.clear_value.color, render_pass.clear_value.depth, render_pass.clear_value.stencil);

	ExecuteCommands(render_pass.commands);
}

void utils::techniques::GaussianBlur(RenderTarget* src, RenderTarget* dst)
{
	auto blur_target = AcquireTransientRenderTarget(src->getWidth(), src->getHeight());
	ExecuteRenderPass(passes::Blit(src, blur_target, {
		.clear = true,
		.effect = effects::GaussianBlur({ 1.0f, 0.0f })
	}));
	ViewStage("gaussian horizontal", blur_target);
	ExecuteRenderPass(passes::Blit(blur_target, dst, {
		.effect = effects::GaussianBlur({ 0.0f, 1.0f })
	}));
	ViewStage("gaussian vertical", dst);
	ReleaseTransientRenderTarget(blur_target);
}

void utils::techniques::Grayscale(RenderTarget* src, RenderTarget* dst, float intensity)
{
	ExecuteRenderPass(passes::Blit(src, dst, {
		.effect = effects::Grayscale{ intensity }
	}));
	ViewStage("grayscale", dst);
}

void utils::techniques::Bloom(RenderTarget* src, RenderTarget* dst, float bright_threshold, float intensity)
{
	ExecuteRenderPass(passes::Blit(src, dst));

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
		ExecuteRenderPass(passes::Blit(src, bright, {
			.clear = true,
			.effect = effects::BrightFilter(bright_threshold)
		}));
		ViewStage("bright", bright);
		downsample_src = bright;
	}

	// downsample

	uint32_t step_number = 0;

	for (auto target : tex_chain)
	{
		ExecuteRenderPass(passes::Blit(downsample_src, target, {
			.effect = effects::BloomDownsample(step_number)
		}));
		ViewStage("downsample", target);
		downsample_src = target;
		step_number += 1;
	}

	// upsample

	for (auto it = std::next(tex_chain.rbegin()); it != tex_chain.rend(); ++it)
	{
		ExecuteRenderPass(passes::Blit(*std::prev(it), *it, {
			.blend_mode = BlendStates::Additive,
			.effect = effects::BloomUpsample()
		}));
		ViewStage("upsample", *it);
	}

	// combine

	ExecuteRenderPass(passes::Blit(*tex_chain.begin(), dst, {
		.color = glm::vec4(intensity),
		.blend_mode = BlendStates::Additive,
		.effect = effects::BloomUpsample()
	}));

	// release targets

	ReleaseTransientRenderTarget(bright);

	for (auto target : tex_chain)
	{
		ReleaseTransientRenderTarget(target);
	}
}

void utils::techniques::BloomGaussian(RenderTarget* src, RenderTarget* dst, float bright_threshold,
	float intensity)
{
	ExecuteRenderPass(passes::Blit(src, dst));

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
		ExecuteRenderPass(passes::Blit(src, bright, {
			.clear = true,
			.effect = effects::BrightFilter(bright_threshold)
		}));
		ViewStage("bright", bright);
		blur_src = bright;
	}

	GaussianBlur(blur_src, blur_dst);

	ExecuteRenderPass(passes::Blit(blur_dst, dst, {
		.color = glm::vec4(intensity),
		.blend_mode = BlendStates::Additive
	}));

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

	std::vector<Command> draw_models;

	for (const auto& model : models)
	{
		auto cmds = Model::Draw(model, options.use_color_textures, options.use_normal_textures);
		std::ranges::move(cmds, std::back_inserter(draw_models));
	}

	auto forward_shading = passes::ForwardShading(target, options.clear_target, camera, options.mipmap_bias,
		draw_models, lights);

	utils::ExecuteRenderPass(forward_shading);
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

	auto color_buffer = AcquireTransientRenderTarget();
	auto normal_buffer = AcquireTransientRenderTarget();
	auto positions_buffer = AcquireTransientRenderTarget();

	auto extract_geometry_pass = passes::DeferredShadingExtractGeometry(camera, models, color_buffer,
		normal_buffer, positions_buffer, {
			.mipmap_bias = options.mipmap_bias,
			.use_color_textures = options.use_color_textures,
			.use_normal_textures = options.use_normal_textures
		});

	auto light_pass = passes::DeferredShadingLightPass(camera, target, options.clear_target, lights, color_buffer,
		normal_buffer, positions_buffer);

	ExecuteRenderPass(extract_geometry_pass);
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
				techniques::Bloom(src, dst, bloom.threshold, bloom.intensity);
			},
			[&](const DrawSceneOptions::GrayscalePosteffect& grayscale) {
				techniques::Grayscale(src, dst, grayscale.intensity);
			},
			[&](const DrawSceneOptions::GaussianBlurPosteffect& blur) {
				techniques::GaussianBlur(src, dst);
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

void utils::scratch::Begin(MeshBuilder::Mode mode, const State& state)
{
	auto& context = GetContext();

	if (!context.scratch.mesh_builder.isBeginAllowed(mode))
		Flush();

	if (context.scratch.state != state)
		Flush();

	context.scratch.state = state;
	context.scratch.mesh_builder.begin(mode);
}

void utils::scratch::Vertex(const vertex::PositionColorTextureNormal& value)
{
	GetContext().scratch.mesh_builder.vertex(value);
}

void utils::scratch::Vertex(const vertex::PositionColorTexture& value)
{
	GetContext().scratch.mesh_builder.vertex(value);
}

void utils::scratch::Vertex(const vertex::PositionColor& value)
{
	GetContext().scratch.mesh_builder.vertex(value);
}

void utils::scratch::Vertex(const glm::vec3& value)
{
	GetContext().scratch.mesh_builder.vertex(value);
}

void utils::scratch::Vertex(const glm::vec2& value)
{
	GetContext().scratch.mesh_builder.vertex(value);
}

void utils::scratch::Color(const glm::vec4& value)
{
	GetContext().scratch.mesh_builder.color(value);
}

void utils::scratch::Color(const glm::vec3& value)
{
	GetContext().scratch.mesh_builder.color(value);
}

void utils::scratch::Normal(const glm::vec3& value)
{
	GetContext().scratch.mesh_builder.normal(value);
}

void utils::scratch::TexCoord(const glm::vec2& value)
{
	GetContext().scratch.mesh_builder.texcoord(value);
}

void utils::scratch::End()
{
	GetContext().scratch.mesh_builder.end();
}

void utils::scratch::Flush()
{
	auto& context = GetContext();

	if (context.scratch.mesh_builder.getVertexCount() == 0)
	{
		context.scratch.mesh_builder.reset(false);
		return;
	}

	context.scratch.mesh_builder.setToMesh(context.scratch.mesh);

	std::vector<Command> cmds;

	if (context.scratch.state.alpha_test_threshold.has_value())
	{
		cmds.push_back(commands::SetEffect(effects::AlphaTest{ context.scratch.state.alpha_test_threshold.value() }));
	}

	cmds.insert(cmds.end(), {
		commands::SetViewport(context.scratch.state.viewport),
		commands::SetScissor(context.scratch.state.scissor),
		commands::SetBlendMode(context.scratch.state.blend_mode),
		commands::SetDepthBias(context.scratch.state.depth_bias),
		commands::SetDepthMode(context.scratch.state.depth_mode),
		commands::SetStencilMode(context.scratch.state.stencil_mode),
		commands::SetCullMode(context.scratch.state.cull_mode),
		commands::SetFrontFace(context.scratch.state.front_face),
		commands::SetSampler(context.scratch.state.sampler),
		commands::SetTextureAddress(context.scratch.state.texaddr),
		commands::SetMipmapBias(context.scratch.state.mipmap_bias),
		commands::SetProjectionMatrix(context.scratch.state.projection_matrix),
		commands::SetViewMatrix(context.scratch.state.view_matrix),
		commands::SetModelMatrix(context.scratch.state.model_matrix),
		commands::SetMesh(&context.scratch.mesh),
		commands::SetColorTexture(context.scratch.state.texture),
		commands::Draw()
	});

	ExecuteCommands(cmds);

	context.scratch.mesh_builder.reset(false);
}

utils::passes::Blit::Blit(Texture* src, RenderTarget* dst, const Options& options) :
	targets({ dst }),
	clear(options.clear)
{
	if (options.effect.has_value())
		commands.push_back(options.effect.value());

	commands.insert(commands.end(), {
		commands::SetSampler(options.sampler),
		commands::SetColor(options.color),
		commands::SetBlendMode(options.blend_mode),
		commands::SetColorTexture(src),
		commands::Draw()
	});
}

std::vector<RenderTarget*> utils::passes::Blit::getTargets() const
{
	return targets;
}

bool utils::passes::Blit::isClear() const
{
	return clear;
}

std::vector<utils::Command> utils::passes::Blit::getCommands() const
{
	return commands;
}

const std::string utils::passes::DeferredShadingExtractGeometry::Effect::Shader = R"(
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

utils::passes::DeferredShadingExtractGeometry::DeferredShadingExtractGeometry(const PerspectiveCamera& camera,
	const std::vector<Model>& models, RenderTarget* color_buffer, RenderTarget* normal_buffer,
	RenderTarget* positions_buffer, Options options) :
	targets({ color_buffer, normal_buffer, positions_buffer })
{
	commands = {
		commands::SetMipmapBias(options.mipmap_bias),
		commands::SetCamera(camera),
		commands::SetEffect(Effect{}),
	};

	for (const auto& model : models)
	{
		auto cmds = Model::Draw(model, options.use_color_textures, options.use_normal_textures);
		std::ranges::move(cmds, std::back_inserter(commands));
	}
}

std::vector<RenderTarget*> utils::passes::DeferredShadingExtractGeometry::getTargets() const
{
	return targets;
}

bool utils::passes::DeferredShadingExtractGeometry::isClear() const
{
	return true;
}

std::vector<utils::Command> utils::passes::DeferredShadingExtractGeometry::getCommands() const
{
	return commands;
}

const std::string utils::passes::DeferredShadingLightPass::DirectionalLightEffect::Shader = R"(
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

const std::string utils::passes::DeferredShadingLightPass::PointLightEffect::Shader = R"(
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

utils::passes::DeferredShadingLightPass::DirectionalLightEffect::DirectionalLightEffect(const utils::DirectionalLight& light) :
	direction(light.direction),
	ambient(light.ambient),
	diffuse(light.diffuse),
	specular(light.specular),
	shininess(light.shininess)
{
}

utils::passes::DeferredShadingLightPass::PointLightEffect::PointLightEffect(const utils::PointLight& light) :
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

utils::passes::DeferredShadingLightPass::DeferredShadingLightPass(const PerspectiveCamera& camera,
	RenderTarget* target, bool _clear, const std::vector<Light>& lights, Texture* color_buffer,
	Texture* normal_buffer, Texture* positions_buffer) :
	clear(_clear)
{
	targets = { target };
	commands = {
		commands::SetEyePosition(camera.position),
		commands::SetBlendMode(BlendStates::Additive),
		commands::SetCustomTexture(5, color_buffer),
		commands::SetCustomTexture(6, normal_buffer),
		commands::SetCustomTexture(7, positions_buffer)
	};

	for (const auto& light : lights)
	{
		commands.insert(commands.end(), {
			std::visit(cases{
				[](const DirectionalLight& light) {
					return commands::SetEffect(DirectionalLightEffect(light));
				},
				[](const PointLight& light) {
					return commands::SetEffect(PointLightEffect(light));
				}
			}, light),
			commands::Draw()
		});
	}
}

std::vector<RenderTarget*> utils::passes::DeferredShadingLightPass::getTargets() const
{
	return targets;
}

bool utils::passes::DeferredShadingLightPass::isClear() const
{
	return clear;
}

std::vector<utils::Command> utils::passes::DeferredShadingLightPass::getCommands() const
{
	return commands;
}

const std::string utils::passes::ForwardShading::DirectionalLightEffect::Shader = R"(
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

const std::string utils::passes::ForwardShading::PointLightEffect::Shader = R"(
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

utils::passes::ForwardShading::DirectionalLightEffect::DirectionalLightEffect(const utils::DirectionalLight& light) :
	direction(light.direction),
	ambient(light.ambient),
	diffuse(light.diffuse),
	specular(light.specular),
	shininess(light.shininess)
{
}

utils::passes::ForwardShading::PointLightEffect::PointLightEffect(const utils::PointLight& light) :
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

utils::passes::ForwardShading::ForwardShading(RenderTarget* target, bool _clear, const PerspectiveCamera& camera,
	float mipmap_bias, const std::vector<Command>& per_light_commands, const std::vector<Light>& lights) :
	targets({ target }),
	clear(_clear)
{
	commands = {
		commands::SetCamera(camera),
		commands::SetMipmapBias(mipmap_bias)
	};

	bool first_light_done = false;

	for (const auto& light : lights)
	{
		commands.insert(commands.end(), {
			std::visit(cases{
				[](const DirectionalLight& light) {
					return commands::SetEffect(DirectionalLightEffect(light));
				},
				[](const PointLight& light) {
					return commands::SetEffect(PointLightEffect(light));
				}
			}, light),
			commands::Subcommands(&per_light_commands)
		});

		if (!first_light_done)
		{
			first_light_done = true;
			commands.push_back(commands::SetBlendMode(BlendStates::Additive));
		}
	}
}

std::vector<RenderTarget*> utils::passes::ForwardShading::getTargets() const
{
	return targets;
}

bool utils::passes::ForwardShading::isClear() const
{
	return clear;
}

std::vector<utils::Command> utils::passes::ForwardShading::getCommands() const
{
	return commands;
}