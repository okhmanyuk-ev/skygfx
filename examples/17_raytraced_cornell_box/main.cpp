#include <skygfx/skygfx.h>
#include <skygfx/utils.h>
#include "../utils/utils.h"
#include "../utils/imgui_helper.h"
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <glm/gtc/matrix_transform.hpp>
#include <numbers>
#include <cstddef>
#include <array>

// Whitted-style raytracer with soft shadows from an area light and photon traced caustics and indirect light.
// Every frame:
// 1. clear pass - zeroes caustic maps of the room surfaces
// 2. photon pass - shoots photons from the light towards the moving glass objects, the ones that reach a room
//    surface through glass go to its caustic map
// 3. render pass - traces camera rays, splitting them on glass into reflection and refraction
// Once at start the photon pass also shoots photons over the whole hemisphere, the ones that reach a room surface
// after diffuse bounces or the mirror sphere go to its indirect map. Moving glass objects barely change it,
// so these photons pass through them.
// Light maps hold irradiance for a light of unit intensity, so changing the intensity doesn't rebuild them.

const uint32_t CausticMapResolution = 160;
const uint32_t IndirectMapResolution = 48;
const uint32_t PhotonGridSize = 384;
const uint32_t CausticLayers = 2; // photon layers, one per moving glass object
const uint32_t IndirectLayers = 64; // photon layers for the indirect map
const uint32_t NoLightMap = 0xFFFFFFFF;

const std::string common_code = R"(
const float PI = 3.14159265359;

struct Payload
{
	vec3 color;
	float distance;
	vec3 normal; // faces the ray
	uint material;
	vec3 geometry_normal; // faces the ray
	uint object;
	bool front_face;
};

struct Object
{
	vec4 color;
	vec4 checker_color;
	uint index_offset;
	uint material;
	uint checker;
	float checker_scale;
	uvec4 light_map; // x - map index or NO_LIGHT_MAP, y - u axis, z - v axis
};

bool hasLightMap(Object object)
{
	return object.light_map.x != NO_LIGHT_MAP;
}

layout(binding = 3) buffer Objects { Object objects[]; };
layout(binding = 6) buffer LightMaps { uint light_maps[]; };
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
	vec4 light_color; // radiance of a light of unit intensity
	vec4 room_min;
	vec4 room_max;
	vec4 caustic_targets[CAUSTIC_LAYERS]; // xyz - center, w - bounding radius
	uvec4 caustic_target_objects;
	float light_intensity;
	float ior;
	float exposure;
	uint shadow_samples;
	uint caustics;
	uint indirect;
} settings;

layout(location = 0) rayPayloadEXT Payload payload;
layout(location = 1) rayPayloadEXT bool visible;

const uint MaxDepth = 8;
const uint StackSize = 12;
const float Epsilon = 0.001;
const float MaxDistance = 100.0; // longer than any path inside the room
const float MinRelativeWeight = 0.01; // rays weaker than this part of their initial weight are dropped

float maxComponent(vec3 v)
{
	return max(v.r, max(v.g, v.b));
}

// throughput for camera rays, power for photons
struct Ray
{
	vec3 origin;
	vec3 direction;
	vec3 weight;
	uint depth;
};

// rays waiting to be traced, a hit on glass adds two of them
Ray stack[StackSize];
uint stack_size = 0;

// rays too weak to matter are dropped
void push(Ray ray, float min_weight)
{
	if (maxComponent(ray.weight) > min_weight && stack_size < StackSize)
		stack[stack_size++] = ray;
}

Ray pop()
{
	return stack[--stack_size];
}

struct LightMap
{
	uint offset;
	uint resolution;
};

const LightMap CausticMap = LightMap(0u, CAUSTIC_MAP_RESOLUTION);
const LightMap IndirectMap = LightMap(INDIRECT_MAP_OFFSET, INDIRECT_MAP_RESOLUTION);

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
	traceRayEXT(topLevelAS, gl_RayFlagsOpaqueEXT, 0xFF, 0, 0, 0, origin, 0.0, direction, MaxDistance, 0);
	return payload.distance >= 0.0;
}

// area is a fraction of the disk around its center, so a uniform area gives uniformly spread points
vec3 lightPoint(float area, float angle)
{
	float r = settings.light.w * sqrt(area);
	return settings.light.xyz + vec3(r * cos(angle), 0.0, r * sin(angle));
}

float lightArea()
{
	return PI * settings.light.w * settings.light.w;
}

// moves a hit point off the surface to the side the ray came from (1) or to the other side (-1)
vec3 offsetFromSurface(vec3 pos, float side)
{
	return pos + payload.geometry_normal * Epsilon * side;
}

void reflectOnMirror(Ray ray, vec3 pos, float min_weight)
{
	push(Ray(offsetFromSurface(pos, 1.0), reflect(ray.direction, payload.normal), ray.weight * payload.color,
		ray.depth + 1), min_weight);
}

// a ray hitting glass splits into a reflected and a refracted part, the latter is empty on total internal reflection
void splitOnGlass(Ray ray, vec3 pos, float min_weight)
{
	float eta = payload.front_face ? 1.0 / settings.ior : settings.ior;
	vec3 refract_direction = refract(ray.direction, payload.normal, eta);
	float reflectance = 1.0;

	if (dot(refract_direction, refract_direction) > 0.0)
	{
		// Schlick's approximation takes the angle on the side of the less dense medium
		float cos_theta = eta > 1.0 ? -dot(refract_direction, payload.normal) : -dot(ray.direction, payload.normal);
		float r0 = (1.0 - eta) / (1.0 + eta);
		r0 *= r0;
		reflectance = r0 + (1.0 - r0) * pow(1.0 - cos_theta, 5.0);
	}

	push(Ray(offsetFromSurface(pos, 1.0), reflect(ray.direction, payload.normal), ray.weight * reflectance,
		ray.depth + 1), min_weight);
	push(Ray(offsetFromSurface(pos, -1.0), refract_direction, ray.weight * (1.0 - reflectance) * payload.color,
		ray.depth + 1), min_weight);
}

// index of the cell's red channel, green and blue follow it
uint lightMapCell(LightMap map, Object object, ivec2 cell)
{
	uint index = object.light_map.x;
	return map.offset + ((index * map.resolution + uint(cell.y)) * map.resolution + uint(cell.x)) * 3;
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

// an orthonormal basis with n as its z axis
mat3 basis(vec3 n)
{
	vec3 t = normalize(cross(n, abs(n.y) < 0.99 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0)));
	return mat3(t, cross(n, t), n);
}

vec3 cosineSampleHemisphere(vec3 n, vec2 u)
{
	float phi = 2.0 * PI * u.x;
	float r = sqrt(u.y);
	return normalize(basis(n) * vec3(r * cos(phi), r * sin(phi), sqrt(1.0 - u.y)));
}
)";

const std::string render_raygen_shader_code = R"(
#version 460
#extension GL_EXT_ray_tracing : require
)" + raygen_common_code + R"(
layout(binding = 1, rgba32f) uniform image2D image;

const vec3 Ambient = vec3(0.005, 0.005, 0.006);

vec3 lightRadiance()
{
	return settings.light_color.rgb * settings.light_intensity;
}

bool isVisible(vec3 origin, vec3 direction, float distance)
{
	uint flags = gl_RayFlagsTerminateOnFirstHitEXT | gl_RayFlagsOpaqueEXT | gl_RayFlagsSkipClosestHitShaderEXT;
	visible = false;
	traceRayEXT(topLevelAS, flags, 0xFF, 0, 0, 1, origin, 0.0, direction, distance, 1);
	return visible;
}

// a fixed set of points spread over the light disk gives smooth noise-free penumbras,
// a per-pixel rotation of the set turns banding into fine static grain
vec3 directIrradiance(vec3 pos, vec3 normal, uint samples, float rotation)
{
	float sum = 0.0;

	for (uint i = 0; i < samples; i++)
	{
		vec3 light_pos = lightPoint((float(i) + 0.5) / float(samples), float(i) * 2.39996323 + rotation);
		vec3 to_light = light_pos - pos;
		float distance_sqr = dot(to_light, to_light);
		float distance = sqrt(distance_sqr);
		vec3 direction = to_light / distance;
		float cos_surface = dot(normal, direction);
		float cos_light = direction.y; // the light faces down

		if (cos_surface <= 0.0 || cos_light <= 0.0)
			continue;

		if (!isVisible(pos, direction, distance - Epsilon))
			continue;

		sum += cos_surface * cos_light / distance_sqr;
	}

	return lightRadiance() * sum * lightArea() / float(samples);
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
	return lightRadiance() * PI * form_factor;
}

vec3 readLightMap(LightMap map, Object object, ivec2 cell)
{
	uint index = lightMapCell(map, object, clamp(cell, ivec2(0), ivec2(int(map.resolution) - 1)));
	return vec3(light_maps[index], light_maps[index + 1], light_maps[index + 2]) / LIGHT_MAP_SCALE;
}

vec3 causticIrradiance(Object object, vec3 pos)
{
	ivec2 center = ivec2(floor(lightMapCoord(CausticMap, object, pos)));
	const float weights[5] = float[](1.0, 4.0, 6.0, 4.0, 1.0);
	vec3 sum = vec3(0.0);

	for (int y = -2; y <= 2; y++)
	{
		for (int x = -2; x <= 2; x++)
		{
			sum += readLightMap(CausticMap, object, center + ivec2(x, y)) * weights[x + 2] * weights[y + 2] / 256.0;
		}
	}

	return sum;
}

vec3 indirectIrradiance(Object object, vec3 pos)
{
	vec2 coord = lightMapCoord(IndirectMap, object, pos) - 0.5;
	ivec2 base = ivec2(floor(coord));
	vec2 f = coord - vec2(base);

	return mix(
		mix(readLightMap(IndirectMap, object, base), readLightMap(IndirectMap, object, base + ivec2(1, 0)), f.x),
		mix(readLightMap(IndirectMap, object, base + ivec2(0, 1)), readLightMap(IndirectMap, object, base + ivec2(1, 1)), f.x),
		f.y);
}

vec3 shadeDiffuse(vec3 pos, uint depth, float rotation)
{
	// surfaces seen through mirrors and glass are distorted by them, so fewer shadow rays are enough
	uint samples = depth == 0 ? settings.shadow_samples : max(settings.shadow_samples / 4, 1);
	vec3 irradiance = directIrradiance(offsetFromSurface(pos, 1.0), payload.normal, samples, rotation) +
		lightBackSideIrradiance(pos, payload.normal);

	Object object = objects[payload.object];

	if (hasLightMap(object))
	{
		vec3 light_maps_irradiance = vec3(0.0);

		if (settings.caustics != 0)
			light_maps_irradiance += causticIrradiance(object, pos);

		if (settings.indirect != 0)
			light_maps_irradiance += indirectIrradiance(object, pos);

		irradiance += light_maps_irradiance * settings.light_intensity;
	}

	return payload.color * (irradiance / PI + Ambient);
}

vec3 cameraRayDirection(uvec2 pixel, uvec2 size)
{
	vec2 d = (vec2(pixel) + 0.5) / vec2(size) * 2.0 - 1.0;
	return normalize(settings.forward.xyz + settings.right.xyz * d.x - settings.up.xyz * d.y);
}

// Narkowicz's fit of the ACES filmic curve
vec3 tonemapAces(vec3 x)
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

	push(Ray(settings.eye.xyz, cameraRayDirection(pixel, size), vec3(1.0), 0), 0.0);

	vec3 color = vec3(0.0);

	while (stack_size > 0)
	{
		Ray ray = pop();

		if (!traceScene(ray.origin, ray.direction))
			continue;

		vec3 pos = ray.origin + ray.direction * payload.distance;

		if (payload.material == MATERIAL_EMISSIVE)
		{
			color += ray.weight * lightRadiance();
			continue;
		}

		if (payload.material == MATERIAL_DIFFUSE)
		{
			color += ray.weight * shadeDiffuse(pos, ray.depth, rotation);
			continue;
		}

		if (ray.depth >= MaxDepth)
			continue;

		if (payload.material == MATERIAL_MIRROR)
			reflectOnMirror(ray, pos, MinRelativeWeight);
		else
			splitOnGlass(ray, pos, MinRelativeWeight);
	}

	vec3 result = pow(tonemapAces(color * settings.exposure), vec3(1.0 / 2.2));

	imageStore(image, ivec2(pixel), vec4(result, 1.0));
})";

const std::string photon_raygen_shader_code = R"(
#version 460
#extension GL_EXT_ray_tracing : require
)" + raygen_common_code + R"(
const uint IndirectBounces = 2;
const uint MaxIndirectSteps = 16; // bounces and passes through glass

// power is what arrives at the surface, the surface's albedo is applied when it is rendered
void splat(LightMap map, Object object, vec3 pos, vec3 power)
{
	vec2 coord = lightMapCoord(map, object, pos) - 0.5;
	ivec2 base = ivec2(floor(coord));
	vec2 f = coord - vec2(base);
	vec3 irradiance = power / lightMapCellArea(map, object) * LIGHT_MAP_SCALE;
	int res = int(map.resolution);

	for (int y = 0; y <= 1; y++)
	{
		for (int x = 0; x <= 1; x++)
		{
			ivec2 cell = base + ivec2(x, y);

			if (any(lessThan(cell, ivec2(0))) || any(greaterThanEqual(cell, ivec2(res))))
				continue;

			float weight = (x == 0 ? 1.0 - f.x : f.x) * (y == 0 ? 1.0 - f.y : f.y);
			uint index = lightMapCell(map, object, cell);

			for (uint c = 0; c < 3; c++)
			{
				atomicAdd(light_maps[index + c], uint(irradiance[c] * weight + 0.5));
			}
		}
	}
}

// u spreads directions uniformly over the solid angle of the cone
vec3 sampleCone(vec3 axis, float cos_max, vec2 u)
{
	float cos_theta = 1.0 - u.x * (1.0 - cos_max);
	float sin_theta = sqrt(1.0 - cos_theta * cos_theta);
	float phi = 2.0 * PI * u.y;
	return normalize(basis(axis) * vec3(cos(phi) * sin_theta, sin(phi) * sin_theta, cos_theta));
}

void traceCausticPhoton(uvec3 id, uvec3 size)
{
	vec4 target = settings.caustic_targets[id.z];
	uint target_object = settings.caustic_target_objects[id.z];
	vec3 origin = lightPoint(random(), 2.0 * PI * random());

	// sample a direction inside the cone that encloses the target's bounding sphere

	vec3 to_target = target.xyz - origin;
	float distance = length(to_target);
	vec3 axis = to_target / distance;
	float sin_max = min(target.w / distance, 1.0);
	float cos_max = sqrt(1.0 - sin_max * sin_max);

	vec2 u = (vec2(id.xy) + vec2(random(), random())) / vec2(size.xy);
	vec3 direction = sampleCone(axis, cos_max, u);

	float cos_light = -direction.y;

	if (cos_light <= 0.0)
		return;

	float solid_angle = 2.0 * PI * (1.0 - cos_max);
	vec3 power = settings.light_color.rgb * cos_light * lightArea() * solid_angle / float(size.x * size.y);
	float min_power = maxComponent(power) * MinRelativeWeight;

	// photons are split on glass instead of picking one way randomly, so dim reflected caustics stay smooth
	push(Ray(origin, direction, power, 0), 0.0);

	while (stack_size > 0)
	{
		Ray photon = pop();

		if (!traceScene(photon.origin, photon.direction))
			continue;

		// photons that hit something else first belong to another target or to direct lighting
		if (photon.depth == 0 && payload.object != target_object)
			continue;

		vec3 pos = photon.origin + photon.direction * payload.distance;

		if (payload.material == MATERIAL_DIFFUSE)
		{
			Object object = objects[payload.object];

			if (photon.depth > 0 && hasLightMap(object))
				splat(CausticMap, object, pos, photon.weight);
			continue;
		}

		if (payload.material == MATERIAL_EMISSIVE || photon.depth >= MaxDepth)
			continue;

		if (payload.material == MATERIAL_MIRROR)
			reflectOnMirror(photon, pos, min_power);
		else
			splitOnGlass(photon, pos, min_power);
	}
}

// indirect light is low frequency, so a coarse map filled by photons that bounce between room surfaces is enough
void traceIndirectPhoton(uvec3 id, uvec3 size)
{
	vec3 origin = lightPoint(random(), 2.0 * PI * random());
	vec2 u = (vec2(id.xy) + vec2(random(), random())) / vec2(size.xy);
	vec3 direction = cosineSampleHemisphere(vec3(0.0, -1.0, 0.0), u);
	vec3 power = settings.light_color.rgb * lightArea() * PI / float(size.x * size.y * (size.z - CAUSTIC_LAYERS));

	uint bounce = 0;
	bool indirect = false;

	for (uint i = 0; i < MaxIndirectSteps && bounce <= IndirectBounces; i++)
	{
		if (!traceScene(origin, direction) || payload.material == MATERIAL_EMISSIVE)
			return;

		vec3 pos = origin + direction * payload.distance;

		// the mirror sphere doesn't move, so its caustics are built here with plenty of photons
		if (payload.material == MATERIAL_MIRROR)
		{
			power *= payload.color;
			origin = offsetFromSurface(pos, 1.0);
			direction = reflect(direction, payload.normal);
			indirect = true;
			continue;
		}

		// glass objects move while the indirect map is built once
		if (payload.material == MATERIAL_GLASS)
		{
			origin = pos + direction * Epsilon;
			continue;
		}

		Object object = objects[payload.object];

		if (indirect && hasLightMap(object))
			splat(IndirectMap, object, pos, power);

		power *= payload.color;
		origin = offsetFromSurface(pos, 1.0);
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

	if (id.z < CAUSTIC_LAYERS)
		traceCausticPhoton(id, size);
	else
		traceIndirectPhoton(id, size);
})";

const std::string clear_raygen_shader_code = R"(
#version 460
#extension GL_EXT_ray_tracing : require
)" + common_code + R"(
// skygfx has no compute shaders, so light maps are cleared by a raygen shader that traces nothing
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

	// vertices are in object space, objects are only rotated and moved, so normals turn with the instance's matrix
	mat3 rotation = mat3(gl_ObjectToWorldEXT);

	vec3 normal = normalize(rotation * (v0.normal.xyz * barycentrics.x + v1.normal.xyz * barycentrics.y +
		v2.normal.xyz * barycentrics.z));
	vec3 geometry_normal = normalize(rotation * cross(v1.pos.xyz - v0.pos.xyz, v2.pos.xyz - v0.pos.xyz));

	payload.front_face = dot(normal, gl_WorldRayDirectionEXT) < 0.0;

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
	Diffuse,
	Mirror,
	Emissive,
	Glass
};

struct alignas(16) Object
{
	glm::vec4 color;
	glm::vec4 checker_color = { 0.0f, 0.0f, 0.0f, 1.0f };
	uint32_t index_offset = 0;
	Material material;
	uint32_t checker = 0;
	float checker_scale = 1.0f;
	glm::uvec4 light_map = { NoLightMap, 0, 0, 0 };
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
	glm::vec4 caustic_targets[CausticLayers];
	glm::uvec4 caustic_target_objects;
	float light_intensity;
	float ior;
	float exposure;
	uint32_t shadow_samples;
	uint32_t caustics;
	uint32_t indirect;
};

// must match the std430 objects buffer and the std140 settings block in the shaders
static_assert(sizeof(Object) == 64 && offsetof(Object, light_map) == 48);
static_assert(offsetof(Settings, light_intensity) == 176);
static_assert(CausticLayers <= 4); // caustic_target_objects holds up to 4 indices

struct SceneObject
{
	Mesh mesh;
	Object object;
	glm::mat4 transform = glm::mat4(1.0f);
	float bounding_radius = 0.0f; // around the object's origin, photons for caustics are aimed at this sphere
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

Mesh CreateSphere(float radius, int slices, int stacks)
{
	Mesh mesh;

	for (int stack = 0; stack <= stacks; stack++)
	{
		auto theta = (float)stack / (float)stacks * std::numbers::pi_v<float>;

		for (int slice = 0; slice <= slices; slice++)
		{
			auto phi = (float)slice / (float)slices * 2.0f * std::numbers::pi_v<float>;
			auto normal = glm::vec3{ glm::sin(theta) * glm::cos(phi), glm::cos(theta), glm::sin(theta) * glm::sin(phi) };
			AddVertex(mesh, normal * radius, normal);
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

Mesh CreateDisk(float radius, int segments)
{
	Mesh mesh;
	auto normal = glm::vec3{ 0.0f, -1.0f, 0.0f };
	AddVertex(mesh, { 0.0f, 0.0f, 0.0f }, normal);

	for (int i = 0; i <= segments; i++)
	{
		auto angle = (float)i / (float)segments * 2.0f * std::numbers::pi_v<float>;
		AddVertex(mesh, glm::vec3{ glm::cos(angle), 0.0f, glm::sin(angle) } * radius, normal);
	}

	for (uint32_t i = 1; i <= (uint32_t)segments; i++)
	{
		mesh.indices.insert(mesh.indices.end(), { 0, i, i + 1 });
	}

	return mesh;
}

const glm::vec3 RoomMin = { -2.0f, 0.0f, -2.0f };
const glm::vec3 RoomMax = { 2.0f, 2.9f, 8.0f };
const glm::vec3 LightCenter = { 0.0f, RoomMax.y - 0.03f, 0.3f };
const float LightRadius = 0.5f;
const glm::vec3 LightColor = { 0.95f, 0.9f, 1.0f };
const glm::vec3 SphereCenter = { 0.0f, 1.105f, 0.16f };
const float SphereRadius = 0.58f;
const float BoxHalfSize = 0.37f;
const float CylinderRadius = 0.35f;
const float CylinderHalfHeight = 0.3f;

const glm::vec3 Eye = { 0.0f, 1.45f, 7.34f };
const float Fov = glm::radians(27.8f);

struct Scene
{
	std::vector<SceneObject> objects;
	std::vector<Vertex> vertices;
	std::vector<uint32_t> indices;
	uint32_t light_map_count = 0;
	uint32_t box = 0;
	uint32_t cylinder = 0;
	std::array<uint32_t, CausticLayers> caustic_targets = {};
};

Scene CreateScene()
{
	const auto white = glm::vec4{ 0.75f, 0.75f, 0.75f, 1.0f };
	const auto floor_white = glm::vec4{ 0.6f, 0.6f, 0.6f, 1.0f };
	const auto black = glm::vec4{ 0.04f, 0.04f, 0.04f, 1.0f };
	const auto chrome = glm::vec4{ 0.85f, 0.85f, 0.85f, 1.0f };
	const auto glass = glm::vec4{ 0.95f, 0.97f, 1.0f, 1.0f };

	const auto& a = RoomMin;
	const auto& b = RoomMax;

	Scene scene;
	auto& objects = scene.objects;

	// a wall's light map spans the two axes the wall doesn't face
	auto add_wall = [&](const glm::vec3& p0, const glm::vec3& p1, const glm::vec3& p2, const glm::vec3& p3,
		const glm::vec4& color, bool checker = false) {
		auto mesh = CreateQuad(p0, p1, p2, p3);
		auto normal = glm::abs(glm::vec3(mesh.vertices.front().normal));
		auto axis = normal.x > 0.5f ? 0u : normal.y > 0.5f ? 1u : 2u;
		auto map_index = (uint32_t)objects.size();
		objects.push_back({ std::move(mesh), {
			.color = color,
			.checker_color = black,
			.material = Material::Diffuse,
			.checker = checker,
			.checker_scale = 5.5f,
			.light_map = { map_index, (axis + 1) % 3, (axis + 2) % 3, 0 }
		} });
	};

	add_wall({ a.x, a.y, b.z }, { b.x, a.y, b.z }, { b.x, a.y, a.z }, { a.x, a.y, a.z }, floor_white, true); // floor
	add_wall({ a.x, b.y, a.z }, { b.x, b.y, a.z }, { b.x, b.y, b.z }, { a.x, b.y, b.z }, white); // ceiling
	add_wall({ a.x, a.y, a.z }, { b.x, a.y, a.z }, { b.x, b.y, a.z }, { a.x, b.y, a.z }, white); // back
	add_wall({ b.x, a.y, b.z }, { a.x, a.y, b.z }, { a.x, b.y, b.z }, { b.x, b.y, b.z }, white); // front
	add_wall({ a.x, a.y, b.z }, { a.x, a.y, a.z }, { a.x, b.y, a.z }, { a.x, b.y, b.z }, { 0.02f, 0.05f, 0.8f, 1.0f }); // left
	add_wall({ b.x, a.y, a.z }, { b.x, a.y, b.z }, { b.x, b.y, b.z }, { b.x, b.y, a.z }, { 0.8f, 0.03f, 0.02f, 1.0f }); // right

	scene.light_map_count = (uint32_t)objects.size();

	// the light's radiance comes from the settings, so its intensity can change at runtime
	objects.push_back({
		.mesh = CreateDisk(LightRadius, 64),
		.object = { .material = Material::Emissive },
		.transform = glm::translate(glm::mat4(1.0f), LightCenter)
	});

	objects.push_back({
		.mesh = CreateSphere(SphereRadius, 64, 32),
		.object = { .color = chrome, .material = Material::Mirror },
		.transform = glm::translate(glm::mat4(1.0f), SphereCenter)
	});

	scene.box = (uint32_t)objects.size();
	objects.push_back({
		.mesh = CreateBox(BoxHalfSize),
		.object = { .color = glass, .material = Material::Glass },
		.bounding_radius = BoxHalfSize * glm::sqrt(3.0f)
	});

	scene.cylinder = (uint32_t)objects.size();
	objects.push_back({
		.mesh = CreateCylinder(CylinderRadius, CylinderHalfHeight, 64),
		.object = { .color = glass, .material = Material::Glass },
		.bounding_radius = glm::length(glm::vec2{ CylinderRadius, CylinderHalfHeight })
	});

	scene.caustic_targets = { scene.box, scene.cylinder };

	for (auto& scene_object : objects)
	{
		// meshes stay in object space, objects are moved by instance transforms of the top level structure
		scene_object.blas = std::make_unique<skygfx::BLAS>(scene_object.mesh.vertices, 0, scene_object.mesh.indices, 0,
			glm::mat4(1.0f));
		scene_object.object.index_offset = (uint32_t)scene.indices.size();

		auto vertex_offset = (uint32_t)scene.vertices.size();
		scene.vertices.insert(scene.vertices.end(), scene_object.mesh.vertices.begin(), scene_object.mesh.vertices.end());

		for (auto index : scene_object.mesh.indices)
		{
			scene.indices.push_back(vertex_offset + index);
		}
	}

	return scene;
}

// the box and the cylinder orbit the sphere on opposite sides, one revolution per 10 seconds
void AnimateScene(Scene& scene, float time)
{
	const auto orbit_radius = glm::vec2{ 1.25f, 0.9f };
	const auto orbit_height = 1.82f;
	auto orbit = time * 2.0f * std::numbers::pi_v<float> / 10.0f;

	auto orbit_position = [&](float angle) {
		return glm::vec3{ SphereCenter.x - glm::cos(angle) * orbit_radius.x, orbit_height,
			SphereCenter.z + glm::sin(angle) * orbit_radius.y };
	};

	auto& box = scene.objects.at(scene.box).transform;
	box = glm::translate(glm::mat4(1.0f), orbit_position(orbit));
	box = glm::rotate(box, time * 1.5f, glm::normalize(glm::vec3{ 0.3f, 1.0f, 0.2f }));
	box = glm::rotate(box, glm::radians(25.0f), { 1.0f, 0.0f, 0.0f });

	auto& cylinder = scene.objects.at(scene.cylinder).transform;
	cylinder = glm::translate(glm::mat4(1.0f), orbit_position(orbit + std::numbers::pi_v<float>));
	cylinder = glm::rotate(cylinder, time * 1.2f, glm::normalize(glm::vec3{ 0.4f, 0.2f, 1.0f }));
	cylinder = glm::rotate(cylinder, glm::radians(20.0f), { 0.0f, 0.0f, 1.0f });
}

struct Options
{
	bool animated = true;
	float speed = 1.0f;
	float light_intensity = 10.0f;
	float exposure = 1.0f;
	float ior = 1.5f;
	int shadow_samples = 16;
	bool caustics = true;
	bool indirect = true;
};

void ShowOptions(Options& options)
{
	ImGui::Begin("Settings", NULL, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove);
	ImGui::SetWindowPos({ 16.0f, 16.0f });
	ImGui::Text("%.0f fps", ImGui::GetIO().Framerate);
	ImGui::Checkbox("Animated", &options.animated);
	ImGui::SliderFloat("Speed", &options.speed, 0.0f, 3.0f);
	ImGui::SliderFloat("Light", &options.light_intensity, 0.0f, 30.0f);
	ImGui::SliderFloat("Exposure", &options.exposure, 0.1f, 4.0f);
	ImGui::SliderFloat("IOR", &options.ior, 1.0f, 2.5f);
	ImGui::SliderInt("Shadow samples", &options.shadow_samples, 1, 64);
	ImGui::Checkbox("Caustics", &options.caustics);
	ImGui::Checkbox("Indirect light", &options.indirect);
	ImGui::End();
}

Settings MakeSettings(const Scene& scene, const Options& options, float aspect)
{
	auto tan_half_fov = glm::tan(Fov * 0.5f);

	auto settings = Settings{
		.eye = glm::vec4(Eye, 0.0f),
		.forward = { 0.0f, 0.0f, -1.0f, 0.0f },
		.right = { tan_half_fov * aspect, 0.0f, 0.0f, 0.0f },
		.up = { 0.0f, tan_half_fov, 0.0f, 0.0f },
		.light = glm::vec4(LightCenter, LightRadius),
		.light_color = glm::vec4(LightColor, 1.0f),
		.room_min = glm::vec4(RoomMin, 0.0f),
		.room_max = glm::vec4(RoomMax, 0.0f),
		.light_intensity = options.light_intensity,
		.ior = options.ior,
		.exposure = options.exposure,
		.shadow_samples = (uint32_t)options.shadow_samples,
		.caustics = options.caustics,
		.indirect = options.indirect
	};

	for (uint32_t i = 0; i < CausticLayers; i++)
	{
		auto index = scene.caustic_targets.at(i);
		const auto& target = scene.objects.at(index);
		settings.caustic_targets[i] = glm::vec4(glm::vec3(target.transform[3]), target.bounding_radius);
		settings.caustic_target_objects[i] = index;
	}

	return settings;
}

// caustic maps of all room surfaces, then their indirect maps
struct LightMapsLayout
{
	uint32_t indirect_offset;
	uint32_t size;
};

LightMapsLayout GetLightMapsLayout(uint32_t map_count)
{
	auto caustic_size = map_count * CausticMapResolution * CausticMapResolution * 3;
	auto indirect_size = map_count * IndirectMapResolution * IndirectMapResolution * 3;
	return { caustic_size, caustic_size + indirect_size };
}

// constants shared with the shaders, light maps store fixed point irradiance,
// so photons can add to them with integer atomics
std::vector<std::string> MakeShaderDefines(const LightMapsLayout& layout)
{
	return {
		"CAUSTIC_MAP_RESOLUTION " + std::to_string(CausticMapResolution) + "u",
		"INDIRECT_MAP_RESOLUTION " + std::to_string(IndirectMapResolution) + "u",
		"INDIRECT_MAP_OFFSET " + std::to_string(layout.indirect_offset) + "u",
		"CAUSTIC_LAYERS " + std::to_string(CausticLayers) + "u",
		"NO_LIGHT_MAP " + std::to_string(NoLightMap) + "u",
		"MATERIAL_DIFFUSE " + std::to_string((uint32_t)Material::Diffuse) + "u",
		"MATERIAL_MIRROR " + std::to_string((uint32_t)Material::Mirror) + "u",
		"MATERIAL_EMISSIVE " + std::to_string((uint32_t)Material::Emissive) + "u",
		"MATERIAL_GLASS " + std::to_string((uint32_t)Material::Glass) + "u",
		"LIGHT_MAP_SCALE 1048576.0"
	};
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

	auto scene = CreateScene();

	const auto light_maps = GetLightMapsLayout(scene.light_map_count);
	const auto defines = MakeShaderDefines(light_maps);

	std::vector<Object> objects;
	std::vector<skygfx::TLAS::Instance> instances;

	for (uint32_t i = 0; i < (uint32_t)scene.objects.size(); i++)
	{
		auto& scene_object = scene.objects.at(i);
		objects.push_back(scene_object.object);
		instances.push_back({ .blas = *scene_object.blas, .custom_index = i });
	}

	auto objects_buffer = skygfx::StorageBuffer(objects.data(), objects.size() * sizeof(Object));
	auto indices_buffer = skygfx::StorageBuffer(scene.indices.data(), scene.indices.size() * sizeof(uint32_t));
	auto vertices_buffer = skygfx::StorageBuffer(scene.vertices.data(), scene.vertices.size() * sizeof(Vertex));
	auto light_maps_buffer = skygfx::StorageBuffer(light_maps.size * sizeof(uint32_t));

	auto miss_shaders = { miss_shader_code, shadow_miss_shader_code };
	auto render_shader = skygfx::RaytracingShader(render_raygen_shader_code, miss_shaders, closesthit_shader_code, defines);
	auto photon_shader = skygfx::RaytracingShader(photon_raygen_shader_code, miss_shaders, closesthit_shader_code, defines);
	auto clear_shader = skygfx::RaytracingShader(clear_raygen_shader_code, miss_shaders, closesthit_shader_code, defines);

	auto imgui = ImguiHelper();

	ImGui_ImplGlfw_InitForOpenGL(window, true);

	Options options;
	bool indirect_ready = false;
	float time = 0.0f;
	auto prev_time = glfwGetTime();

	while (!glfwWindowShouldClose(window))
	{
		auto now = glfwGetTime();
		auto dt = (float)(now - prev_time);
		prev_time = now;

		ImGui_ImplGlfw_NewFrame();
		ImGui::NewFrame();
		ShowOptions(options);

		if (options.animated)
			time += dt * options.speed;

		AnimateScene(scene, time);

		for (size_t i = 0; i < scene.objects.size(); i++)
		{
			instances.at(i).transform = scene.objects.at(i).transform;
		}

		auto tlas = skygfx::TLAS(instances);

		auto target = skygfx::AcquireTransientRenderTarget();
		auto aspect = (float)target->getWidth() / (float)target->getHeight();

		skygfx::SetUniformBuffer(2, MakeSettings(scene, options, aspect));
		skygfx::SetStorageBuffer(3, objects_buffer);
		skygfx::SetStorageBuffer(4, indices_buffer);
		skygfx::SetStorageBuffer(5, vertices_buffer);
		skygfx::SetStorageBuffer(6, light_maps_buffer);
		skygfx::SetRenderTarget(*target);
		skygfx::SetAccelerationStructure(0, tlas);

		skygfx::SetShader(clear_shader);
		skygfx::DispatchRays(indirect_ready ? light_maps.indirect_offset : light_maps.size, 1, 1);

		skygfx::SetShader(photon_shader);
		skygfx::DispatchRays(PhotonGridSize, PhotonGridSize, CausticLayers + (indirect_ready ? 0 : IndirectLayers));
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
