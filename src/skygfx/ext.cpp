#include "ext.h"

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

void ext::Mesh::setVertices(const Vertices& value)
{
	mVertices = value;

	size_t size = value.size() * sizeof(Vertices::value_type);
	size_t stride = sizeof(Vertices::value_type);

	if (!mVertexBuffer.has_value() || mVertexBuffer.value().getSize() < size)
		mVertexBuffer.emplace(size, stride);

	mVertexBuffer.value().write(value);
}

void ext::Mesh::setIndices(const Indices& value)
{
	mIndices = value;
	
	size_t size = value.size() * sizeof(Indices::value_type);
	size_t stride = sizeof(Indices::value_type);

	if (!mIndexBuffer.has_value() || mIndexBuffer.value().getSize() < size)
		mIndexBuffer.emplace(size, stride);

	mIndexBuffer.value().write(value);
}

std::tuple<glm::mat4/*proj*/, glm::mat4/*view*/, glm::vec3/*eye_pos*/> ext::MakeCameraMatrices(const Camera& camera, 
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

void ext::SetMesh(Commands& cmds, const Mesh* mesh)
{
	cmds.push_back(commands::SetMesh{
		.mesh = const_cast<Mesh*>(mesh)
	});
}

void ext::SetLight(Commands& cmds, Light light)
{
	cmds.push_back(commands::SetLight{
		.light = light
	});
}

void ext::SetColorTexture(Commands& cmds, Texture* color_texture)
{
	cmds.push_back(commands::SetColorTexture{
		.color_texture = color_texture
	});
}

void ext::SetNormalTexture(Commands& cmds, Texture* normal_texture)
{
	cmds.push_back(commands::SetNormalTexture{
		.normal_texture = normal_texture
	});
}

void ext::SetColor(Commands& cmds, glm::vec4 color)
{
	cmds.push_back(commands::SetColor{
		.color = color
	});
}

void ext::SetProjectionMatrix(Commands& cmds, glm::mat4 projection_matrix)
{
	cmds.push_back(commands::SetProjectionMatrix{
		.projection_matrix = projection_matrix
	});
}

void ext::SetViewMatrix(Commands& cmds, glm::mat4 view_matrix)
{
	cmds.push_back(commands::SetViewMatrix{
		.view_matrix = view_matrix
	});
}

void ext::SetModelMatrix(Commands& cmds, glm::mat4 model_matrix)
{
	cmds.push_back(commands::SetModelMatrix{
		.model_matrix = model_matrix
	});
}

void ext::SetCamera(Commands& cmds, Camera camera)
{
	cmds.push_back(commands::SetCamera{
		.camera = camera
	});
}

void ext::SetEyePosition(Commands& cmds, glm::vec3 eye_position)
{
	cmds.push_back(commands::SetEyePosition{
		.eye_position = eye_position	
	});
}

void ext::SetMipmapBias(Commands& cmds, float mipmap_bias)
{
	cmds.push_back(commands::SetMipmapBias{
		.mipmap_bias = mipmap_bias
	});
}

void ext::Callback(Commands& cmds, std::function<void()> func)
{
	cmds.push_back(commands::Callback{
		.func = func
	});
}

void ext::InsertSubcommands(Commands& cmds, const Commands* subcommands)
{
	cmds.push_back(commands::InsertSubcommands{
		.subcommands = const_cast<Commands*>(subcommands)
	});
}

void ext::Draw(Commands& cmds, std::optional<DrawCommand> draw_command)
{
	cmds.push_back(commands::Draw{
		.draw_command = draw_command
	});
}

void ext::ExecuteCommands(const Commands& cmds)
{
	Mesh* mesh = nullptr;
	bool mesh_dirty = true;

	Light light = NoLight{};
	bool light_dirty = true;

	Shader* shader = nullptr;

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
	};

	auto settings = Settings{};
	bool settings_dirty = true;

	std::function<void(const Command&)> execute_command = [&](const Command& _cmd) {
		std::visit(cases{
			[&](const commands::SetMesh& cmd) {
				if (mesh == cmd.mesh)
					return;

				mesh = cmd.mesh;
				mesh_dirty = true;
			},
			[&](const commands::SetLight& cmd) {
				light = cmd.light;
				light_dirty = true;
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
			[&](const commands::InsertSubcommands& cmd) {
				for (const auto& subcommand : *cmd.subcommands)
				{
					execute_command(subcommand);
				}
			},
			[&](const commands::Draw& cmd) {
				assert(mesh != nullptr);

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

				if (light_dirty)
				{
					auto prev_shader = shader;

					auto shader = std::visit(cases{
						[&](const NoLight& no_light) {
							static auto no_light_shader = Shader(Mesh::Vertex::Layout, vertex_shader_code, fragment_shader_code_no_light, {
								"COLOR_TEXTURE_BINDING 0",
								"SETTINGS_UNIFORM_BINDING 1"
							});
							return &no_light_shader;
						},
						[&](const DirectionalLight& directional_light) {
							static auto directional_light_shader = Shader(Mesh::Vertex::Layout, vertex_shader_code, fragment_shader_code_directional_light, {
								"COLOR_TEXTURE_BINDING 0",
								"NORMAL_TEXTURE_BINDING 1",
								"SETTINGS_UNIFORM_BINDING 2",
								"DIRECTIONAL_LIGHT_UNIFORM_BINDING 3"
							});
							return &directional_light_shader;
						},
						[&](const PointLight& point_light) {
							static auto point_light_shader = Shader(Mesh::Vertex::Layout, vertex_shader_code, fragment_shader_code_point_light, {
								"COLOR_TEXTURE_BINDING 0",
								"NORMAL_TEXTURE_BINDING 1",
								"SETTINGS_UNIFORM_BINDING 2",
								"POINT_LIGHT_UNIFORM_BINDING 3"
							});
							return &point_light_shader;
						},
					}, light);

					if (shader != prev_shader)
					{
						SetShader(*shader);
						textures_dirty = true;
						settings_dirty = true;
					}

					std::visit(cases{
						[&](const NoLight& no_light) {
						},
						[&](const DirectionalLight& directional_light) {
							SetDynamicUniformBuffer(3, directional_light);
						},
						[&](const PointLight& point_light) {
							SetDynamicUniformBuffer(3, point_light);
						},
					}, light);

					light_dirty = false;
				}

				if (textures_dirty)
				{
					uint32_t white_pixel = 0xFFFFFFFF;
					static auto white_pixel_texture = Texture(1, 1, 4, &white_pixel);

					const auto& _color_texture = color_texture != nullptr ? *color_texture : white_pixel_texture;
					const auto& _normal_texture = normal_texture != nullptr ? *normal_texture : white_pixel_texture;

					std::visit(cases{
						[&](const NoLight& no_light) {
							SetTexture(0, _color_texture);
						},
						[&](const DirectionalLight& directional_light) {
							SetTexture(0, _color_texture);
							SetTexture(1, _normal_texture);
						},
						[&](const PointLight& point_light) {
							SetTexture(0, _color_texture);
							SetTexture(1, _normal_texture);
						},
					}, light);

					textures_dirty = false;
				}

				if (settings_dirty)
				{
					std::visit(cases{
						[&](const NoLight& no_light) {
							SetDynamicUniformBuffer(1, settings);
						},
						[&](const DirectionalLight& directional_light) {
							SetDynamicUniformBuffer(2, settings);
						},
						[&](const PointLight& point_light) {
							SetDynamicUniformBuffer(2, settings);
						},
					}, light);

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
