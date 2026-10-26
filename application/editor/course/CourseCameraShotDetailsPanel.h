#pragma once

#include "CourseSequencerTrackProvider.h"
#include "CoursePreviewSimulationSystem.h"
#include "CourseCameraCompositionController.h"

#include <array>
#include <functional>
#include <string>

namespace editor {

struct CourseCameraShotDetailsPanelContext final {
    CourseAsset* course = nullptr;
    CourseSequencerTrackProvider* provider = nullptr;
    EditorSequencerService* sequencer = nullptr;
    float railLength = 0.0f;
    bool canMutateAuthoring = false;
    CoursePreviewSimulationSystem* preview = nullptr;
    bool* freezeRuntime = nullptr;
    float currentDistance = 0.0f;
    bool canPreview = true;
    std::function<void()> onOpenMapEditor;
};

class CourseCameraShotDetailsPanel final {
public:
    bool HandlesSelection(
        const EditorSequencerService* sequencer,
        const CourseSequencerTrackProvider* provider) const;
    void Draw(const CourseCameraShotDetailsPanelContext& context);
    void DrawEditor(const CourseCameraShotDetailsPanelContext& context);
    void DrawMapControls(const CourseCameraShotDetailsPanelContext& context);
    void SyncMapSelection(const CourseCameraShotDetailsPanelContext& context);
    void DrawMapActions(const CourseCameraShotDetailsPanelContext& context);
    bool ReturnToFollow(const CourseCameraShotDetailsPanelContext& context);
    bool AddMapSection(const CourseCameraShotDetailsPanelContext& context, float start, float end);
    bool SetMapSectionRange(const CourseCameraShotDetailsPanelContext& context,
        std::string_view id, float start, float end, bool previewStart);
    bool SelectMapSection(const CourseCameraShotDetailsPanelContext& context, std::string_view id);
    const std::string& Message() const { return lastMessage_; }
    void OnCourseSaved();
    const CourseCinematicCameraShot* SelectedShot() const { return selectedId_.empty() ? nullptr : &buffer_; }
    bool CameraPreviewActive() const { return cameraPreview_; }
    const char* PlaybackStatus(const CoursePreviewSimulationSystem* preview) const;
    float PreviewEndDistance() const { return previewEndDistance_; }
    CourseCameraCompositionController& Composition() { return composition_; }
    const CourseCameraCompositionController& Composition() const { return composition_; }
    void CancelEdit(CourseAsset* course = nullptr);

private:
    static CourseCinematicCameraShot* FindShot(
        CourseAsset* course, std::string_view id);
    void Sync(const CourseCameraShotDetailsPanelContext& context, std::string_view id);
    void BeginContinuousEdit(const CourseCameraShotDetailsPanelContext& context);
    void RefreshPreview(const CourseCameraShotDetailsPanelContext& context);
    void CommitContinuousEdit(
        const CourseCameraShotDetailsPanelContext& context,
        std::string_view label);
    bool CommitShot(
        const CourseCameraShotDetailsPanelContext& context,
        const CourseCinematicCameraShot& shot,
        std::string_view label);
    void RestorePreview(CourseAsset* course);
    void SyncTextBuffers();
    void ReadTextBuffers();
    void DrawPreviewControls(const CourseCameraShotDetailsPanelContext& context, bool map = false);
    void BeginComposition(const CourseCameraShotDetailsPanelContext& context);
    void AddSection(const CourseCameraShotDetailsPanelContext& context);
    void PreviewAt(const CourseCameraShotDetailsPanelContext& context, float distance, bool play);
    void StopPreview(const CourseCameraShotDetailsPanelContext& context);
    void RegisterComposition(const CourseCameraShotDetailsPanelContext& context, bool play);

    CourseAsset* previewCourse_ = nullptr;
    std::string selectedId_;
    std::string syncedPayload_;
    CourseCinematicCameraShot buffer_{};
    CourseCinematicCameraShot editOriginal_{};
    bool continuousEditActive_ = false;
    bool previewApplied_ = false;
    std::array<char, 128> modeBuffer_{};
    std::string lastMessage_;
    bool cameraPreview_ = false;
    std::string previewShotId_;
    bool previousFreeze_ = false;
    float previousDistance_ = 0.0f;
    float previewEndDistance_ = 0.0f;
    float newShotLength_ = 100.0f;
    int newViewMode_ = 4;
    uint64_t newShotSerial_ = 0;
    CourseCameraCompositionController composition_{};
};

} // namespace editor
