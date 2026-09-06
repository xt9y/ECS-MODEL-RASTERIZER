#include "Renderer/Gi/Gi.hpp"

#include "Models/Models.hpp"

#include <lwcgl/gl11_compat.h>
#include <lwcgl/glmodern.h>
#include <lwcgl/lwcgl.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <vector>

#ifndef GL_COLOR_ATTACHMENT1
#define GL_COLOR_ATTACHMENT1 0x8CE1
#endif
#ifndef GL_COLOR_ATTACHMENT2
#define GL_COLOR_ATTACHMENT2 0x8CE2
#endif
#ifndef GL_RGBA8
#define GL_RGBA8 0x8058
#endif
#ifndef GL_RG16F
#define GL_RG16F 0x822F
#endif
#ifndef GL_RG
#define GL_RG 0x8227
#endif
#ifndef GL_DEPTH_COMPONENT32F
#define GL_DEPTH_COMPONENT32F 0x8CAC
#endif
#ifndef GL_DEPTH_COMPONENT
#define GL_DEPTH_COMPONENT 0x1902
#endif
#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif

namespace Renderer {
namespace {

using Mat4 = std::array<float, 16>;

struct Vec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

constexpr float kPi = 3.14159265358979323846f;
constexpr std::uint32_t kLeafBit = 0x80000000u;
constexpr std::uint32_t kLeafSize = 4u;

Vec3 minVec(const Vec3& a, const Vec3& b)
{
    return {std::min(a.x, b.x), std::min(a.y, b.y), std::min(a.z, b.z)};
}

Vec3 maxVec(const Vec3& a, const Vec3& b)
{
    return {std::max(a.x, b.x), std::max(a.y, b.y), std::max(a.z, b.z)};
}

Vec3 normalizeVec(const Vec3& value)
{
    const float length_squared = value.x * value.x + value.y * value.y + value.z * value.z;
    if (length_squared <= 1.0e-20f) return {0.0f, 1.0f, 0.0f};
    const float inverse_length = 1.0f / std::sqrt(length_squared);
    return {value.x * inverse_length, value.y * inverse_length, value.z * inverse_length};
}

float component(const Vec3& value, int axis)
{
    if (axis == 0) return value.x;
    if (axis == 1) return value.y;
    return value.z;
}

Mat4 identityMatrix()
{
    return {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f,
    };
}

Mat4 multiply(const Mat4& a, const Mat4& b)
{
    Mat4 result{};
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            float value = 0.0f;
            for (int k = 0; k < 4; ++k) value += a[k * 4 + row] * b[column * 4 + k];
            result[column * 4 + row] = value;
        }
    }
    return result;
}

Mat4 translation(float x, float y, float z)
{
    Mat4 result = identityMatrix();
    result[12] = x;
    result[13] = y;
    result[14] = z;
    return result;
}

Mat4 scaling(float x, float y, float z)
{
    Mat4 result{};
    result[0] = x;
    result[5] = y;
    result[10] = z;
    result[15] = 1.0f;
    return result;
}

Mat4 rotationX(float degrees)
{
    const float radians = degrees * (kPi / 180.0f);
    const float c = std::cos(radians);
    const float s = std::sin(radians);
    Mat4 result = identityMatrix();
    result[5] = c;
    result[6] = s;
    result[9] = -s;
    result[10] = c;
    return result;
}

Mat4 rotationY(float degrees)
{
    const float radians = degrees * (kPi / 180.0f);
    const float c = std::cos(radians);
    const float s = std::sin(radians);
    Mat4 result = identityMatrix();
    result[0] = c;
    result[2] = -s;
    result[8] = s;
    result[10] = c;
    return result;
}

Mat4 rotationZ(float degrees)
{
    const float radians = degrees * (kPi / 180.0f);
    const float c = std::cos(radians);
    const float s = std::sin(radians);
    Mat4 result = identityMatrix();
    result[0] = c;
    result[1] = s;
    result[4] = -s;
    result[5] = c;
    return result;
}

Mat4 modelMatrix(const Ecs::TransformComponent& transform)
{
    return multiply(
        multiply(
            multiply(
                multiply(
                    translation(transform.position.x, transform.position.y, transform.position.z),
                    rotationX(transform.rotation.x)
                ),
                rotationY(transform.rotation.y)
            ),
            rotationZ(transform.rotation.z)
        ),
        scaling(transform.scale.x, transform.scale.y, transform.scale.z)
    );
}

Mat4 inverseModelMatrix(const Ecs::TransformComponent& transform)
{
    const float sx = std::abs(transform.scale.x) > 1.0e-8f ? 1.0f / transform.scale.x : 0.0f;
    const float sy = std::abs(transform.scale.y) > 1.0e-8f ? 1.0f / transform.scale.y : 0.0f;
    const float sz = std::abs(transform.scale.z) > 1.0e-8f ? 1.0f / transform.scale.z : 0.0f;

    return multiply(
        multiply(
            multiply(
                multiply(
                    scaling(sx, sy, sz),
                    rotationZ(-transform.rotation.z)
                ),
                rotationY(-transform.rotation.y)
            ),
            rotationX(-transform.rotation.x)
        ),
        translation(-transform.position.x, -transform.position.y, -transform.position.z)
    );
}

Vec3 transformPoint(const Mat4& matrix, const Models::Vec3& value)
{
    return {
        matrix[0] * value.x + matrix[4] * value.y + matrix[8] * value.z + matrix[12],
        matrix[1] * value.x + matrix[5] * value.y + matrix[9] * value.z + matrix[13],
        matrix[2] * value.x + matrix[6] * value.y + matrix[10] * value.z + matrix[14],
    };
}

Vec3 transformNormal(const Mat4& inverse_model, const Models::Vec3& value)
{
    return normalizeVec({
        inverse_model[0] * value.x + inverse_model[1] * value.y + inverse_model[2] * value.z,
        inverse_model[4] * value.x + inverse_model[5] * value.y + inverse_model[6] * value.z,
        inverse_model[8] * value.x + inverse_model[9] * value.y + inverse_model[10] * value.z,
    });
}

bool inverseMatrix(const Mat4& input, Mat4& output)
{
    double augmented[4][8]{};

    for (int row = 0; row < 4; ++row) {
        for (int column = 0; column < 4; ++column) {
            augmented[row][column] = static_cast<double>(input[column * 4 + row]);
        }
        augmented[row][row + 4] = 1.0;
    }

    for (int column = 0; column < 4; ++column) {
        int pivot = column;
        for (int row = column + 1; row < 4; ++row) {
            if (std::abs(augmented[row][column]) > std::abs(augmented[pivot][column])) pivot = row;
        }
        if (std::abs(augmented[pivot][column]) < 1.0e-12) return false;

        if (pivot != column) {
            for (int k = 0; k < 8; ++k) std::swap(augmented[pivot][k], augmented[column][k]);
        }

        const double divisor = augmented[column][column];
        for (int k = 0; k < 8; ++k) augmented[column][k] /= divisor;

        for (int row = 0; row < 4; ++row) {
            if (row == column) continue;
            const double factor = augmented[row][column];
            for (int k = 0; k < 8; ++k) augmented[row][k] -= factor * augmented[column][k];
        }
    }

    for (int row = 0; row < 4; ++row) {
        for (int column = 0; column < 4; ++column) {
            output[column * 4 + row] = static_cast<float>(augmented[row][column + 4]);
        }
    }
    return true;
}

GLuint createTexture2D(
    int width,
    int height,
    GLint internal_format,
    GLenum format,
    GLenum type,
    bool linear)
{
    const GLuint texture = lwcgl_glGenTexture();
    if (texture == 0u) return 0u;

    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, linear ? GL_LINEAR : GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, linear ? GL_LINEAR : GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(
        GL_TEXTURE_2D,
        0,
        internal_format,
        width,
        height,
        0,
        format,
        type,
        nullptr
    );
    return texture;
}

void deleteTexture(GLuint& texture)
{
    if (texture != 0u) lwcgl_glDeleteTexture(texture);
    texture = 0u;
}

void bindTextureUnit(int unit, GLuint texture)
{
    GLModern.glActiveTexture(static_cast<GLenum>(GL_TEXTURE0 + unit));
    glBindTexture(GL_TEXTURE_2D, texture);
}

GLuint compileShader(GLenum stage, const char *source)
{
    const GLuint shader = GL20.glCreateShader(stage);
    if (shader == 0u) return 0u;

    GL20.glShaderSource(shader, 1, &source, nullptr);
    GL20.glCompileShader(shader);

    GLint status = 0;
    GL20.glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
    if (status == GL_TRUE) return shader;

    GLint length = 0;
    GL20.glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);
    std::vector<char> log(static_cast<std::size_t>(std::max(length, 1)), '\0');
    GL20.glGetShaderInfoLog(shader, length, nullptr, log.data());
    std::fprintf(stderr, "[GI]: shader compile failed: %s\n", log.data());
    GL20.glDeleteShader(shader);
    return 0u;
}

GLuint linkProgram(const std::vector<GLuint>& shaders)
{
    const GLuint program = GL20.glCreateProgram();
    if (program == 0u) return 0u;

    for (const GLuint shader : shaders) GL20.glAttachShader(program, shader);
    GL20.glLinkProgram(program);

    GLint status = 0;
    GL20.glGetProgramiv(program, GL_LINK_STATUS, &status);
    if (status == GL_TRUE) {
        for (const GLuint shader : shaders) GL20.glDetachShader(program, shader);
        return program;
    }

    GLint length = 0;
    GL20.glGetProgramiv(program, GL_INFO_LOG_LENGTH, &length);
    std::vector<char> log(static_cast<std::size_t>(std::max(length, 1)), '\0');
    GL20.glGetProgramInfoLog(program, length, nullptr, log.data());
    std::fprintf(stderr, "[GI]: program link failed: %s\n", log.data());
    GL20.glDeleteProgram(program);
    return 0u;
}

GLuint createGraphicsProgram(const char *vertex_source, const char *fragment_source)
{
    const GLuint vertex = compileShader(GL_VERTEX_SHADER, vertex_source);
    if (vertex == 0u) return 0u;

    const GLuint fragment = compileShader(GL_FRAGMENT_SHADER, fragment_source);
    if (fragment == 0u) {
        GL20.glDeleteShader(vertex);
        return 0u;
    }

    const GLuint program = linkProgram({vertex, fragment});
    GL20.glDeleteShader(vertex);
    GL20.glDeleteShader(fragment);
    return program;
}

GLuint createComputeProgram(const char *source)
{
    const GLuint shader = compileShader(GL_COMPUTE_SHADER, source);
    if (shader == 0u) return 0u;

    const GLuint program = linkProgram({shader});
    GL20.glDeleteShader(shader);
    return program;
}

void setInt(GLuint program, const char *name, int value)
{
    const GLint location = GL20.glGetUniformLocation(program, name);
    if (location >= 0) GL20.glUniform1i(location, value);
}

void setFloat(GLuint program, const char *name, float value)
{
    const GLint location = GL20.glGetUniformLocation(program, name);
    if (location >= 0) GL20.glUniform1f(location, value);
}

void setSize(GLuint program, const char *name, int width, int height)
{
    const GLint location = GL20.glGetUniformLocation(program, name);
    if (location >= 0) {
        GL20.glUniform2f(location, static_cast<float>(width), static_cast<float>(height));
    }
}

void setMatrix(GLuint program, const char *name, const Mat4& value)
{
    const GLint location = GL20.glGetUniformLocation(program, name);
    if (location >= 0) GL20.glUniformMatrix4fv(location, 1, GL_FALSE, value.data());
}

const char *kGBufferVertexShader = R"GLSL(
#version 430 compatibility
uniform mat4 uInverseView;
uniform mat4 uPreviousViewProjection;
out vec3 vWorldNormal;
out vec2 vUv;
out vec4 vColor;
out vec4 vCurrentClip;
out vec4 vPreviousClip;
void main() {
    vec4 viewPosition = gl_ModelViewMatrix * gl_Vertex;
    vec4 worldPosition = uInverseView * viewPosition;
    vec3 viewNormal = normalize(gl_NormalMatrix * gl_Normal);
    vWorldNormal = normalize(mat3(uInverseView) * viewNormal);
    vUv = gl_MultiTexCoord0.xy;
    vColor = gl_Color;
    vCurrentClip = gl_ModelViewProjectionMatrix * gl_Vertex;
    vPreviousClip = uPreviousViewProjection * worldPosition;
    gl_Position = vCurrentClip;
}
)GLSL";

const char *kGBufferFragmentShader = R"GLSL(
#version 430 compatibility
uniform sampler2D uDiffuse;
uniform int uHasTexture;
in vec3 vWorldNormal;
in vec2 vUv;
in vec4 vColor;
in vec4 vCurrentClip;
in vec4 vPreviousClip;
layout(location = 0) out vec4 outAlbedo;
layout(location = 1) out vec4 outNormal;
layout(location = 2) out vec2 outVelocity;
void main() {
    vec4 base = vColor;
    if (uHasTexture != 0) base *= texture(uDiffuse, vUv);
    if (base.a < 0.5) discard;

    vec3 normal = normalize(vWorldNormal);
    vec2 currentNdc = vCurrentClip.xy / max(abs(vCurrentClip.w), 1e-6);
    vec2 previousNdc = vPreviousClip.xy / max(abs(vPreviousClip.w), 1e-6);

    outAlbedo = base;
    outNormal = vec4(normal * 0.5 + 0.5, 1.0);
    outVelocity = (currentNdc - previousNdc) * 0.5;
}
)GLSL";

const char *kTraceShader = R"GLSL(
#version 430
layout(local_size_x = 8, local_size_y = 8) in;

struct BvhNode {
    vec3 bmin;
    uint left;
    vec3 bmax;
    uint meta;
};

struct Triangle {
    vec4 p0;
    vec4 p1;
    vec4 p2;
    vec4 n0;
    vec4 n1;
    vec4 n2;
    vec4 color;
};

layout(std430, binding = 0) readonly buffer Nodes {
    BvhNode nodes[];
};

layout(std430, binding = 1) readonly buffer Triangles {
    Triangle triangles[];
};

uniform sampler2D uAlbedo;
uniform sampler2D uNormal;
uniform sampler2D uDepth;
uniform mat4 uViewProjection;
uniform mat4 uInverseViewProjection;
uniform vec2 uOutputSize;
uniform int uFrame;
uniform int uNodeCount;
uniform int uUseScreen;
uniform int uUseBvh;
uniform int uRaysPerPixel;
uniform int uMaxBounces;

layout(rgba16f, binding = 0) writeonly uniform image2D uRaw;

const uint LEAF_BIT = 0x80000000u;

uint hashUint(uint value) {
    value ^= value >> 16;
    value *= 0x7feb352du;
    value ^= value >> 15;
    value *= 0x846ca68bu;
    value ^= value >> 16;
    return value;
}

float randomFloat(inout uint state) {
    state = hashUint(state);
    return float(state) * (1.0 / 4294967296.0);
}

vec3 cosineHemisphere(vec3 normal, inout uint state) {
    float r1 = randomFloat(state);
    float r2 = randomFloat(state);
    float phi = 6.28318530718 * r1;
    float radius = sqrt(r2);
    vec3 local = vec3(
        radius * cos(phi),
        radius * sin(phi),
        sqrt(max(0.0, 1.0 - r2))
    );

    vec3 helper = abs(normal.z) < 0.999
        ? vec3(0.0, 0.0, 1.0)
        : vec3(1.0, 0.0, 0.0);
    vec3 tangent = normalize(cross(helper, normal));
    vec3 bitangent = cross(normal, tangent);
    return normalize(tangent * local.x + bitangent * local.y + normal * local.z);
}

vec3 inverseDirection(vec3 direction) {
    return vec3(
        abs(direction.x) > 1e-8 ? 1.0 / direction.x : 1e30,
        abs(direction.y) > 1e-8 ? 1.0 / direction.y : 1e30,
        abs(direction.z) > 1e-8 ? 1.0 / direction.z : 1e30
    );
}

bool hitAabb(
    vec3 origin,
    vec3 inverse_direction,
    vec3 bmin,
    vec3 bmax,
    float maximum_distance)
{
    vec3 t0 = (bmin - origin) * inverse_direction;
    vec3 t1 = (bmax - origin) * inverse_direction;
    vec3 near_values = min(t0, t1);
    vec3 far_values = max(t0, t1);
    float near_distance = max(max(near_values.x, near_values.y), max(near_values.z, 0.0));
    float far_distance = min(min(far_values.x, far_values.y), far_values.z);
    return far_distance >= near_distance && near_distance < maximum_distance;
}

bool hitTriangle(
    vec3 origin,
    vec3 direction,
    Triangle triangle,
    inout float distance,
    out vec3 normal,
    out vec3 color)
{
    vec3 edge1 = triangle.p1.xyz - triangle.p0.xyz;
    vec3 edge2 = triangle.p2.xyz - triangle.p0.xyz;
    vec3 p = cross(direction, edge2);
    float determinant = dot(edge1, p);
    if (abs(determinant) < 1e-8) return false;

    float inverse_determinant = 1.0 / determinant;
    vec3 s = origin - triangle.p0.xyz;
    float u = dot(s, p) * inverse_determinant;
    if (u < 0.0 || u > 1.0) return false;

    vec3 q = cross(s, edge1);
    float v = dot(direction, q) * inverse_determinant;
    if (v < 0.0 || u + v > 1.0) return false;

    float t = dot(edge2, q) * inverse_determinant;
    if (t <= 0.002 || t >= distance) return false;

    distance = t;
    float w = 1.0 - u - v;
    normal = normalize(
        triangle.n0.xyz * w +
        triangle.n1.xyz * u +
        triangle.n2.xyz * v
    );
    if (dot(normal, direction) > 0.0) normal = -normal;
    color = triangle.color.rgb;
    return true;
}

bool traceScene(
    vec3 origin,
    vec3 direction,
    float maximum_distance,
    out float distance,
    out vec3 normal,
    out vec3 color)
{
    distance = maximum_distance;
    normal = vec3(0.0, 1.0, 0.0);
    color = vec3(1.0);

    if (uUseBvh == 0 || uNodeCount <= 0) return false;

    vec3 inverse_direction = inverseDirection(direction);
    uint stack[96];
    int stack_size = 0;
    stack[stack_size++] = 0u;
    bool found = false;

    while (stack_size > 0) {
        uint node_index = stack[--stack_size];
        if (node_index >= uint(uNodeCount)) continue;

        BvhNode node = nodes[node_index];
        if (!hitAabb(origin, inverse_direction, node.bmin, node.bmax, distance)) continue;

        if ((node.meta & LEAF_BIT) != 0u) {
            uint count = node.meta & ~LEAF_BIT;
            for (uint i = 0u; i < count; ++i) {
                vec3 candidate_normal;
                vec3 candidate_color;
                if (hitTriangle(
                    origin,
                    direction,
                    triangles[node.left + i],
                    distance,
                    candidate_normal,
                    candidate_color))
                {
                    normal = candidate_normal;
                    color = candidate_color;
                    found = true;
                }
            }
        } else if (stack_size <= 93) {
            stack[stack_size++] = node.meta;
            stack[stack_size++] = node.left;
        }
    }

    return found;
}

bool occluded(vec3 origin, vec3 direction) {
    float distance;
    vec3 normal;
    vec3 color;
    return traceScene(origin, direction, 1e20, distance, normal, color);
}

vec3 reconstructWorld(vec2 uv, float depth) {
    vec4 clip = vec4(uv * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
    vec4 world = uInverseViewProjection * clip;
    return world.xyz / max(abs(world.w), 1e-8);
}

bool screenTrace(
    vec3 origin,
    vec3 direction,
    out vec2 hit_uv,
    out float hit_distance)
{
    for (int step = 0; step < 24; ++step) {
        float t = 0.10 + float(step * step) * 0.035;
        vec4 clip = uViewProjection * vec4(origin + direction * t, 1.0);
        if (clip.w <= 0.0) continue;

        vec3 ndc = clip.xyz / clip.w;
        vec2 uv = ndc.xy * 0.5 + 0.5;
        if (any(lessThan(uv, vec2(0.001))) || any(greaterThan(uv, vec2(0.999)))) {
            return false;
        }

        float scene_depth = texture(uDepth, uv).r;
        float ray_depth = ndc.z * 0.5 + 0.5;
        float difference = ray_depth - scene_depth;
        if (scene_depth < 0.999999 && difference > 0.0004 && difference < 0.02) {
            hit_uv = uv;
            hit_distance = t;
            return true;
        }
    }
    return false;
}

vec3 skyRadiance(vec3 direction) {
    float elevation = clamp(direction.y * 0.5 + 0.5, 0.0, 1.0);
    vec3 horizon = vec3(0.012, 0.016, 0.025);
    vec3 zenith = vec3(0.055, 0.070, 0.100);
    return mix(horizon, zenith, elevation);
}

float directVisibility(vec3 position, vec3 normal, vec3 light_direction) {
    float ndl = max(dot(normal, light_direction), 0.0);
    if (ndl <= 0.0) return 0.0;
    if (uUseBvh == 0 || uNodeCount <= 0) return ndl;
    bool blocked = occluded(position + normal * 0.025, light_direction);
    return blocked ? 0.0 : ndl;
}

void main() {
    ivec2 output_size = ivec2(uOutputSize);
    ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
    if (any(greaterThanEqual(pixel, output_size))) return;

    vec2 uv = (vec2(pixel) + 0.5) / uOutputSize;
    float depth = texture(uDepth, uv).r;
    if (depth >= 0.999999) {
        imageStore(uRaw, pixel, vec4(0.0));
        return;
    }

    vec3 position = reconstructWorld(uv, depth);
    vec3 normal = normalize(texture(uNormal, uv).xyz * 2.0 - 1.0);
    vec3 light_direction = normalize(vec3(-0.35, 0.8, 0.45));

    float primary_direct = directVisibility(position, normal, light_direction);
    vec3 direct_irradiance = vec3(primary_direct * 1.15);

    int ray_count = clamp(uRaysPerPixel, 1, 4);
    int bounce_count = clamp(uMaxBounces, 1, 4);
    vec3 indirect = vec3(0.0);

    for (int sample_index = 0; sample_index < ray_count; ++sample_index) {
        uint state =
            uint(pixel.x) * 1973u +
            uint(pixel.y) * 9277u +
            uint(max(uFrame, 0)) * 26699u +
            uint(sample_index) * 31847u +
            1u;

        vec3 ray_origin = position + normal * 0.025;
        vec3 ray_direction = cosineHemisphere(normal, state);
        vec3 throughput = vec3(1.0);
        vec3 radiance = vec3(0.0);

        for (int bounce = 0; bounce < bounce_count; ++bounce) {
            if (bounce == 0 && uUseScreen != 0) {
                vec2 hit_uv;
                float screen_distance;
                if (screenTrace(ray_origin, ray_direction, hit_uv, screen_distance)) {
                    float hit_depth = texture(uDepth, hit_uv).r;
                    vec3 hit_position = reconstructWorld(hit_uv, hit_depth);
                    vec3 hit_normal = normalize(texture(uNormal, hit_uv).xyz * 2.0 - 1.0);
                    vec3 hit_color = texture(uAlbedo, hit_uv).rgb;
                    float hit_direct = directVisibility(hit_position, hit_normal, light_direction);
                    radiance += throughput * hit_color * (hit_direct * 1.15);

                    throughput *= hit_color * 0.62;
                    ray_origin = hit_position + hit_normal * 0.025;
                    ray_direction = cosineHemisphere(hit_normal, state);
                    continue;
                }
            }

            float hit_distance;
            vec3 hit_normal;
            vec3 hit_color;
            if (!traceScene(
                ray_origin,
                ray_direction,
                1e20,
                hit_distance,
                hit_normal,
                hit_color))
            {
                radiance += throughput * skyRadiance(ray_direction);
                break;
            }

            vec3 hit_position = ray_origin + ray_direction * hit_distance;
            float hit_direct = directVisibility(hit_position, hit_normal, light_direction);
            radiance += throughput * hit_color * (hit_direct * 1.15);

            throughput *= hit_color * 0.62;
            float maximum_throughput = max(max(throughput.r, throughput.g), throughput.b);
            if (maximum_throughput < 0.015) break;

            ray_origin = hit_position + hit_normal * 0.025;
            ray_direction = cosineHemisphere(hit_normal, state);
        }

        indirect += radiance;
    }

    indirect /= float(ray_count);
    imageStore(uRaw, pixel, vec4(max(direct_irradiance + indirect, vec3(0.0)), 1.0));
}
)GLSL";

const char *kTemporalShader = R"GLSL(
#version 430
layout(local_size_x = 8, local_size_y = 8) in;

uniform sampler2D uRaw;
uniform sampler2D uPreviousHistory;
uniform sampler2D uPreviousMoments;
uniform sampler2D uPreviousGeometry;
uniform sampler2D uDepth;
uniform sampler2D uNormal;
uniform sampler2D uVelocity;
uniform vec2 uOutputSize;
uniform float uAlpha;
uniform float uDepthReject;
uniform float uNormalReject;
uniform int uHasHistory;

layout(rgba16f, binding = 0) writeonly uniform image2D uHistory;
layout(rg16f, binding = 1) writeonly uniform image2D uMoments;
layout(rgba16f, binding = 2) writeonly uniform image2D uGeometry;

void main() {
    ivec2 output_size = ivec2(uOutputSize);
    ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
    if (any(greaterThanEqual(pixel, output_size))) return;

    vec2 uv = (vec2(pixel) + 0.5) / uOutputSize;
    vec3 current = texture(uRaw, uv).rgb;
    float depth = texture(uDepth, uv).r;
    vec3 normal = normalize(texture(uNormal, uv).xyz * 2.0 - 1.0);
    vec2 previous_uv = uv - texture(uVelocity, uv).xy;

    bool valid =
        uHasHistory != 0 &&
        all(greaterThanEqual(previous_uv, vec2(0.0))) &&
        all(lessThanEqual(previous_uv, vec2(1.0)));

    vec3 result = current;
    float luminance = dot(current, vec3(0.2126, 0.7152, 0.0722));
    vec2 moments = vec2(luminance, luminance * luminance);

    if (valid) {
        vec4 old_geometry = texture(uPreviousGeometry, previous_uv);
        vec3 old_normal = normalize(old_geometry.xyz);
        valid =
            abs(old_geometry.w - depth) <= uDepthReject &&
            dot(old_normal, normal) >= uNormalReject;

        if (valid) {
            float alpha = clamp(uAlpha, 0.02, 1.0);
            result = mix(texture(uPreviousHistory, previous_uv).rgb, current, alpha);
            moments = mix(texture(uPreviousMoments, previous_uv).rg, moments, alpha);
        }
    }

    imageStore(uHistory, pixel, vec4(result, 1.0));
    imageStore(uMoments, pixel, vec4(moments, 0.0, 0.0));
    imageStore(uGeometry, pixel, vec4(normal, depth));
}
)GLSL";

const char *kDenoiseShader = R"GLSL(
#version 430
layout(local_size_x = 8, local_size_y = 8) in;

uniform sampler2D uInput;
uniform sampler2D uDepth;
uniform sampler2D uNormal;
uniform sampler2D uMoments;
uniform vec2 uOutputSize;
uniform int uStep;
uniform int uUseMoments;

layout(rgba16f, binding = 0) writeonly uniform image2D uOutput;

float luminance(vec3 color) {
    return dot(color, vec3(0.2126, 0.7152, 0.0722));
}

void main() {
    ivec2 output_size = ivec2(uOutputSize);
    ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
    if (any(greaterThanEqual(pixel, output_size))) return;

    vec2 uv = (vec2(pixel) + 0.5) / uOutputSize;
    vec3 center = texture(uInput, uv).rgb;
    float center_depth = texture(uDepth, uv).r;
    vec3 center_normal = normalize(texture(uNormal, uv).xyz * 2.0 - 1.0);
    vec2 center_moments = texture(uMoments, uv).rg;
    float variance = uUseMoments != 0
        ? max(center_moments.y - center_moments.x * center_moments.x, 1e-4)
        : 0.02;

    const float kernel[5] = float[](1.0, 4.0, 6.0, 4.0, 1.0);
    vec3 sum = vec3(0.0);
    float weight_sum = 0.0;

    for (int y = -2; y <= 2; ++y) {
        for (int x = -2; x <= 2; ++x) {
            ivec2 sample_pixel = clamp(
                pixel + ivec2(x, y) * uStep,
                ivec2(0),
                output_size - ivec2(1)
            );
            vec2 sample_uv = (vec2(sample_pixel) + 0.5) / uOutputSize;
            vec3 sample_color = texture(uInput, sample_uv).rgb;
            float sample_depth = texture(uDepth, sample_uv).r;
            vec3 sample_normal = normalize(texture(uNormal, sample_uv).xyz * 2.0 - 1.0);

            float normal_weight = pow(max(dot(center_normal, sample_normal), 0.0), 32.0);
            float depth_weight = exp(
                -abs(sample_depth - center_depth) * 180.0 / float(max(uStep, 1))
            );
            float color_weight = exp(
                -abs(luminance(sample_color) - luminance(center)) /
                (sqrt(variance) * 4.0 + 0.03)
            );
            float weight =
                kernel[x + 2] * kernel[y + 2] *
                normal_weight * depth_weight * color_weight;

            sum += sample_color * weight;
            weight_sum += weight;
        }
    }

    imageStore(uOutput, pixel, vec4(sum / max(weight_sum, 1e-6), 1.0));
}
)GLSL";

const char *kComposeVertexShader = R"GLSL(
#version 430 compatibility
out vec2 vUv;
void main() {
    gl_Position = vec4(gl_Vertex.xy, 0.0, 1.0);
    vUv = gl_Vertex.xy * 0.5 + 0.5;
}
)GLSL";

const char *kComposeFragmentShader = R"GLSL(
#version 430 compatibility
uniform sampler2D uAlbedo;
uniform sampler2D uDepth;
uniform sampler2D uLighting;
in vec2 vUv;
layout(location = 0) out vec4 outColor;
void main() {
    float depth = texture(uDepth, vUv).r;
    if (depth >= 0.999999) {
        outColor = vec4(0.035, 0.035, 0.045, 1.0);
        return;
    }

    vec4 albedo = texture(uAlbedo, vUv);
    vec3 incident_lighting = max(texture(uLighting, vUv).rgb, vec3(0.0));
    vec3 color = albedo.rgb * incident_lighting;
    color = color / (vec3(1.0) + color);
    outColor = vec4(color, albedo.a);
}
)GLSL";

} // namespace

struct GI::Impl {
    struct alignas(16) BvhNode {
        float min_x = 0.0f;
        float min_y = 0.0f;
        float min_z = 0.0f;
        std::uint32_t left = 0u;
        float max_x = 0.0f;
        float max_y = 0.0f;
        float max_z = 0.0f;
        std::uint32_t meta = 0u;
    };

    struct alignas(16) GpuTriangle {
        std::array<float, 4> p0{};
        std::array<float, 4> p1{};
        std::array<float, 4> p2{};
        std::array<float, 4> n0{};
        std::array<float, 4> n1{};
        std::array<float, 4> n2{};
        std::array<float, 4> color{};
    };

    GiSettings settings{};

    bool initialized = false;
    bool geometry_ready = false;
    int width = 1;
    int height = 1;
    int gi_width = 1;
    int gi_height = 1;
    int history_index = 0;
    std::uint64_t frame = 0u;
    std::uint64_t geometry_hash = 0u;

    Mat4 current_view_projection = identityMatrix();
    Mat4 previous_view_projection = identityMatrix();
    Mat4 inverse_view_projection = identityMatrix();
    Mat4 inverse_view = identityMatrix();

    GLuint gbuffer_framebuffer = 0u;
    GLuint gbuffer_albedo = 0u;
    GLuint gbuffer_normal = 0u;
    GLuint gbuffer_velocity = 0u;
    GLuint gbuffer_depth = 0u;

    GLuint raw = 0u;
    std::array<GLuint, 2> history{};
    std::array<GLuint, 2> moments{};
    std::array<GLuint, 2> geometry{};
    std::array<GLuint, 2> denoise{};

    GLuint node_buffer = 0u;
    GLuint triangle_buffer = 0u;

    GLuint gbuffer_program = 0u;
    GLuint trace_program = 0u;
    GLuint temporal_program = 0u;
    GLuint denoise_program = 0u;
    GLuint compose_program = 0u;

    std::vector<BvhNode> nodes;
    std::vector<GpuTriangle> triangles;

    bool active() const
    {
        return initialized && settings.enabled;
    }

    void updateResolution()
    {
        const int divisor = std::max(settings.resolution_divisor, 1);
        gi_width = std::max(width / divisor, 1);
        gi_height = std::max(height / divisor, 1);
    }

    static void hashValue(std::uint64_t& hash, std::uint32_t value)
    {
        hash ^= static_cast<std::uint64_t>(value);
        hash *= 1099511628211ull;
    }

    static void hashFloat(std::uint64_t& hash, float value)
    {
        hashValue(hash, std::bit_cast<std::uint32_t>(value));
    }

    std::uint64_t geometryHash(const Ecs::World& world) const
    {
        std::uint64_t hash = 1469598103934665603ull;

        for (const Ecs::Entity entity : world.entities()) {
            const Ecs::RenderableComponent *renderable = world.getRenderable(entity);
            const Ecs::MeshComponent *mesh = world.getMesh(entity);
            const Ecs::TransformComponent *transform = world.getTransform(entity);
            if (!renderable || !renderable->visible || !mesh || !transform) continue;

            hashValue(hash, entity);
            hashValue(hash, mesh->mesh);
            hashValue(hash, mesh->material);
            hashFloat(hash, transform->position.x);
            hashFloat(hash, transform->position.y);
            hashFloat(hash, transform->position.z);
            hashFloat(hash, transform->rotation.x);
            hashFloat(hash, transform->rotation.y);
            hashFloat(hash, transform->rotation.z);
            hashFloat(hash, transform->scale.x);
            hashFloat(hash, transform->scale.y);
            hashFloat(hash, transform->scale.z);
        }

        return hash;
    }

    Vec3 materialColor(std::uint32_t handle) const
    {
        const Models::MaterialData *material = Models::material(handle);
        if (!material) return {1.0f, 1.0f, 1.0f};

        Vec3 result{material->color.x, material->color.y, material->color.z};
        if (material->diffuse_texture == Models::INVALID_TEXTURE) return result;

        const Models::TextureAsset *texture = Models::texture(material->diffuse_texture);
        if (!texture || texture->image.rgba.size() < 4u) return result;

        const std::size_t pixel_count = texture->image.rgba.size() / 4u;
        const std::size_t stride = std::max<std::size_t>(pixel_count / 4096u, 1u);
        std::uint64_t red = 0u;
        std::uint64_t green = 0u;
        std::uint64_t blue = 0u;
        std::size_t samples = 0u;

        for (std::size_t pixel = 0u; pixel < pixel_count; pixel += stride) {
            const std::size_t offset = pixel * 4u;
            red += texture->image.rgba[offset + 0u];
            green += texture->image.rgba[offset + 1u];
            blue += texture->image.rgba[offset + 2u];
            ++samples;
        }

        if (samples == 0u) return result;
        const float inverse = 1.0f / (255.0f * static_cast<float>(samples));
        result.x *= static_cast<float>(red) * inverse;
        result.y *= static_cast<float>(green) * inverse;
        result.z *= static_cast<float>(blue) * inverse;
        return result;
    }

    static Vec3 triangleCentroid(const GpuTriangle& triangle)
    {
        return {
            (triangle.p0[0] + triangle.p1[0] + triangle.p2[0]) / 3.0f,
            (triangle.p0[1] + triangle.p1[1] + triangle.p2[1]) / 3.0f,
            (triangle.p0[2] + triangle.p1[2] + triangle.p2[2]) / 3.0f,
        };
    }

    std::uint32_t buildNode(std::uint32_t start, std::uint32_t count)
    {
        const float infinity = std::numeric_limits<float>::infinity();
        Vec3 bounds_min{infinity, infinity, infinity};
        Vec3 bounds_max{-infinity, -infinity, -infinity};
        Vec3 centroid_min{infinity, infinity, infinity};
        Vec3 centroid_max{-infinity, -infinity, -infinity};

        for (std::uint32_t i = 0u; i < count; ++i) {
            const GpuTriangle& triangle = triangles[start + i];
            const Vec3 p0{triangle.p0[0], triangle.p0[1], triangle.p0[2]};
            const Vec3 p1{triangle.p1[0], triangle.p1[1], triangle.p1[2]};
            const Vec3 p2{triangle.p2[0], triangle.p2[1], triangle.p2[2]};
            bounds_min = minVec(bounds_min, minVec(p0, minVec(p1, p2)));
            bounds_max = maxVec(bounds_max, maxVec(p0, maxVec(p1, p2)));
            const Vec3 centroid = triangleCentroid(triangle);
            centroid_min = minVec(centroid_min, centroid);
            centroid_max = maxVec(centroid_max, centroid);
        }

        BvhNode node{};
        node.min_x = bounds_min.x;
        node.min_y = bounds_min.y;
        node.min_z = bounds_min.z;
        node.max_x = bounds_max.x;
        node.max_y = bounds_max.y;
        node.max_z = bounds_max.z;

        const std::uint32_t node_index = static_cast<std::uint32_t>(nodes.size());
        nodes.push_back(node);

        const Vec3 centroid_extent{
            centroid_max.x - centroid_min.x,
            centroid_max.y - centroid_min.y,
            centroid_max.z - centroid_min.z,
        };

        int axis = 0;
        if (centroid_extent.y > centroid_extent.x) axis = 1;
        if (component(centroid_extent, 2) > component(centroid_extent, axis)) axis = 2;

        if (count <= kLeafSize || component(centroid_extent, axis) <= 1.0e-6f) {
            nodes[node_index].left = start;
            nodes[node_index].meta = kLeafBit | count;
            return node_index;
        }

        const std::uint32_t left_count = count / 2u;
        const std::uint32_t middle = start + left_count;
        std::nth_element(
            triangles.begin() + start,
            triangles.begin() + middle,
            triangles.begin() + start + count,
            [axis](const GpuTriangle& a, const GpuTriangle& b) {
                return component(triangleCentroid(a), axis) < component(triangleCentroid(b), axis);
            }
        );

        const std::uint32_t left_child = buildNode(start, left_count);
        const std::uint32_t right_child = buildNode(middle, count - left_count);
        nodes[node_index].left = left_child;
        nodes[node_index].meta = right_child;
        return node_index;
    }

    bool uploadGeometry()
    {
        if (node_buffer == 0u) GL15.glGenBuffers(1, &node_buffer);
        if (triangle_buffer == 0u) GL15.glGenBuffers(1, &triangle_buffer);
        if (node_buffer == 0u || triangle_buffer == 0u) return false;

        GL15.glBindBuffer(GL_SHADER_STORAGE_BUFFER, node_buffer);
        GL15.glBufferData(
            GL_SHADER_STORAGE_BUFFER,
            static_cast<LWCGLsizeiptr>(nodes.size() * sizeof(BvhNode)),
            nodes.empty() ? nullptr : nodes.data(),
            GL_STATIC_DRAW
        );

        GL15.glBindBuffer(GL_SHADER_STORAGE_BUFFER, triangle_buffer);
        GL15.glBufferData(
            GL_SHADER_STORAGE_BUFFER,
            static_cast<LWCGLsizeiptr>(triangles.size() * sizeof(GpuTriangle)),
            triangles.empty() ? nullptr : triangles.data(),
            GL_STATIC_DRAW
        );

        GL15.glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0u);
        return true;
    }

    bool rebuildGeometry(const Ecs::World& world)
    {
        const std::uint64_t next_hash = geometryHash(world);
        if (geometry_ready && next_hash == geometry_hash) return true;

        geometry_hash = next_hash;
        geometry_ready = false;
        triangles.clear();
        nodes.clear();

        for (const Ecs::Entity entity : world.entities()) {
            const Ecs::RenderableComponent *renderable = world.getRenderable(entity);
            const Ecs::MeshComponent *mesh_component = world.getMesh(entity);
            const Ecs::TransformComponent *transform = world.getTransform(entity);
            if (!renderable || !renderable->visible || !mesh_component || !transform) continue;

            const Models::MeshData *mesh = Models::mesh(mesh_component->mesh);
            if (!mesh || mesh->indices.size() < 3u) continue;

            const Models::MaterialData *material = Models::material(mesh_component->material);
            if (material && material->opacity < 0.1f) continue;

            const Vec3 color = materialColor(mesh_component->material);
            const Mat4 model = modelMatrix(*transform);
            const Mat4 inverse_model = inverseModelMatrix(*transform);

            for (std::size_t index = 0u; index + 2u < mesh->indices.size(); index += 3u) {
                const std::uint32_t i0 = mesh->indices[index + 0u];
                const std::uint32_t i1 = mesh->indices[index + 1u];
                const std::uint32_t i2 = mesh->indices[index + 2u];
                if (i0 >= mesh->vertices.size() || i1 >= mesh->vertices.size() || i2 >= mesh->vertices.size()) {
                    continue;
                }

                const Models::Vertex& v0 = mesh->vertices[i0];
                const Models::Vertex& v1 = mesh->vertices[i1];
                const Models::Vertex& v2 = mesh->vertices[i2];
                const Vec3 p0 = transformPoint(model, v0.position);
                const Vec3 p1 = transformPoint(model, v1.position);
                const Vec3 p2 = transformPoint(model, v2.position);
                const Vec3 n0 = transformNormal(inverse_model, v0.normal);
                const Vec3 n1 = transformNormal(inverse_model, v1.normal);
                const Vec3 n2 = transformNormal(inverse_model, v2.normal);

                GpuTriangle triangle{};
                triangle.p0 = {p0.x, p0.y, p0.z, 0.0f};
                triangle.p1 = {p1.x, p1.y, p1.z, 0.0f};
                triangle.p2 = {p2.x, p2.y, p2.z, 0.0f};
                triangle.n0 = {n0.x, n0.y, n0.z, 0.0f};
                triangle.n1 = {n1.x, n1.y, n1.z, 0.0f};
                triangle.n2 = {n2.x, n2.y, n2.z, 0.0f};
                triangle.color = {
                    color.x,
                    color.y,
                    color.z,
                    material ? std::clamp(material->opacity, 0.0f, 1.0f) : 1.0f,
                };
                triangles.push_back(triangle);
            }
        }

        if (!triangles.empty()) buildNode(0u, static_cast<std::uint32_t>(triangles.size()));
        if (!uploadGeometry()) return false;

        geometry_ready = true;
        std::fprintf(
            stderr,
            "[GI]: world BVH %zu triangles, %zu nodes\n",
            triangles.size(),
            nodes.size()
        );
        return true;
    }

    bool createPrograms()
    {
        gbuffer_program = createGraphicsProgram(kGBufferVertexShader, kGBufferFragmentShader);
        trace_program = createComputeProgram(kTraceShader);
        temporal_program = createComputeProgram(kTemporalShader);
        denoise_program = createComputeProgram(kDenoiseShader);
        compose_program = createGraphicsProgram(kComposeVertexShader, kComposeFragmentShader);
        return
            gbuffer_program != 0u &&
            trace_program != 0u &&
            temporal_program != 0u &&
            denoise_program != 0u &&
            compose_program != 0u;
    }

    void destroyPrograms()
    {
        if (!GL20.glDeleteProgram) return;
        GLuint *programs[] = {
            &gbuffer_program,
            &trace_program,
            &temporal_program,
            &denoise_program,
            &compose_program,
        };
        for (GLuint *program : programs) {
            if (*program != 0u) GL20.glDeleteProgram(*program);
            *program = 0u;
        }
    }

    bool createGBuffer()
    {
        gbuffer_albedo = createTexture2D(width, height, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, false);
        gbuffer_normal = createTexture2D(width, height, GL_RGBA16F, GL_RGBA, GL_FLOAT, false);
        gbuffer_velocity = createTexture2D(width, height, GL_RG16F, GL_RG, GL_FLOAT, false);
        gbuffer_depth = createTexture2D(
            width,
            height,
            GL_DEPTH_COMPONENT32F,
            GL_DEPTH_COMPONENT,
            GL_FLOAT,
            false
        );

        if (
            gbuffer_albedo == 0u ||
            gbuffer_normal == 0u ||
            gbuffer_velocity == 0u ||
            gbuffer_depth == 0u)
        {
            return false;
        }

        GL30.glGenFramebuffers(1, &gbuffer_framebuffer);
        if (gbuffer_framebuffer == 0u) return false;

        GL30.glBindFramebuffer(GL_FRAMEBUFFER, gbuffer_framebuffer);
        GL30.glFramebufferTexture2D(
            GL_FRAMEBUFFER,
            GL_COLOR_ATTACHMENT0,
            GL_TEXTURE_2D,
            gbuffer_albedo,
            0
        );
        GL30.glFramebufferTexture2D(
            GL_FRAMEBUFFER,
            GL_COLOR_ATTACHMENT1,
            GL_TEXTURE_2D,
            gbuffer_normal,
            0
        );
        GL30.glFramebufferTexture2D(
            GL_FRAMEBUFFER,
            GL_COLOR_ATTACHMENT2,
            GL_TEXTURE_2D,
            gbuffer_velocity,
            0
        );
        GL30.glFramebufferTexture2D(
            GL_FRAMEBUFFER,
            GL_DEPTH_ATTACHMENT,
            GL_TEXTURE_2D,
            gbuffer_depth,
            0
        );

        const GLenum attachments[] = {
            GL_COLOR_ATTACHMENT0,
            GL_COLOR_ATTACHMENT1,
            GL_COLOR_ATTACHMENT2,
        };
        GL20.glDrawBuffers(3, attachments);

        const GLenum status = GL30.glCheckFramebufferStatus(GL_FRAMEBUFFER);
        GL30.glBindFramebuffer(GL_FRAMEBUFFER, 0u);
        if (status != GL_FRAMEBUFFER_COMPLETE) {
            std::fprintf(stderr, "[GI]: GBuffer framebuffer incomplete: 0x%x\n", status);
            return false;
        }
        return true;
    }

    void destroyGBuffer()
    {
        if (gbuffer_framebuffer != 0u && GL30.glDeleteFramebuffers) {
            GL30.glDeleteFramebuffers(1, &gbuffer_framebuffer);
        }
        gbuffer_framebuffer = 0u;
        deleteTexture(gbuffer_albedo);
        deleteTexture(gbuffer_normal);
        deleteTexture(gbuffer_velocity);
        deleteTexture(gbuffer_depth);
    }

    bool createGiBuffers()
    {
        raw = createTexture2D(gi_width, gi_height, GL_RGBA16F, GL_RGBA, GL_FLOAT, true);

        for (std::size_t i = 0u; i < 2u; ++i) {
            history[i] = createTexture2D(gi_width, gi_height, GL_RGBA16F, GL_RGBA, GL_FLOAT, true);
            moments[i] = createTexture2D(gi_width, gi_height, GL_RG16F, GL_RG, GL_FLOAT, true);
            geometry[i] = createTexture2D(gi_width, gi_height, GL_RGBA16F, GL_RGBA, GL_FLOAT, false);
            denoise[i] = createTexture2D(gi_width, gi_height, GL_RGBA16F, GL_RGBA, GL_FLOAT, true);
        }

        if (raw == 0u) return false;
        for (std::size_t i = 0u; i < 2u; ++i) {
            if (history[i] == 0u || moments[i] == 0u || geometry[i] == 0u || denoise[i] == 0u) {
                return false;
            }
        }
        return true;
    }

    void destroyGiBuffers()
    {
        deleteTexture(raw);
        for (std::size_t i = 0u; i < 2u; ++i) {
            deleteTexture(history[i]);
            deleteTexture(moments[i]);
            deleteTexture(geometry[i]);
            deleteTexture(denoise[i]);
        }
    }

    void beginGBuffer()
    {
        GL30.glBindFramebuffer(GL_FRAMEBUFFER, gbuffer_framebuffer);
        const GLenum attachments[] = {
            GL_COLOR_ATTACHMENT0,
            GL_COLOR_ATTACHMENT1,
            GL_COLOR_ATTACHMENT2,
        };
        GL20.glDrawBuffers(3, attachments);
        glViewport(0, 0, width, height);
        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        GL20.glUseProgram(gbuffer_program);
        setInt(gbuffer_program, "uDiffuse", 0);
        setMatrix(gbuffer_program, "uInverseView", inverse_view);
        setMatrix(gbuffer_program, "uPreviousViewProjection", previous_view_projection);
    }

    void endGBuffer()
    {
        GL20.glUseProgram(0u);
        GL30.glBindFramebuffer(GL_FRAMEBUFFER, 0u);
        GL42.glMemoryBarrier(GL_FRAMEBUFFER_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);
        glClearColor(0.035f, 0.035f, 0.045f, 1.0f);
    }

    void trace()
    {
        GL20.glUseProgram(trace_program);
        bindTextureUnit(0, gbuffer_albedo);
        bindTextureUnit(1, gbuffer_normal);
        bindTextureUnit(2, gbuffer_depth);

        setInt(trace_program, "uAlbedo", 0);
        setInt(trace_program, "uNormal", 1);
        setInt(trace_program, "uDepth", 2);
        setMatrix(trace_program, "uViewProjection", current_view_projection);
        setMatrix(trace_program, "uInverseViewProjection", inverse_view_projection);
        setSize(trace_program, "uOutputSize", gi_width, gi_height);
        setInt(trace_program, "uFrame", static_cast<int>(frame & 0x7fffffffu));
        setInt(trace_program, "uNodeCount", static_cast<int>(nodes.size()));
        setInt(trace_program, "uUseScreen", settings.screen_space_first ? 1 : 0);
        setInt(trace_program, "uUseBvh", settings.bvh_fallback ? 1 : 0);
        setInt(trace_program, "uRaysPerPixel", std::clamp(settings.rays_per_pixel, 1, 4));
        setInt(trace_program, "uMaxBounces", std::clamp(settings.max_bounces, 1, 4));

        GL30.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0u, node_buffer);
        GL30.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1u, triangle_buffer);
        GL42.glBindImageTexture(0u, raw, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA16F);
        GL43.glDispatchCompute(
            static_cast<GLuint>((gi_width + 7) / 8),
            static_cast<GLuint>((gi_height + 7) / 8),
            1u
        );
        GL42.glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);
    }

    GLuint temporal()
    {
        const int next = history_index ^ 1;
        GL20.glUseProgram(temporal_program);

        bindTextureUnit(0, raw);
        bindTextureUnit(1, history[history_index]);
        bindTextureUnit(2, moments[history_index]);
        bindTextureUnit(3, geometry[history_index]);
        bindTextureUnit(4, gbuffer_depth);
        bindTextureUnit(5, gbuffer_normal);
        bindTextureUnit(6, gbuffer_velocity);

        setInt(temporal_program, "uRaw", 0);
        setInt(temporal_program, "uPreviousHistory", 1);
        setInt(temporal_program, "uPreviousMoments", 2);
        setInt(temporal_program, "uPreviousGeometry", 3);
        setInt(temporal_program, "uDepth", 4);
        setInt(temporal_program, "uNormal", 5);
        setInt(temporal_program, "uVelocity", 6);
        setSize(temporal_program, "uOutputSize", gi_width, gi_height);
        setFloat(temporal_program, "uAlpha", settings.temporal_alpha);
        setFloat(temporal_program, "uDepthReject", settings.depth_rejection);
        setFloat(temporal_program, "uNormalReject", settings.normal_rejection);
        setInt(temporal_program, "uHasHistory", frame > 0u ? 1 : 0);

        GL42.glBindImageTexture(0u, history[next], 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA16F);
        GL42.glBindImageTexture(1u, moments[next], 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RG16F);
        GL42.glBindImageTexture(2u, geometry[next], 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA16F);
        GL43.glDispatchCompute(
            static_cast<GLuint>((gi_width + 7) / 8),
            static_cast<GLuint>((gi_height + 7) / 8),
            1u
        );
        GL42.glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);
        return history[next];
    }

    GLuint filter(GLuint source)
    {
        GLuint input = source;
        const int iterations = std::clamp(settings.denoise_iterations, 1, 6);
        const int moment_index = settings.temporal_reuse ? (history_index ^ 1) : history_index;

        for (int iteration = 0; iteration < iterations; ++iteration) {
            const GLuint output = denoise[static_cast<std::size_t>(iteration & 1)];
            GL20.glUseProgram(denoise_program);
            bindTextureUnit(0, input);
            bindTextureUnit(1, gbuffer_depth);
            bindTextureUnit(2, gbuffer_normal);
            bindTextureUnit(3, moments[static_cast<std::size_t>(moment_index)]);

            setInt(denoise_program, "uInput", 0);
            setInt(denoise_program, "uDepth", 1);
            setInt(denoise_program, "uNormal", 2);
            setInt(denoise_program, "uMoments", 3);
            setSize(denoise_program, "uOutputSize", gi_width, gi_height);
            setInt(denoise_program, "uStep", 1 << iteration);
            setInt(denoise_program, "uUseMoments", settings.temporal_reuse ? 1 : 0);

            GL42.glBindImageTexture(0u, output, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA16F);
            GL43.glDispatchCompute(
                static_cast<GLuint>((gi_width + 7) / 8),
                static_cast<GLuint>((gi_height + 7) / 8),
                1u
            );
            GL42.glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);
            input = output;
        }

        return input;
    }

    void compose(GLuint lighting)
    {
        GL30.glBindFramebuffer(GL_FRAMEBUFFER, 0u);
        glViewport(0, 0, width, height);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        glDisable(GL_LIGHTING);
        glDisable(GL_BLEND);

        GL20.glUseProgram(compose_program);
        bindTextureUnit(0, gbuffer_albedo);
        bindTextureUnit(1, gbuffer_depth);
        bindTextureUnit(2, lighting);
        setInt(compose_program, "uAlbedo", 0);
        setInt(compose_program, "uDepth", 1);
        setInt(compose_program, "uLighting", 2);

        glBegin(GL_TRIANGLES);
        glVertex2f(-1.0f, -1.0f);
        glVertex2f(3.0f, -1.0f);
        glVertex2f(-1.0f, 3.0f);
        glEnd();

        GL20.glUseProgram(0u);
        GLModern.glActiveTexture(GL_TEXTURE0);
        glEnable(GL_DEPTH_TEST);
        glEnable(GL_CULL_FACE);
        glEnable(GL_LIGHTING);
        glEnable(GL_COLOR_MATERIAL);
    }

    void destroyGeometryBuffers()
    {
        if (!GL15.glDeleteBuffers) return;
        if (node_buffer != 0u) GL15.glDeleteBuffers(1, &node_buffer);
        if (triangle_buffer != 0u) GL15.glDeleteBuffers(1, &triangle_buffer);
        node_buffer = 0u;
        triangle_buffer = 0u;
    }
};

GI::GI() : impl_(new Impl) {}

GI::~GI()
{
    shutdown();
    delete impl_;
    impl_ = nullptr;
}

bool GI::init(int width, int height)
{
    if (impl_->initialized) return true;

    impl_->width = std::max(width, 1);
    impl_->height = std::max(height, 1);
    impl_->updateResolution();

    if (!lwcglModernGLAvailable() && lwcglLoadModernGL() != 0) {
        std::fprintf(stderr, "[GI]: modern OpenGL unavailable\n");
        return false;
    }

    const int major = lwcglModernGLMajorVersion();
    const int minor = lwcglModernGLMinorVersion();
    if (
        major < 4 ||
        (major == 4 && minor < 3) ||
        !GL43.glDispatchCompute ||
        !GL42.glBindImageTexture ||
        !GL30.glBindBufferBase)
    {
        std::fprintf(
            stderr,
            "[GI]: OpenGL 4.3 compatibility context required; found %d.%d\n",
            major,
            minor
        );
        return false;
    }

    if (!impl_->createPrograms() || !impl_->createGBuffer() || !impl_->createGiBuffers()) {
        shutdown();
        return false;
    }

    impl_->history_index = 0;
    impl_->frame = 0u;
    impl_->geometry_ready = false;
    impl_->initialized = true;

    std::fprintf(
        stderr,
        "[GI]: initialized OpenGL %d.%d, framebuffer %dx%d, GI %dx%d\n",
        major,
        minor,
        impl_->width,
        impl_->height,
        impl_->gi_width,
        impl_->gi_height
    );
    return true;
}

void GI::resize(int width, int height)
{
    const int new_width = std::max(width, 1);
    const int new_height = std::max(height, 1);
    if (new_width == impl_->width && new_height == impl_->height) return;

    impl_->width = new_width;
    impl_->height = new_height;
    impl_->updateResolution();
    if (!impl_->initialized) return;

    impl_->destroyGBuffer();
    impl_->destroyGiBuffers();
    if (!impl_->createGBuffer() || !impl_->createGiBuffers()) {
        shutdown();
        return;
    }

    impl_->history_index = 0;
    impl_->frame = 0u;
}

void GI::begin(const Ecs::World& world)
{
    if (!impl_->active()) return;

    Mat4 projection{};
    Mat4 view{};
    glGetFloatv(GL_PROJECTION_MATRIX, projection.data());
    glGetFloatv(GL_MODELVIEW_MATRIX, view.data());

    const Mat4 captured = multiply(projection, view);
    impl_->previous_view_projection = impl_->frame == 0u
        ? captured
        : impl_->current_view_projection;
    impl_->current_view_projection = captured;

    if (!inverseMatrix(captured, impl_->inverse_view_projection)) {
        impl_->inverse_view_projection = identityMatrix();
    }
    if (!inverseMatrix(view, impl_->inverse_view)) {
        impl_->inverse_view = identityMatrix();
    }

    if (!impl_->rebuildGeometry(world)) {
        std::fprintf(stderr, "[GI]: failed to rebuild world BVH\n");
        impl_->settings.enabled = false;
        return;
    }

    impl_->beginGBuffer();
}

void GI::bindMaterial(unsigned int texture_id)
{
    if (!impl_->active()) return;

    GL20.glUseProgram(impl_->gbuffer_program);
    bindTextureUnit(0, static_cast<GLuint>(texture_id));
    setInt(impl_->gbuffer_program, "uDiffuse", 0);
    setInt(impl_->gbuffer_program, "uHasTexture", texture_id != 0u ? 1 : 0);
    glDisable(GL_BLEND);
}

void GI::end(const Ecs::World& world)
{
    (void)world;
    if (!impl_->active()) return;

    impl_->endGBuffer();
    impl_->trace();

    GLuint result = impl_->raw;
    if (impl_->settings.temporal_reuse) result = impl_->temporal();
    if (impl_->settings.denoise) result = impl_->filter(result);

    impl_->compose(result);
    if (impl_->settings.temporal_reuse) impl_->history_index ^= 1;
    ++impl_->frame;
}

void GI::shutdown()
{
    if (!impl_) return;

    if (GL20.glUseProgram) GL20.glUseProgram(0u);
    impl_->destroyPrograms();
    impl_->destroyGiBuffers();
    impl_->destroyGBuffer();
    impl_->destroyGeometryBuffers();
    impl_->nodes.clear();
    impl_->triangles.clear();
    impl_->initialized = false;
    impl_->geometry_ready = false;
    impl_->geometry_hash = 0u;
    impl_->history_index = 0;
    impl_->frame = 0u;
}

bool GI::initialized() const
{
    return impl_->initialized;
}

bool GI::enabled() const
{
    return impl_->settings.enabled;
}

void GI::setEnabled(bool enabled)
{
    impl_->settings.enabled = enabled;
}

GiSettings& GI::settings()
{
    return impl_->settings;
}

const GiSettings& GI::settings() const
{
    return impl_->settings;
}

} // namespace Renderer
