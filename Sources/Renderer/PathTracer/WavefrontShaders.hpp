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

struct Material {
    vec4 base_color;
    ivec4 data;
};

struct RayData {
    vec4 origin;
    vec4 direction;
    uvec4 data;
};

struct SurfaceData {
    vec4 position_depth;
    vec4 normal_material;
    vec4 uv;
    vec4 direct;
};

struct ReservoirData {
    vec4 sample_position;
    vec4 sample_normal;
    vec4 radiance;
    vec4 weights;
};

struct CacheEntry {
    uvec4 header;
    uvec4 radiance;
};

layout(std430, binding = 0) readonly buffer BvhNodes { WideNode bvh_nodes[]; };
layout(std430, binding = 1) readonly buffer Triangles { Triangle triangles[]; };
layout(std430, binding = 2) readonly buffer Materials { Material materials[]; };

uniform int uNodeCount;
uniform int uMaterialCount;
uniform vec3 uLightPosition;
uniform vec3 uLightColor;
uniform float uLightIntensity;
uniform int uHasLight;
uniform uint uFrameIndex;

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

void decodeChildBounds(WideNode node, int child, out vec3 bmin, out vec3 bmax)
{
    uint p0 = childBounds0(node, child);
    uint p1 = childBounds1(node, child);
    vec3 qmin = vec3(
        float(p0 & 0xffu),
        float((p0 >> 8u) & 0xffu),
        float((p0 >> 16u) & 0xffu)
    ) * (1.0 / 255.0);
    vec3 qmax = vec3(
        float((p0 >> 24u) & 0xffu),
        float(p1 & 0xffu),
        float((p1 >> 8u) & 0xffu)
    ) * (1.0 / 255.0);
    bmin = node.base_min.xyz + node.extent.xyz * qmin;
    bmax = node.base_min.xyz + node.extent.xyz * qmax;
}

vec3 safeInverse(vec3 direction)
{
    return vec3(
        abs(direction.x) > 1.0e-10 ? 1.0 / direction.x : 1.0e30,
        abs(direction.y) > 1.0e-10 ? 1.0 / direction.y : 1.0e30,
        abs(direction.z) > 1.0e-10 ? 1.0 / direction.z : 1.0e30
    );
}

float aabbEntry(vec3 origin, vec3 inverse_direction, vec3 bmin, vec3 bmax, float maximum_distance)
{
    vec3 t0 = (bmin - origin) * inverse_direction;
    vec3 t1 = (bmax - origin) * inverse_direction;
    vec3 near_t = min(t0, t1);
    vec3 far_t = max(t0, t1);
    float enter = max(max(near_t.x, near_t.y), max(near_t.z, 0.0));
    float exit = min(min(far_t.x, far_t.y), far_t.z);
    return exit >= enter && enter < maximum_distance ? enter : INF;
}

bool hitTriangle(vec3 origin, vec3 direction, Triangle triangle, inout float distance, out vec3 barycentric)
{
    vec3 edge1 = triangle.p1.xyz - triangle.p0.xyz;
    vec3 edge2 = triangle.p2.xyz - triangle.p0.xyz;
    vec3 p = cross(direction, edge2);
    float determinant = dot(edge1, p);
    if (abs(determinant) < 1.0e-8) return false;
    float inv_det = 1.0 / determinant;
    vec3 offset = origin - triangle.p0.xyz;
    float u = dot(offset, p) * inv_det;
    if (u < 0.0 || u > 1.0) return false;
    vec3 q = cross(offset, edge1);
    float v = dot(direction, q) * inv_det;
    if (v < 0.0 || u + v > 1.0) return false;
    float t = dot(edge2, q) * inv_det;
    if (t <= RAY_EPSILON || t >= distance) return false;
    distance = t;
    barycentric = vec3(1.0 - u - v, u, v);
    return true;
}

uint childHitMask(WideNode node, vec3 origin, vec3 inverse_direction, float maximum_distance)
{
    uint mask = 0u;
    int count = clamp(int(node.base_min.w + 0.5), 0, 8);
    for (int child = 0; child < count; ++child) {
        vec3 bmin;
        vec3 bmax;
        decodeChildBounds(node, child, bmin, bmax);
        if (aabbEntry(origin, inverse_direction, bmin, bmax, maximum_distance) < INF) {
            mask |= 1u << uint(child);
        }
    }
    return mask;
}

int nearestChild(WideNode node, uint mask, vec3 origin, vec3 inverse_direction, float maximum_distance)
{
    int best_child = -1;
    float best_entry = INF;
    while (mask != 0u) {
        int child = findLSB(mask);
        mask &= ~(1u << uint(child));
        vec3 bmin;
        vec3 bmax;
        decodeChildBounds(node, child, bmin, bmax);
        float entry = aabbEntry(origin, inverse_direction, bmin, bmax, maximum_distance);
        if (entry < best_entry) {
            best_entry = entry;
            best_child = child;
        }
    }
    return best_child;
}

Hit traceClosest(vec3 origin, vec3 direction, float maximum_distance)
{
    Hit best;
    best.found = false;
    best.distance = maximum_distance;
    best.position = vec3(0.0);
    best.normal = vec3(0.0, 1.0, 0.0);
    best.uv = vec2(0.0);
    best.material = 0u;
    if (uNodeCount <= 0) return best;

    vec3 inverse_direction = safeInverse(direction);
    uvec2 stack[WIDE_STACK_DEPTH];
    int stack_size = 0;
    uint node_index = 0u;
    WideNode node = bvh_nodes[0];
    uint mask = childHitMask(node, origin, inverse_direction, best.distance);

    for (int guard = 0; guard < 4096; ++guard) {
        if (mask == 0u) {
            if (stack_size == 0) break;
            uvec2 frame = stack[--stack_size];
            node_index = frame.x;
            mask = frame.y;
            node = bvh_nodes[node_index];
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
                Triangle triangle = triangles[first + index];
                float distance = best.distance;
                vec3 barycentric;
                if (!hitTriangle(origin, direction, triangle, distance, barycentric)) continue;
                best.found = true;
                best.distance = distance;
                best.position = origin + direction * distance;
                vec3 normal = normalize(
                    triangle.n0.xyz * barycentric.x +
                    triangle.n1.xyz * barycentric.y +
                    triangle.n2.xyz * barycentric.z
                );
                if (dot(normal, direction) > 0.0) normal = -normal;
                best.normal = normal;
                best.uv =
                    triangle.uv01.xy * barycentric.x +
                    triangle.uv01.zw * barycentric.y +
                    triangle.uv2.xy * barycentric.z;
                best.material = uint(max(triangle.uv2.z, 0.0) + 0.5);
            }
        } else if (reference < uint(uNodeCount)) {
            if (stack_size >= WIDE_STACK_DEPTH) continue;
            stack[stack_size++] = uvec2(node_index, mask);
            node_index = reference;
            node = bvh_nodes[node_index];
            mask = childHitMask(node, origin, inverse_direction, best.distance);
        }
    }
    return best;
}

bool traceAny(vec3 origin, vec3 direction, float maximum_distance)
{
    if (uNodeCount <= 0 || maximum_distance <= RAY_EPSILON) return false;
    vec3 inverse_direction = safeInverse(direction);
    uvec2 stack[WIDE_STACK_DEPTH];
    int stack_size = 0;
    uint node_index = 0u;
    WideNode node = bvh_nodes[0];
    uint mask = childHitMask(node, origin, inverse_direction, maximum_distance);

    for (int guard = 0; guard < 2048; ++guard) {
        if (mask == 0u) {
            if (stack_size == 0) break;
            uvec2 frame = stack[--stack_size];
            node_index = frame.x;
            mask = frame.y;
            node = bvh_nodes[node_index];
            continue;
        }
        int child = findLSB(mask);
        mask &= ~(1u << uint(child));
        uint reference = childReference(node, child);
        if ((reference & LEAF_BIT) != 0u) {
            uint first = reference & LEAF_FIRST_MASK;
            uint count = (reference >> 24u) & 0x7fu;
            for (uint index = 0u; index < count; ++index) {
                float distance = maximum_distance;
                vec3 barycentric;
                if (hitTriangle(origin, direction, triangles[first + index], distance, barycentric)) return true;
            }
        } else if (reference < uint(uNodeCount)) {
            if (stack_size >= WIDE_STACK_DEPTH) continue;
            stack[stack_size++] = uvec2(node_index, mask);
            node_index = reference;
            node = bvh_nodes[node_index];
            mask = childHitMask(node, origin, inverse_direction, maximum_distance);
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
    float distance = sqrt(distance_squared);
    vec3 direction = to_light / distance;
    float cosine = max(dot(normal, direction), 0.0);
    if (cosine <= 0.0) return vec3(0.0);
    vec3 shadow_origin = position + normal * RAY_EPSILON * 4.0;
    if (traceAny(shadow_origin, direction, max(distance - RAY_EPSILON * 8.0, 0.0))) return vec3(0.0);
    vec3 incoming = max(uLightColor, vec3(0.0)) * max(uLightIntensity, 0.0) / distance_squared;
    return albedo * incoming * (cosine / PI);
}
)GLSL";

inline constexpr const char *primary_generate = R"GLSL(
layout(local_size_x = 8, local_size_y = 8) in;
layout(std430, binding = 3) writeonly buffer PrimaryRays { RayData primary_rays[]; };
uniform ivec2 uResolution;
uniform vec3 uCameraPosition;
uniform vec3 uCameraForward;
uniform vec3 uCameraRight;
uniform vec3 uCameraUp;
uniform float uTanHalfFov;
uniform float uAspect;

void main()
{
    ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
    if (any(greaterThanEqual(pixel, uResolution))) return;
    uint index = uint(pixel.y * uResolution.x + pixel.x);
    uint state = hashUint(index * 9781u ^ uFrameIndex * 6271u ^ 0x68bc21ebu);
    vec2 jitter = vec2(randomFloat(state), randomFloat(state)) - 0.5;
    vec2 uv = (vec2(pixel) + vec2(0.5) + jitter) / vec2(uResolution);
    vec2 ndc = uv * 2.0 - 1.0;
    vec3 direction = normalize(
        uCameraForward +
        uCameraRight * (ndc.x * uAspect * uTanHalfFov) +
        uCameraUp * (ndc.y * uTanHalfFov)
    );
    RayData ray;
    ray.origin = vec4(uCameraPosition, 0.0);
    ray.direction = vec4(direction, 0.0);
    ray.data = uvec4(index, state, 0u, 0u);
    primary_rays[index] = ray;
}
)GLSL";

inline constexpr const char *primary_intersect = R"GLSL(
layout(local_size_x = 64) in;
layout(std430, binding = 3) readonly buffer PrimaryRays { RayData primary_rays[]; };
layout(std430, binding = 4) writeonly buffer PrimarySurfaces { SurfaceData primary_surfaces[]; };
layout(std430, binding = 5) writeonly buffer HitQueue { uint hit_queue[]; };
layout(std430, binding = 6) buffer QueueCounters { uint hit_count; uint bounce_count; uint qpad0; uint qpad1; };
uniform uint uPixelCount;

void main()
{
    uint index = gl_GlobalInvocationID.x;
    if (index >= uPixelCount) return;
    RayData ray = primary_rays[index];
    Hit hit = traceClosest(ray.origin.xyz, ray.direction.xyz, INF);
    SurfaceData surface;
    surface.position_depth = vec4(0.0, 0.0, 0.0, -1.0);
    surface.normal_material = vec4(0.0, 1.0, 0.0, 0.0);
    surface.uv = vec4(0.0);
    surface.direct = vec4(0.0);
    if (hit.found) {
        surface.position_depth = vec4(hit.position, hit.distance);
        surface.normal_material = vec4(hit.normal, float(hit.material));
        surface.uv = vec4(hit.uv, 0.0, 0.0);
        uint queue_index = atomicAdd(hit_count, 1u);
        if (queue_index < uPixelCount) hit_queue[queue_index] = index;
    }
    primary_surfaces[index] = surface;
}
)GLSL";

inline constexpr const char *prepare_dispatch = R"GLSL(
layout(local_size_x = 1) in;
layout(std430, binding = 3) readonly buffer QueueCounters { uint hit_count; uint bounce_count; uint qpad0; uint qpad1; };
layout(std430, binding = 4) writeonly buffer DispatchCommands { uvec4 hit_dispatch; uvec4 bounce_dispatch; };

void main()
{
    hit_dispatch = uvec4((hit_count + 63u) / 64u, 1u, 1u, 0u);
    bounce_dispatch = uvec4((bounce_count + 63u) / 64u, 1u, 1u, 0u);
}
)GLSL";

inline constexpr const char *primary_shade = R"GLSL(
layout(local_size_x = 64) in;
layout(std430, binding = 3) readonly buffer HitQueue { uint hit_queue[]; };
layout(std430, binding = 4) buffer PrimarySurfaces { SurfaceData primary_surfaces[]; };
layout(std430, binding = 5) writeonly buffer BounceRays { RayData bounce_rays[]; };
layout(std430, binding = 6) buffer QueueCounters { uint hit_count; uint bounce_count; uint qpad0; uint qpad1; };
uniform uint uPixelCount;

void main()
{
    uint queue_index = gl_GlobalInvocationID.x;
    if (queue_index >= hit_count) return;
    uint pixel = hit_queue[queue_index];
    if (pixel >= uPixelCount) return;
    SurfaceData surface = primary_surfaces[pixel];
    uint material = uint(max(surface.normal_material.w, 0.0) + 0.5);
    vec3 albedo = materialAlbedo(material, surface.uv.xy);
    surface.direct = vec4(pointLight(surface.position_depth.xyz, surface.normal_material.xyz, albedo), 1.0);
    primary_surfaces[pixel] = surface;

    uint state = hashUint(pixel * 9781u ^ uFrameIndex * 7919u ^ 0xa511e9b3u);
    RayData bounce;
    bounce.origin = vec4(surface.position_depth.xyz + surface.normal_material.xyz * RAY_EPSILON * 4.0, 0.0);
    bounce.direction = vec4(cosineHemisphere(surface.normal_material.xyz, state), 0.0);
    bounce.data = uvec4(pixel, state, 0u, 0u);
    uint output_index = atomicAdd(bounce_count, 1u);
    if (output_index < uPixelCount) bounce_rays[output_index] = bounce;
}
)GLSL";

inline constexpr const char *bounce_intersect = R"GLSL(
layout(local_size_x = 64) in;
layout(std430, binding = 3) readonly buffer BounceRays { RayData bounce_rays[]; };
layout(std430, binding = 4) writeonly buffer SecondarySurfaces { SurfaceData secondary_surfaces[]; };
layout(std430, binding = 5) readonly buffer QueueCounters { uint hit_count; uint bounce_count; uint qpad0; uint qpad1; };
uniform uint uPixelCount;

void main()
{
    uint index = gl_GlobalInvocationID.x;
    if (index >= bounce_count || index >= uPixelCount) return;
    RayData ray = bounce_rays[index];
    Hit hit = traceClosest(ray.origin.xyz, ray.direction.xyz, INF);
    SurfaceData surface;
    surface.position_depth = vec4(0.0, 0.0, 0.0, -1.0);
    surface.normal_material = vec4(0.0, 1.0, 0.0, 0.0);
    surface.uv = vec4(0.0, 0.0, float(ray.data.x), 0.0);
    surface.direct = vec4(0.0);
    if (hit.found) {
        surface.position_depth = vec4(hit.position, hit.distance);
        surface.normal_material = vec4(hit.normal, float(hit.material));
        surface.uv = vec4(hit.uv, float(ray.data.x), 0.0);
    }
    secondary_surfaces[index] = surface;
}
)GLSL";

inline constexpr const char *bounce_shade = R"GLSL(
layout(local_size_x = 64) in;
layout(std430, binding = 3) readonly buffer SecondarySurfaces { SurfaceData secondary_surfaces[]; };
layout(std430, binding = 4) readonly buffer PrimarySurfaces { SurfaceData primary_surfaces[]; };
layout(std430, binding = 5) buffer InitialReservoirs { ReservoirData initial_reservoirs[]; };
layout(std430, binding = 6) buffer RadianceCache { CacheEntry cache_entries[]; };
layout(std430, binding = 7) readonly buffer QueueCounters { uint hit_count; uint bounce_count; uint qpad0; uint qpad1; };
uniform uint uPixelCount;
uniform uint uCacheSize;

uint cacheKey(vec3 position, vec3 normal)
{
    ivec3 cell = ivec3(floor(position * 0.5));
    uint normal_bits = (normal.x >= 0.0 ? 1u : 0u) | (normal.y >= 0.0 ? 2u : 0u) | (normal.z >= 0.0 ? 4u : 0u);
    uint key = hashUint(uint(cell.x) * 73856093u ^ uint(cell.y) * 19349663u ^ uint(cell.z) * 83492791u ^ normal_bits * 2654435761u);
    return key | 1u;
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
    uvec3 fixed_radiance = uvec3(clamp(radiance, vec3(0.0), vec3(64.0)) * 1024.0 + 0.5);
    for (uint probe = 0u; probe < 4u; ++probe) {
        uint index = (start + probe) % uCacheSize;
        uint existing = atomicCompSwap(cache_entries[index].header.x, 0u, key);
        if (existing == 0u || existing == key) {
            uint count = atomicAdd(cache_entries[index].header.y, 1u);
            if (count < 65535u) {
                atomicAdd(cache_entries[index].radiance.x, fixed_radiance.x);
                atomicAdd(cache_entries[index].radiance.y, fixed_radiance.y);
                atomicAdd(cache_entries[index].radiance.z, fixed_radiance.z);
            }
            return;
        }
    }
}

void main()
{
    uint index = gl_GlobalInvocationID.x;
    if (index >= bounce_count || index >= uPixelCount) return;
    SurfaceData secondary = secondary_surfaces[index];
    uint pixel = uint(max(secondary.uv.z, 0.0) + 0.5);
    if (pixel >= uPixelCount) return;

    ReservoirData reservoir;
    reservoir.sample_position = vec4(0.0);
    reservoir.sample_normal = vec4(0.0);
    reservoir.radiance = vec4(0.0);
    reservoir.weights = vec4(0.0);

    if (secondary.position_depth.w > 0.0) {
        uint secondary_material = uint(max(secondary.normal_material.w, 0.0) + 0.5);
        vec3 secondary_albedo = materialAlbedo(secondary_material, secondary.uv.xy);
        vec3 direct = pointLight(secondary.position_depth.xyz, secondary.normal_material.xyz, secondary_albedo);
        vec3 cached = cacheQuery(secondary.position_depth.xyz, secondary.normal_material.xyz);
        vec3 outgoing = direct + cached * secondary_albedo;
        cacheUpdate(secondary.position_depth.xyz, secondary.normal_material.xyz, direct);

        SurfaceData primary = primary_surfaces[pixel];
        uint primary_material = uint(max(primary.normal_material.w, 0.0) + 0.5);
        vec3 primary_albedo = materialAlbedo(primary_material, primary.uv.xy);
        vec3 contribution = primary_albedo * outgoing;
        float target = max(dot(contribution, vec3(0.2126, 0.7152, 0.0722)), 1.0e-6);

        reservoir.sample_position = vec4(secondary.position_depth.xyz, 1.0);
        reservoir.sample_normal = vec4(secondary.normal_material.xyz, 1.0);
        reservoir.radiance = vec4(contribution, target);
        reservoir.weights = vec4(target, target, 1.0, 0.0);
    }
    initial_reservoirs[pixel] = reservoir;
}
)GLSL";

} // namespace Renderer::WavefrontShaders

#endif
