#pragma once

#include <array>
#include <cstddef>
#include <filesystem>
#include <string>

#include "utils/math/Vector.h"

enum class TerrainPbrOrmInputMode {
    Packed,
    Separate,
};

struct TerrainPbrMaterialDefinition {
    std::string id;
    std::filesystem::path sourcePath;
    std::filesystem::path baseColorPath;
    std::filesystem::path normalPath;
    TerrainPbrOrmInputMode ormInputMode = TerrainPbrOrmInputMode::Packed;
    std::filesystem::path ormPath;
    std::filesystem::path ambientOcclusionPath;
    std::filesystem::path roughnessPath;
    std::filesystem::path metallicPath;
    std::filesystem::path heightPath;
    Vector4 baseColorTint = {1.0f, 1.0f, 1.0f, 1.0f};
    float worldTileSize = 6.0f;
    float normalStrength = 1.0f;
    float detailNormalStrength = 0.65f;
    float roughnessScale = 1.0f;
    float roughnessBias = 0.0f;
    float aoStrength = 1.0f;
    float heightScale = 0.035f;
    float heightBlendSharpness = 4.0f;
    float macroVariationStrength = 0.18f;
    float wetnessResponse = 0.0f;
};

class TerrainMaterialLibrary {
public:
    static constexpr size_t kLayerCount = 3;

    bool LoadFromSet(
        const std::filesystem::path& setPath,
        std::string* error = nullptr);

    void BuildFallback();

    [[nodiscard]] const std::array<TerrainPbrMaterialDefinition, kLayerCount>&
    Layers() const noexcept {
        return layers_;
    }

    [[nodiscard]] const std::filesystem::path& SourcePath() const noexcept {
        return sourcePath_;
    }

private:
    std::filesystem::path sourcePath_;
    std::array<TerrainPbrMaterialDefinition, kLayerCount> layers_{};
};

[[nodiscard]] std::filesystem::path DefaultTerrainMaterialSetPath();
[[nodiscard]] std::filesystem::path DefaultTitleGroundMaterialPath();

bool LoadTerrainMaterialDefinition(
    const std::filesystem::path& path,
    TerrainPbrMaterialDefinition& output,
    std::string* error = nullptr);

[[nodiscard]] std::string SerializeTerrainMaterialDefinition(
    const TerrainPbrMaterialDefinition& material);
