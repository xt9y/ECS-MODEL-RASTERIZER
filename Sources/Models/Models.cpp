#include "Models/Models.hpp"
#include "Models/Obj.hpp"
#include "Models/Texture.hpp"

#include <filesystem>
#include <unordered_map>
#include <utility>

namespace RW::Models {
namespace {

struct Part {
    MeshHandle mesh = INVALID_MESH;
    MaterialHandle material = INVALID_MATERIAL;
};

struct Model {
    std::string path;
    std::vector<Part> parts;
};

std::vector<Model>& models() { static std::vector<Model> values; return values; }
std::vector<MeshData>& meshes() { static std::vector<MeshData> values; return values; }
std::vector<MaterialData>& materials() { static std::vector<MaterialData> values; return values; }
std::unordered_map<std::string, ModelHandle>& cache() { static std::unordered_map<std::string, ModelHandle> values; return values; }

std::string normalizedPath(const std::string& path)
{
    std::error_code ec;
    const std::filesystem::path absolute = std::filesystem::absolute(path, ec);
    return (ec ? std::filesystem::path(path) : absolute).lexically_normal().string();
}

} // namespace

ModelHandle load(const std::string& path, std::string *error)
{
    if (error) error->clear();
    const std::string key = normalizedPath(path);
    if (const auto found = cache().find(key); found != cache().end()) return found->second;

    const std::filesystem::path extension = std::filesystem::path(key).extension();
    if (extension != ".obj" && extension != ".OBJ") {
        if (error) *error = "unsupported model format: " + extension.string();
        return INVALID_MODEL;
    }

    Obj::Document document;
    if (!Obj::load(key, &document, error)) return INVALID_MODEL;

    Model model;
    model.path = key;
    model.parts.reserve(document.parts.size());

    for (Obj::Part& part : document.parts) {
        const MeshHandle mesh_handle = static_cast<MeshHandle>(meshes().size());
        meshes().push_back(std::move(part.mesh));

        const MaterialHandle material_handle = static_cast<MaterialHandle>(materials().size());
        materials().push_back(std::move(part.material));
        model.parts.push_back({mesh_handle, material_handle});
    }

    const ModelHandle handle = static_cast<ModelHandle>(models().size());
    models().push_back(std::move(model));
    cache().emplace(key, handle);
    return handle;
}

std::vector<Ecs::Entity> spawn(Ecs::World& world, ModelHandle handle, const SpawnOptions& options, std::string *error)
{
    if (error) error->clear();
    if (handle >= models().size()) {
        if (error) *error = "invalid model handle";
        return {};
    }

    const Model& model = models()[handle];
    std::vector<Ecs::Entity> entities;
    entities.reserve(model.parts.size());

    for (const Part& part : model.parts) {
        const Ecs::Entity entity = world.createEntity();
        world.addTransform(entity, options.transform);
        world.addMesh(entity, {part.mesh, part.material});
        world.addRenderable(entity, {options.visible});
        entities.push_back(entity);
    }
    return entities;
}

std::vector<Ecs::Entity> loadInto(Ecs::World& world, const std::string& path, const SpawnOptions& options, std::string *error)
{
    const ModelHandle handle = load(path, error);
    return handle == INVALID_MODEL ? std::vector<Ecs::Entity>{} : spawn(world, handle, options, error);
}

const MeshData *mesh(MeshHandle handle)
{
    return handle < meshes().size() ? &meshes()[handle] : nullptr;
}

const MaterialData *material(MaterialHandle handle)
{
    return handle < materials().size() ? &materials()[handle] : nullptr;
}

std::size_t partCount(ModelHandle handle)
{
    return handle < models().size() ? models()[handle].parts.size() : 0u;
}

void clearCache()
{
    cache().clear();
    models().clear();
    meshes().clear();
    materials().clear();
    clearTextureCache();
}

} // namespace RW::Models
