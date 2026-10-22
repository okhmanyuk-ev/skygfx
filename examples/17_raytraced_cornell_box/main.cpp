#include <skygfx/skygfx.h>
#include <skygfx/utils.h>
#include "../utils/utils.h"
#include "../utils/imgui_helper.h"
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <glm/gtc/matrix_transform.hpp>
#include <numbers>

// Whitted-style raytracer with soft shadows from an area light and photon traced caustics and indirect light.
// Every frame:
// 1. clear pass - zeroes caustic maps of the room surfaces
// 2. photon pass - shoots photons from the light towards the moving glass objects, the ones that reach a room
//    surface through glass go to its caustic map
// 3. render pass - traces camera rays, splitting them on glass into reflection and refraction
// Once at start the photon pass also shoots photons over the whole hemisphere, the ones that reach a room surface
// after diffuse bounces or the mirror sphere go to its indirect map. Moving glass objects barely change it,
// so these photons pass through them.

const uint32_t CausticMapResolution = 160;
const uint32_t IndirectMapResolution = 48;
const uint32_t PhotonGridSize = 384;
const uint32_t CausticPhotonLayers = 2; // one per moving glass object
const uint32_t IndirectPhotonLayers = 64;

const std::string common_code = R"(
const uint MaterialDiffuse = 0;
const uint MaterialMirror = 1;
const uint MaterialEmissive = 2;
const uint MaterialGlass = 3;

const float PI = 3.14159265359;

struct Payload
{
	vec3 color;
	float distance;
	vec3 normal; // faces the ray
	uint material;
	vec3 geometry_normal; // faces the ray
	uint object;
	uint front_face;
};

struct Object
{
	uint index_offset;
	uint material;
	uint checker;
	float checker_scale;
	vec4 color;
	vec4 checker_color;
	uvec4 light_map; // x - map index + 1 (0 if none), y - u axis, z - v axis
};

layout(binding = 3) buffer Objects { Object objects[]; };
layout(binding = 7) buffer LightMaps { uint light_maps[]; };
)";

const std::string raygen_common_code = common_code + R"(
layout(binding = 0) uniform accelerationStructureEXT topLevelAS;

layout(binding = 2) uniform _settings
{
	vec4 eye;
	vec4 forward;
	vec4 right;
	vec4 up;
	vec4 light; // xyz - center, w - radius
	vec4 light_color;
	vec4 room_min;
	vec4 room_max;
	vec4 ambient;
	vec4 caustic_targets[2]; // xyz - center, w - bounding radius
	uvec4 caustic_target_objects;
	uint shadow_samples;
	uint caustic_resolution;
	uint indirect_resolution;
	uint indirect_offset;
	float light_map_scale;
	float ior;
	float exposure;
	uint caustics;
	uint indirect;
} settings;

layout(location = 0) rayPayloadEXT Payload payload;
layout(location = 1) rayPayloadEXT bool visible;

struct LightMap
{
	uint offset;
	uint resolution;
};

LightMap causticMap()
{
	return LightMap(0, settings.caustic_resolution);
}

LightMap indirectMap()
{
	return LightMap(settings.indirect_offset, settings.indirect_resolution);
}

uint rng_state;

uint pcg()
{
	uint state = rng_state * 747796405u + 2891336453u;
	rng_state = state;
	uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
	return (word >> 22u) ^ word;
}

float random()
{
	return float(pcg()) / 4294967296.0;
}

bool traceScene(vec3 origin, vec3 direction)
{
	payload.distance = -1.0;
	traceRayEXT(topLevelAS, gl_RayFlagsOpaqueEXT, 0xFF, 0, 0, 0, origin, 0.0, direction, 100.0, 0);
	return payload.distance >= 0.0;
}

float fresnel(float cos_i, float n1, float n2)
{
	float r0 = (n1 - n2) / (n1 + n2);
	r0 *= r0;
	return r0 + (1.0 - r0) * pow(1.0 - cos_i, 5.0);
}

uint lightMapCell(LightMap map, Object object, ivec2 cell, uint channel)
{
	uint index = object.light_map.x - 1;
	return map.offset + ((index * map.resolution + uint(cell.y)) * map.resolution + uint(cell.x)) * 3 + channel;
}

vec2 lightMapCoord(LightMap map, Object object, vec3 pos)
{
	vec3 uvw = (pos - settings.room_min.xyz) / (settings.room_max.xyz - settings.room_min.xyz);
	return vec2(uvw[object.light_map.y], uvw[object.light_map.z]) * float(map.resolution);
}

float lightMapCellArea(LightMap map, Object object)
{
	vec3 size = settings.room_max.xyz - settings.room_min.xyz;
	return size[object.light_map.y] * size[object.light_map.z] / float(map.resolution * map.resolution);
}

vec3 readLightMap(LightMap map, Object object, ivec2 cell)
{
	cell = clamp(cell, ivec2(0), ivec2(int(map.resolution) - 1));
	vec3 value;
	for (uint c = 0; c < 3; c++)
	{
		value[c] = float(light_maps[lightMapCell(map, object, cell, c)]);
	}
	return value / settings.light_map_scale;
}

vec3 cosineSampleHemisphere(vec3 n, vec2 u)
{
	float phi = 2.0 * PI * u.x;
	float r = sqrt(u.y);
	vec3 t = normalize(cross(n, abs(n.y) < 0.99 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0)));
	vec3 b = cross(n, t);
	return normalize(t * cos(phi) * r + b * sin(phi) * r + n * sqrt(1.0 - u.y));
}
)";

const std::string render_raygen_shader_code = R"(
#version 460
#extension GL_EXT_ray_tracing : require
)" + raygen_common_code + R"(
layout(binding = 1, rgba32f) uniform image2D image;

const uint MaxDepth = 8;
const uint StackSize = 12;

struct Ray
{
	vec3 origin;
	vec3 direction;
	vec3 throughput;
	uint depth;
};

bool isVisible(vec3 origin, vec3 direction, float distance)
{
	uint flags = gl_RayFlagsTerminateOnFirstHitEXT | gl_RayFlagsOpaqueEXT | gl_RayFlagsSkipClosestHitShaderEXT;
	visible = false;
	traceRayEXT(topLevelAS, flags, 0xFF, 0, 0, 1, origin, 0.0, direction, distance, 1);
	return visible;
}

// a fixed set of points spread over the light disk gives smooth noise-free penumbras,
// a per-pixel rotation of the set turns banding into fine static grain
vec3 directLight(vec3 pos, vec3 normal, uint samples, float rotation)
{
	float radius = settings.light.w;
	float sum = 0.0;

	for (uint i = 0; i < samples; i++)
	{
		float r = radius * sqrt((float(i) + 0.5) / float(samples));
		float angle = float(i) * 2.39996323 + rotation;
		vec3 light_pos = settings.light.xyz + vec3(r * cos(angle), 0.0, r * sin(angle));
		vec3 to_light = light_pos - pos;
		float distance_sqr = dot(to_light, to_light);
		float distance = sqrt(distance_sqr);
		vec3 direction = to_light / distance;
		float cos_surface = dot(normal, direction);

		float cos_light = direction.y; // the light faces down

		if (cos_surface <= 0.0 || cos_light <= 0.0)
			continue;

		if (!isVisible(pos, direction, distance - 0.001))
			continue;

		sum += cos_surface * cos_light / distance_sqr;
	}

	float area = PI * radius * radius;
	return settings.light_color.rgb * sum * area / float(samples);
}

vec3 causticIrradiance(Object object, vec3 pos)
{
	LightMap map = causticMap();
	ivec2 center = ivec2(floor(lightMapCoord(map, object, pos)));
	const float weights[5] = float[](1.0, 4.0, 6.0, 4.0, 1.0);
	vec3 sum = vec3(0.0);

	for (int y = -2; y <= 2; y++)
	{
		for (int x = -2; x <= 2; x++)
		{
			sum += readLightMap(map, object, center + ivec2(x, y)) * weights[x + 2] * weights[y + 2] / 256.0;
		}
	}

	return sum;
}

vec3 indirectIrradiance(Object object, vec3 pos)
{
	LightMap map = indirectMap();
	vec2 coord = lightMapCoord(map, object, pos) - 0.5;
	ivec2 base = ivec2(floor(coord));
	vec2 f = coord - vec2(base);

	return mix(
		mix(readLightMap(map, object, base), readLightMap(map, object, base + ivec2(1, 0)), f.x),
		mix(readLightMap(map, object, base + ivec2(0, 1)), readLightMap(map, object, base + ivec2(1, 1)), f.x),
		f.y);
}

// the light hangs a bit below the ceiling and glows from both sides, nothing is between its back side
// and the ceiling, so the ceiling's irradiance from it is computed exactly, without noisy shadow rays
vec3 lightBackSideIrradiance(vec3 pos, vec3 normal)
{
	float h = pos.y - settings.light.y;

	if (normal.y > -0.99 || h <= 0.0)
		return vec3(0.0);

	float r = settings.light.w;
	float rho = length(pos.xz - settings.light.xz);
	float a = rho * rho + h * h + r * r;
	float form_factor = 0.5 * (1.0 - (a - 2.0 * r * r) / sqrt(a * a - 4.0 * rho * rho * r * r));
	return settings.light_color.rgb * PI * form_factor;
}

vec3 tonemap(vec3 x)
{
	const float a = 2.51;
	const float b = 0.03;
	const float c = 2.43;
	const float d = 0.59;
	const float e = 0.14;
	return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

void main()
{
	uvec2 pixel = gl_LaunchIDEXT.xy;
	uvec2 size = gl_LaunchSizeEXT.xy;

	float noise = fract(52.9829189 * fract(dot(vec2(pixel), vec2(0.06711056, 0.00583715))));
	float rotation = noise * 2.0 * PI;

	vec2 d = (vec2(pixel) + 0.5) / vec2(size) * 2.0 - 1.0;
	vec3 direction = normalize(settings.forward.xyz + settings.right.xyz * d.x - settings.up.xyz * d.y);

	Ray stack[StackSize];
	uint stack_size = 0;
	stack[stack_size++] = Ray(settings.eye.xyz, direction, vec3(1.0), 0);

	vec3 color = vec3(0.0);

	while (stack_size > 0)
	{
		Ray ray = stack[--stack_size];

		if (!traceScene(ray.origin, ray.direction))
			continue;

		Object object = objects[payload.object];
		vec3 pos = ray.origin + ray.direction * payload.distance;

		if (payload.material == MaterialEmissive)
		{
			color += ray.throughput * settings.light_color.rgb;
			continue;
		}

		if (payload.material == MaterialDiffuse)
		{
			vec3 offset_pos = pos + payload.geometry_normal * 0.001;
			uint samples = ray.depth == 0 ? settings.shadow_samples : max(settings.shadow_samples / 4, 1);
			vec3 irradiance = directLight(offset_pos, payload.normal, samples, rotation) +
				lightBackSideIrradiance(pos, payload.normal);

			if (object.light_map.x != 0 && settings.caustics != 0)
				irradiance += causticIrradiance(object, pos);

			if (object.light_map.x != 0 && settings.indirect != 0)
				irradiance += indirectIrradiance(object, pos);

			color += ray.throughput * payload.color * (irradiance / PI + settings.ambient.rgb);
			continue;
		}

		if (ray.depth >= MaxDepth)
			continue;

		vec3 reflect_origin = pos + payload.geometry_normal * 0.001;
		vec3 reflect_direction = reflect(ray.direction, payload.normal);

		if (payload.material == MaterialMirror)
		{
			if (stack_size < StackSize)
				stack[stack_size++] = Ray(reflect_origin, reflect_direction, ray.throughput * payload.color, ray.depth + 1);
			continue;
		}

		// glass

		float n1 = payload.front_face != 0 ? 1.0 : settings.ior;
		float n2 = payload.front_face != 0 ? settings.ior : 1.0;
		vec3 refract_direction = refract(ray.direction, payload.normal, n1 / n2);
		float reflectance = 1.0;

		if (dot(refract_direction, refract_direction) > 0.0)
			reflectance = fresnel(-dot(ray.direction, payload.normal), n1, n2);

		vec3 reflect_throughput = ray.throughput * reflectance;
		vec3 refract_throughput = ray.throughput * (1.0 - reflectance) * payload.color;

		if (max(reflect_throughput.r, max(reflect_throughput.g, reflect_throughput.b)) > 0.01 && stack_size < StackSize)
			stack[stack_size++] = Ray(reflect_origin, reflect_direction, reflect_throughput, ray.depth + 1);

		if (max(refract_throughput.r, max(refract_throughput.g, refract_throughput.b)) > 0.01 && stack_size < StackSize)
			stack[stack_size++] = Ray(pos - payload.geometry_normal * 0.001, refract_direction, refract_throughput, ray.depth + 1);
	}

	vec3 result = pow(tonemap(color * settings.exposure), vec3(1.0 / 2.2));

	imageStore(image, ivec2(pixel), vec4(result, 1.0));
})";

const std::string photon_raygen_shader_code = R"(
#version 460
#extension GL_EXT_ray_tracing : require
)" + raygen_common_code + R"(
const uint MaxDepth = 8;
const uint StackSize = 12;
const uint IndirectBounces = 2;
const uint CausticLayers = 2;

struct Photon
{
	vec3 origin;
	vec3 direction;
	vec3 power;
	uint depth;
};

// power is what arrives at the surface, the surface's albedo is applied when it is rendered
void splat(LightMap map, Object object, vec3 pos, vec3 power)
{
	vec2 coord = lightMapCoord(map, object, pos) - 0.5;
	ivec2 base = ivec2(floor(coord));
	vec2 f = coord - vec2(base);
	vec3 irradiance = power / lightMapCellArea(map, object) * settings.light_map_scale;
	int res = int(map.resolution);

	for (int y = 0; y <= 1; y++)
	{
		for (int x = 0; x <= 1; x++)
		{
			ivec2 cell = base + ivec2(x, y);

			if (any(lessThan(cell, ivec2(0))) || any(greaterThanEqual(cell, ivec2(res))))
				continue;

			float weight = (x == 0 ? 1.0 - f.x : f.x) * (y == 0 ? 1.0 - f.y : f.y);

			for (uint c = 0; c < 3; c++)
			{
				atomicAdd(light_maps[lightMapCell(map, object, cell, c)], uint(irradiance[c] * weight + 0.5));
			}
		}
	}
}

vec3 sampleLightOrigin()
{
	float radius = settings.light.w;
	float r = radius * sqrt(random());
	float angle = 2.0 * PI * random();
	return settings.light.xyz + vec3(r * cos(angle), 0.0, r * sin(angle));
}

void traceCausticPhoton(uvec3 id, uvec3 size)
{
	vec4 target = settings.caustic_targets[id.z];
	uint target_object = settings.caustic_target_objects[id.z];
	vec3 origin = sampleLightOrigin();

	// sample a direction inside the cone that encloses the target's bounding sphere

	vec3 to_target = target.xyz - origin;
	float distance = length(to_target);
	vec3 axis = to_target / distance;
	float sin_max = min(target.w / distance, 1.0);
	float cos_max = sqrt(1.0 - sin_max * sin_max);

	vec2 u = (vec2(id.xy) + vec2(random(), random())) / vec2(size.xy);
	float cos_theta = 1.0 - u.x * (1.0 - cos_max);
	float sin_theta = sqrt(1.0 - cos_theta * cos_theta);
	float phi = 2.0 * PI * u.y;

	vec3 tangent = normalize(cross(axis, abs(axis.y) < 0.99 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0)));
	vec3 bitangent = cross(axis, tangent);
	vec3 direction = normalize(tangent * cos(phi) * sin_theta + bitangent * sin(phi) * sin_theta + axis * cos_theta);

	float cos_light = -direction.y;

	if (cos_light <= 0.0)
		return;

	float area = PI * settings.light.w * settings.light.w;
	float solid_angle = 2.0 * PI * (1.0 - cos_max);
	vec3 power = settings.light_color.rgb * cos_light * area * solid_angle / float(size.x * size.y);
	float min_power = max(power.r, max(power.g, power.b)) * 0.01;

	// photons are split on glass instead of picking one way randomly, so dim reflected caustics stay smooth
	Photon stack[StackSize];
	uint stack_size = 0;
	stack[stack_size++] = Photon(origin, direction, power, 0);

	while (stack_size > 0)
	{
		Photon photon = stack[--stack_size];

		if (!traceScene(photon.origin, photon.direction))
			continue;

		// photons that hit something else first belong to another target or to direct lighting
		if (photon.depth == 0 && payload.object != target_object)
			continue;

		Object object = objects[payload.object];
		vec3 pos = photon.origin + photon.direction * payload.distance;

		if (payload.material == MaterialDiffuse)
		{
			if (photon.depth > 0 && object.light_map.x != 0)
				splat(causticMap(), object, pos, photon.power);
			continue;
		}

		if (payload.material == MaterialEmissive || photon.depth >= MaxDepth)
			continue;

		vec3 reflect_origin = pos + payload.geometry_normal * 0.001;
		vec3 reflect_direction = reflect(photon.direction, payload.normal);

		if (payload.material == MaterialMirror)
		{
			if (stack_size < StackSize)
				stack[stack_size++] = Photon(reflect_origin, reflect_direction, photon.power * payload.color, photon.depth + 1);
			continue;
		}

		float n1 = payload.front_face != 0 ? 1.0 : settings.ior;
		float n2 = payload.front_face != 0 ? settings.ior : 1.0;
		vec3 refract_direction = refract(photon.direction, payload.normal, n1 / n2);
		float reflectance = 1.0;

		if (dot(refract_direction, refract_direction) > 0.0)
			reflectance = fresnel(-dot(photon.direction, payload.normal), n1, n2);

		vec3 reflect_power = photon.power * reflectance;
		vec3 refract_power = photon.power * (1.0 - reflectance) * payload.color;

		if (max(reflect_power.r, max(reflect_power.g, reflect_power.b)) > min_power && stack_size < StackSize)
			stack[stack_size++] = Photon(reflect_origin, reflect_direction, reflect_power, photon.depth + 1);

		if (max(refract_power.r, max(refract_power.g, refract_power.b)) > min_power && stack_size < StackSize)
			stack[stack_size++] = Photon(pos - payload.geometry_normal * 0.001, refract_direction, refract_power, photon.depth + 1);
	}
}

// indirect light is low frequency, so a coarse map filled by photons that bounce between room surfaces is enough
void traceIndirectPhoton(uvec3 id, uvec3 size)
{
	vec3 origin = sampleLightOrigin();
	vec2 u = (vec2(id.xy) + vec2(random(), random())) / vec2(size.xy);
	vec3 direction = cosineSampleHemisphere(vec3(0.0, -1.0, 0.0), u);

	float area = PI * settings.light.w * settings.light.w;
	vec3 power = settings.light_color.rgb * area * PI / float(size.x * size.y * (size.z - CausticLayers));

	uint bounce = 0;
	bool indirect = false;

	for (uint i = 0; i < 16 && bounce <= IndirectBounces; i++)
	{
		if (!traceScene(origin, direction) || payload.material == MaterialEmissive)
			return;

		Object object = objects[payload.object];
		vec3 pos = origin + direction * payload.distance;

		// the mirror sphere doesn't move, so its caustics are built here with plenty of photons
		if (payload.material == MaterialMirror)
		{
			power *= payload.color;
			origin = pos + payload.geometry_normal * 0.001;
			direction = reflect(direction, payload.normal);
			indirect = true;
			continue;
		}

		// glass objects move while the indirect map is built once
		if (payload.material == MaterialGlass)
		{
			origin = pos + direction * 0.001;
			continue;
		}

		if (indirect && object.light_map.x != 0)
			splat(indirectMap(), object, pos, power);

		power *= payload.color;
		origin = pos + payload.geometry_normal * 0.001;
		direction = cosineSampleHemisphere(payload.normal, vec2(random(), random()));
		bounce += 1;
		indirect = true;
	}
}

void main()
{
	uvec3 id = gl_LaunchIDEXT;
	uvec3 size = gl_LaunchSizeEXT;

	// a fixed seed per photon keeps light maps stable from frame to frame
	rng_state = (id.z * size.y + id.y) * size.x + id.x;
	pcg();

	if (id.z < CausticLayers)
		traceCausticPhoton(id, size);
	else
		traceIndirectPhoton(id, size);
})";

const std::string clear_raygen_shader_code = R"(
#version 460
#extension GL_EXT_ray_tracing : require
)" + common_code + R"(
void main()
{
	light_maps[gl_LaunchIDEXT.x] = 0;
})";

const std::string miss_shader_code = R"(
#version 460
#extension GL_EXT_ray_tracing : require
)" + common_code + R"(
layout(location = 0) rayPayloadInEXT Payload payload;

void main()
{
	payload.distance = -1.0;
})";

const std::string shadow_miss_shader_code = R"(
#version 460
#extension GL_EXT_ray_tracing : require

layout(location = 1) rayPayloadInEXT bool visible;

void main()
{
	visible = true;
})";

const std::string closesthit_shader_code = R"(
#version 460
#extension GL_EXT_ray_tracing : require
)" + common_code + R"(
layout(location = 0) rayPayloadInEXT Payload payload;

hitAttributeEXT vec2 attribs;

struct Vertex
{
	vec4 pos;
	vec4 normal;
};

layout(binding = 4) buffer Indices { uint indices[]; };
layout(binding = 5) buffer Vertices { Vertex vertices[]; };

void main()
{
	Object object = objects[gl_InstanceCustomIndexEXT];

	uint base = object.index_offset + gl_PrimitiveID * 3;
	Vertex v0 = vertices[indices[base + 0]];
	Vertex v1 = vertices[indices[base + 1]];
	Vertex v2 = vertices[indices[base + 2]];

	vec3 barycentrics = vec3(1.0 - attribs.x - attribs.y, attribs.x, attribs.y);

	vec3 normal = normalize(v0.normal.xyz * barycentrics.x + v1.normal.xyz * barycentrics.y +
		v2.normal.xyz * barycentrics.z);
	vec3 geometry_normal = normalize(cross(v1.pos.xyz - v0.pos.xyz, v2.pos.xyz - v0.pos.xyz));

	bool front_face = dot(normal, gl_WorldRayDirectionEXT) < 0.0;

	if (dot(geometry_normal, gl_WorldRayDirectionEXT) > 0.0)
		geometry_normal = -geometry_normal;

	if (dot(normal, geometry_normal) < 0.0)
		normal = -normal;

	vec3 color = object.color.rgb;

	if (object.checker != 0)
	{
		vec3 pos = gl_WorldRayOriginEXT + gl_WorldRayDirectionEXT * gl_HitTEXT;
		ivec2 cell = ivec2(floor(pos.xz * object.checker_scale));

		if (((cell.x + cell.y) & 1) != 0)
			color = object.checker_color.rgb;
	}

	payload.color = color;
	payload.distance = gl_HitTEXT;
	payload.normal = normal;
	payload.material = object.material;
	payload.geometry_normal = geometry_normal;
	payload.object = gl_InstanceCustomIndexEXT;
	payload.front_face = front_face ? 1 : 0;
})";

struct Vertex
{
	glm::vec4 pos;
	glm::vec4 normal;
};

struct Mesh
{
	std::vector<Vertex> vertices;
	std::vector<uint32_t> indices;
};

enum class Material : uint32_t
{
	Diffuse = 0,
	Mirror = 1,
	Emissive = 2,
	Glass = 3
};

struct alignas(16) Object
{
	uint32_t index_offset;
	Material material;
	uint32_t checker = 0;
	float checker_scale = 1.0f;
	glm::vec4 color;
	glm::vec4 checker_color = { 0.0f, 0.0f, 0.0f, 1.0f };
	glm::uvec4 light_map = { 0, 0, 0, 0 };
};

struct alignas(16) Settings
{
	glm::vec4 eye;
	glm::vec4 forward;
	glm::vec4 right;
	glm::vec4 up;
	glm::vec4 light;
	glm::vec4 light_color;
	glm::vec4 room_min;
	glm::vec4 room_max;
	glm::vec4 ambient;
	glm::vec4 caustic_targets[2];
	glm::uvec4 caustic_target_objects;
	uint32_t shadow_samples;
	uint32_t caustic_resolution;
	uint32_t indirect_resolution;
	uint32_t indirect_offset;
	float light_map_scale;
	float ior;
	float exposure;
	uint32_t caustics;
	uint32_t indirect;
};

struct SceneObject
{
	Mesh mesh;
	Object object;
	bool animated = false;
	glm::mat4 transform = glm::mat4(1.0f);
	std::unique_ptr<skygfx::BLAS> blas;
};

void AddVertex(Mesh& mesh, const glm::vec3& pos, const glm::vec3& normal)
{
	mesh.vertices.push_back({ glm::vec4(pos, 1.0f), glm::vec4(normal, 0.0f) });
}

void AddQuad(Mesh& mesh, const glm::vec3& p0, const glm::vec3& p1, const glm::vec3& p2, const glm::vec3& p3,
	const glm::vec3& normal)
{
	auto base = (uint32_t)mesh.vertices.size();
	for (const auto& p : { p0, p1, p2, p3 })
	{
		AddVertex(mesh, p, normal);
	}
	mesh.indices.insert(mesh.indices.end(), { base, base + 1, base + 2, base, base + 2, base + 3 });
}

Mesh CreateQuad(const glm::vec3& p0, const glm::vec3& p1, const glm::vec3& p2, const glm::vec3& p3)
{
	Mesh mesh;
	AddQuad(mesh, p0, p1, p2, p3, glm::normalize(glm::cross(p1 - p0, p3 - p0)));
	return mesh;
}

Mesh CreateBox(float half_size)
{
	const glm::vec3 c[8] = {
		{ -1.0f, -1.0f, -1.0f }, { 1.0f, -1.0f, -1.0f }, { 1.0f, 1.0f, -1.0f }, { -1.0f, 1.0f, -1.0f },
		{ -1.0f, -1.0f, 1.0f }, { 1.0f, -1.0f, 1.0f }, { 1.0f, 1.0f, 1.0f }, { -1.0f, 1.0f, 1.0f },
	};

	const int sides[6][4] = {
		{ 4, 5, 6, 7 }, { 1, 0, 3, 2 }, { 0, 4, 7, 3 }, { 5, 1, 2, 6 }, { 7, 6, 2, 3 }, { 0, 1, 5, 4 },
	};

	Mesh mesh;

	for (const auto& s : sides)
	{
		auto normal = glm::normalize(c[s[0]] + c[s[1]] + c[s[2]] + c[s[3]]);
		AddQuad(mesh, c[s[0]] * half_size, c[s[1]] * half_size, c[s[2]] * half_size, c[s[3]] * half_size, normal);
	}

	return mesh;
}

Mesh CreateCylinder(float radius, float half_height, int segments)
{
	Mesh mesh;

	auto ring = [&](int i) {
		auto angle = (float)i / (float)segments * 2.0f * std::numbers::pi_v<float>;
		return glm::vec3{ glm::cos(angle), 0.0f, glm::sin(angle) };
	};

	for (int i = 0; i <= segments; i++)
	{
		auto dir = ring(i);
		AddVertex(mesh, dir * radius + glm::vec3{ 0.0f, -half_height, 0.0f }, dir);
		AddVertex(mesh, dir * radius + glm::vec3{ 0.0f, half_height, 0.0f }, dir);
	}

	for (uint32_t i = 0; i < (uint32_t)segments; i++)
	{
		auto i0 = i * 2;
		mesh.indices.insert(mesh.indices.end(), { i0, i0 + 1, i0 + 2, i0 + 1, i0 + 3, i0 + 2 });
	}

	for (auto y : { -half_height, half_height })
	{
		auto normal = glm::vec3{ 0.0f, glm::sign(y), 0.0f };
		auto center = (uint32_t)mesh.vertices.size();
		AddVertex(mesh, { 0.0f, y, 0.0f }, normal);

		for (int i = 0; i <= segments; i++)
		{
			AddVertex(mesh, ring(i) * radius + glm::vec3{ 0.0f, y, 0.0f }, normal);
		}

		for (uint32_t i = 1; i <= (uint32_t)segments; i++)
		{
			mesh.indices.insert(mesh.indices.end(), { center, center + i, center + i + 1 });
		}
	}

	return mesh;
}

Mesh CreateSphere(const glm::vec3& center, float radius, int slices, int stacks)
{
	Mesh mesh;

	for (int stack = 0; stack <= stacks; stack++)
	{
		auto theta = (float)stack / (float)stacks * std::numbers::pi_v<float>;

		for (int slice = 0; slice <= slices; slice++)
		{
			auto phi = (float)slice / (float)slices * 2.0f * std::numbers::pi_v<float>;
			auto normal = glm::vec3{ glm::sin(theta) * glm::cos(phi), glm::cos(theta), glm::sin(theta) * glm::sin(phi) };
			AddVertex(mesh, center + normal * radius, normal);
		}
	}

	for (int stack = 0; stack < stacks; stack++)
	{
		for (int slice = 0; slice < slices; slice++)
		{
			auto i0 = (uint32_t)(stack * (slices + 1) + slice);
			auto i1 = i0 + 1;
			auto i2 = i0 + slices + 1;
			auto i3 = i2 + 1;
			mesh.indices.insert(mesh.indices.end(), { i0, i2, i1, i1, i2, i3 });
		}
	}

	return mesh;
}

Mesh CreateDisk(const glm::vec3& center, float radius, int segments)
{
	Mesh mesh;
	auto normal = glm::vec3{ 0.0f, -1.0f, 0.0f };
	AddVertex(mesh, center, normal);

	for (int i = 0; i <= segments; i++)
	{
		auto angle = (float)i / (float)segments * 2.0f * std::numbers::pi_v<float>;
		AddVertex(mesh, center + glm::vec3{ glm::cos(angle), 0.0f, glm::sin(angle) } * radius, normal);
	}

	for (uint32_t i = 1; i <= (uint32_t)segments; i++)
	{
		mesh.indices.insert(mesh.indices.end(), { 0, i, i + 1 });
	}

	return mesh;
}

int main()
{
	auto features = { skygfx::Feature::Raytracing };
	auto backend_type = utils::ChooseBackendTypeViaConsole(features);

	glfwInit();
	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

	auto [window, native_window, width, height] = utils::SpawnWindow(800, 600, "Raytraced cornell box");

	skygfx::Initialize(native_window, width, height, backend_type, skygfx::Adapter::HighPerformance, features);

	glfwSetFramebufferSizeCallback(window, [](GLFWwindow* window, int width, int height) {
		skygfx::Resize(static_cast<uint32_t>(width), static_cast<uint32_t>(height));
	});

	auto miss_shaders = { miss_shader_code, shadow_miss_shader_code };
	auto render_shader = skygfx::RaytracingShader(render_raygen_shader_code, miss_shaders, closesthit_shader_code);
	auto photon_shader = skygfx::RaytracingShader(photon_raygen_shader_code, miss_shaders, closesthit_shader_code);
	auto clear_shader = skygfx::RaytracingShader(clear_raygen_shader_code, miss_shaders, closesthit_shader_code);

	const auto room_min = glm::vec3{ -2.0f, 0.0f, -2.0f };
	const auto room_max = glm::vec3{ 2.0f, 2.9f, 8.0f };
	const auto light_center = glm::vec3{ 0.0f, room_max.y - 0.03f, 0.3f };
	const auto light_radius = 0.5f;
	const auto light_tint = glm::vec3{ 0.95f, 0.9f, 1.0f };
	const auto sphere_center = glm::vec3{ 0.0f, 1.105f, 0.16f };
	const auto sphere_radius = 0.58f;
	const auto box_half_size = 0.37f;
	const auto cylinder_radius = 0.35f;
	const auto cylinder_half_height = 0.3f;

	const auto white = glm::vec4{ 0.75f, 0.75f, 0.75f, 1.0f };
	const auto floor_white = glm::vec4{ 0.6f, 0.6f, 0.6f, 1.0f };
	const auto black = glm::vec4{ 0.04f, 0.04f, 0.04f, 1.0f };
	const auto chrome = glm::vec4{ 0.85f, 0.85f, 0.85f, 1.0f };
	const auto glass = glm::vec4{ 0.95f, 0.97f, 1.0f, 1.0f };

	const auto& a = room_min;
	const auto& b = room_max;

	std::vector<SceneObject> scene;

	auto add_wall = [&](const glm::vec3& p0, const glm::vec3& p1, const glm::vec3& p2, const glm::vec3& p3,
		const glm::vec4& color, uint32_t u_axis, uint32_t v_axis, uint32_t checker = 0) {
		auto map_index = (uint32_t)scene.size();
		scene.push_back({ CreateQuad(p0, p1, p2, p3), {
			.material = Material::Diffuse,
			.checker = checker,
			.checker_scale = 5.5f,
			.color = color,
			.checker_color = black,
			.light_map = { map_index + 1, u_axis, v_axis, 0 }
		} });
	};

	add_wall({ a.x, a.y, b.z }, { b.x, a.y, b.z }, { b.x, a.y, a.z }, { a.x, a.y, a.z }, floor_white, 0, 2, 1); // floor
	add_wall({ a.x, b.y, a.z }, { b.x, b.y, a.z }, { b.x, b.y, b.z }, { a.x, b.y, b.z }, white, 0, 2); // ceiling
	add_wall({ a.x, a.y, a.z }, { b.x, a.y, a.z }, { b.x, b.y, a.z }, { a.x, b.y, a.z }, white, 0, 1); // back
	add_wall({ b.x, a.y, b.z }, { a.x, a.y, b.z }, { a.x, b.y, b.z }, { b.x, b.y, b.z }, white, 0, 1); // front
	add_wall({ a.x, a.y, b.z }, { a.x, a.y, a.z }, { a.x, b.y, a.z }, { a.x, b.y, b.z },
		{ 0.02f, 0.05f, 0.8f, 1.0f }, 2, 1); // left
	add_wall({ b.x, a.y, a.z }, { b.x, a.y, b.z }, { b.x, b.y, b.z }, { b.x, b.y, a.z },
		{ 0.8f, 0.03f, 0.02f, 1.0f }, 2, 1); // right

	const auto light_map_count = (uint32_t)scene.size();

	scene.push_back({ CreateDisk(light_center, light_radius, 64), {
		.material = Material::Emissive,
		.color = glm::vec4(light_tint, 1.0f)
	} });

	scene.push_back({ CreateSphere(sphere_center, sphere_radius, 64, 32), { .material = Material::Mirror, .color = chrome } });

	const auto box_index = (uint32_t)scene.size();
	scene.push_back({ CreateBox(box_half_size), { .material = Material::Glass, .color = glass }, true });

	const auto cylinder_index = (uint32_t)scene.size();
	scene.push_back({ CreateCylinder(cylinder_radius, cylinder_half_height, 64), { .material = Material::Glass, .color = glass }, true });

	auto& box = scene.at(box_index);
	auto& cylinder = scene.at(cylinder_index);

	std::vector<Vertex> vertices;
	std::vector<uint32_t> indices;
	std::vector<Object> objects;

	for (auto& scene_object : scene)
	{
		scene_object.object.index_offset = (uint32_t)indices.size();
		objects.push_back(scene_object.object);

		auto vertex_offset = (uint32_t)vertices.size();
		vertices.insert(vertices.end(), scene_object.mesh.vertices.begin(), scene_object.mesh.vertices.end());

		for (auto index : scene_object.mesh.indices)
		{
			indices.push_back(vertex_offset + index);
		}
	}

	const auto caustic_maps_size = light_map_count * CausticMapResolution * CausticMapResolution * 3;
	const auto indirect_maps_size = light_map_count * IndirectMapResolution * IndirectMapResolution * 3;
	const auto light_maps_size = caustic_maps_size + indirect_maps_size;

	auto objects_buffer = skygfx::StorageBuffer(objects.data(), objects.size() * sizeof(Object));
	auto indices_buffer = skygfx::StorageBuffer(indices.data(), indices.size() * sizeof(uint32_t));
	auto vertices_buffer = skygfx::StorageBuffer(vertices.size() * sizeof(Vertex));
	auto light_maps_buffer = skygfx::StorageBuffer(light_maps_size * sizeof(uint32_t));

	auto imgui = ImguiHelper();

	ImGui_ImplGlfw_InitForOpenGL(window, true);

	bool animated = true;
	float speed = 1.0f;
	float light_intensity = 10.0f;
	float exposure = 1.0f;
	float ior = 1.5f;
	int shadow_samples = 16;
	bool caustics = true;
	bool indirect = true;

	bool indirect_ready = false;
	float time = 0.0f;
	auto prev_time = glfwGetTime();

	const auto eye = glm::vec3{ 0.0f, 1.45f, 7.34f };
	const auto forward = glm::vec3{ 0.0f, 0.0f, -1.0f };
	const auto right = glm::vec3{ 1.0f, 0.0f, 0.0f };
	const auto up = glm::vec3{ 0.0f, 1.0f, 0.0f };
	const auto fov = glm::radians(27.8f);

	while (!glfwWindowShouldClose(window))
	{
		auto now = glfwGetTime();
		auto dt = (float)(now - prev_time);
		prev_time = now;

		ImGui_ImplGlfw_NewFrame();

		ImGui::NewFrame();

		ImGui::Begin("Settings", NULL, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove);
		ImGui::SetWindowPos({ 16.0f, 16.0f });
		ImGui::Text("%.0f fps", ImGui::GetIO().Framerate);
		ImGui::Checkbox("Animated", &animated);
		ImGui::SliderFloat("Speed", &speed, 0.0f, 3.0f);

		// the indirect map is built once, so it is rebuilt when the light changes
		if (ImGui::SliderFloat("Light", &light_intensity, 0.0f, 30.0f))
			indirect_ready = false;

		ImGui::SliderFloat("Exposure", &exposure, 0.1f, 4.0f);
		ImGui::SliderFloat("IOR", &ior, 1.0f, 2.5f);
		ImGui::SliderInt("Shadow samples", &shadow_samples, 1, 64);
		ImGui::Checkbox("Caustics", &caustics);
		ImGui::Checkbox("Indirect light", &indirect);
		ImGui::End();

		if (animated)
			time += dt * speed;

		auto light_color = light_tint * light_intensity;

		// the box and the cylinder orbit the sphere on opposite sides, one revolution per 10 seconds

		const auto orbit_radius = glm::vec2{ 1.25f, 0.9f };
		const auto orbit_height = 1.82f;
		auto orbit = time * 2.0f * std::numbers::pi_v<float> / 10.0f;

		auto orbit_position = [&](float angle) {
			return glm::vec3{ sphere_center.x - glm::cos(angle) * orbit_radius.x, orbit_height,
				sphere_center.z + glm::sin(angle) * orbit_radius.y };
		};

		box.transform = glm::translate(glm::mat4(1.0f), orbit_position(orbit));
		box.transform = glm::rotate(box.transform, time * 1.5f, glm::normalize(glm::vec3{ 0.3f, 1.0f, 0.2f }));
		box.transform = glm::rotate(box.transform, glm::radians(25.0f), { 1.0f, 0.0f, 0.0f });

		cylinder.transform = glm::translate(glm::mat4(1.0f), orbit_position(orbit + std::numbers::pi_v<float>));
		cylinder.transform = glm::rotate(cylinder.transform, time * 1.2f, glm::normalize(glm::vec3{ 0.4f, 0.2f, 1.0f }));
		cylinder.transform = glm::rotate(cylinder.transform, glm::radians(20.0f), { 0.0f, 0.0f, 1.0f });

		auto vertex_offset = vertices.begin();

		for (auto& scene_object : scene)
		{
			if (scene_object.animated || !scene_object.blas)
			{
				auto world_mesh = scene_object.mesh;

				for (auto& vertex : world_mesh.vertices)
				{
					vertex.pos = scene_object.transform * vertex.pos;
					vertex.normal = scene_object.transform * vertex.normal;
				}

				std::copy(world_mesh.vertices.begin(), world_mesh.vertices.end(), vertex_offset);
				scene_object.blas = std::make_unique<skygfx::BLAS>(world_mesh.vertices, 0, world_mesh.indices, 0, glm::mat4(1.0f));
			}

			vertex_offset += scene_object.mesh.vertices.size();
		}

		vertices_buffer.write(vertices.data(), vertices.size() * sizeof(Vertex));

		std::vector<skygfx::BottomLevelAccelerationStructureHandle*> blases;

		for (auto& scene_object : scene)
		{
			blases.push_back(*scene_object.blas);
		}

		auto tlas = skygfx::TLAS(blases);

		auto target = skygfx::AcquireTransientRenderTarget();
		auto tan_half_fov = glm::tan(fov * 0.5f);
		auto aspect = (float)target->getWidth() / (float)target->getHeight();

		skygfx::SetUniformBuffer(2, Settings{
			.eye = glm::vec4(eye, 0.0f),
			.forward = glm::vec4(forward, 0.0f),
			.right = glm::vec4(right * tan_half_fov * aspect, 0.0f),
			.up = glm::vec4(up * tan_half_fov, 0.0f),
			.light = glm::vec4(light_center, light_radius),
			.light_color = glm::vec4(light_color, 1.0f),
			.room_min = glm::vec4(room_min, 0.0f),
			.room_max = glm::vec4(room_max, 0.0f),
			.ambient = { 0.005f, 0.005f, 0.006f, 0.0f },
			.caustic_targets = {
				glm::vec4(glm::vec3(box.transform[3]), box_half_size * glm::sqrt(3.0f)),
				glm::vec4(glm::vec3(cylinder.transform[3]), glm::length(glm::vec2{ cylinder_radius, cylinder_half_height }))
			},
			.caustic_target_objects = { box_index, cylinder_index, 0, 0 },
			.shadow_samples = (uint32_t)shadow_samples,
			.caustic_resolution = CausticMapResolution,
			.indirect_resolution = IndirectMapResolution,
			.indirect_offset = caustic_maps_size,
			.light_map_scale = 65536.0f,
			.ior = ior,
			.exposure = exposure,
			.caustics = caustics,
			.indirect = indirect
		});

		skygfx::SetStorageBuffer(3, objects_buffer);
		skygfx::SetStorageBuffer(4, indices_buffer);
		skygfx::SetStorageBuffer(5, vertices_buffer);
		skygfx::SetStorageBuffer(7, light_maps_buffer);
		skygfx::SetRenderTarget(*target);
		skygfx::SetAccelerationStructure(0, tlas);

		skygfx::SetShader(clear_shader);
		skygfx::DispatchRays(indirect_ready ? caustic_maps_size : light_maps_size, 1, 1);

		skygfx::SetShader(photon_shader);
		skygfx::DispatchRays(PhotonGridSize, PhotonGridSize, CausticPhotonLayers + (indirect_ready ? 0 : IndirectPhotonLayers));
		indirect_ready = true;

		skygfx::SetShader(render_shader);
		skygfx::DispatchRays(target->getWidth(), target->getHeight(), 1);

		skygfx::utils::passes::Blit(target, nullptr, {
			.clear = true
		});

		imgui.draw();

		skygfx::Present();

		glfwPollEvents();
	}

	skygfx::Finalize();

	glfwTerminate();

	return 0;
}
