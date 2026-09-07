#include "Models/Formats/Fbx.hpp"

#include <cassert>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>

int main()
{
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "rw_fbx_import_contract.fbx";

    const char *fixture = R"FBX(
FBXHeaderExtension:  {
    FBXVersion: 7400
}
Objects:  {
    Geometry: 100, "Geometry::Triangle", "Mesh" {
        Vertices: *9 { a: 0,0,0, 1,0,0, 0,1,0 }
        PolygonVertexIndex: *3 { a: 0,1,-3 }
        LayerElementNormal: 0 {
            MappingInformationType: "ByPolygonVertex"
            ReferenceInformationType: "Direct"
            Normals: *9 { a: 0,0,1, 0,0,1, 0,0,1 }
        }
        LayerElementUV: 0 {
            MappingInformationType: "ByPolygonVertex"
            ReferenceInformationType: "IndexToDirect"
            UV: *6 { a: 0,0, 1,0, 0,1 }
            UVIndex: *3 { a: 0,1,2 }
        }
        LayerElementMaterial: 0 {
            MappingInformationType: "AllSame"
            ReferenceInformationType: "IndexToDirect"
            Materials: *1 { a: 0 }
        }
    }
    Model: 200, "Model::Triangle", "Mesh" {
        Properties70:  {
            P: "Lcl Translation", "Lcl Translation", "", "A", 2,3,4
            P: "Lcl Rotation", "Lcl Rotation", "", "A", 0,0,0
            P: "Lcl Scaling", "Lcl Scaling", "", "A", 1,1,1
        }
    }
    Material: 300, "Material::White", "" {
        Properties70:  {
            P: "DiffuseColor", "Color", "", "A", 0.5,0.6,0.7
            P: "TransparencyFactor", "Number", "", "A", 0.25
        }
    }
    Model: 399, "Model::Root", "LimbNode" {
        Properties70:  {
            P: "Lcl Translation", "Lcl Translation", "", "A", 0,0,0
            P: "Lcl Rotation", "Lcl Rotation", "", "A", 0,0,0
            P: "Lcl Scaling", "Lcl Scaling", "", "A", 1,1,1
        }
    }
    Model: 400, "Model::Bone", "LimbNode" {
        Properties70:  {
            P: "Lcl Translation", "Lcl Translation", "", "A", 0,1,0
            P: "Lcl Rotation", "Lcl Rotation", "", "A", 0,0,0
            P: "Lcl Scaling", "Lcl Scaling", "", "A", 1,1,1
        }
    }
    Deformer: 500, "Deformer::Skin", "Skin" { }
    Deformer: 501, "SubDeformer::Bone", "Cluster" {
        Indexes: *3 { a: 0,1,2 }
        Weights: *3 { a: 1,0.5,0.25 }
        Transform: *16 { a: 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 }
        TransformLink: *16 { a: 1,0,0,0, 0,1,0,1, 0,0,1,0, 0,0,0,1 }
    }
    AnimationStack: 600, "AnimStack::Move", "" {
        Properties70:  {
            P: "LocalStart", "KTime", "Time", "", 0
            P: "LocalStop", "KTime", "Time", "", 46186158000
        }
    }
    AnimationLayer: 601, "AnimLayer::Base", "" { }
    AnimationCurveNode: 602, "AnimCurveNode::T", "" { }
    AnimationCurve: 603, "AnimCurve::TX", "" {
        KeyTime: *2 { a: 0,46186158000 }
        KeyValueFloat: *2 { a: 0,2 }
    }
}
Connections:  {
    C: "OO",100,200
    C: "OO",300,200
    C: "OO",400,399
    C: "OO",500,100
    C: "OO",501,500
    C: "OO",400,501
    C: "OO",601,600
    C: "OO",602,601
    C: "OP",603,602,"d|X"
    C: "OP",602,400,"Lcl Translation"
}
)FBX";

    {
        std::ofstream file(path, std::ios::binary);
        file << fixture;
    }

    Models::Fbx::Document document;
    std::string error;
    const bool loaded = Models::Fbx::load(path.string(), &document, &error);
    std::filesystem::remove(path);

    assert(loaded && error.empty());
    assert(document.parts.size() == 1u);
    assert(document.parts[0].mesh.indices.size() == 3u);
    assert(document.parts[0].mesh.vertices.size() == 3u);
    assert(std::abs(document.parts[0].material.color.x - 0.5f) < 1.0e-4f);
    assert(std::abs(document.parts[0].material.opacity - 0.75f) < 1.0e-4f);

    assert(document.has_skeleton);
    assert(document.skeleton.bones.size() == 2u);
    assert(document.skeleton.bones[0].name == "Root");
    assert(document.skeleton.bones[1].name == "Bone");
    assert(document.skeleton.bones[1].parent == 0);
    assert(document.parts[0].mesh.skin_inverse_bind.size() == 2u);

    const auto& skin = document.parts[0].mesh.vertices[0].skin;
    assert(skin.weights[0] > 0.999f);
    assert(skin.joints[0] == 1u);

    assert(document.animations.size() == 1u);
    assert(document.animations[0].name == "Move");
    assert(document.animations[0].duration > 0.99f);
    assert(document.animations[0].tracks.size() == 2u);
    assert(document.animations[0].tracks[1].samples.size() >= 2u);
    assert(document.animations[0].tracks[1].samples.back().translation.x > 1.9f);

    return 0;
}
