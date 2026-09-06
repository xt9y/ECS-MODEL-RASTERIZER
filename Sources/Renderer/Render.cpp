#include "Renderer/Render.hpp"

#include "Models/Models.hpp"
#include "Models/Texture.hpp"

#include <lwcgl/lwcgl.h>

#include <algorithm>
#include <cmath>

namespace Renderer {
namespace {

void applyCamera(const Ecs::World& world)
{
    const Ecs::Entity camera_entity = world.activeCamera();
    if (camera_entity == Ecs::INVALID_ENTITY) return;

    const Ecs::TransformComponent *transform = world.getTransform(camera_entity);
    if (!transform) return;

    glRotatef(-transform->rotation.x, 1.0f, 0.0f, 0.0f);
    glRotatef(-transform->rotation.y, 0.0f, 1.0f, 0.0f);
    glRotatef(-transform->rotation.z, 0.0f, 0.0f, 1.0f);
    glTranslatef(-transform->position.x, -transform->position.y, -transform->position.z);
}

void applyTransform(const Ecs::TransformComponent& transform)
{
    glTranslatef(transform.position.x, transform.position.y, transform.position.z);
    glRotatef(transform.rotation.x, 1.0f, 0.0f, 0.0f);
    glRotatef(transform.rotation.y, 0.0f, 1.0f, 0.0f);
    glRotatef(transform.rotation.z, 0.0f, 0.0f, 1.0f);
    glScalef(transform.scale.x, transform.scale.y, transform.scale.z);
}

void applyInfinitePerspective(float fov_degrees, float aspect, float near_plane)
{
    constexpr float pi = 3.14159265358979323846f;
    const float safe_fov = std::clamp(fov_degrees, 1.0f, 179.0f);
    const float safe_aspect = aspect > 1.0e-6f ? aspect : 1.0f;
    const float safe_near = std::max(near_plane, 1.0e-4f);
    const float focal = 1.0f / std::tan(safe_fov * (pi / 360.0f));

    const GLfloat projection[16] = {
        focal / safe_aspect, 0.0f, 0.0f, 0.0f,
        0.0f, focal, 0.0f, 0.0f,
        0.0f, 0.0f, -1.0f, -1.0f,
        0.0f, 0.0f, -2.0f * safe_near, 0.0f,
    };
    glLoadMatrixf(projection);
}

} // namespace

bool Rasterizer::init()
{
    if (initialized_) return true;

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glEnable(GL_NORMALIZE);
    glEnable(GL_LIGHTING);
    glEnable(GL_LIGHT0);
    glEnable(GL_COLOR_MATERIAL);
    glColorMaterial(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE);
    glShadeModel(GL_SMOOTH);

    const GLfloat scene_ambient[] = {
        LightingDefaults::scene_ambient,
        LightingDefaults::scene_ambient,
        LightingDefaults::scene_ambient,
        1.0f,
    };
    const GLfloat no_light_ambient[] = {0.0f, 0.0f, 0.0f, 1.0f};
    const GLfloat diffuse[] = {
        LightingDefaults::direct_diffuse,
        LightingDefaults::direct_diffuse,
        LightingDefaults::direct_diffuse,
        1.0f,
    };

    glLightModelfv(GL_LIGHT_MODEL_AMBIENT, scene_ambient);
    glLightfv(GL_LIGHT0, GL_AMBIENT, no_light_ambient);
    glLightfv(GL_LIGHT0, GL_DIFFUSE, diffuse);

    glClearColor(0.035f, 0.035f, 0.045f, 1.0f);

    (void)gi_.init(width_, height_);

    initialized_ = true;
    return true;
}

void Rasterizer::resize(int width, int height)
{
    width_ = std::max(width, 1);
    height_ = std::max(height, 1);
    glViewport(0, 0, width_, height_);
    gi_.resize(width_, height_);
}

unsigned int Rasterizer::textureFor(std::uint32_t handle)
{
    if (handle == Models::INVALID_TEXTURE) return 0u;

    const auto found = textures_.find(handle);
    if (found != textures_.end()) return found->second;

    const Models::TextureAsset *asset = Models::texture(handle);
    if (!asset || asset->image.width <= 0 || asset->image.height <= 0 || asset->image.rgba.empty()) {
        return 0u;
    }

    const GLuint texture_id = glGenTextures();
    if (texture_id == 0u) return 0u;

    glBindTexture(GL_TEXTURE_2D, texture_id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(
        GL_TEXTURE_2D,
        0,
        GL_RGBA,
        asset->image.width,
        asset->image.height,
        0,
        GL_RGBA,
        GL_UNSIGNED_BYTE,
        asset->image.rgba.data()
    );

    textures_.emplace(handle, texture_id);
    return texture_id;
}

void Rasterizer::render(const Ecs::World& world)
{
    if (!initialized_) return;

    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    const Ecs::Entity camera_entity = world.activeCamera();
    const Ecs::CameraComponent *camera = camera_entity == Ecs::INVALID_ENTITY
        ? nullptr
        : world.getCamera(camera_entity);

    glMatrixMode(GL_PROJECTION);
    const float aspect = static_cast<float>(width_) / static_cast<float>(height_);
    applyInfinitePerspective(
        camera ? camera->fov_degrees : 60.0f,
        aspect,
        camera ? camera->near_plane : 0.1f
    );

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    applyCamera(world);

    const GLfloat light_direction[] = {-0.35f, 0.8f, 0.45f, 0.0f};
    glLightfv(GL_LIGHT0, GL_POSITION, light_direction);

    const bool use_gi = gi_.initialized() && gi_.enabled();
    if (use_gi) gi_.begin(world);

    for (const Ecs::Entity entity : world.entities()) {
        const Ecs::RenderableComponent *renderable = world.getRenderable(entity);
        const Ecs::MeshComponent *mesh_component = world.getMesh(entity);
        const Ecs::TransformComponent *transform = world.getTransform(entity);
        if (!renderable || !renderable->visible || !mesh_component || !transform) continue;

        const Models::MeshData *mesh = Models::mesh(mesh_component->mesh);
        if (!mesh || mesh->indices.empty()) continue;

        const Models::MaterialData *material = Models::material(mesh_component->material);
        const float opacity = material ? std::clamp(material->opacity, 0.0f, 1.0f) : 1.0f;
        const unsigned int texture_id = material ? textureFor(material->diffuse_texture) : 0u;
        const Models::TextureAsset *texture_asset = material
            && material->diffuse_texture != Models::INVALID_TEXTURE
            ? Models::texture(material->diffuse_texture)
            : nullptr;
        const bool transparent = opacity < 1.0f
            || (texture_asset && texture_asset->image.meaningful_alpha);

        if (transparent) {
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        } else {
            glDisable(GL_BLEND);
        }

        if (texture_id != 0u) {
            glEnable(GL_TEXTURE_2D);
            glBindTexture(GL_TEXTURE_2D, texture_id);
            glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
        } else {
            glDisable(GL_TEXTURE_2D);
        }

        if (material) {
            glColor4f(material->color.x, material->color.y, material->color.z, opacity);
        } else {
            glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
        }

        if (use_gi) gi_.bindMaterial(texture_id);

        glPushMatrix();
        applyTransform(*transform);
        glBegin(GL_TRIANGLES);
        for (const std::uint32_t index : mesh->indices) {
            if (index >= mesh->vertices.size()) continue;
            const Models::Vertex& vertex = mesh->vertices[index];
            glNormal3f(vertex.normal.x, vertex.normal.y, vertex.normal.z);
            if (texture_id != 0u) glTexCoord2f(vertex.uv.x, 1.0f - vertex.uv.y);
            glVertex3f(vertex.position.x, vertex.position.y, vertex.position.z);
        }
        glEnd();
        glPopMatrix();
    }

    if (use_gi) gi_.end(world);

    glDisable(GL_TEXTURE_2D);
    glDisable(GL_BLEND);
}

void Rasterizer::shutdown()
{
    gi_.shutdown();

    for (const auto& [handle, texture_id] : textures_) {
        (void)handle;
        const GLuint id = static_cast<GLuint>(texture_id);
        if (id != 0u) glDeleteTextures(id);
    }
    textures_.clear();
    initialized_ = false;
}

} // namespace Renderer
