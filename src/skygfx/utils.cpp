#include "utils.h"

using namespace skygfx;

static const std::string vertex_shader_code = R"(
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

out gl_PerVertex
{
	vec4 gl_Position;
};

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

static const std::string fragment_shader_code_no_light = R"(
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

void main()
{
	result = In.color;
	result *= settings.color;
	result *= texture(sColorTexture, In.tex_coord, settings.mipmap_bias);
})";

static const std::string fragment_shader_code_directional_light = R"(
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

layout(binding = DIRECTIONAL_LIGHT_UNIFORM_BINDING) uniform _light
{
	vec3 direction;
	vec3 ambient;
	vec3 diffuse;
	vec3 specular;
	float shininess;
} light;

layout(location = 0) in struct
{
	vec3 frag_position;
	vec4 color;
	vec2 tex_coord;
	vec3 normal;
} In;

layout(location = 0) out vec4 result;

layout(binding = COLOR_TEXTURE_BINDING) uniform sampler2D sColorTexture;
layout(binding = NORMAL_TEXTURE_BINDING) uniform sampler2D sNormalTexture;

void main()
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

static const std::string fragment_shader_code_point_light = R"(
#version 450 core

layout(binding = POINT_LIGHT_UNIFORM_BINDING) uniform _light
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

layout(binding = SETTINGS_UNIFORM_BINDING) uniform _settings
{
	mat4 projection;
	mat4 view;
	mat4 model;
	vec3 eye_position;
	float mipmap_bias;
	vec4 color;
} settings;

layout(location = 0) in struct {
	vec3 frag_position;
	vec4 color;
	vec2 tex_coord;
	vec3 normal;
} In;

layout(location = 0) out vec4 result;

layout(binding = COLOR_TEXTURE_BINDING) uniform sampler2D sColorTexture;
layout(binding = NORMAL_TEXTURE_BINDING) uniform sampler2D sNormalTexture;

void main()
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

void utils::Mesh::setVertices(const Vertices& value)
{
	mVertices = value;

	size_t size = value.size() * sizeof(Vertices::value_type);
	size_t stride = sizeof(Vertices::value_type);

	mVertexBuffer = EnsureBufferSpace(mVertexBuffer, size, stride);
	mVertexBuffer->write(value);
}

void utils::Mesh::setIndices(const Indices& value)
{
	mIndices = value;
	
	size_t size = value.size() * sizeof(Indices::value_type);
	size_t stride = sizeof(Indices::value_type);

	mIndexBuffer = EnsureBufferSpace(mIndexBuffer, size, stride);
	mIndexBuffer->write(value);
}

void utils::DrawMesh(const Mesh& mesh, const Matrices& matrices, const Material& material,
	std::optional<DrawCommand> draw_command, float mipmap_bias, std::optional<Light> light,
	const glm::vec3& eye_position)
{
	struct alignas(16) Settings
	{
		glm::mat4 projection = glm::mat4(1.0f);
		glm::mat4 view = glm::mat4(1.0f);
		glm::mat4 model = glm::mat4(1.0f);
		alignas(16) glm::vec3 eye_position = { 0.0f, 0.0f, 0.0f };
		float mipmap_bias = 0.0f;
		alignas(16) glm::vec4 color = { 1.0f, 1.0f, 1.0f, 1.0f };
	};

	uint32_t white_pixel = 0xFFFFFFFF;
	static auto white_pixel_texture = Texture(1, 1, 4, &white_pixel);

	const auto& color_texture = material.color_texture != nullptr ? *material.color_texture : white_pixel_texture;
	const auto& normal_texture = material.normal_texture != nullptr ? *material.normal_texture : white_pixel_texture;

	auto settings = Settings{
		.projection = matrices.projection,
		.view = matrices.view,
		.model = matrices.model,
		.eye_position = eye_position,
		.mipmap_bias = mipmap_bias,
		.color = material.color
	};

	if (light.has_value())
	{
		std::visit(cases{
			[&](const DirectionalLight& directional_light) {
				static auto shader = Shader(Mesh::Vertex::Layout, vertex_shader_code, fragment_shader_code_directional_light, {
					"COLOR_TEXTURE_BINDING 0",
					"NORMAL_TEXTURE_BINDING 1",
					"SETTINGS_UNIFORM_BINDING 2",
					"DIRECTIONAL_LIGHT_UNIFORM_BINDING 3"
				});

				SetShader(shader);
				SetTexture(0, color_texture);
				SetTexture(1, normal_texture);
				SetDynamicUniformBuffer(2, settings);
				SetDynamicUniformBuffer(3, directional_light);
			},
			[&](const PointLight& point_light) {
				static auto shader = Shader(Mesh::Vertex::Layout, vertex_shader_code, fragment_shader_code_point_light, {
					"COLOR_TEXTURE_BINDING 0",
					"NORMAL_TEXTURE_BINDING 1",
					"SETTINGS_UNIFORM_BINDING 2",
					"POINT_LIGHT_UNIFORM_BINDING 3"
				});

				SetShader(shader);
				SetTexture(0, color_texture);
				SetTexture(1, normal_texture);
				SetDynamicUniformBuffer(2, settings);
				SetDynamicUniformBuffer(3, point_light);
			},
		}, light.value());
	}
	else
	{
		static auto shader = Shader(Mesh::Vertex::Layout, vertex_shader_code, fragment_shader_code_no_light, {
			"COLOR_TEXTURE_BINDING 0",
			"SETTINGS_UNIFORM_BINDING 1"
		});

		SetShader(shader);
		SetTexture(0, color_texture);
		SetDynamicUniformBuffer(1, settings);
	}

	auto topology = mesh.getTopology();
	const auto& vertex_buffer = mesh.getVertexBuffer();

	SetTopology(topology);
	SetVertexBuffer(vertex_buffer);

	if (!draw_command.has_value())
	{
		if (mesh.getIndices().empty())
			draw_command = DrawVerticesCommand{};
		else
			draw_command = DrawIndexedVerticesCommand{};
	}

	std::visit(cases{
		[&](const DrawVerticesCommand& draw) {
			const auto& vertices = mesh.getVertices();
			
			auto vertex_count = draw.vertex_count.value_or(static_cast<uint32_t>(vertices.size()));
			auto vertex_offset = draw.vertex_offset;

			Draw(vertex_count, vertex_offset);
		},
		[&](const DrawIndexedVerticesCommand& draw) {
			const auto& indices = mesh.getIndices();
			const auto& index_buffer = mesh.getIndexBuffer();
			
			SetIndexBuffer(index_buffer);
			
			auto index_count = draw.index_count.value_or(static_cast<uint32_t>(indices.size()));
			auto index_offset = draw.index_offset;

			DrawIndexed(index_count, index_offset);
		}
	}, draw_command.value());
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

void utils::DrawMesh(const Mesh& mesh, const Camera& camera, const glm::mat4& model,
	const Material& material, std::optional<DrawCommand> draw_command,
	float mipmap_bias, std::optional<Light> light)
{
	auto [proj, view, eye_pos] = MakeCameraMatrices(camera);

	auto matrices = Matrices{
		.projection = proj,
		.view = view,
		.model = model
	};

	DrawMesh(mesh, matrices, material, draw_command, mipmap_bias, light, eye_pos);
}

std::shared_ptr<VertexBuffer> utils::EnsureBufferSpace(std::shared_ptr<VertexBuffer> buffer, size_t size, size_t stride)
{
	size_t buffer_size = 0;

	if (buffer)
		buffer_size = buffer->getSize();

	if (buffer_size < size)
		buffer = std::make_shared<VertexBuffer>(size, stride);
		
	return buffer;
}

std::shared_ptr<IndexBuffer> utils::EnsureBufferSpace(std::shared_ptr<IndexBuffer> buffer, size_t size, size_t stride)
{
	size_t buffer_size = 0;

	if (buffer)
		buffer_size = buffer->getSize();

	if (buffer_size < size)
		buffer = std::make_shared<IndexBuffer>(size, stride);
		
	return buffer;
}

std::shared_ptr<UniformBuffer> utils::EnsureBufferSpace(std::shared_ptr<UniformBuffer> buffer, size_t size)
{
	size_t buffer_size = 0;

	if (buffer)
		buffer_size = buffer->getSize();

	if (buffer_size < size)
		buffer = std::make_shared<UniformBuffer>(size);
		
	return buffer;
}
