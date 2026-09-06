#include "Renderer/Render.hpp"

#include "Models/Models.hpp"
#include "Models/Texture.hpp"

#include <lwcgl/lwcgl.h>

#include <algorithm>
#include <cmath>

#define GI (gi_initialized_ && gi_settings_.enabled)

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

    if (!initGi()) {
        shutdownGi();
    }

    initialized_ = true;
    return true;
}

void Rasterizer::resize(int width, int height)
{
    width_ = std::max(width, 1);
    height_ = std::max(height, 1);
    glViewport(0, 0, width_, height_);

    if (GI) {
        resizeGi();
    }
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

    const auto texture_id = glGenTextures();
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

bool Rasterizer::initGI() 
{
    gi_width_ = std::max(
        width_ / std::max(gi_settings_.resolution_divisor, 1),
        1
    );

    gi_height_ = std::max(
        height_ / std::max(gi_settings_.resolution_divisor, 1),
        1
    );

    createGBuffer();
    createGiBuffers();
    createSurfaceCache();

    if (!createGiPrograms()) {
        destroySurfaceCache();
        destroyGiBuffers();
        destroyGBuffer();
        return false;
    }

    gi_history_index_ = 0;
    gi_frame_ = 0;
    gi_initialized_ = true;

    return true;
}

void Rasterizer::resizeGI()
{
    const int divisor = std::max(
        gi_settings_.resolution_divisor,
        1
    );

    gi_width_  = std::max(width_  / divisor, 1);
    gi_height_ = std::max(height_ / divisor, 1);

    destroyGBuffer();
    destroyGiBuffers();

    createGBuffer();
    createGiBuffers();

    gi_history_index_ = 0;
}

void Rasterizer::beginGiFrame(const Ecs::World& world) 
{
    (void) world;

    previous_view_projection_ = current_view_projection_;

    /* TODO
    
        current_view_projection_ =
            projection_matrix * view_matrix;
    */
}

void Rasterizer::endGiFrame()
{
    swapGiHistory();
    ++gi_frame_;
}

void Rasterizer::ensureBlas(std::uint32_t mesh_handle)
{
    auto found = blas_cache_.find(mesh_handle);

    if (found != blas_cache_.end()) {
        return;
    }

    Blas blas;

    buildBlas(mesh_handle, blas);
    uploadBlas(blas);

    blas_cache_.emplace(
        mesh_handle,
        std::move(blas)
    );
}

void Rasterizer::buildBlas(
    std::uint32_t mesh_handle,
    Blas& blas)
{
    (void)mesh_handle;
    (void)blas;

    /*  TODO

        1. Models::mesh(mesh_handle)

        2. Convert every triangle to GpuTriangle.

        3. Calculate triangle AABBs.

        4. Calculate centroids.

        5. Create root BVH node.

        6. Recursively / iteratively partition triangles.

        Good first split:
            largest AABB axis
            median centroid split

        Better later:
            binned SAH

        Leaf target:
            about 4-8 triangles

        IMPORTANT:
        BLAS uses LOCAL mesh coordinates.

        Do not bake entity transforms into triangles.
        That lets hundreds of ECS entities share one BLAS.
    */
}

void Rasterizer::uploadBlas(Blas& blas)
{
    (void)blas;

    /*  TODO
        
        Create/upload:

        blas.node_buffer
            SSBO<BvhNode>

        blas.triangle_buffer
            SSBO<GpuTriangle>

        glBufferData initially.

        Later:
            persistent mapped storage if useful.

        blas.uploaded = true;
    */
}

void Rasterizer::updateAccelerationStructures(
    const Ecs::World& world)
{
    for (const Ecs::Entity entity : world.entities()) 
    {
        const Ecs::RenderableComponent* renderable =
            world.getRenderable(entity);

        const Ecs::MeshComponent* mesh =
            world.getMesh(entity);

        if (!renderable ||
            !renderable->visible ||
            !mesh) {
            continue;
        }

        ensureBlas(mesh->mesh);
    }

    rebuildTlas(world);
    uploadTlas();
}

void Rasterizer::rebuildTlas(
    const Ecs::World& world)
{
    (void)world;

    tlas_nodes_.clear();
    tlas_instances_.clear();

    /*  TODO
        
        For each visible entity:

            TransformComponent
            MeshComponent

        Find its BLAS.

        Build TlasInstance containing:

            object_to_world
            world_to_object
            blas_index
            material
            transformed world AABB

        Then build BVH over INSTANCE AABBs.

        TLAS leaf:
            normally one/few instances.

        Dynamic objects:
            update instance transform/AABB

        Static BLAS:
            never rebuild unless mesh changes.
    */
}

void Rasterizer::uploadTlas()
{
    /*  TODO
        
        Upload/update:

        tlas_nodes_
            -> tlas_node_buffer_

        tlas_instances_
            -> tlas_instance_buffer_

        Prefer glBufferSubData / mapped memory
        once buffers are large enough.
    */
}

void Rasterizer::createGBuffer()
{
    /*  TODO

        FULL render resolution:

        framebuffer

        albedo:
            RGBA8 or SRGB8_ALPHA8

        normal:
            RG16F if octahedral encoded
            OR RGBA16F initially

        material:
            RGBA8 / RGBA16F

            suggested:
                R = roughness
                G = metallic
                B = emissive strength / flags
                A = material flags

        velocity:
            RG16F

        depth:
            DEPTH_COMPONENT32F

        Do NOT store world position.

        Reconstruct world position from:
            depth + inverse view-projection

        Saves considerable bandwidth.
    */
}

void Rasterizer::destroyGBuffer()
{
    // Delete framebuffer + textures here.

    gbuffer_ = {};
}

void Rasterizer::beginGBufferPass()
{
    /*  TODO

        Eventually:

        glBindFramebuffer(... gbuffer_.framebuffer)

        glViewport(
            0,
            0,
            width_,
            height_
        );

        clear color/depth

        bind gi_programs_.gbuffer

        upload:
            current view matrix
            current projection
            previous view-projection
    */
}

void Rasterizer::endGBufferPass()
{
    /*  TODO

        glBindFramebuffer(GL_FRAMEBUFFER, 0);

        Insert only the memory barrier actually required
        by the following compute stage.
    */
}

void Rasterizer::createSurfaceCache()
{
    surface_cache_.width = 2048;
    surface_cache_.height = 2048;

    /*  TODO

        Create atlas textures:

            albedo
            normal
            emissive
            radiance

        Plus metadata_buffer containing
        card/page allocation metadata.

        Don't update the whole cache every frame.
    */
}

void Rasterizer::updateSurfaceCache(
    const Ecs::World& world)
{
    (void)world;

    /*  TODO

        EACH FRAME UPDATE ONLY A SMALL BUDGET.

        Example:
            8-32 cache pages/cards per frame.

        Priority:

            visible
            close to camera
            stale
            high lighting change
            newly exposed

        Each cache texel should eventually represent:

            surface albedo
            world normal
            emissive
            direct illumination
            cached indirect illumination

        BVH hit -> surface cache lookup

        avoids evaluating expensive lighting recursively
        at every ray hit.
    */
}

void Rasterizer::createGiBuffers()
{
    /*  TODO

        GI resolution:

            gi_width_
            gi_height_

        Create:

            gi_buffers_.raw

            gi_buffers_.history[0]
            gi_buffers_.history[1]

            gi_buffers_.moments[0]
            gi_buffers_.moments[1]

            gi_buffers_.denoised

            gi_buffers_.hit_distance

        Recommended:

            radiance:
                RGBA16F

            moments:
                RG16F

            hit_distance:
                R16F / R32F
    */
}

void Rasterizer::destroyGiBuffers()
{
    // Delete GI textures here.

    gi_buffers_ = {};
}

void Rasterizer::traceGi(
    const Ecs::World& world)
{
    (void)world;

    /*  TODO

        One invocation per GI pixel.


        1. READ GBUFFER

        depth
        normal
        albedo
        material

        reconstruct world position from depth.


        2. GENERATE STOCHASTIC DIFFUSE DIRECTION

        cosine-weighted hemisphere around world normal.

        Seed from:
            pixel
            frame index
            blue-noise texture


        3. SCREEN-SPACE TRACE FIRST

        if gi_settings_.screen_space_first:

            march ray through depth buffer.

        If valid hit:
            consume hit cheaply.


        4. BVH FALLBACK

        if screen trace misses:

            traverse TLAS

            transform ray into BLAS local coordinates

            traverse BLAS

            intersect leaf triangles

            return nearest hit.


        5. SHADE HIT

        Prefer:

            surface-cache radiance lookup

        instead of recursively evaluating
        all lights/materials.


        6. MISS

        sample:
            sky
            environment
            ambient radiance


        7. WRITE

        gi_buffers_.raw

        gi_buffers_.hit_distance

        Start with:
            1 ray per GI pixel
            1 bounce
            half resolution
    */
}

void Rasterizer::temporalGi()
{
    /*  TODO

        current:
            gi_buffers_.raw

        previous:
            gi_buffers_.history[
                gi_history_index_
            ]


        REPROJECT

        Use:
            velocity buffer

        OR:
            current/previous view-projection matrices.


        VALIDATE HISTORY

        reject when:

            depth difference too large
            normal difference too large
            outside framebuffer
            geometry newly visible
            motion/disocclusion invalid


        ACCUMULATE

        history =
            mix(
                previous,
                current,
                temporal_alpha
            );

        Track luminance moments:

            first moment
            second moment

        Needed for variance estimation.

        Never blindly accumulate invalid history.
    */
}

void Rasterizer::denoiseGi()
{
    /*  TODO

        edge-aware A-Trous / wavelet filter.

        Inputs:

            temporal GI
            depth
            normal
            albedo
            variance
            hit distance

        Example iterations:

            step = 1
            step = 2
            step = 4
            step = 8

        Weight neighbors by:

            normal similarity
            depth similarity
            luminance similarity
            hit-distance similarity

        Ping-pong temporary GI textures.

        Final result:
            gi_buffers_.denoised
    */
}

void Rasterizer::composeGi()
{
    /*  TODO

        final =

            emissive
            +
            direct_lighting
            +
            albedo * indirect_diffuse

        Later:

            specular GI
            reflections
            AO
            fog
            tone mapping

        IMPORTANT:

        don't multiply indirect radiance by albedo twice.
    */
}

void Rasterizer::swapGiHistory()
{
    gi_history_index_ ^= 1;
}

bool Rasterizer::createGiPrograms()
{
    /*  TODO

        Compile/link:

        gbuffer
            vertex + fragment

        surface_cache
            vertex/fragment or compute

        trace
            compute

        temporal
            compute

        denoise
            compute

        compose
            fullscreen vertex + fragment

        Later you may want a reusable shader compiler,
        but don't build another abstraction yet.
    */

    return true;
}

void Rasterizer::destroyGiPrograms()
{
    // glDeleteProgram(...)

    gi_programs_ = {};
}

void Rasterizer::destroySurfaceCache()
{
    /*  TODO
     
        Delete:

            framebuffer
            albedo
            normal
            emissive
            radiance
            metadata buffer
    */

    surface_cache_ = {};
}

void Rasterizer::shutdownGi()
{
    if (!gi_initialized_) {
        return;
    }

    destroyGiPrograms();
    destroySurfaceCache();
    destroyGiBuffers();
    destroyGBuffer();

    for (auto& [mesh, blas] : blas_cache_) {
        (void)mesh;

        /*
            Delete:
                blas.node_buffer
                blas.triangle_buffer
        */
    }

    blas_cache_.clear();

    /*
        Delete:
            tlas_node_buffer_
            tlas_instance_buffer_
    */

    tlas_nodes_.clear();
    tlas_instances_.clear();

    tlas_node_buffer_ = 0;
    tlas_instance_buffer_ = 0;

    gi_initialized_ = false;
    gi_frame_ = 0;
}

void Rasterizer::render(const Ecs::World& world)
{
    if (!initialized_) return;

    if (GI) {
        beginGiFrame(world);
        updateAccelerationStructures(world);
    }

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

    if (gi_initialized_ && gi_settings_.enabled) {
        beginGBufferPass();
    }
    
    for (const Ecs::Entity entity : world.entities()) 
    {
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
        
    if (GI) {
        endGBufferPass();

        if (gi_settings_.surface_cache) {
            updateSurfaceCache(world);
        }

        traceGi(world);

        if (gi_settings_.temporal_reuse) {
            temporalGi();
        }

        if (gi_settings_.denoise) {
            denoiseGi();
        }
        
        composeGi();
        endGiFrame();
    }    

    glDisable(GL_TEXTURE_2D);
    glDisable(GL_BLEND);
}

void Rasterizer::shutdown()
{
    shutdownGi();
    for (const auto& [handle, texture_id] : textures_) {
        (void)handle;
        const GLuint id = static_cast<GLuint>(texture_id);
        if (id != 0u) glDeleteTextures(id);
    }
    textures_.clear();
    initialized_ = false;
}

} // namespace Renderer
