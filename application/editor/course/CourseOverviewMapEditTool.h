#pragma once

#include <cstdint>
#include <string>

#include "CourseOverviewMapController.h"
#include "CourseOverviewMapSnapService.h"

namespace editor {

enum class CourseOverviewMapEditMode : uint8_t {
    SelectMove,
    AddRailPoint,
    AddEnemy,
    AddWave,
    Delete,
};

struct CourseOverviewMapEditSettings final {
    std::string defaultActorAssetId = "drone";
    std::string defaultWaveName = "Wave";
    float defaultWavePrewarmDistance = 80.0f;

    bool Validate(std::string* errorMessage = nullptr) const;
};

struct CourseOverviewMapEditInput final {
    Vector2 mapPosition{};
    CourseOverviewMapPickResult hovered{};
    bool primaryPressed = false;
    bool primaryDown = false;
    bool primaryReleased = false;
    bool cancelPressed = false;
    bool deletePressed = false;
};

struct CourseOverviewMapEditState final {
    bool active = true;
    bool canMutate = false;
    bool dragging = false;
    bool previewValid = false;
    CourseOverviewMapEditMode mode = CourseOverviewMapEditMode::SelectMove;
    CourseOverviewMapItemKind dragKind = CourseOverviewMapItemKind::None;
    std::string dragGuid;
    std::string message;
    uint64_t editRevision = 0;
};

// Direct 2D authoring tool. It edits a private CourseAsset during a drag and
// emits exactly one domain Controller mutation when the pointer is released.
class CourseOverviewMapEditTool final {
public:
    void Bind(
        CourseOverviewMapController* overview,
        CourseRailEditorController* rail,
        CourseEnemyEditorController* enemies,
        CourseWaveEditorController* waves,
        EditorSelection* selection,
        const CourseOverviewMapSnapService* snapping);
    void SetActive(bool active);
    void SetMode(CourseOverviewMapEditMode mode);
    bool SetSettings(CourseOverviewMapEditSettings settings, std::string* errorMessage = nullptr);
    void Tick(const CourseOverviewMapEditInput& input);
    void Cancel(std::string message = {});

    const CourseOverviewMapEditState& State() const noexcept { return state_; }
    const CourseOverviewMapEditSettings& Settings() const noexcept { return settings_; }
    const CourseAsset* PreviewCourse() const noexcept {
        return state_.previewValid ? &previewCourse_ : nullptr;
    }

private:
    bool CanMutate(CourseOverviewMapItemKind kind) const;
    bool BeginDrag(const CourseOverviewMapPickResult& pick);
    void UpdateDrag(Vector2 mapPosition);
    void CommitDrag();
    void AddRailPoint(Vector2 mapPosition);
    void AddEnemy(Vector2 mapPosition);
    void AddWave(Vector2 mapPosition);
    void Delete(const CourseOverviewMapPickResult& pick);
    void RefreshPreview();
    void SelectCreated(EditorDomainId domain, std::string guid, std::string displayName);

    CourseOverviewMapController* overview_ = nullptr;
    CourseRailEditorController* rail_ = nullptr;
    CourseEnemyEditorController* enemies_ = nullptr;
    CourseWaveEditorController* waves_ = nullptr;
    EditorSelection* selection_ = nullptr;
    const CourseOverviewMapSnapService* snapping_ = nullptr;
    CourseOverviewMapEditSettings settings_{};
    CourseOverviewMapEditState state_{};
    CourseAsset previewCourse_{};
    RailPathControlPoint originalPoint_{};
    CourseEnemyPlacement originalEnemy_{};
    CourseWaveDefinition originalWave_{};
    float dragDepth_ = 0.0f;
    uint64_t expectedRevision_ = 0;
};

const char* ToString(CourseOverviewMapEditMode mode);

} // namespace editor
