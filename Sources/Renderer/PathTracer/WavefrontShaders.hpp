#ifndef RW_ENGINE_RENDERER_PATHTRACER_WAVEFRONT_SHADERS_HPP
#define RW_ENGINE_RENDERER_PATHTRACER_WAVEFRONT_SHADERS_HPP

namespace Renderer::WavefrontShaders {

inline constexpr const char *common = R"GLSL(
#version 430

const uint LEAF_BIT = 0x80000000u;
const uint LEAF_FIRST_MASK = 0x00ffffffu;
const float PI = 3.14159265358979323846;
const float INF = 1.0e30;
const float RAY_EPSILON = 0.0025;
const int WIDE_STACK_DEPTH = 16;

struct WideNode {
    vec4 base_min;
    vec4 extent;
    uvec4 child_ref0;
    uvec4 child_ref1;
    uvec4 bounds0_0;
    uvec4 bounds0_1;
    uvec4 bounds1_0;
    uvec4 bounds1_1;
};

struct Triangle {
    vec4 p0;
    vec4 p1;
    vec4 p2;
    vec4 n0;
    vec4 n1;
    vec4 n2;
    vec4 uv01;
    vec4 uv2;
};

struct Instance {
    mat4 object_to_world;
    mat4 world_to_object;
    uvec4 data;
};

struct Material { vec4 base_color; ivec4 data; };
struct RayData { vec4 origin_pixel; vec4 direction_rng; };
struct SurfaceData { vec4 position_depth; vec4 normal_material; vec4 uv_source; vec4 direct; };
struct ReservoirData { vec4 sample_position_m; vec4 radiance_weight; };
struct CacheEntry { uvec4 header; uvec4 radiance; };

layout(std430, binding = 0) readonly buffer AllNodes { WideNode all_nodes[]; };
layout(std430, binding = 1) readonly buffer Triangles { Triangle triangles[]; };
layout(std430, binding = 2) readonly buffer Instances { Instance instances[]; };
layout(std430, binding = 3) readonly buffer Materials { Material materials[]; };

uniform int uTlasNodeCount;
uniform int uNodeCount;
uniform int uInstanceCount;
uniform int uMaterialCount;
uniform int uHasLight;
uniform uint uFrameIndex;
uniform vec3 uLightPosition;
uniform vec3 uLightColor;
uniform float uLightIntensity;

uniform sampler2D uTexture0;
uniform sampler2D uTexture1;
uniform sampler2D uTexture2;
uniform sampler2D uTexture3;
uniform sampler2D uTexture4;
uniform sampler2D uTexture5;
uniform sampler2D uTexture6;
uniform sampler2D uTexture7;
uniform sampler2D uTexture8;
uniform sampler2D uTexture9;
uniform sampler2D uTexture10;
uniform sampler2D uTexture11;
uniform sampler2D uTexture12;
uniform sampler2D uTexture13;
uniform sampler2D uTexture14;
uniform sampler2D uTexture15;

struct Hit {
    bool found;
    float distance;
    vec3 position;
    vec3 normal;
    vec2 uv;
    uint material;
};

uint hashUint(uint value)
{
    value ^= value >> 16;
    value *= 0x7feb352du;
    value ^= value >> 15;
    value *= 0x846ca68bu;
    value ^= value >> 16;
    return value;
}

float randomFloat(inout uint state)
{
    state = hashUint(state);
    return float(state) * (1.0 / 4294967296.0);
}

uint childReference(WideNode node, int child)
{
    return child < 4 ? node.child_ref0[child] : node.child_ref1[child - 4];
}

uint childBounds0(WideNode node, int child)
{
    return child < 4 ? node.bounds0_0[child] : node.bounds0_1[child - 4];
}

uint childBounds1(WideNode node, int child)
{
    return child < 4 ? node.bounds1_0[child] : node.bounds1_1[child - 4];
}

void decodeChildBounds(WideNode node, int child, out vec3 minimum, out vec3 maximum)
{
    uint packed0 = childBounds0(node, child);
    uint packed1 = childBounds1(node, child);
    vec3 qmin = vec3(
        float(packed0 & 255u),
        float((packed0 >> 8u) & 255u),
        float((packed0 >> 16u) & 255u)
    ) / 255.0;
    vec3 qmax = vec3(
        float((packed0 >> 24u) & 255u),
        float(packed1 & 255u),
        float((packed1 >> 8u) & 255u)
    ) / 255.0;
    minimum = node.base_min.xyz + node.extent.xyz * qmin;
    maximum = node.base_min.xyz + node.extent.xyz * qmax;
}

vec3 safeInverse(vec3 direction)
{
    return vec3(
        abs(direction.x) > 1.0e-10 ? 1.0 / direction.x : 1.0e30,
        abs(direction.y) > 1.0e-10 ? 1.0 / direction.y : 1.0e30,
        abs(direction.z) > 1.0e-10 ? 1.0 / direction.z : 1.0e30
    );
}

float aabbEntry(vec3 origin, vec3 inverse_direction, vec3 minimum, vec3 maximum, float max_distance)
{
    vec3 t0 = (minimum - origin) * inverse_direction;
    vec3 t1 = (maximum - origin) * inverse_direction;
    vec3 near_t = min(t0, t1);
    vec3 far_t = max(t0, t1);
    float enter = max(max(near_t.x, near_t.y), max(near_t.z, 0.0));
    float exit = min(min(far_t.x, far_t.y), far_t.z);
    return exit >= enter && enter < max_distance ? enter : INF;
}

bool hitTriangle(vec3 origin, vec3 direction, Triangle triangle, inout float distance, out vec3 barycentric)
{
    vec3 edge1 = triangle.p1.xyz - triangle.p0.xyz;
    vec3 edge2 = triangle.p2.xyz - triangle.p0.xyz;
    vec3 p = cross(direction, edge2);
    float determinant = dot(edge1, p);
    if (abs(determinant) < 1.0e-8) return false;
    float inverse_determinant = 1.0 / determinant;
    vec3 offset = origin - triangle.p0.xyz;
    float u = dot(offset, p) * inverse_determinant;
    if (u < 0.0 || u > 1.0) return false;
    vec3 q = cross(offset, edge1);
    float v = dot(direction, q) * inverse_determinant;
    if (v < 0.0 || u + v > 1.0) return false;
    float t = dot(edge2, q) * inverse_determinant;
    if (t <= RAY_EPSILON || t >= distance) return false;
    distance = t;
    barycentric = vec3(1.0 - u - v, u, v);
    return true;
}

vec3 transformPoint(mat4 matrix, vec3 point)
{
    vec4 result = matrix * vec4(point, 1.0);
    return result.xyz / max(abs(result.w), 1.0e-8);
}

vec3 transformDirection(mat4 matrix, vec3 direction)
{
    return (matrix * vec4(direction, 0.0)).xyz;
}

uint childHitMask(WideNode node, vec3 origin, vec3 inverse_direction, float max_distance)
{
    uint mask = 0u;
    int count = clamp(int(node.base_min.w + 0.5), 0, 8);
    for (int child = 0; child < count; ++child) {
        vec3 minimum;
        vec3 maximum;
        decodeChildBounds(node, child, minimum, maximum);
        if (aabbEntry(origin, inverse_direction, minimum, maximum, max_distance) < INF) {
            mask |= 1u << uint(child);
        }
    }
    return mask;
}

int nearestChild(WideNode node, uint mask, vec3 origin, vec3 inverse_direction, float max_distance)
{
    int best_child = -1;
    float best_entry = INF;
    while (mask != 0u) {
        int child = findLSB(mask);
        mask &= ~(1u << uint(child));
        vec3 minimum;
        vec3 maximum;
        decodeChildBounds(node, child, minimum, maximum);
        float entry = aabbEntry(origin, inverse_direction, minimum, maximum, max_distance);
        if (entry < best_entry) {
            best_entry = entry;
            best_child = child;
        }
    }
    return best_child;
}

void traceBlasClosest(
    uint instance_index,
    vec3 world_origin,
    vec3 world_direction,
    float max_world_distance,
    inout Hit best)
{
    if (instance_index >= uint(max(uInstanceCount, 0))) return;
    Instance instance = instances[instance_index];
    uint node_begin = instance.data.x;
    uint node_end = node_begin + instance.data.y;
    if (instance.data.y == 0u || node_begin >= uint(max(uNodeCount, 0)) || node_end > uint(max(uNodeCount, 0))) return;

    vec3 local_origin = transformPoint(instance.world_to_object, world_origin);
    vec3 local_direction_raw = transformDirection(instance.world_to_object, world_direction);
    float local_direction_length = length(local_direction_raw);
    if (local_direction_length <= 1.0e-10) return;
    vec3 local_direction = local_direction_raw / local_direction_length;
    vec3 inverse_direction = safeInverse(local_direction);

    float local_best = INF;
    if (best.distance < INF * 0.5) {
        vec3 local_end = transformPoint(
            instance.world_to_object,
            world_origin + world_direction * best.distance
        );
        local_best = length(local_end - local_origin);
    }

    uvec2 stack[WIDE_STACK_DEPTH];
    int stack_size = 0;
    uint node_index = node_begin;
    WideNode node = all_nodes[node_index];
    uint mask = childHitMask(node, local_origin, inverse_direction, local_best);

    for (int guard = 0; guard < 4096; ++guard) {
        if (mask == 0u) {
            if (stack_size == 0) break;
            uvec2 frame = stack[--stack_size];
            node_index = frame.x;
            mask = frame.y;
            node = all_nodes[node_index];
            continue;
        }

        int child = nearestChild(node, mask, local_origin, inverse_direction, local_best);
        if (child < 0) {
            mask = 0u;
            continue;
        }
        mask &= ~(1u << uint(child));
        uint reference = childReference(node, child);

        if ((reference & LEAF_BIT) != 0u) {
            uint first = reference & LEAF_FIRST_MASK;
            uint count = (reference >> 24u) & 0x7fu;
            uint triangle_end = instance.data.z + instance.data.w;
            if (first < instance.data.z || first + count > triangle_end) continue;
            for (uint index = 0u; index < count; ++index) {
                Triangle triangle = triangles[first + index];
                float local_distance = local_best;
                vec3 barycentric;
                if (!hitTriangle(local_origin, local_direction, triangle, local_distance, barycentric)) continue;
                local_best = local_distance;

                vec3 local_position = local_origin + local_direction * local_distance;
                vec3 world_position = transformPoint(instance.object_to_world, local_position);
                float world_distance = length(world_position - world_origin);
                if (world_distance >= best.distance || world_distance >= max_world_distance) continue;

                vec3 local_normal = normalize(
                    triangle.n0.xyz * barycentric.x +
                    triangle.n1.xyz * barycentric.y +
                    triangle.n2.xyz * barycentric.z
                );
                vec3 world_normal = normalize(transpose(mat3(instance.world_to_object)) * local_normal);
                if (dot(world_normal, world_direction) > 0.0) world_normal = -world_normal;

                best.found = true;
                best.distance = world_distance;
                best.position = world_position;
                best.normal = world_normal;
                best.uv =
                    triangle.uv01.xy * barycentric.x +
                    triangle.uv01.zw * barycentric.y +
                    triangle.uv2.xy * barycentric.z;
                best.material = uint(max(triangle.uv2.z, 0.0) + 0.5);
            }
        } else if (reference >= node_begin && reference < node_end) {
            if (stack_size >= WIDE_STACK_DEPTH) continue;
            stack[stack_size++] = uvec2(node_index, mask);
            node_index = reference;
            node = all_nodes[node_index];
            mask = childHitMask(node, local_origin, inverse_direction, local_best);
        }
    }
}

bool traceBlasAny(uint instance_index, vec3 world_origin, vec3 world_direction, float max_world_distance)
{
    if (instance_index >= uint(max(uInstanceCount, 0)) || max_world_distance <= RAY_EPSILON) return false;
    Instance instance = instances[instance_index];
    uint node_begin = instance.data.x;
    uint node_end = node_begin + instance.data.y;
    if (instance.data.y == 0u || node_begin >= uint(max(uNodeCount, 0)) || node_end > uint(max(uNodeCount, 0))) return false;

    vec3 local_origin = transformPoint(instance.world_to_object, world_origin);
    vec3 local_direction_raw = transformDirection(instance.world_to_object, world_direction);
    float local_direction_length = length(local_direction_raw);
    if (local_direction_length <= 1.0e-10) return false;
    vec3 local_direction = local_direction_raw / local_direction_length;
    vec3 inverse_direction = safeInverse(local_direction);
    vec3 local_end = transformPoint(
        instance.world_to_object,
        world_origin + world_direction * max_world_distance
    );
    float local_max_distance = length(local_end - local_origin);

    uvec2 stack[WIDE_STACK_DEPTH];
    int stack_size = 0;
    uint node_index = node_begin;
    WideNode node = all_nodes[node_index];
    uint mask = childHitMask(node, local_origin, inverse_direction, local_max_distance);

    for (int guard = 0; guard < 2048; ++guard) {
        if (mask == 0u) {
            if (stack_size == 0) break;
            uvec2 frame = stack[--stack_size];
            node_index = frame.x;
            mask = frame.y;
            node = all_nodes[node_index];
            continue;
        }

        int child = findLSB(mask);
        mask &= ~(1u << uint(child));
        uint reference = childReference(node, child);
        if ((reference & LEAF_BIT) != 0u) {
            uint first = reference & LEAF_FIRST_MASK;
            uint count = (reference >> 24u) & 0x7fu;
            uint triangle_end = instance.data.z + instance.data.w;
            if (first < instance.data.z || first + count > triangle_end) continue;
            for (uint index = 0u; index < count; ++index) {
                float distance = local_max_distance;
                vec3 barycentric;
                if (hitTriangle(local_origin, local_direction, triangles[first + index], distance, barycentric)) {
                    return true;
                }
            }
        } else if (reference >= node_begin && reference < node_end) {
            if (stack_size >= WIDE_STACK_DEPTH) continue;
            stack[stack_size++] = uvec2(node_index, mask);
            node_index = reference;
            node = all_nodes[node_index];
            mask = childHitMask(node, local_origin, inverse_direction, local_max_distance);
        }
    }
    return false;
}

Hit traceClosest(vec3 origin, vec3 direction, float max_distance)
{
    Hit best;
    best.found = false;
    best.distance = max_distance;
    best.position = vec3(0.0);
    best.normal = vec3(0.0, 1.0, 0.0);
    best.uv = vec2(0.0);
    best.material = 0u;
    if (uTlasNodeCount <= 0 || uInstanceCount <= 0) return best;

    vec3 inverse_direction = safeInverse(direction);
    uvec2 stack[WIDE_STACK_DEPTH];
    int stack_size = 0;
    uint node_index = 0u;
    WideNode node = all_nodes[0];
    uint mask = childHitMask(node, origin, inverse_direction, best.distance);

    for (int guard = 0; guard < 1024; ++guard) {
        if (mask == 0u) {
            if (stack_size == 0) break;
            uvec2 frame = stack[--stack_size];
            node_index = frame.x;
            mask = frame.y;
            node = all_nodes[node_index];
            continue;
        }
        int child = nearestChild(node, mask, origin, inverse_direction, best.distance);
        if (child < 0) {
            mask = 0u;
            continue;
        }
        mask &= ~(1u << uint(child));
        uint reference = childReference(node, child);
        if ((reference & LEAF_BIT) != 0u) {
            uint first = reference & LEAF_FIRST_MASK;
            uint count = (reference >> 24u) & 0x7fu;
            for (uint index = 0u; index < count; ++index) {
                uint instance_index = first + index;
                if (instance_index < uint(uInstanceCount)) {
                    traceBlasClosest(instance_index, origin, direction, best.distance, best);
                }
            }
        } else if (reference < uint(uTlasNodeCount)) {
            if (stack_size >= WIDE_STACK_DEPTH) continue;
            stack[stack_size++] = uvec2(node_index, mask);
            node_index = reference;
            node = all_nodes[node_index];
            mask = childHitMask(node, origin, inverse_direction, best.distance);
        }
    }
    return best;
}

bool traceAny(vec3 origin, vec3 direction, float max_distance)
{
    if (uTlasNodeCount <= 0 || uInstanceCount <= 0 || max_distance <= RAY_EPSILON) return false;
    vec3 inverse_direction = safeInverse(direction);
    uvec2 stack[WIDE_STACK_DEPTH];
    int stack_size = 0;
    uint node_index = 0u;
    WideNode node = all_nodes[0];
    uint mask = childHitMask(node, origin, inverse_direction, max_distance);

    for (int guard = 0; guard < 512; ++guard) {
        if (mask == 0u) {
            if (stack_size == 0) break;
            uvec2 frame = stack[--stack_size];
            node_index = frame.x;
            mask = frame.y;
            node = all_nodes[node_index];
            continue;
        }
        int child = findLSB(mask);
        mask &= ~(1u << uint(child));
        uint reference = childReference(node, child);
        if ((reference & LEAF_BIT) != 0u) {
            uint first = reference & LEAF_FIRST_MASK;
            uint count = (reference >> 24u) & 0x7fu;
            for (uint index = 0u; index < count; ++index) {
                uint instance_index = first + index;
                if (instance_index < uint(uInstanceCount) && traceBlasAny(instance_index, origin, direction, max_distance)) {
                    return true;
                }
            }
        } else if (reference < uint(uTlasNodeCount)) {
            if (stack_size >= WIDE_STACK_DEPTH) continue;
            stack[stack_size++] = uvec2(node_index, mask);
            node_index = reference;
            node = all_nodes[node_index];
            mask = childHitMask(node, origin, inverse_direction, max_distance);
        }
    }
    return false;
}

vec4 sampleTextureSlot(int slot, vec2 uv)
{
    vec2 coordinates = vec2(uv.x, 1.0 - uv.y);
    switch (slot) {
        case 0: return texture(uTexture0, coordinates);
        case 1: return texture(uTexture1, coordinates);
        case 2: return texture(uTexture2, coordinates);
        case 3: return texture(uTexture3, coordinates);
        case 4: return texture(uTexture4, coordinates);
        case 5: return texture(uTexture5, coordinates);
        case 6: return texture(uTexture6, coordinates);
        case 7: return texture(uTexture7, coordinates);
        case 8: return texture(uTexture8, coordinates);
        case 9: return texture(uTexture9, coordinates);
        case 10: return texture(uTexture10, coordinates);
        case 11: return texture(uTexture11, coordinates);
        case 12: return texture(uTexture12, coordinates);
        case 13: return texture(uTexture13, coordinates);
        case 14: return texture(uTexture14, coordinates);
        case 15: return texture(uTexture15, coordinates);
        default: return vec4(1.0);
    }
}

vec3 materialAlbedo(uint material_index, vec2 uv)
{
    if (material_index >= uint(max(uMaterialCount, 0))) return vec3(1.0);
    Material material = materials[material_index];
    vec3 albedo = max(material.base_color.rgb, vec3(0.0));
    int slot = material.data.x;
    if (slot >= 0 && slot < 16) {
        vec4 texel = sampleTextureSlot(slot, uv);
        albedo *= pow(max(texel.rgb, vec3(0.0)), vec3(2.2));
    }
    return clamp(albedo, vec3(0.0), vec3(1.0));
}

vec3 cosineHemisphere(vec3 normal, inout uint state)
{
    float u1 = randomFloat(state);
    float u2 = randomFloat(state);
    float radius = sqrt(u1);
    float phi = 2.0 * PI * u2;
    vec3 local = vec3(radius * cos(phi), radius * sin(phi), sqrt(max(0.0, 1.0 - u1)));
    vec3 helper = abs(normal.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
    vec3 tangent = normalize(cross(helper, normal));
    vec3 bitangent = cross(normal, tangent);
    return normalize(tangent * local.x + bitangent * local.y + normal * local.z);
}

vec3 pointLight(vec3 position, vec3 normal, vec3 albedo)
{
    if (uHasLight == 0 || uLightIntensity <= 0.0) return vec3(0.0);
    vec3 to_light = uLightPosition - position;
    float distance_squared = max(dot(to_light, to_light), 1.0e-4);
    float light_distance = sqrt(distance_squared);
    vec3 light_direction = to_light / light_distance;
    float cosine = max(dot(normal, light_direction), 0.0);
    if (cosine <= 0.0) return vec3(0.0);
    if (traceAny(
        position + normal * RAY_EPSILON * 4.0,
        light_direction,
        max(light_distance - RAY_EPSILON * 8.0, 0.0)))
    {
        return vec3(0.0);
    }
    return
        albedo *
        max(uLightColor, vec3(0.0)) *
        max(uLightIntensity, 0.0) /
        distance_squared *
        (cosine / PI);
}
)GLSL";

inline constexpr const char *primary_generate = R"GLSL(
layout(local_size_x = 8, local_size_y = 8) in;
layout(std430, binding = 4) writeonly buffer PrimaryRays { RayData primary_rays[]; };
uniform int uResolutionX;
uniform int uResolutionY;
uniform vec3 uCameraPosition;
uniform vec3 uCameraForward;
uniform vec3 uCameraRight;
uniform vec3 uCameraUp;
uniform float uTanHalfFov;
uniform float uAspect;
void main()
{
    ivec2 resolution = ivec2(uResolutionX, uResolutionY);
    ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
    if (any(greaterThanEqual(pixel, resolution))) return;
    uint index = uint(pixel.y * resolution.x + pixel.x);
    uint state = hashUint(index * 9781u ^ uFrameIndex * 6271u ^ 0x68bc21ebu);
    vec2 jitter = vec2(randomFloat(state), randomFloat(state)) - 0.5;
    vec2 uv = (vec2(pixel) + 0.5 + jitter) / vec2(resolution);
    vec2 ndc = uv * 2.0 - 1.0;
    vec3 direction = normalize(
        uCameraForward +
        uCameraRight * (ndc.x * uAspect * uTanHalfFov) +
        uCameraUp * (ndc.y * uTanHalfFov)
    );
    RayData ray;
    ray.origin_pixel = vec4(uCameraPosition, uintBitsToFloat(index));
    ray.direction_rng = vec4(direction, uintBitsToFloat(state));
    primary_rays[index] = ray;
}
)GLSL";

inline constexpr const char *primary_intersect = R"GLSL(
layout(local_size_x = 64) in;
layout(std430, binding = 4) readonly buffer PrimaryRays { RayData primary_rays[]; };
layout(std430, binding = 5) writeonly buffer PrimarySurfaces { SurfaceData primary_surfaces[]; };
layout(std430, binding = 6) buffer QueueControlBlock {
    uvec4 counters;
    uvec4 hit_dispatch;
    uvec4 bounce_dispatch;
    uvec4 bucket_count0;
    uvec4 bucket_count1;
    uvec4 bucket_offset0;
    uvec4 bucket_offset1;
    uvec4 bucket_cursor0;
    uvec4 bucket_cursor1;
    uint hit_queue[];
};
layout(std430, binding = 7) writeonly buffer InitialReservoirs { ReservoirData initial_reservoirs[]; };
uniform int uPixelCount;
void main()
{
    uint index = gl_GlobalInvocationID.x;
    if (index >= uint(max(uPixelCount, 0))) return;
    RayData ray = primary_rays[index];
    Hit hit = traceClosest(ray.origin_pixel.xyz, ray.direction_rng.xyz, INF);

    SurfaceData surface;
    surface.position_depth = vec4(0.0, 0.0, 0.0, -1.0);
    surface.normal_material = vec4(0.0, 1.0, 0.0, 0.0);
    surface.uv_source = vec4(0.0);
    surface.direct = vec4(0.0);
    ReservoirData empty_reservoir;
    empty_reservoir.sample_position_m = vec4(0.0);
    empty_reservoir.radiance_weight = vec4(0.0);
    initial_reservoirs[index] = empty_reservoir;

    if (hit.found) {
        surface.position_depth = vec4(hit.position, hit.distance);
        surface.normal_material = vec4(hit.normal, float(hit.material));
        surface.uv_source = vec4(hit.uv, 0.0, 0.0);
        uint queue_index = atomicAdd(counters.x, 1u);
        if (queue_index < uint(uPixelCount)) hit_queue[queue_index] = index;
    }
    primary_surfaces[index] = surface;
}
)GLSL";

inline constexpr const char *prepare_dispatch = R"GLSL(
#version 430
layout(local_size_x = 1) in;
layout(std430, binding = 0) buffer QueueControlBlock {
    uvec4 counters;
    uvec4 hit_dispatch;
    uvec4 bounce_dispatch;
    uvec4 bucket_count0;
    uvec4 bucket_count1;
    uvec4 bucket_offset0;
    uvec4 bucket_offset1;
    uvec4 bucket_cursor0;
    uvec4 bucket_cursor1;
    uint hit_queue[];
};
void main()
{
    hit_dispatch = uvec4((counters.x + 63u) / 64u, 1u, 1u, 0u);
    bounce_dispatch = uvec4((counters.y + 63u) / 64u, 1u, 1u, 0u);
}
)GLSL";

inline constexpr const char *primary_shade = R"GLSL(
layout(local_size_x = 64) in;
layout(std430, binding = 4) buffer PrimarySurfaces { SurfaceData primary_surfaces[]; };
layout(std430, binding = 5) writeonly buffer BounceRays { RayData bounce_rays[]; };
layout(std430, binding = 6) buffer QueueControlBlock {
    uvec4 counters;
    uvec4 hit_dispatch;
    uvec4 bounce_dispatch;
    uvec4 bucket_count0;
    uvec4 bucket_count1;
    uvec4 bucket_offset0;
    uvec4 bucket_offset1;
    uvec4 bucket_cursor0;
    uvec4 bucket_cursor1;
    uint hit_queue[];
};
uniform int uPixelCount;
uniform int uResolutionX;
void main()
{
    uint queue_index = gl_GlobalInvocationID.x;
    if (queue_index >= counters.x) return;
    uint pixel = hit_queue[queue_index];
    if (pixel >= uint(max(uPixelCount, 0))) return;

    SurfaceData surface = primary_surfaces[pixel];
    uint material = uint(max(surface.normal_material.w, 0.0) + 0.5);
    vec3 albedo = materialAlbedo(material, surface.uv_source.xy);
    surface.direct = vec4(pointLight(surface.position_depth.xyz, surface.normal_material.xyz, albedo), 1.0);
    primary_surfaces[pixel] = surface;

    uint width = uint(max(uResolutionX, 1));
    uint x = pixel % width;
    uint y = pixel / width;
    uint pixel_phase = (x & 1u) | ((y & 1u) << 1u);
    if (pixel_phase != (uFrameIndex & 3u)) return;

    uint state = hashUint(pixel * 9781u ^ uFrameIndex * 7919u ^ 0xa511e9b3u);
    RayData bounce;
    bounce.origin_pixel = vec4(
        surface.position_depth.xyz + surface.normal_material.xyz * RAY_EPSILON * 4.0,
        uintBitsToFloat(pixel)
    );
    bounce.direction_rng = vec4(cosineHemisphere(surface.normal_material.xyz, state), uintBitsToFloat(state));
    uint output_index = atomicAdd(counters.y, 1u);
    if (output_index < uint(uPixelCount)) bounce_rays[output_index] = bounce;
}
)GLSL";

inline constexpr const char *classify_bounces = R"GLSL(
#version 430
layout(local_size_x = 64) in;
struct RayData { vec4 origin_pixel; vec4 direction_rng; };
layout(std430, binding = 0) readonly buffer BounceRays { RayData bounce_rays[]; };
layout(std430, binding = 1) buffer QueueControlBlock {
    uvec4 counters;
    uvec4 hit_dispatch;
    uvec4 bounce_dispatch;
    uvec4 bucket_count0;
    uvec4 bucket_count1;
    uvec4 bucket_offset0;
    uvec4 bucket_offset1;
    uvec4 bucket_cursor0;
    uvec4 bucket_cursor1;
    uint hit_queue[];
};
uniform int uPixelCount;
uint directionBucket(vec3 direction)
{
    return
        (direction.x >= 0.0 ? 1u : 0u) |
        (direction.y >= 0.0 ? 2u : 0u) |
        (direction.z >= 0.0 ? 4u : 0u);
}
void main()
{
    uint index = gl_GlobalInvocationID.x;
    if (index >= counters.y || index >= uint(max(uPixelCount, 0))) return;
    uint bucket = directionBucket(bounce_rays[index].direction_rng.xyz);
    if (bucket < 4u) atomicAdd(bucket_count0[bucket], 1u);
    else atomicAdd(bucket_count1[bucket - 4u], 1u);
}
)GLSL";

inline constexpr const char *prepare_reorder = R"GLSL(
#version 430
layout(local_size_x = 1) in;
layout(std430, binding = 0) buffer QueueControlBlock {
    uvec4 counters;
    uvec4 hit_dispatch;
    uvec4 bounce_dispatch;
    uvec4 bucket_count0;
    uvec4 bucket_count1;
    uvec4 bucket_offset0;
    uvec4 bucket_offset1;
    uvec4 bucket_cursor0;
    uvec4 bucket_cursor1;
    uint hit_queue[];
};
uint getBucketCount(uint bucket)
{
    return bucket < 4u ? bucket_count0[bucket] : bucket_count1[bucket - 4u];
}
void setBucketOffset(uint bucket, uint value)
{
    if (bucket < 4u) bucket_offset0[bucket] = value;
    else bucket_offset1[bucket - 4u] = value;
}
void main()
{
    uint offset = 0u;
    for (uint bucket = 0u; bucket < 8u; ++bucket) {
        setBucketOffset(bucket, offset);
        offset += getBucketCount(bucket);
    }
    bucket_cursor0 = uvec4(0u);
    bucket_cursor1 = uvec4(0u);
}
)GLSL";

inline constexpr const char *scatter_bounces = R"GLSL(
#version 430
layout(local_size_x = 64) in;
struct RayData { vec4 origin_pixel; vec4 direction_rng; };
layout(std430, binding = 0) readonly buffer BounceRays { RayData bounce_rays[]; };
layout(std430, binding = 1) writeonly buffer SortedBounceRays { RayData sorted_bounce_rays[]; };
layout(std430, binding = 2) buffer QueueControlBlock {
    uvec4 counters;
    uvec4 hit_dispatch;
    uvec4 bounce_dispatch;
    uvec4 bucket_count0;
    uvec4 bucket_count1;
    uvec4 bucket_offset0;
    uvec4 bucket_offset1;
    uvec4 bucket_cursor0;
    uvec4 bucket_cursor1;
    uint hit_queue[];
};
uniform int uPixelCount;
uint directionBucket(vec3 direction)
{
    return
        (direction.x >= 0.0 ? 1u : 0u) |
        (direction.y >= 0.0 ? 2u : 0u) |
        (direction.z >= 0.0 ? 4u : 0u);
}
uint bucketOffset(uint bucket)
{
    return bucket < 4u ? bucket_offset0[bucket] : bucket_offset1[bucket - 4u];
}
uint reserveBucket(uint bucket)
{
    if (bucket < 4u) return atomicAdd(bucket_cursor0[bucket], 1u);
    return atomicAdd(bucket_cursor1[bucket - 4u], 1u);
}
void main()
{
    uint index = gl_GlobalInvocationID.x;
    if (index >= counters.y || index >= uint(max(uPixelCount, 0))) return;
    RayData ray = bounce_rays[index];
    uint bucket = directionBucket(ray.direction_rng.xyz);
    uint output_index = bucketOffset(bucket) + reserveBucket(bucket);
    if (output_index < uint(uPixelCount)) sorted_bounce_rays[output_index] = ray;
}
)GLSL";

inline constexpr const char *bounce_intersect = R"GLSL(
layout(local_size_x = 64) in;
layout(std430, binding = 4) readonly buffer BounceRays { RayData bounce_rays[]; };
layout(std430, binding = 5) writeonly buffer SecondarySurfaces { SurfaceData secondary_surfaces[]; };
layout(std430, binding = 6) readonly buffer QueueControlBlock {
    uvec4 counters;
    uvec4 hit_dispatch;
    uvec4 bounce_dispatch;
    uvec4 bucket_count0;
    uvec4 bucket_count1;
    uvec4 bucket_offset0;
    uvec4 bucket_offset1;
    uvec4 bucket_cursor0;
    uvec4 bucket_cursor1;
    uint hit_queue[];
};
uniform int uPixelCount;
void main()
{
    uint index = gl_GlobalInvocationID.x;
    if (index >= uint(max(uPixelCount, 0))) return;

    SurfaceData surface;
    surface.position_depth = vec4(0.0, 0.0, 0.0, -1.0);
    surface.normal_material = vec4(0.0, 1.0, 0.0, 0.0);
    surface.uv_source = vec4(0.0);
    surface.direct = vec4(0.0);
    if (index >= counters.y) {
        secondary_surfaces[index] = surface;
        return;
    }

    RayData ray = bounce_rays[index];
    uint pixel = floatBitsToUint(ray.origin_pixel.w);
    surface.uv_source.z = float(pixel);
    Hit hit = traceClosest(ray.origin_pixel.xyz, ray.direction_rng.xyz, INF);
    if (hit.found) {
        surface.position_depth = vec4(hit.position, hit.distance);
        surface.normal_material = vec4(hit.normal, float(hit.material));
        surface.uv_source = vec4(hit.uv, float(pixel), 0.0);
    }
    secondary_surfaces[index] = surface;
}
)GLSL";

inline constexpr const char *bounce_shade = R"GLSL(
layout(local_size_x = 64) in;
layout(std430, binding = 4) readonly buffer SecondarySurfaces { SurfaceData secondary_surfaces[]; };
layout(std430, binding = 5) readonly buffer PrimarySurfaces { SurfaceData primary_surfaces[]; };
layout(std430, binding = 6) buffer InitialReservoirs { ReservoirData initial_reservoirs[]; };
layout(std430, binding = 7) buffer RadianceCache { CacheEntry cache_entries[]; };
uniform int uPixelCount;
uniform uint uCacheSize;

uint cacheKey(vec3 position, vec3 normal)
{
    ivec3 cell = ivec3(floor(position * 0.5));
    uint normal_bits =
        (normal.x >= 0.0 ? 1u : 0u) |
        (normal.y >= 0.0 ? 2u : 0u) |
        (normal.z >= 0.0 ? 4u : 0u);
    return hashUint(
        uint(cell.x) * 73856093u ^
        uint(cell.y) * 19349663u ^
        uint(cell.z) * 83492791u ^
        normal_bits * 2654435761u
    ) | 1u;
}

vec3 cacheQuery(vec3 position, vec3 normal)
{
    if (uCacheSize == 0u) return vec3(0.0);
    uint key = cacheKey(position, normal);
    uint start = key % uCacheSize;
    for (uint probe = 0u; probe < 4u; ++probe) {
        uint index = (start + probe) % uCacheSize;
        CacheEntry entry = cache_entries[index];
        if (entry.header.x == key && entry.header.y > 0u) {
            return vec3(entry.radiance.xyz) / (1024.0 * float(entry.header.y));
        }
        if (entry.header.x == 0u) break;
    }
    return vec3(0.0);
}

void cacheUpdate(vec3 position, vec3 normal, vec3 radiance)
{
    if (uCacheSize == 0u) return;
    uint key = cacheKey(position, normal);
    uint start = key % uCacheSize;
    uvec3 fixed_point = uvec3(clamp(radiance, vec3(0.0), vec3(64.0)) * 1024.0 + 0.5);
    for (uint probe = 0u; probe < 4u; ++probe) {
        uint index = (start + probe) % uCacheSize;
        uint existing = atomicCompSwap(cache_entries[index].header.x, 0u, key);
        if (existing == 0u || existing == key) {
            uint count = atomicAdd(cache_entries[index].header.y, 1u);
            if (count < 65535u) {
                atomicAdd(cache_entries[index].radiance.x, fixed_point.x);
                atomicAdd(cache_entries[index].radiance.y, fixed_point.y);
                atomicAdd(cache_entries[index].radiance.z, fixed_point.z);
            }
            return;
        }
    }
}

void main()
{
    uint index = gl_GlobalInvocationID.x;
    if (index >= uint(max(uPixelCount, 0))) return;
    SurfaceData secondary = secondary_surfaces[index];
    if (secondary.position_depth.w <= 0.0) return;

    uint pixel = uint(max(secondary.uv_source.z, 0.0) + 0.5);
    if (pixel >= uint(uPixelCount)) return;

    uint secondary_material = uint(max(secondary.normal_material.w, 0.0) + 0.5);
    vec3 secondary_albedo = materialAlbedo(secondary_material, secondary.uv_source.xy);
    vec3 direct = pointLight(
        secondary.position_depth.xyz,
        secondary.normal_material.xyz,
        secondary_albedo
    );
    vec3 cached = cacheQuery(secondary.position_depth.xyz, secondary.normal_material.xyz);
    vec3 outgoing = direct + cached;
    cacheUpdate(secondary.position_depth.xyz, secondary.normal_material.xyz, direct + cached * 0.5);

    SurfaceData primary = primary_surfaces[pixel];
    uint primary_material = uint(max(primary.normal_material.w, 0.0) + 0.5);
    vec3 contribution = materialAlbedo(primary_material, primary.uv_source.xy) * outgoing;
    float target = max(dot(contribution, vec3(0.2126, 0.7152, 0.0722)), 1.0e-6);

    ReservoirData reservoir;
    reservoir.sample_position_m = vec4(secondary.position_depth.xyz, 1.0);
    reservoir.radiance_weight = vec4(contribution, target);
    initial_reservoirs[pixel] = reservoir;
}
)GLSL";

} // namespace Renderer::WavefrontShaders

#endif
