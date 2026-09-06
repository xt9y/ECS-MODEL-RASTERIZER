#include "Renderer/Gi/Gi.hpp"

#include "Models/Models.hpp"

#include <lwcgl/gl11_compat.h>
#include <lwcgl/glmodern.h>
#include <lwcgl/lwcgl.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <unordered_map>
#include <utility>
#include <vector>

#ifndef GL_COLOR_ATTACHMENT1
#define GL_COLOR_ATTACHMENT1 0x8CE1
#endif
#ifndef GL_COLOR_ATTACHMENT2
#define GL_COLOR_ATTACHMENT2 0x8CE2
#endif
#ifndef GL_COLOR_ATTACHMENT3
#define GL_COLOR_ATTACHMENT3 0x8CE3
#endif
#ifndef GL_RGBA8
#define GL_RGBA8 0x8058
#endif
#ifndef GL_RG16F
#define GL_RG16F 0x822F
#endif
#ifndef GL_R16F
#define GL_R16F 0x822D
#endif
#ifndef GL_RG
#define GL_RG 0x8227
#endif
#ifndef GL_RED
#define GL_RED 0x1903
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
constexpr std::uint32_t kBlasLeafSize = 4u;
constexpr std::uint32_t kTlasLeafSize = 2u;

float component(const Vec3& value, int axis)
{
    return axis == 0 ? value.x : axis == 1 ? value.y : value.z;
}

Vec3 minimum(const Vec3& a, const Vec3& b)
{
    return {std::min(a.x, b.x), std::min(a.y, b.y), std::min(a.z, b.z)};
}

Vec3 maximum(const Vec3& a, const Vec3& b)
{
    return {std::max(a.x, b.x), std::max(a.y, b.y), std::max(a.z, b.z)};
}

Vec3 add(const Vec3& a, const Vec3& b)
{
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}

Vec3 scale(const Vec3& value, float factor)
{
    return {value.x * factor, value.y * factor, value.z * factor};
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
            for (int k = 0; k < 4; ++k) {
                value += a[k * 4 + row] * b[column * 4 + k];
            }
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

Vec3 transformPoint(const Mat4& matrix, const Vec3& value)
{
    const float x = matrix[0] * value.x + matrix[4] * value.y + matrix[8] * value.z + matrix[12];
    const float y = matrix[1] * value.x + matrix[5] * value.y + matrix[9] * value.z + matrix[13];
    const float z = matrix[2] * value.x + matrix[6] * value.y + matrix[10] * value.z + matrix[14];
    const float w = matrix[3] * value.x + matrix[7] * value.y + matrix[11] * value.z + matrix[15];
    if (std::abs(w) <= 1.0e-8f || std::abs(w - 1.0f) <= 1.0e-8f) return {x, y, z};
    return {x / w, y / w, z / w};
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

GLuint createTexture2D(int width, int height, GLint internal_format, GLenum format, GLenum type)
{
    const GLuint texture = lwcgl_glGenTexture();
    if (texture == 0u) return 0u;

    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, internal_format, width, height, 0, format, type, nullptr);
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
layout(location = 2) out vec4 outMaterial;
layout(location = 3) out vec2 outVelocity;
void main() {
    vec4 base = vColor;
    if (uHasTexture != 0) base *= texture(uDiffuse, vUv);
    if (base.a < 0.02) discard;
    vec3 normal = normalize(vWorldNormal);
    vec2 currentNdc = vCurrentClip.xy / max(abs(vCurrentClip.w), 1e-6);
    vec2 previousNdc = vPreviousClip.xy / max(abs(vPreviousClip.w), 1e-6);
    outAlbedo = base;
    outNormal = vec4(normal * 0.5 + 0.5, 1.0);
    outMaterial = vec4(0.8, 0.0, 0.0, 1.0);
    outVelocity = (currentNdc - previousNdc) * 0.5;
}
)GLSL";

const char *kSurfaceCacheShader = R"GLSL(
#version 430
layout(local_size_x = 8, local_size_y = 8) in;
uniform sampler2D uAlbedo;
uniform sampler2D uNormal;
uniform sampler2D uDepth;
uniform ivec2 uOutputSize;
layout(rgba16f, binding = 0) writeonly uniform image2D uRadiance;
void main() {
    ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
    if (any(greaterThanEqual(pixel, uOutputSize))) return;
    vec2 uv = (vec2(pixel) + 0.5) / vec2(uOutputSize);
    float depth = texture(uDepth, uv).r;
    if (depth >= 0.999999) {
        imageStore(uRadiance, pixel, vec4(0.03, 0.04, 0.06, 1.0));
        return;
    }
    vec3 albedo = texture(uAlbedo, uv).rgb;
    vec3 normal = normalize(texture(uNormal, uv).xyz * 2.0 - 1.0);
    vec3 lightDirection = normalize(vec3(-0.35, 0.8, 0.45));
    float diffuse = max(dot(normal, lightDirection), 0.0);
    vec3 radiance = albedo * (0.08 + 0.75 * diffuse);
    imageStore(uRadiance, pixel, vec4(radiance, 1.0));
}
)GLSL";

const char *kTraceShader = R"GLSL(
#version 430
layout(local_size_x = 8, local_size_y = 8) in;

struct BvhNode { vec3 bmin; uint leftFirst; vec3 bmax; uint count; };
struct Triangle {
    vec4 p0; vec4 p1; vec4 p2;
    vec4 n0; vec4 n1; vec4 n2;
    vec4 uv01; vec4 uv2;
};
struct Instance {
    mat4 objectToWorld;
    mat4 worldToObject;
    vec4 bmin;
    vec4 bmax;
    uvec4 meta;
    vec4 colorOpacity;
};

layout(std430, binding = 0) readonly buffer BlasNodes { BvhNode blasNodes[]; };
layout(std430, binding = 1) readonly buffer Triangles { Triangle triangles[]; };
layout(std430, binding = 2) readonly buffer TlasNodes { BvhNode tlasNodes[]; };
layout(std430, binding = 3) readonly buffer Instances { Instance instances[]; };
layout(std430, binding = 4) readonly buffer BlasMetadata { uvec4 blasMetadata[]; };

uniform sampler2D uAlbedo;
uniform sampler2D uNormal;
uniform sampler2D uDepth;
uniform sampler2D uSurface;
uniform mat4 uViewProjection;
uniform mat4 uInverseViewProjection;
uniform ivec2 uOutputSize;
uniform uint uFrame;
uniform int uTlasNodeCount;
uniform int uUseScreen;
uniform int uUseBvh;
uniform int uUseSurface;
uniform int uRaysPerPixel;
uniform int uMaxBounces;

layout(rgba16f, binding = 0) writeonly uniform image2D uRaw;
layout(r16f, binding = 1) writeonly uniform image2D uHitDistance;

uint hashUint(uint x) {
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}
float randomFloat(inout uint state) {
    state = hashUint(state);
    return float(state) * (1.0 / 4294967296.0);
}
vec3 cosineHemisphere(vec3 n, inout uint state) {
    float r1 = randomFloat(state);
    float r2 = randomFloat(state);
    float phi = 6.28318530718 * r1;
    float r = sqrt(r2);
    vec3 local = vec3(r * cos(phi), r * sin(phi), sqrt(max(0.0, 1.0 - r2)));
    vec3 helper = abs(n.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
    vec3 tangent = normalize(cross(helper, n));
    vec3 bitangent = cross(n, tangent);
    return normalize(tangent * local.x + bitangent * local.y + n * local.z);
}
vec3 inverseDirection(vec3 direction) {
    return vec3(
        abs(direction.x) > 1e-8 ? 1.0 / direction.x : 1e30,
        abs(direction.y) > 1e-8 ? 1.0 / direction.y : 1e30,
        abs(direction.z) > 1e-8 ? 1.0 / direction.z : 1e30
    );
}
bool hitAabb(vec3 origin, vec3 invDir, vec3 bmin, vec3 bmax, float maxDistance) {
    vec3 t0 = (bmin - origin) * invDir;
    vec3 t1 = (bmax - origin) * invDir;
    vec3 nearV = min(t0, t1);
    vec3 farV = max(t0, t1);
    float nearT = max(max(nearV.x, nearV.y), max(nearV.z, 0.0));
    float farT = min(min(farV.x, farV.y), farV.z);
    return farT >= nearT && nearT < maxDistance;
}
bool hitTriangle(vec3 origin, vec3 direction, Triangle triangle, inout float distance, out vec3 normal) {
    vec3 a = triangle.p0.xyz;
    vec3 b = triangle.p1.xyz;
    vec3 c = triangle.p2.xyz;
    vec3 e1 = b - a;
    vec3 e2 = c - a;
    vec3 p = cross(direction, e2);
    float determinant = dot(e1, p);
    if (abs(determinant) < 1e-8) return false;
    float invDet = 1.0 / determinant;
    vec3 s = origin - a;
    float u = dot(s, p) * invDet;
    if (u < 0.0 || u > 1.0) return false;
    vec3 q = cross(s, e1);
    float v = dot(direction, q) * invDet;
    if (v < 0.0 || u + v > 1.0) return false;
    float t = dot(e2, q) * invDet;
    if (t <= 0.001 || t >= distance) return false;
    distance = t;
    float w = 1.0 - u - v;
    normal = normalize(triangle.n0.xyz * w + triangle.n1.xyz * u + triangle.n2.xyz * v);
    return true;
}

bool traceBlas(Instance instance, vec3 worldOrigin, vec3 worldDirection, inout float bestDistance, out vec3 worldNormal) {
    uvec4 meta = blasMetadata[instance.meta.x];
    vec3 origin = (instance.worldToObject * vec4(worldOrigin, 1.0)).xyz;
    vec3 direction = (instance.worldToObject * vec4(worldDirection, 0.0)).xyz;
    vec3 invDir = inverseDirection(direction);
    uint stack[64];
    int stackSize = 0;
    stack[stackSize++] = 0u;
    bool hit = false;
    vec3 bestLocalNormal = vec3(0.0, 1.0, 0.0);
    while (stackSize > 0) {
        uint localNodeIndex = stack[--stackSize];
        BvhNode node = blasNodes[meta.x + localNodeIndex];
        if (!hitAabb(origin, invDir, node.bmin, node.bmax, bestDistance)) continue;
        if (node.count != 0u) {
            for (uint i = 0u; i < node.count; ++i) {
                vec3 localNormal;
                if (hitTriangle(origin, direction, triangles[meta.y + node.leftFirst + i], bestDistance, localNormal)) {
                    bestLocalNormal = localNormal;
                    hit = true;
                }
            }
        } else if (stackSize <= 61) {
            stack[stackSize++] = node.leftFirst + 1u;
            stack[stackSize++] = node.leftFirst;
        }
    }
    if (hit) worldNormal = normalize(mat3(transpose(instance.worldToObject)) * bestLocalNormal);
    return hit;
}

bool traceScene(vec3 origin, vec3 direction, out float distance, out vec3 normal, out vec3 color) {
    distance = 1e30;
    normal = vec3(0.0, 1.0, 0.0);
    color = vec3(1.0);
    if (uUseBvh == 0 || uTlasNodeCount == 0) return false;
    vec3 invDir = inverseDirection(direction);
    uint stack[64];
    int stackSize = 0;
    stack[stackSize++] = 0u;
    bool anyHit = false;
    while (stackSize > 0) {
        uint nodeIndex = stack[--stackSize];
        if (nodeIndex >= uint(uTlasNodeCount)) continue;
        BvhNode node = tlasNodes[nodeIndex];
        if (!hitAabb(origin, invDir, node.bmin, node.bmax, distance)) continue;
        if (node.count != 0u) {
            for (uint i = 0u; i < node.count; ++i) {
                Instance instance = instances[node.leftFirst + i];
                if (!hitAabb(origin, invDir, instance.bmin.xyz, instance.bmax.xyz, distance)) continue;
                vec3 candidateNormal;
                float candidateDistance = distance;
                if (traceBlas(instance, origin, direction, candidateDistance, candidateNormal)) {
                    distance = candidateDistance;
                    normal = candidateNormal;
                    color = instance.colorOpacity.rgb;
                    anyHit = true;
                }
            }
        } else if (stackSize <= 61) {
            stack[stackSize++] = node.leftFirst + 1u;
            stack[stackSize++] = node.leftFirst;
        }
    }
    return anyHit;
}

bool screenTrace(vec3 origin, vec3 direction, out vec2 hitUv, out float distance) {
    for (int step = 0; step < 24; ++step) {
        float t = 0.12 + float(step) * float(step) * 0.035;
        vec3 point = origin + direction * t;
        vec4 clip = uViewProjection * vec4(point, 1.0);
        if (clip.w <= 0.0) continue;
        vec3 ndc = clip.xyz / clip.w;
        vec2 uv = ndc.xy * 0.5 + 0.5;
        if (any(lessThan(uv, vec2(0.001))) || any(greaterThan(uv, vec2(0.999)))) return false;
        float rayDepth = ndc.z * 0.5 + 0.5;
        float sceneDepth = texture(uDepth, uv).r;
        float delta = rayDepth - sceneDepth;
        if (sceneDepth < 0.999999 && delta > 0.0005 && delta < 0.025) {
            hitUv = uv;
            distance = t;
            return true;
        }
    }
    return false;
}

vec3 reconstructWorld(vec2 uv, float depth) {
    vec4 clip = vec4(uv * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
    vec4 world = uInverseViewProjection * clip;
    return world.xyz / max(abs(world.w), 1e-8);
}

void main() {
    ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
    if (any(greaterThanEqual(pixel, uOutputSize))) return;
    vec2 uv = (vec2(pixel) + 0.5) / vec2(uOutputSize);
    float depth = texture(uDepth, uv).r;
    if (depth >= 0.999999) {
        imageStore(uRaw, pixel, vec4(0.0));
        imageStore(uHitDistance, pixel, vec4(0.0));
        return;
    }

    vec3 position = reconstructWorld(uv, depth);
    vec3 normal = normalize(texture(uNormal, uv).xyz * 2.0 - 1.0);
    int rayCount = clamp(uRaysPerPixel, 1, 4);
    int bounceCount = clamp(uMaxBounces, 1, 4);
    vec3 total = vec3(0.0);
    float firstDistance = 0.0;

    for (int sampleIndex = 0; sampleIndex < rayCount; ++sampleIndex) {
        uint state = uint(pixel.x) * 1973u + uint(pixel.y) * 9277u + uFrame * 26699u + uint(sampleIndex) * 31847u + 1u;
        vec3 origin = position + normal * 0.03;
        vec3 direction = cosineHemisphere(normal, state);
        vec3 throughput = vec3(1.0);
        vec3 radiance = vec3(0.0);

        for (int bounce = 0; bounce < bounceCount; ++bounce) {
            if (bounce == 0 && uUseScreen != 0) {
                vec2 hitUv;
                float screenDistance;
                if (screenTrace(origin, direction, hitUv, screenDistance)) {
                    vec3 hitRadiance;
                    if (uUseSurface != 0) hitRadiance = texture(uSurface, hitUv).rgb;
                    else {
                        vec3 hitAlbedo = texture(uAlbedo, hitUv).rgb;
                        vec3 hitNormal = normalize(texture(uNormal, hitUv).xyz * 2.0 - 1.0);
                        float ndl = max(dot(hitNormal, normalize(vec3(-0.35, 0.8, 0.45))), 0.0);
                        hitRadiance = hitAlbedo * (0.08 + 0.75 * ndl);
                    }
                    radiance += throughput * hitRadiance;
                    if (sampleIndex == 0) firstDistance = screenDistance;
                    break;
                }
            }

            float hitDistance;
            vec3 hitNormal;
            vec3 hitColor;
            if (!traceScene(origin, direction, hitDistance, hitNormal, hitColor)) {
                radiance += throughput * vec3(0.03, 0.04, 0.06);
                break;
            }

            if (bounce == 0 && sampleIndex == 0) firstDistance = hitDistance;
            vec3 lightDirection = normalize(vec3(-0.35, 0.8, 0.45));
            float direct = max(dot(hitNormal, lightDirection), 0.0);
            radiance += throughput * hitColor * (0.05 + 0.75 * direct);
            throughput *= hitColor * 0.55;
            if (max(max(throughput.r, throughput.g), throughput.b) < 0.02) break;
            origin += direction * hitDistance + hitNormal * 0.025;
            direction = cosineHemisphere(hitNormal, state);
        }
        total += radiance;
    }

    total /= float(rayCount);
    imageStore(uRaw, pixel, vec4(total, 1.0));
    imageStore(uHitDistance, pixel, vec4(firstDistance));
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
uniform ivec2 uOutputSize;
uniform float uAlpha;
uniform float uDepthReject;
uniform float uNormalReject;
uniform int uHasHistory;
layout(rgba16f, binding = 0) writeonly uniform image2D uHistory;
layout(rg16f, binding = 1) writeonly uniform image2D uMoments;
layout(rgba16f, binding = 2) writeonly uniform image2D uGeometry;
void main() {
    ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
    if (any(greaterThanEqual(pixel, uOutputSize))) return;
    vec2 uv = (vec2(pixel) + 0.5) / vec2(uOutputSize);
    vec3 current = texture(uRaw, uv).rgb;
    float depth = texture(uDepth, uv).r;
    vec3 normal = normalize(texture(uNormal, uv).xyz * 2.0 - 1.0);
    vec2 velocity = texture(uVelocity, uv).xy;
    vec2 previousUv = uv - velocity;
    bool valid = uHasHistory != 0 && all(greaterThanEqual(previousUv, vec2(0.0))) && all(lessThanEqual(previousUv, vec2(1.0)));
    vec3 result = current;
    vec2 moments = vec2(dot(current, vec3(0.2126, 0.7152, 0.0722)));
    moments.y *= moments.x;
    if (valid) {
        vec4 previousGeometry = texture(uPreviousGeometry, previousUv);
        vec3 previousNormal = normalize(previousGeometry.xyz);
        float previousDepth = previousGeometry.w;
        valid = abs(previousDepth - depth) <= uDepthReject && dot(previousNormal, normal) >= uNormalReject;
        if (valid) {
            vec3 history = texture(uPreviousHistory, previousUv).rgb;
            result = mix(history, current, clamp(uAlpha, 0.01, 1.0));
            vec2 oldMoments = texture(uPreviousMoments, previousUv).rg;
            moments = mix(oldMoments, moments, clamp(uAlpha, 0.01, 1.0));
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
uniform ivec2 uOutputSize;
uniform int uStep;
layout(rgba16f, binding = 0) writeonly uniform image2D uOutput;
float luminance(vec3 c) { return dot(c, vec3(0.2126, 0.7152, 0.0722)); }
void main() {
    ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
    if (any(greaterThanEqual(pixel, uOutputSize))) return;
    vec2 size = vec2(uOutputSize);
    vec2 uv = (vec2(pixel) + 0.5) / size;
    vec3 center = texture(uInput, uv).rgb;
    float centerDepth = texture(uDepth, uv).r;
    vec3 centerNormal = normalize(texture(uNormal, uv).xyz * 2.0 - 1.0);
    vec2 centerMoments = texture(uMoments, uv).rg;
    float variance = max(centerMoments.y - centerMoments.x * centerMoments.x, 1e-4);
    const float kernel[5] = float[](1.0, 4.0, 6.0, 4.0, 1.0);
    vec3 sum = vec3(0.0);
    float weightSum = 0.0;
    for (int y = -2; y <= 2; ++y) {
        for (int x = -2; x <= 2; ++x) {
            ivec2 q = clamp(pixel + ivec2(x, y) * uStep, ivec2(0), uOutputSize - ivec2(1));
            vec2 quv = (vec2(q) + 0.5) / size;
            vec3 sampleColor = texture(uInput, quv).rgb;
            float sampleDepth = texture(uDepth, quv).r;
            vec3 sampleNormal = normalize(texture(uNormal, quv).xyz * 2.0 - 1.0);
            float normalWeight = pow(max(dot(centerNormal, sampleNormal), 0.0), 32.0);
            float depthWeight = exp(-abs(sampleDepth - centerDepth) * 180.0 / float(max(uStep, 1)));
            float colorWeight = exp(-abs(luminance(sampleColor) - luminance(center)) / (sqrt(variance) * 4.0 + 0.03));
            float kernelWeight = kernel[x + 2] * kernel[y + 2];
            float weight = kernelWeight * normalWeight * depthWeight * colorWeight;
            sum += sampleColor * weight;
            weightSum += weight;
        }
    }
    imageStore(uOutput, pixel, vec4(sum / max(weightSum, 1e-6), 1.0));
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
uniform sampler2D uNormal;
uniform sampler2D uDepth;
uniform sampler2D uGi;
in vec2 vUv;
layout(location = 0) out vec4 outColor;
void main() {
    float depth = texture(uDepth, vUv).r;
    if (depth >= 0.999999) {
        outColor = vec4(0.035, 0.035, 0.045, 1.0);
        return;
    }
    vec4 albedo = texture(uAlbedo, vUv);
    vec3 normal = normalize(texture(uNormal, vUv).xyz * 2.0 - 1.0);
    vec3 indirect = texture(uGi, vUv).rgb;
    vec3 lightDirection = normalize(vec3(-0.35, 0.8, 0.45));
    float direct = max(dot(normal, lightDirection), 0.0);
    vec3 color = albedo.rgb * (0.08 + 0.75 * direct) + albedo.rgb * indirect;
    outColor = vec4(color, albedo.a);
}
)GLSL";

} // namespace

struct GI::Impl {
    struct alignas(16) BvhNode {
        float min_x = 0.0f;
        float min_y = 0.0f;
        float min_z = 0.0f;
        std::uint32_t left_first = 0u;
        float max_x = 0.0f;
        float max_y = 0.0f;
        float max_z = 0.0f;
        std::uint32_t count = 0u;
    };

    struct alignas(16) GpuTriangle {
        std::array<float, 4> p0{};
        std::array<float, 4> p1{};
        std::array<float, 4> p2{};
        std::array<float, 4> n0{};
        std::array<float, 4> n1{};
        std::array<float, 4> n2{};
        std::array<float, 4> uv01{};
        std::array<float, 4> uv2{};
    };

    struct Blas {
        std::vector<BvhNode> nodes;
        std::vector<GpuTriangle> triangles;
        std::uint32_t meta_index = 0u;
    };

    struct alignas(16) TlasInstance {
        Mat4 object_to_world{};
        Mat4 world_to_object{};
        std::array<float, 4> bounds_min{};
        std::array<float, 4> bounds_max{};
        std::array<std::uint32_t, 4> meta{};
        std::array<float, 4> color_opacity{};
    };

    struct GBuffer {
        GLuint framebuffer = 0u;
        GLuint albedo = 0u;
        GLuint normal = 0u;
        GLuint material = 0u;
        GLuint velocity = 0u;
        GLuint depth = 0u;
    } gbuffer;

    struct Buffers {
        GLuint raw = 0u;
        std::array<GLuint, 2> history{};
        std::array<GLuint, 2> moments{};
        std::array<GLuint, 2> geometry{};
        GLuint denoised = 0u;
        GLuint temporary = 0u;
        GLuint hit_distance = 0u;
        GLuint final_texture = 0u;
    } buffers;

    struct SurfaceCache {
        GLuint radiance = 0u;
    } surface;

    struct Programs {
        GLuint gbuffer = 0u;
        GLuint surface = 0u;
        GLuint trace = 0u;
        GLuint temporal = 0u;
        GLuint denoise = 0u;
        GLuint compose = 0u;
    } programs;

    GiSettings settings{};
    bool initialized = false;
    int width = 1;
    int height = 1;
    int gi_width = 1;
    int gi_height = 1;
    std::uint64_t frame = 0u;
    int history_index = 0;

    Mat4 current_view_projection = identityMatrix();
    Mat4 previous_view_projection = identityMatrix();
    Mat4 inverse_view_projection = identityMatrix();
    Mat4 inverse_view = identityMatrix();

    std::unordered_map<std::uint32_t, Blas> blas_cache;
    bool blas_buffers_dirty = true;
    std::vector<BvhNode> global_blas_nodes;
    std::vector<GpuTriangle> global_triangles;
    std::vector<std::array<std::uint32_t, 4>> blas_metadata;
    std::vector<BvhNode> tlas_nodes;
    std::vector<TlasInstance> tlas_instances;

    GLuint blas_node_buffer = 0u;
    GLuint triangle_buffer = 0u;
    GLuint blas_metadata_buffer = 0u;
    GLuint tlas_node_buffer = 0u;
    GLuint tlas_instance_buffer = 0u;

    struct BuildTriangle {
        GpuTriangle triangle;
        Vec3 minimum;
        Vec3 maximum;
        Vec3 centroid;
    };

    struct BuildInstance {
        TlasInstance instance;
        Vec3 minimum;
        Vec3 maximum;
        Vec3 centroid;
    };

    bool active() const { return initialized && settings.enabled; }

    void updateResolution()
    {
        const int divisor = std::max(settings.resolution_divisor, 1);
        gi_width = std::max(width / divisor, 1);
        gi_height = std::max(height / divisor, 1);
    }

    void destroyGBuffer()
    {
        if (gbuffer.framebuffer != 0u) GL30.glDeleteFramebuffers(1, &gbuffer.framebuffer);
        deleteTexture(gbuffer.albedo);
        deleteTexture(gbuffer.normal);
        deleteTexture(gbuffer.material);
        deleteTexture(gbuffer.velocity);
        deleteTexture(gbuffer.depth);
        gbuffer = {};
    }

    bool createGBuffer()
    {
        gbuffer.albedo = createTexture2D(width, height, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE);
        gbuffer.normal = createTexture2D(width, height, GL_RGBA16F, GL_RGBA, GL_FLOAT);
        gbuffer.material = createTexture2D(width, height, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE);
        gbuffer.velocity = createTexture2D(width, height, GL_RG16F, GL_RG, GL_FLOAT);
        gbuffer.depth = createTexture2D(width, height, GL_DEPTH_COMPONENT32F, GL_DEPTH_COMPONENT, GL_FLOAT);
        if (!gbuffer.albedo || !gbuffer.normal || !gbuffer.material || !gbuffer.velocity || !gbuffer.depth) return false;

        GL30.glGenFramebuffers(1, &gbuffer.framebuffer);
        GL30.glBindFramebuffer(GL_FRAMEBUFFER, gbuffer.framebuffer);
        GL30.glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, gbuffer.albedo, 0);
        GL30.glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, gbuffer.normal, 0);
        GL30.glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT2, GL_TEXTURE_2D, gbuffer.material, 0);
        GL30.glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT3, GL_TEXTURE_2D, gbuffer.velocity, 0);
        GL30.glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, gbuffer.depth, 0);
        const GLenum attachments[] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2, GL_COLOR_ATTACHMENT3};
        GL20.glDrawBuffers(4, attachments);
        const bool complete = GL30.glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
        GL30.glBindFramebuffer(GL_FRAMEBUFFER, 0u);
        if (!complete) std::fprintf(stderr, "[GI]: GBuffer framebuffer incomplete\n");
        return complete;
    }

    void destroyGiBuffers()
    {
        deleteTexture(buffers.raw);
        for (GLuint& texture : buffers.history) deleteTexture(texture);
        for (GLuint& texture : buffers.moments) deleteTexture(texture);
        for (GLuint& texture : buffers.geometry) deleteTexture(texture);
        deleteTexture(buffers.denoised);
        deleteTexture(buffers.temporary);
        deleteTexture(buffers.hit_distance);
        buffers = {};
    }

    bool createGiBuffers()
    {
        buffers.raw = createTexture2D(gi_width, gi_height, GL_RGBA16F, GL_RGBA, GL_FLOAT);
        for (GLuint& texture : buffers.history) texture = createTexture2D(gi_width, gi_height, GL_RGBA16F, GL_RGBA, GL_FLOAT);
        for (GLuint& texture : buffers.moments) texture = createTexture2D(gi_width, gi_height, GL_RG16F, GL_RG, GL_FLOAT);
        for (GLuint& texture : buffers.geometry) texture = createTexture2D(gi_width, gi_height, GL_RGBA16F, GL_RGBA, GL_FLOAT);
        buffers.denoised = createTexture2D(gi_width, gi_height, GL_RGBA16F, GL_RGBA, GL_FLOAT);
        buffers.temporary = createTexture2D(gi_width, gi_height, GL_RGBA16F, GL_RGBA, GL_FLOAT);
        buffers.hit_distance = createTexture2D(gi_width, gi_height, GL_R16F, GL_RED, GL_FLOAT);
        const bool valid = buffers.raw && buffers.history[0] && buffers.history[1] && buffers.moments[0] && buffers.moments[1]
            && buffers.geometry[0] && buffers.geometry[1] && buffers.denoised && buffers.temporary && buffers.hit_distance;
        if (!valid) return false;

        const float zero[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        for (GLuint texture : buffers.history) {
            GL30.glGenFramebuffers(1, &gbuffer.framebuffer);
            GL30.glBindFramebuffer(GL_FRAMEBUFFER, gbuffer.framebuffer);
            GL30.glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
            GL30.glClearBufferfv(GL_COLOR, 0, zero);
            GL30.glDeleteFramebuffers(1, &gbuffer.framebuffer);
            gbuffer.framebuffer = 0u;
        }
        GL30.glBindFramebuffer(GL_FRAMEBUFFER, 0u);
        return true;
    }

    void destroySurface()
    {
        deleteTexture(surface.radiance);
        surface = {};
    }

    bool createSurface()
    {
        surface.radiance = createTexture2D(gi_width, gi_height, GL_RGBA16F, GL_RGBA, GL_FLOAT);
        return surface.radiance != 0u;
    }

    void destroyPrograms()
    {
        if (programs.gbuffer) GL20.glDeleteProgram(programs.gbuffer);
        if (programs.surface) GL20.glDeleteProgram(programs.surface);
        if (programs.trace) GL20.glDeleteProgram(programs.trace);
        if (programs.temporal) GL20.glDeleteProgram(programs.temporal);
        if (programs.denoise) GL20.glDeleteProgram(programs.denoise);
        if (programs.compose) GL20.glDeleteProgram(programs.compose);
        programs = {};
    }

    bool createPrograms()
    {
        programs.gbuffer = createGraphicsProgram(kGBufferVertexShader, kGBufferFragmentShader);
        programs.surface = createComputeProgram(kSurfaceCacheShader);
        programs.trace = createComputeProgram(kTraceShader);
        programs.temporal = createComputeProgram(kTemporalShader);
        programs.denoise = createComputeProgram(kDenoiseShader);
        programs.compose = createGraphicsProgram(kComposeVertexShader, kComposeFragmentShader);
        return programs.gbuffer && programs.surface && programs.trace && programs.temporal && programs.denoise && programs.compose;
    }

    template <typename T>
    void uploadBuffer(GLuint& buffer, const std::vector<T>& values, GLenum usage)
    {
        if (buffer == 0u) GL15.glGenBuffers(1, &buffer);
        GL15.glBindBuffer(GL_SHADER_STORAGE_BUFFER, buffer);
        if (values.empty()) {
            const std::uint32_t zero = 0u;
            GL15.glBufferData(GL_SHADER_STORAGE_BUFFER, static_cast<LWCGLsizeiptr>(sizeof(zero)), &zero, usage);
        } else {
            GL15.glBufferData(
                GL_SHADER_STORAGE_BUFFER,
                static_cast<LWCGLsizeiptr>(values.size() * sizeof(T)),
                values.data(),
                usage
            );
        }
        GL15.glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0u);
    }

    void buildBlas(std::uint32_t mesh_handle, Blas& blas)
    {
        const Models::MeshData *mesh = Models::mesh(mesh_handle);
        if (!mesh || mesh->indices.size() < 3u) return;

        std::vector<BuildTriangle> build;
        build.reserve(mesh->indices.size() / 3u);
        for (std::size_t i = 0; i + 2u < mesh->indices.size(); i += 3u) {
            const std::uint32_t ia = mesh->indices[i];
            const std::uint32_t ib = mesh->indices[i + 1u];
            const std::uint32_t ic = mesh->indices[i + 2u];
            if (ia >= mesh->vertices.size() || ib >= mesh->vertices.size() || ic >= mesh->vertices.size()) continue;
            const Models::Vertex& a = mesh->vertices[ia];
            const Models::Vertex& b = mesh->vertices[ib];
            const Models::Vertex& c = mesh->vertices[ic];

            BuildTriangle item{};
            item.triangle.p0 = {a.position.x, a.position.y, a.position.z, 0.0f};
            item.triangle.p1 = {b.position.x, b.position.y, b.position.z, 0.0f};
            item.triangle.p2 = {c.position.x, c.position.y, c.position.z, 0.0f};
            item.triangle.n0 = {a.normal.x, a.normal.y, a.normal.z, 0.0f};
            item.triangle.n1 = {b.normal.x, b.normal.y, b.normal.z, 0.0f};
            item.triangle.n2 = {c.normal.x, c.normal.y, c.normal.z, 0.0f};
            item.triangle.uv01 = {a.uv.x, a.uv.y, b.uv.x, b.uv.y};
            item.triangle.uv2 = {c.uv.x, c.uv.y, 0.0f, 0.0f};
            const Vec3 pa{a.position.x, a.position.y, a.position.z};
            const Vec3 pb{b.position.x, b.position.y, b.position.z};
            const Vec3 pc{c.position.x, c.position.y, c.position.z};
            item.minimum = minimum(pa, minimum(pb, pc));
            item.maximum = maximum(pa, maximum(pb, pc));
            item.centroid = scale(add(add(pa, pb), pc), 1.0f / 3.0f);
            build.push_back(item);
        }
        if (build.empty()) return;

        blas.nodes.clear();
        blas.nodes.emplace_back();

        const auto build_node = [&](auto&& self, std::uint32_t node_index, std::uint32_t first, std::uint32_t count) -> void {
            Vec3 bounds_min{std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max()};
            Vec3 bounds_max{-std::numeric_limits<float>::max(), -std::numeric_limits<float>::max(), -std::numeric_limits<float>::max()};
            Vec3 centroid_min = bounds_min;
            Vec3 centroid_max = bounds_max;
            for (std::uint32_t i = first; i < first + count; ++i) {
                bounds_min = minimum(bounds_min, build[i].minimum);
                bounds_max = maximum(bounds_max, build[i].maximum);
                centroid_min = minimum(centroid_min, build[i].centroid);
                centroid_max = maximum(centroid_max, build[i].centroid);
            }

            BvhNode& node = blas.nodes[node_index];
            node.min_x = bounds_min.x; node.min_y = bounds_min.y; node.min_z = bounds_min.z;
            node.max_x = bounds_max.x; node.max_y = bounds_max.y; node.max_z = bounds_max.z;

            const Vec3 extent{centroid_max.x - centroid_min.x, centroid_max.y - centroid_min.y, centroid_max.z - centroid_min.z};
            int axis = 0;
            if (extent.y > extent.x) axis = 1;
            if (component(extent, 2) > component(extent, axis)) axis = 2;
            if (count <= kBlasLeafSize || component(extent, axis) <= 1.0e-6f) {
                node.left_first = first;
                node.count = count;
                return;
            }

            const std::uint32_t middle = first + count / 2u;
            std::nth_element(
                build.begin() + first,
                build.begin() + middle,
                build.begin() + first + count,
                [axis](const BuildTriangle& lhs, const BuildTriangle& rhs) {
                    return component(lhs.centroid, axis) < component(rhs.centroid, axis);
                }
            );

            const std::uint32_t left = static_cast<std::uint32_t>(blas.nodes.size());
            blas.nodes.emplace_back();
            blas.nodes.emplace_back();
            blas.nodes[node_index].left_first = left;
            blas.nodes[node_index].count = 0u;
            self(self, left, first, middle - first);
            self(self, left + 1u, middle, first + count - middle);
        };
        build_node(build_node, 0u, 0u, static_cast<std::uint32_t>(build.size()));

        blas.triangles.clear();
        blas.triangles.reserve(build.size());
        for (const BuildTriangle& item : build) blas.triangles.push_back(item.triangle);
    }

    void ensureBlas(std::uint32_t mesh_handle)
    {
        if (blas_cache.find(mesh_handle) != blas_cache.end()) return;
        Blas blas;
        buildBlas(mesh_handle, blas);
        blas_cache.emplace(mesh_handle, std::move(blas));
        blas_buffers_dirty = true;
    }

    void uploadBlasBuffers()
    {
        if (!blas_buffers_dirty) return;
        global_blas_nodes.clear();
        global_triangles.clear();
        blas_metadata.clear();

        std::uint32_t meta_index = 0u;
        for (auto& [mesh_handle, blas] : blas_cache) {
            (void)mesh_handle;
            blas.meta_index = meta_index++;
            const std::uint32_t node_offset = static_cast<std::uint32_t>(global_blas_nodes.size());
            const std::uint32_t triangle_offset = static_cast<std::uint32_t>(global_triangles.size());
            global_blas_nodes.insert(global_blas_nodes.end(), blas.nodes.begin(), blas.nodes.end());
            global_triangles.insert(global_triangles.end(), blas.triangles.begin(), blas.triangles.end());
            blas_metadata.push_back({
                node_offset,
                triangle_offset,
                static_cast<std::uint32_t>(blas.nodes.size()),
                static_cast<std::uint32_t>(blas.triangles.size())
            });
        }

        uploadBuffer(blas_node_buffer, global_blas_nodes, GL_STATIC_DRAW);
        uploadBuffer(triangle_buffer, global_triangles, GL_STATIC_DRAW);
        uploadBuffer(blas_metadata_buffer, blas_metadata, GL_STATIC_DRAW);
        blas_buffers_dirty = false;
    }

    void rebuildTlas(const Ecs::World& world)
    {
        std::vector<BuildInstance> build;
        build.reserve(world.entities().size());

        for (const Ecs::Entity entity : world.entities()) {
            const Ecs::RenderableComponent *renderable = world.getRenderable(entity);
            const Ecs::MeshComponent *mesh_component = world.getMesh(entity);
            const Ecs::TransformComponent *transform = world.getTransform(entity);
            if (!renderable || !renderable->visible || !mesh_component || !transform) continue;

            const auto found = blas_cache.find(mesh_component->mesh);
            const Models::MeshData *mesh = Models::mesh(mesh_component->mesh);
            if (found == blas_cache.end() || !mesh || found->second.nodes.empty()) continue;

            const Mat4 object_to_world = modelMatrix(*transform);
            const Mat4 world_to_object = inverseModelMatrix(*transform);
            const Vec3 local_min{mesh->bounds.minimum.x, mesh->bounds.minimum.y, mesh->bounds.minimum.z};
            const Vec3 local_max{mesh->bounds.maximum.x, mesh->bounds.maximum.y, mesh->bounds.maximum.z};
            Vec3 world_min{std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max()};
            Vec3 world_max{-std::numeric_limits<float>::max(), -std::numeric_limits<float>::max(), -std::numeric_limits<float>::max()};
            for (int corner = 0; corner < 8; ++corner) {
                const Vec3 local{
                    (corner & 1) ? local_max.x : local_min.x,
                    (corner & 2) ? local_max.y : local_min.y,
                    (corner & 4) ? local_max.z : local_min.z,
                };
                const Vec3 world_point = transformPoint(object_to_world, local);
                world_min = minimum(world_min, world_point);
                world_max = maximum(world_max, world_point);
            }

            BuildInstance item{};
            item.instance.object_to_world = object_to_world;
            item.instance.world_to_object = world_to_object;
            item.instance.bounds_min = {world_min.x, world_min.y, world_min.z, 0.0f};
            item.instance.bounds_max = {world_max.x, world_max.y, world_max.z, 0.0f};
            item.instance.meta = {found->second.meta_index, mesh_component->material, 0u, 0u};
            const Models::MaterialData *material = Models::material(mesh_component->material);
            item.instance.color_opacity = material
                ? std::array<float, 4>{material->color.x, material->color.y, material->color.z, material->opacity}
                : std::array<float, 4>{1.0f, 1.0f, 1.0f, 1.0f};
            item.minimum = world_min;
            item.maximum = world_max;
            item.centroid = scale(add(world_min, world_max), 0.5f);
            build.push_back(item);
        }

        tlas_nodes.clear();
        tlas_instances.clear();
        if (build.empty()) return;
        tlas_nodes.emplace_back();

        const auto build_node = [&](auto&& self, std::uint32_t node_index, std::uint32_t first, std::uint32_t count) -> void {
            Vec3 bounds_min{std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max()};
            Vec3 bounds_max{-std::numeric_limits<float>::max(), -std::numeric_limits<float>::max(), -std::numeric_limits<float>::max()};
            Vec3 centroid_min = bounds_min;
            Vec3 centroid_max = bounds_max;
            for (std::uint32_t i = first; i < first + count; ++i) {
                bounds_min = minimum(bounds_min, build[i].minimum);
                bounds_max = maximum(bounds_max, build[i].maximum);
                centroid_min = minimum(centroid_min, build[i].centroid);
                centroid_max = maximum(centroid_max, build[i].centroid);
            }

            BvhNode& node = tlas_nodes[node_index];
            node.min_x = bounds_min.x; node.min_y = bounds_min.y; node.min_z = bounds_min.z;
            node.max_x = bounds_max.x; node.max_y = bounds_max.y; node.max_z = bounds_max.z;
            const Vec3 extent{centroid_max.x - centroid_min.x, centroid_max.y - centroid_min.y, centroid_max.z - centroid_min.z};
            int axis = 0;
            if (extent.y > extent.x) axis = 1;
            if (component(extent, 2) > component(extent, axis)) axis = 2;
            if (count <= kTlasLeafSize || component(extent, axis) <= 1.0e-6f) {
                node.left_first = first;
                node.count = count;
                return;
            }

            const std::uint32_t middle = first + count / 2u;
            std::nth_element(
                build.begin() + first,
                build.begin() + middle,
                build.begin() + first + count,
                [axis](const BuildInstance& lhs, const BuildInstance& rhs) {
                    return component(lhs.centroid, axis) < component(rhs.centroid, axis);
                }
            );
            const std::uint32_t left = static_cast<std::uint32_t>(tlas_nodes.size());
            tlas_nodes.emplace_back();
            tlas_nodes.emplace_back();
            tlas_nodes[node_index].left_first = left;
            tlas_nodes[node_index].count = 0u;
            self(self, left, first, middle - first);
            self(self, left + 1u, middle, first + count - middle);
        };
        build_node(build_node, 0u, 0u, static_cast<std::uint32_t>(build.size()));
        tlas_instances.reserve(build.size());
        for (const BuildInstance& item : build) tlas_instances.push_back(item.instance);
    }

    void updateAccelerationStructures(const Ecs::World& world)
    {
        for (const Ecs::Entity entity : world.entities()) {
            const Ecs::RenderableComponent *renderable = world.getRenderable(entity);
            const Ecs::MeshComponent *mesh = world.getMesh(entity);
            if (renderable && renderable->visible && mesh) ensureBlas(mesh->mesh);
        }
        uploadBlasBuffers();
        rebuildTlas(world);
        uploadBuffer(tlas_node_buffer, tlas_nodes, GL_DYNAMIC_DRAW);
        uploadBuffer(tlas_instance_buffer, tlas_instances, GL_DYNAMIC_DRAW);
    }

    void beginGBuffer()
    {
        GL30.glBindFramebuffer(GL_FRAMEBUFFER, gbuffer.framebuffer);
        const GLenum attachments[] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2, GL_COLOR_ATTACHMENT3};
        GL20.glDrawBuffers(4, attachments);
        glViewport(0, 0, width, height);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        GL20.glUseProgram(programs.gbuffer);
        setInt(programs.gbuffer, "uDiffuse", 0);
        setMatrix(programs.gbuffer, "uInverseView", inverse_view);
        setMatrix(programs.gbuffer, "uPreviousViewProjection", previous_view_projection);
    }

    void endGBuffer()
    {
        GL20.glUseProgram(0u);
        GL30.glBindFramebuffer(GL_FRAMEBUFFER, 0u);
        GL42.glMemoryBarrier(GL_FRAMEBUFFER_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);
    }

    void updateSurface()
    {
        GL20.glUseProgram(programs.surface);
        bindTextureUnit(0, gbuffer.albedo);
        bindTextureUnit(1, gbuffer.normal);
        bindTextureUnit(2, gbuffer.depth);
        setInt(programs.surface, "uAlbedo", 0);
        setInt(programs.surface, "uNormal", 1);
        setInt(programs.surface, "uDepth", 2);
        const GLint size_location = GL20.glGetUniformLocation(programs.surface, "uOutputSize");
        if (size_location >= 0) GL20.glUniform2f(size_location, static_cast<float>(gi_width), static_cast<float>(gi_height));
        GL42.glBindImageTexture(0, surface.radiance, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA16F);
        GL43.glDispatchCompute(static_cast<GLuint>((gi_width + 7) / 8), static_cast<GLuint>((gi_height + 7) / 8), 1u);
        GL42.glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);
    }

    void traceGi()
    {
        GL20.glUseProgram(programs.trace);
        bindTextureUnit(0, gbuffer.albedo);
        bindTextureUnit(1, gbuffer.normal);
        bindTextureUnit(2, gbuffer.depth);
        bindTextureUnit(3, surface.radiance);
        setInt(programs.trace, "uAlbedo", 0);
        setInt(programs.trace, "uNormal", 1);
        setInt(programs.trace, "uDepth", 2);
        setInt(programs.trace, "uSurface", 3);
        setMatrix(programs.trace, "uViewProjection", current_view_projection);
        setMatrix(programs.trace, "uInverseViewProjection", inverse_view_projection);
        const GLint size_location = GL20.glGetUniformLocation(programs.trace, "uOutputSize");
        if (size_location >= 0) GL20.glUniform2f(size_location, static_cast<float>(gi_width), static_cast<float>(gi_height));
        setInt(programs.trace, "uTlasNodeCount", static_cast<int>(tlas_nodes.size()));
        setInt(programs.trace, "uUseScreen", settings.screen_space_first ? 1 : 0);
        setInt(programs.trace, "uUseBvh", settings.bvh_fallback ? 1 : 0);
        setInt(programs.trace, "uUseSurface", settings.surface_cache ? 1 : 0);
        setInt(programs.trace, "uRaysPerPixel", std::clamp(settings.rays_per_pixel, 1, 4));
        setInt(programs.trace, "uMaxBounces", std::clamp(settings.max_bounces, 1, 4));
        const GLint frame_location = GL20.glGetUniformLocation(programs.trace, "uFrame");
        if (frame_location >= 0) GL20.glUniform1i(frame_location, static_cast<GLint>(frame & 0x7fffffffu));

        GL30.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0u, blas_node_buffer);
        GL30.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1u, triangle_buffer);
        GL30.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2u, tlas_node_buffer);
        GL30.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3u, tlas_instance_buffer);
        GL30.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4u, blas_metadata_buffer);
        GL42.glBindImageTexture(0, buffers.raw, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA16F);
        GL42.glBindImageTexture(1, buffers.hit_distance, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_R16F);
        GL43.glDispatchCompute(static_cast<GLuint>((gi_width + 7) / 8), static_cast<GLuint>((gi_height + 7) / 8), 1u);
        GL42.glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);
    }

    GLuint temporalGi()
    {
        const int next = history_index ^ 1;
        GL20.glUseProgram(programs.temporal);
        bindTextureUnit(0, buffers.raw);
        bindTextureUnit(1, buffers.history[history_index]);
        bindTextureUnit(2, buffers.moments[history_index]);
        bindTextureUnit(3, buffers.geometry[history_index]);
        bindTextureUnit(4, gbuffer.depth);
        bindTextureUnit(5, gbuffer.normal);
        bindTextureUnit(6, gbuffer.velocity);
        setInt(programs.temporal, "uRaw", 0);
        setInt(programs.temporal, "uPreviousHistory", 1);
        setInt(programs.temporal, "uPreviousMoments", 2);
        setInt(programs.temporal, "uPreviousGeometry", 3);
        setInt(programs.temporal, "uDepth", 4);
        setInt(programs.temporal, "uNormal", 5);
        setInt(programs.temporal, "uVelocity", 6);
        const GLint size_location = GL20.glGetUniformLocation(programs.temporal, "uOutputSize");
        if (size_location >= 0) GL20.glUniform2f(size_location, static_cast<float>(gi_width), static_cast<float>(gi_height));
        setFloat(programs.temporal, "uAlpha", settings.temporal_alpha);
        setFloat(programs.temporal, "uDepthReject", settings.depth_rejection);
        setFloat(programs.temporal, "uNormalReject", settings.normal_rejection);
        setInt(programs.temporal, "uHasHistory", frame > 0u ? 1 : 0);
        GL42.glBindImageTexture(0, buffers.history[next], 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA16F);
        GL42.glBindImageTexture(1, buffers.moments[next], 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RG16F);
        GL42.glBindImageTexture(2, buffers.geometry[next], 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA16F);
        GL43.glDispatchCompute(static_cast<GLuint>((gi_width + 7) / 8), static_cast<GLuint>((gi_height + 7) / 8), 1u);
        GL42.glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);
        return buffers.history[next];
    }

    GLuint denoiseGi(GLuint source)
    {
        const int moment_index = settings.temporal_reuse ? (history_index ^ 1) : history_index;
        GLuint input = source;
        GLuint output = buffers.denoised;
        const int iterations = std::clamp(settings.denoise_iterations, 1, 6);
        for (int iteration = 0; iteration < iterations; ++iteration) {
            output = (iteration & 1) == 0 ? buffers.denoised : buffers.temporary;
            GL20.glUseProgram(programs.denoise);
            bindTextureUnit(0, input);
            bindTextureUnit(1, gbuffer.depth);
            bindTextureUnit(2, gbuffer.normal);
            bindTextureUnit(3, buffers.moments[moment_index]);
            setInt(programs.denoise, "uInput", 0);
            setInt(programs.denoise, "uDepth", 1);
            setInt(programs.denoise, "uNormal", 2);
            setInt(programs.denoise, "uMoments", 3);
            const GLint size_location = GL20.glGetUniformLocation(programs.denoise, "uOutputSize");
            if (size_location >= 0) GL20.glUniform2f(size_location, static_cast<float>(gi_width), static_cast<float>(gi_height));
            setInt(programs.denoise, "uStep", 1 << iteration);
            GL42.glBindImageTexture(0, output, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA16F);
            GL43.glDispatchCompute(static_cast<GLuint>((gi_width + 7) / 8), static_cast<GLuint>((gi_height + 7) / 8), 1u);
            GL42.glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);
            input = output;
        }
        return output;
    }

    void compose(GLuint gi_texture)
    {
        GL30.glBindFramebuffer(GL_FRAMEBUFFER, 0u);
        glViewport(0, 0, width, height);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        glDisable(GL_LIGHTING);
        glDisable(GL_BLEND);
        GL20.glUseProgram(programs.compose);
        bindTextureUnit(0, gbuffer.albedo);
        bindTextureUnit(1, gbuffer.normal);
        bindTextureUnit(2, gbuffer.depth);
        bindTextureUnit(3, gi_texture);
        setInt(programs.compose, "uAlbedo", 0);
        setInt(programs.compose, "uNormal", 1);
        setInt(programs.compose, "uDepth", 2);
        setInt(programs.compose, "uGi", 3);
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

    void deleteBuffers()
    {
        const GLuint handles[] = {
            blas_node_buffer,
            triangle_buffer,
            blas_metadata_buffer,
            tlas_node_buffer,
            tlas_instance_buffer,
        };
        for (const GLuint handle : handles) {
            if (handle != 0u) GL15.glDeleteBuffers(1, &handle);
        }
        blas_node_buffer = 0u;
        triangle_buffer = 0u;
        blas_metadata_buffer = 0u;
        tlas_node_buffer = 0u;
        tlas_instance_buffer = 0u;
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
    if (major < 4 || (major == 4 && minor < 3) || !GL43.glDispatchCompute || !GL42.glBindImageTexture) {
        std::fprintf(stderr, "[GI]: OpenGL 4.3 compatibility context required; found %d.%d\n", major, minor);
        return false;
    }

    if (!impl_->createPrograms() || !impl_->createGBuffer() || !impl_->createGiBuffers() || !impl_->createSurface()) {
        shutdown();
        return false;
    }

    impl_->history_index = 0;
    impl_->frame = 0u;
    impl_->initialized = true;
    return true;
}

void GI::resize(int width, int height)
{
    impl_->width = std::max(width, 1);
    impl_->height = std::max(height, 1);
    impl_->updateResolution();
    if (!impl_->initialized) return;

    impl_->destroyGBuffer();
    impl_->destroyGiBuffers();
    impl_->destroySurface();
    if (!impl_->createGBuffer() || !impl_->createGiBuffers() || !impl_->createSurface()) {
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
    impl_->previous_view_projection = impl_->frame == 0u ? captured : impl_->current_view_projection;
    impl_->current_view_projection = captured;
    if (!inverseMatrix(captured, impl_->inverse_view_projection)) impl_->inverse_view_projection = identityMatrix();
    if (!inverseMatrix(view, impl_->inverse_view)) impl_->inverse_view = identityMatrix();

    impl_->updateAccelerationStructures(world);
    impl_->beginGBuffer();
}

void GI::bindMaterial(unsigned int texture_id)
{
    if (!impl_->active()) return;
    GL20.glUseProgram(impl_->programs.gbuffer);
    bindTextureUnit(0, static_cast<GLuint>(texture_id));
    setInt(impl_->programs.gbuffer, "uDiffuse", 0);
    setInt(impl_->programs.gbuffer, "uHasTexture", texture_id != 0u ? 1 : 0);
    glDisable(GL_BLEND);
}

void GI::end(const Ecs::World& world)
{
    (void)world;
    if (!impl_->active()) return;

    impl_->endGBuffer();
    if (impl_->settings.surface_cache) impl_->updateSurface();
    impl_->traceGi();

    GLuint result = impl_->buffers.raw;
    if (impl_->settings.temporal_reuse) result = impl_->temporalGi();
    if (impl_->settings.denoise) result = impl_->denoiseGi(result);
    impl_->buffers.final_texture = result;
    impl_->compose(result);

    if (impl_->settings.temporal_reuse) impl_->history_index ^= 1;
    ++impl_->frame;
}

void GI::shutdown()
{
    if (!impl_) return;
    GL20.glUseProgram(0u);
    impl_->destroyPrograms();
    impl_->destroySurface();
    impl_->destroyGiBuffers();
    impl_->destroyGBuffer();
    impl_->deleteBuffers();
    impl_->blas_cache.clear();
    impl_->global_blas_nodes.clear();
    impl_->global_triangles.clear();
    impl_->blas_metadata.clear();
    impl_->tlas_nodes.clear();
    impl_->tlas_instances.clear();
    impl_->initialized = false;
    impl_->frame = 0u;
    impl_->history_index = 0;
}

bool GI::initialized() const { return impl_->initialized; }
bool GI::enabled() const { return impl_->settings.enabled; }
void GI::setEnabled(bool enabled) { impl_->settings.enabled = enabled; }
GiSettings& GI::settings() { return impl_->settings; }
const GiSettings& GI::settings() const { return impl_->settings; }

} // namespace Renderer
