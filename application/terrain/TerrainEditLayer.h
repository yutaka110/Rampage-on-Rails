#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

enum class TerrainEditOperation : uint32_t {
    Sculpt = 0,
    Smooth = 1,
    Flatten = 2,
    Paint = 3,
    CloseCeiling = 4,
};

struct TerrainBrushStamp {
    std::string strokeGuid;
    std::string stampGuid;
    TerrainEditOperation operation = TerrainEditOperation::Sculpt;
    float distance = 0.0f;
    float angle = 0.0f;
    float radius = 8.0f;
    float surfaceRadius = 24.0f;
    float strength = 1.0f;
    float hardness = 0.5f;
    uint32_t materialLayer = 0;
};

struct TerrainEditDirtyRegion {
    float minDistance = 0.0f;
    float maxDistance = 0.0f;
    bool valid = false;

    bool Overlaps(float begin, float end) const noexcept {
        return valid && maxDistance >= begin && minDistance <= end;
    }
};

struct TerrainEditEvaluation {
    float radialOffset = 0.0f;
    float paintWeights[4]{};

    float MaterialVariation() const noexcept;
};

class TerrainEditLayer {
public:
    static constexpr uint32_t kSchemaVersion = 1;
    static constexpr std::size_t kMaxStrokeStamps = 256;
    static constexpr std::size_t kMaxStamps = 8192;

    bool ApplyStroke(
        const std::vector<TerrainBrushStamp>& stamps,
        std::string* errorMessage = nullptr);
    bool RemoveStroke(
        std::string_view strokeGuid,
        std::string* errorMessage = nullptr);
    bool ReplaceFromSnapshot(
        const TerrainEditLayer& snapshot,
        std::string* errorMessage = nullptr);
    void Clear();

    TerrainEditEvaluation Evaluate(float distance, float angle) const noexcept;
    float CeilingClosure(float distance) const noexcept;
    TerrainEditDirtyRegion DirtyRegionForStroke(
        std::string_view strokeGuid) const noexcept;
    TerrainEditDirtyRegion DirtyRegionFor(
        const std::vector<TerrainBrushStamp>& stamps) const noexcept;
    uint64_t ContentHashForRange(float begin, float end) const noexcept;
    TerrainEditLayer Filtered(float begin, float end) const;
    bool Validate(std::string* errorMessage = nullptr) const;

    uint32_t Revision() const noexcept { return revision_; }
    const std::vector<TerrainBrushStamp>& Stamps() const noexcept { return stamps_; }

private:
    uint32_t revision_ = 0;
    std::vector<TerrainBrushStamp> stamps_;
};

const char* ToString(TerrainEditOperation operation) noexcept;
bool ParseTerrainEditOperation(std::string_view text, TerrainEditOperation& operation) noexcept;

