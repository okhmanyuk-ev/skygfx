#include "extended.h"

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
	result = In.color * texture(sColorTexture, In.tex_coord, settings.mipmap_bias);
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
	result = In.color * texture(sColorTexture, In.tex_coord, settings.mipmap_bias);

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
	result = In.color * texture(sColorTexture, In.tex_coord, settings.mipmap_bias);

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

void extended::Mesh::setVertices(const Vertices& value)
{
	mVertices = value;

	size_t size = value.size() * sizeof(Vertices::value_type);
	size_t stride = sizeof(Vertices::value_type);

	mVertexBuffer = EnsureBufferSpace(mVertexBuffer, size, stride);
	mVertexBuffer->write(value);
}

void extended::Mesh::setIndices(const Indices& value)
{
	mIndices = value;
	
	size_t size = value.size() * sizeof(Indices::value_type);
	size_t stride = sizeof(Indices::value_type);

	mIndexBuffer = EnsureBufferSpace(mIndexBuffer, size, stride);
	mIndexBuffer->write(value);
}

void extended::DrawMesh(const Mesh& mesh, const Matrices& matrices, const Material& material,
	float mipmap_bias, const Light& light, const glm::vec3& eye_position)
{
	struct alignas(16) Settings
	{
		glm::mat4 projection = glm::mat4(1.0f);
		glm::mat4 view = glm::mat4(1.0f);
		glm::mat4 model = glm::mat4(1.0f);
		alignas(16) glm::vec3 eye_position = { 0.0f, 0.0f, 0.0f };
		float mipmap_bias = 0.0f;
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
		.mipmap_bias = mipmap_bias
	};

	if (light.has_value())
	{
		std::visit(cases{
			[&](const DirectionalLight& directional_light) {
				static auto shader = skygfx::Shader(Mesh::Vertex::Layout, vertex_shader_code, fragment_shader_code_directional_light, {
					"COLOR_TEXTURE_BINDING 0",
					"NORMAL_TEXTURE_BINDING 1",
					"SETTINGS_UNIFORM_BINDING 2",
					"DIRECTIONAL_LIGHT_UNIFORM_BINDING 3"
				});

				skygfx::SetShader(shader);
				skygfx::SetTexture(0, color_texture);
				skygfx::SetTexture(1, normal_texture);
				skygfx::SetDynamicUniformBuffer(2, settings);
				skygfx::SetDynamicUniformBuffer(3, directional_light);
			},
			[&](const PointLight& point_light) {
				static auto shader = skygfx::Shader(Mesh::Vertex::Layout, vertex_shader_code, fragment_shader_code_point_light, {
					"COLOR_TEXTURE_BINDING 0",
					"NORMAL_TEXTURE_BINDING 1",
					"SETTINGS_UNIFORM_BINDING 2",
					"POINT_LIGHT_UNIFORM_BINDING 3"
				});

				skygfx::SetShader(shader);
				skygfx::SetTexture(0, color_texture);
				skygfx::SetTexture(1, normal_texture);
				skygfx::SetDynamicUniformBuffer(2, settings);
				skygfx::SetDynamicUniformBuffer(3, point_light);
			},
		}, light.value());
	}
	else
	{
		static auto shader = skygfx::Shader(Mesh::Vertex::Layout, vertex_shader_code, fragment_shader_code_no_light, {
			"COLOR_TEXTURE_BINDING 0",
			"SETTINGS_UNIFORM_BINDING 1"
		});

		skygfx::SetShader(shader);
		skygfx::SetTexture(0, color_texture);
		skygfx::SetDynamicUniformBuffer(1, settings);
	}

	auto topology = mesh.getTopology();
	const auto& vertex_buffer = mesh.getVertexBuffer();

	skygfx::SetTopology(topology);
	skygfx::SetVertexBuffer(vertex_buffer);

	const auto& drawing_type = mesh.getDrawingType();

	std::visit(cases{
		[&](const Mesh::DrawVertices& draw) {
			const auto& vertices = mesh.getVertices();
		
			skygfx::Draw(draw.vertex_count.value_or(static_cast<uint32_t>(vertices.size())),
				draw.vertex_offset);
		},
		[&](const Mesh::DrawIndexedVertices& draw) {
			const auto& indices = mesh.getIndices();
			const auto& index_buffer = mesh.getIndexBuffer();
			
			skygfx::SetIndexBuffer(index_buffer);
			
			skygfx::DrawIndexed(draw.index_count.value_or(static_cast<uint32_t>(indices.size())),
				draw.index_offset);
		}
	}, drawing_type);
}

void extended::DrawMesh(const Mesh& mesh, const Camera& camera, const glm::mat4& model,
	const Material& material, float mipmap_bias, const Light& light)
{
	glm::vec3 eye_position = { 0.0f, 0.0f, 0.0f };

	auto width = (float)skygfx::GetBackbufferWidth();
	auto height = (float)skygfx::GetBackbufferHeight();

	auto matrices = std::visit(cases{
		[&](const OrthogonalCamera& camera) {
			return Matrices{
				.projection = glm::orthoLH(0.0f, width, height, 0.0f, -1.0f, 1.0f),
				.view = glm::lookAtLH(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f),
					glm::vec3(0.0f, 1.0f, 0.0f))
			};
		},
		[&](const PerspectiveCamera& camera) {
			auto sin_yaw = glm::sin(camera.yaw);
			auto sin_pitch = glm::sin(camera.pitch);

			auto cos_yaw = glm::cos(camera.yaw);
			auto cos_pitch = glm::cos(camera.pitch);

			auto front = glm::normalize(glm::vec3(cos_yaw * cos_pitch, sin_pitch, sin_yaw * cos_pitch));
			auto right = glm::normalize(glm::cross(front, camera.world_up));
			auto up = glm::normalize(glm::cross(right, front));
			
			eye_position = camera.position;
			
			return Matrices{
				.projection = glm::perspectiveFov(camera.fov, width, height, camera.near_plane, camera.far_plane),
				.view = glm::lookAtRH(camera.position, camera.position + front, up)
			};
		}
	}, camera);
	
	matrices.model = model;

	DrawMesh(mesh, matrices, material, mipmap_bias, light, eye_position);
}

std::shared_ptr<skygfx::VertexBuffer> extended::EnsureBufferSpace(std::shared_ptr<skygfx::VertexBuffer> buffer, size_t size, size_t stride)
{
	size_t buffer_size = 0;

	if (buffer)
		buffer_size = buffer->getSize();

	if (buffer_size < size)
		buffer = std::make_shared<skygfx::VertexBuffer>(size, stride);
		
	return buffer;
}

std::shared_ptr<skygfx::IndexBuffer> extended::EnsureBufferSpace(std::shared_ptr<skygfx::IndexBuffer> buffer, size_t size, size_t stride)
{
	size_t buffer_size = 0;

	if (buffer)
		buffer_size = buffer->getSize();

	if (buffer_size < size)
		buffer = std::make_shared<skygfx::IndexBuffer>(size, stride);
		
	return buffer;
}

std::shared_ptr<skygfx::UniformBuffer> extended::EnsureBufferSpace(std::shared_ptr<skygfx::UniformBuffer> buffer, size_t size)
{
	size_t buffer_size = 0;

	if (buffer)
		buffer_size = buffer->getSize();

	if (buffer_size < size)
		buffer = std::make_shared<skygfx::UniformBuffer>(size);
		
	return buffer;
}
