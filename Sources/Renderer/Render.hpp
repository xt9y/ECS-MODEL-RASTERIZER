#ifndef RW_ENGINE_RENDER_HPP
#define RW_ENGINE_RENDER_HPP

#include "Ecs/Ecs.hpp"

#include <cstdint>
#include <unordered_map>
#include <array>
#include <vector>

namespace Renderer {

struct GiSettings {
    // Start very low. Temporal reuse does most of the work.
    // Work with half the resolution for now
    int resolution_divisor = 2,
        rays_per_pixel = 1, 
        denoise_iterations = 4,
        max_bounces = 1;

    bool enabled = true,
         screen_space_first = true,
         bvh_fallback = true,
         surface_cache = true,
         temporal_reuse = true,
         denoise = true;

    float temporal_alpha = 0.08f,
          depth_rejection = 0.02f,
          normal_rejection = 0.85f;
};

struct LightingDefaults {
    static constexpr float scene_ambient = 0.65f;
    static constexpr float direct_diffuse = 0.75f;
};

class Rasterizer {
public:
    bool init();
    void resize(int width, int height);
    void render(const Ecs::World& world);
    void shutdown();

    bool initialized() const { return initialized_; }

    GiSettings& giSettings() { return gi_settings_; }
    const GiSettings& giSettings() const { return gi_settings_; }

    void setGiEnabled(bool enabled) { gi_settings_.enabled = enabled; }
    bool giEnabled() const { return gi_settings_.enabled; }

private:
    struct BvhNode {
        float min_x = 0.0f,
              min_y = 0.0f,
              min_z = 0.0f;

        // triangle_count == 0 = interior node.
        // triangle_count  > 0 = number of triangles in leaf.
        std::uint32_t left_first = 0, triangle_count = 0;

        float max_x = 0.0f,
              max_y = 0.0f,
              max_z = 0.0f;
    };

    struct GpuTriangle {
        std::array<float, 4> p0{}, p1{}, p2{},
                             n0{}, n1{}, n2{},
                             uv01{}, uv2_material{};
    };

    struct Blas {
        std::vector<BvhNode> nodes;
        std::vector<GpuTriangle> triangles;
        unsigned int node_buffer = 0, triangle_buffer = 0;

        bool uploaded = false;
    };

    struct TlasInstance {
        std::array<float, 16> object_to_world{}, world_to_object{};
        std::array<float, 3>  bounds_min{}, bounds_max{};
        std::uint32_t blas_index = 0, material = 0;
    };

    struct GBuffer {
        unsigned int framebuffer = 0,
                     albedo = 0,
                     normal = 0,
                     material = 0,
                     velocity = 0,
                     depth = 0;
    };

    struct GiBuffers {
        unsigned int raw = 0, 
                     denoised = 0, 
                     it_distance = 0;

        std::array<unsigned int, 2> history{}, moments{};
    };

    struct SurfaceCache {
        unsigned int framebuffer = 0, 
                     albedo = 0, 
                     normal = 0, 
                     emissive = 0, 
                     radiance = 0, 
                     metadata_buffer = 0;

        int width = 0, height = 0;
    };

    struct GiPrograms {
        unsigned int gbuffer = 0, 
                     surface_cache = 0, 
                     trace = 0, 
                     temporal = 0, 
                     denoise = 0, 
                     compose = 0;
    };

    bool initGi();
    void resizeGi();
    void shutdownGi();

    void beginGiFrame(const Ecs::World& world);
    void endGiFrame();

    // Acceleration structures.
    void updateAccelerationStructures(const Ecs::World& world);
    void ensureBlas(std::uint32_t mesh_handle);
    void buildBlas(std::uint32_t mesh_handle, Blas& blas);
    void uploadBlas(Blas& blas);

    void rebuildTlas(const Ecs::World& world);
    void uploadTlas();

    // Raster stage.
    void beginGBufferPass();
    void endGBufferPass();

    // Cached world-space lighting.
    void updateSurfaceCache(const Ecs::World& world);

    // Hybrid indirect lighting.
    void traceGi(const Ecs::World& world);

    // Reuse previous frames.
    void temporalGi();

    // Edge-aware GI filtering.
    void denoiseGi();

    // Direct + indirect + emissive.
    void composeGi();

    // History management.
    void swapGiHistory();

    // Resource helpers.
    void createGBuffer();
    void destroyGBuffer();

    void createGiBuffers();
    void destroyGiBuffers();

    void createSurfaceCache();
    void destroySurfaceCache();

    bool createGiPrograms();
    void destroyGiPrograms();

    unsigned int textureFor(std::uint32_t handle);

    bool initialized_ = false;
    int width_ = 1, height_ = 1;

    GiSettings gi_settings_{};

    bool gi_initialized_ = false;

    int gi_width_ = 1, gi_height_ = 1;

    std::uint64_t gi_frame_ = 0;

    GBuffer gbuffer_{};
    GiBuffers gi_buffers_{};
    SurfaceCache surface_cache_{};
    GiPrograms gi_programs_{};

    // One BLAS per unique mesh.
    std::unordered_map<std::uint32_t, Blas> blas_cache_;

    // Current-frame top-level acceleration structure.
    std::vector<BvhNode> tlas_nodes_;
    std::vector<TlasInstance> tlas_instances_;

    unsigned int tlas_node_buffer_ = 0;
    unsigned int tlas_instance_buffer_ = 0;

    // 0/1 ping-pong index.
    int gi_history_index_ = 0;

    std::array<float, 16> current_view_projection_{};
    std::array<float, 16> previous_view_projection_{};

    std::unordered_map<std::uint32_t, unsigned int> textures_;
};

} // namespace Renderer

#endif
