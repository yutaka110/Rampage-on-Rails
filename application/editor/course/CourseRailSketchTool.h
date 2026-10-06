#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "CourseOverviewMapController.h"
#include "CourseRailCurveFitService.h"

namespace editor {

enum class CourseRailSketchMode : uint8_t {
    Append,
    Prepend,
    ReplaceSegment,
};

struct CourseRailSketchSettings final {
    CourseRailCurveFitSettings curveFit{};
    float minimumSamplePixels = 3.0f;
    bool reprojectOrphanedAnchors = true;

    bool Validate(std::string* errorMessage = nullptr) const;
};

struct CourseRailSketchInput final {
    Vector2 mapPosition{};
    CourseOverviewMapPickResult hovered{};
    bool primaryPressed = false;
    bool primaryDown = false;
    bool primaryReleased = false;
    bool cancelPressed = false;
};

struct CourseRailSketchState final {
    bool active = false;
    bool drawing = false;
    bool previewValid = false;
    bool canMutate = false;
    CourseRailSketchMode mode = CourseRailSketchMode::Append;
    uint32_t rawSamples = 0;
    uint32_t fittedControlPoints = 0;
    float fittedLength = 0.0f;
    float minimumTurnRadius = 0.0f;
    std::string targetSegmentGuid;
    std::string message;
    uint64_t editRevision = 0;
};

// Pointer-stroke rail authoring. Every in-progress fit lives in a private
// CourseAsset; release emits one ReplaceRail command and Esc discards it.
class CourseRailSketchTool final {
public:
    void Bind(
        CourseOverviewMapController* overview,
        CourseRailEditorController* rail,
        EditorSelection* selection,
        const CourseRailCurveFitService* curveFit);
    void SetActive(bool active);
    void SetMode(CourseRailSketchMode mode);
    bool SetSettings(CourseRailSketchSettings settings, std::string* errorMessage = nullptr);
    void Tick(const CourseRailSketchInput& input);
    void Cancel(std::string message = {});

    const CourseRailSketchState& State() const noexcept { return state_; }
    const CourseRailSketchSettings& Settings() const noexcept { return settings_; }
    const std::vector<Vector2>& RawMapSamples() const noexcept { return mapSamples_; }
    const std::vector<RailPathControlPoint>& FittedControlPoints() const noexcept {
        return lastFit_.controlPoints;
    }
    const CourseRailCurveFitResult& LastFit() const noexcept { return lastFit_; }

private:
    bool CanMutate() const;
    bool Begin(const CourseRailSketchInput& input);
    void Sample(Vector2 mapPosition, bool force);
    void RefreshPreview(bool finalSample);
    void Commit();
    std::vector<RailPathControlPoint> BuildReplacement(
        const std::vector<RailPathControlPoint>& fitted) const;
    void ReprojectAnchors(const CourseRailAuthoringModel& before);
    void SelectResultPoint();

    CourseOverviewMapController* overview_ = nullptr;
    CourseRailEditorController* rail_ = nullptr;
    EditorSelection* selection_ = nullptr;
    const CourseRailCurveFitService* curveFit_ = nullptr;
    CourseRailSketchSettings settings_{};
    CourseRailSketchState state_{};
    CourseAsset sourceCourse_{};
    CourseAsset previewCourse_{};
    std::vector<Vector2> mapSamples_;
    std::vector<Vector3> worldSamples_;
    CourseRailCurveFitResult lastFit_{};
    float drawingDepth_ = 0.0f;
    uint32_t replaceSegmentIndex_ = 0;
    uint64_t expectedRevision_ = 0;
};

const char* ToString(CourseRailSketchMode mode);

} // namespace editor
