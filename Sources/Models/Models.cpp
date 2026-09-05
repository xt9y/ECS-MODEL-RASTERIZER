#include "Models/Models.hpp"
#include "Models/Obj.hpp"
#include "Models/Texture.hpp"

#include <filesystem>
#include <unordered_map>
#include <utility>

namespace Models {
namespace {

struct Model {
    std::string path;
    std::vector<ModelPart> parts;
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

    for (Obj::Part& source_part : document.parts) {
        const MeshHandle mesh_handle = static_cast<MeshHandle>(meshes().size());
        meshes().push_back(std::move(source_part.mesh));

        const MaterialHandle material_handle = static_cast<MaterialHandle>(materials().size());
        materials().push_back(std::move(source_part.material));
        model.parts.push_back({mesh_handle, material_handle});
    }

    const ModelHandle handle = static_cast<ModelHandle>(models().size());
    models().push_back(std::move(model));
    cache().emplace(key, handle);
    return handle;
}

const ModelPart *part(ModelHandle handle, std::size_t index)
{
    if (handle >= models().size()) return nullptr;
    const Model& model = models()[handle];
    return index < model.parts.size() ? &model.parts[index] : nullptr;
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

} // namespace Models
