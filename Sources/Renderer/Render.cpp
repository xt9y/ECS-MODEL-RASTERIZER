#include "Renderer/Render.hpp"

#include "Models/Models.hpp"

#include <lwcgl/lwcgl.h>

#include <algorithm>

namespace RW::Renderer {
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

    const GLfloat ambient[] = {0.22f, 0.22f, 0.22f, 1.0f};
    const GLfloat diffuse[] = {0.90f, 0.90f, 0.90f, 1.0f};
    glLightfv(GL_LIGHT0, GL_AMBIENT, ambient);
    glLightfv(GL_LIGHT0, GL_DIFFUSE, diffuse);

    glClearColor(0.035f, 0.035f, 0.045f, 1.0f);
    initialized_ = true;
    return true;
}

void Rasterizer::resize(int width, int height)
{
    width_ = std::max(width, 1);
    height_ = std::max(height, 1);
    glViewport(0, 0, width_, height_);
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
    glLoadIdentity();
    const double aspect = static_cast<double>(width_) / static_cast<double>(height_);
    gluPerspective(
        camera ? camera->fov_degrees : 60.0,
        aspect,
        camera ? camera->near_plane : 0.1,
        camera ? camera->far_plane : 1000.0
    );

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    applyCamera(world);

    const GLfloat light_direction[] = {-0.35f, 0.8f, 0.45f, 0.0f};
    glLightfv(GL_LIGHT0, GL_POSITION, light_direction);

    for (const Ecs::Entity entity : world.entities()) {
        const Ecs::RenderableComponent *renderable = world.getRenderable(entity);
        const Ecs::MeshComponent *mesh_component = world.getMesh(entity);
        const Ecs::TransformComponent *transform = world.getTransform(entity);
        if (!renderable || !renderable->visible || !mesh_component || !transform) continue;

        const Models::MeshData *mesh = Models::mesh(mesh_component->mesh);
        if (!mesh || mesh->indices.empty()) continue;

        const Models::MaterialData *material = Models::material(mesh_component->material);
        const float opacity = material ? std::clamp(material->opacity, 0.0f, 1.0f) : 1.0f;

        if (opacity < 1.0f) {
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        } else {
            glDisable(GL_BLEND);
        }

        if (material) {
            glColor4f(material->color.x, material->color.y, material->color.z, opacity);
        } else {
            glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
        }

        glPushMatrix();
        applyTransform(*transform);
        glBegin(GL_TRIANGLES);
        for (const std::uint32_t index : mesh->indices) {
            if (index >= mesh->vertices.size()) continue;
            const Models::Vertex& vertex = mesh->vertices[index];
            glNormal3f(vertex.normal.x, vertex.normal.y, vertex.normal.z);
            glVertex3f(vertex.position.x, vertex.position.y, vertex.position.z);
        }
        glEnd();
        glPopMatrix();
    }

    glDisable(GL_BLEND);
}

void Rasterizer::shutdown()
{
    initialized_ = false;
}

} // namespace RW::Renderer
