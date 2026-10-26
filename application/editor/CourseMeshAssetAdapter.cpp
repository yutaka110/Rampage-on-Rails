#include "CourseMeshAssetAdapter.h"

#include <filesystem>
#include <utility>

namespace editor {
namespace {

void RegisterMesh(EditorAssetRegistry& registry, const char* id, const char* sourcePath) {
    // Folder indexing owns durable Asset identity. Course defaults are only a
    // compatibility fallback and must never replace indexed .meta data with an
    // auto GUID.
    if (registry.Find(EditorAssetKind::Mesh, id) != nullptr) {
        return;
    }
    EditorAssetRecord record{};
    record.kind = EditorAssetKind::Mesh;
    record.id = id;
    record.displayName = id;
    record.sourcePath = sourcePath;
    record.logicalPath = sourcePath;
    record.metadataPath = std::string(sourcePath) + ".meta";
    record.missing = !std::filesystem::exists(record.sourcePath);
    record.referenceable = true;
    registry.Register(std::move(record));
}

} // namespace

void CourseMeshAssetAdapter::RegisterAssets(EditorAssetRegistry& registry) const {
    RegisterMesh(registry, "ball", "Resources/ball/ball.obj");
    RegisterMesh(registry, "animated_cube", "Resources/AnimatedCube/AnimatedCube.gltf");
    RegisterMesh(registry, "organic_arch_large", "Resources/course_meshes/OrganicArchLarge/OrganicArchLarge.obj");
    RegisterMesh(registry, "rib_tunnel_wall", "Resources/course_meshes/RibTunnelWall/RibTunnelWall.obj");
    RegisterMesh(registry, "rib_tunnel_inner", "Resources/course_meshes/RibTunnelInner/RibTunnelInner.obj");
    RegisterMesh(registry, "root_spire_column", "Resources/course_meshes/RootSpireColumn/RootSpireColumn.obj");
    RegisterMesh(registry, "curved_canyon_wall", "Resources/course_meshes/CurvedCanyonWall/CurvedCanyonWall.obj");
    RegisterMesh(registry, "vista_hole_wall", "Resources/course_meshes/VistaHoleWall/VistaHoleWall.obj");
    RegisterMesh(registry, "spire_broken_bridge_arc", "Resources/course_meshes/SpireBrokenBridgeArc/SpireBrokenBridgeArc.obj");
}

} // namespace editor
