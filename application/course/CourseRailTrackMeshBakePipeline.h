#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "CourseRailTrackDefinitionAsset.h"
#include "../terrain/RailPath.h"
#include "utils/math/MathUtils.h"

enum class CourseRailTrackMeshPart : uint8_t {
    LeftRail,
    RightRail,
    Sleeper,
    LeftSupport,
    RightSupport,
};

struct CourseRailTrackBakedInstance final {
    CourseRailTrackMeshPart part = CourseRailTrackMeshPart::LeftRail;
    std::string meshId;
    Matrix4x4 worldMatrix = MakeIdentity4x4();
    Vector4 color{1.0f, 1.0f, 1.0f, 1.0f};
    float startDistance = 0.0f;
    float endDistance = 0.0f;
    uint32_t detailOrdinal = 0;
};

// Immutable CPU bake product. It can be shared by gameplay, editor preview,
// and render threads without asking RailPath to tessellate every frame.
struct CourseRailTrackMeshBakeResult final {
    bool valid = false;
    std::string assetId;
    std::string status;
    CourseRailTrackDefinitionAsset definition{};
    std::vector<CourseRailTrackBakedInstance> instances;
    float railLength = 0.0f;
    uint32_t railSegmentCount = 0;
    uint32_t sleeperCount = 0;
    uint32_t supportCount = 0;
    uint64_t sourceFingerprint = 0;
    uint64_t revision = 0;
};

class CourseRailTrackMeshBakePipeline final {
public:
    void Reset();
    bool Bake(
        const CourseRailTrackDefinitionAsset& definition,
        const RailPath& railPath,
        std::string* errorMessage = nullptr,
        float rearPresentationLength = 0.0f);

    const CourseRailTrackMeshBakeResult& Result() const noexcept { return result_; }

private:
    CourseRailTrackMeshBakeResult result_{};
    uint64_t revision_ = 0;
};
