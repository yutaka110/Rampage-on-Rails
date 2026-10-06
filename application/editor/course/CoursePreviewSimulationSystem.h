#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

#include "../../course/CourseAsset.h"
#include "../EditorViewportOverlay.h"

namespace editor {

enum class CoursePreviewPlaybackState : uint8_t {
    Stopped,
    Paused,
    Playing,
    Completed,
    Error,
};

enum class CoursePreviewWavePhase : uint8_t {
    Disabled,
    Pending,
    Prewarming,
    Blocked,
    Active,
    Completed,
};

enum class CoursePreviewEnemyPhase : uint8_t {
    Disabled,
    Dormant,
    Prewarmed,
    Active,
    Defeated,
    Retired,
};

enum class CoursePreviewEventType : uint8_t {
    PreviewStarted,
    PreviewStopped,
    PreviewCompleted,
    WavePrewarmed,
    WaveActivated,
    WaveCompleted,
    EnemyActivated,
    EnemyDefeated,
    TriggerEvent,
};

struct CoursePreviewSimulationSettings final {
    float fixedStepSeconds = 1.0f / 60.0f;
    float travelSpeed = 0.0f; // Zero follows the authored RailPath speed.
    float playbackRate = 1.0f;
    float automaticEnemyDefeatSeconds = 2.0f;
    uint32_t maximumSubsteps = 8;
    bool loop = false;
    bool automaticallyDefeatEnemies = true;
    bool showViewportOverlay = true;

    bool Validate(std::string* errorMessage = nullptr) const;
};

struct CoursePreviewWaveState final {
    std::string waveGuid;
    std::string displayName;
    CoursePreviewWavePhase phase = CoursePreviewWavePhase::Pending;
    float triggerDistance = 0.0f;
    float activeSeconds = 0.0f;
    uint32_t memberCount = 0;
    uint32_t defeatedMemberCount = 0;
};

struct CoursePreviewEnemyState final {
    std::string placementGuid;
    std::string actorAssetId;
    std::string waveGuid;
    CoursePreviewEnemyPhase phase = CoursePreviewEnemyPhase::Dormant;
    float activeSeconds = 0.0f;
};

struct CoursePreviewSimulationEvent final {
    CoursePreviewEventType type = CoursePreviewEventType::PreviewStarted;
    float distance = 0.0f;
    float elapsedSeconds = 0.0f;
    std::string subjectGuid;
    std::string message;
};

struct CoursePreviewSimulationFrame final {
    CoursePreviewPlaybackState playback = CoursePreviewPlaybackState::Stopped;
    float distance = 0.0f;
    float railLength = 0.0f;
    float normalizedProgress = 0.0f;
    float elapsedSeconds = 0.0f;
    float currentSpeed = 0.0f;
    RailPathSample railSample{};
    uint32_t activeWaves = 0;
    uint32_t prewarmingWaves = 0;
    uint32_t completedWaves = 0;
    uint32_t activeEnemies = 0;
    uint64_t simulationRevision = 0;
    std::string message;
};

struct CoursePreviewViewportStats final {
    uint32_t waveMarkers = 0;
    uint32_t enemyMarkers = 0;
    uint32_t labels = 0;
    uint32_t rejectedBehindCamera = 0;
};

// Non-destructive editor-only playback of one immutable CourseAsset snapshot.
// It deliberately does not write to CourseAsset, runtime actor state, or Undo.
class CoursePreviewSimulationSystem final : public IEditorViewportOverlayProvider {
public:
    std::string_view Id() const override {
        return "editor.course.preview-simulation";
    }
    EditorViewportOverlayLayerId Layer() const override {
        return EditorViewportOverlayLayerId::CourseNavigation;
    }
    void Build(
        const EditorViewportOverlayFrameContext& context,
        EditorViewportOverlayCommandSink& sink) const override;

    bool BeginPreview(
        const CourseAsset& source,
        float startDistance,
        std::string* errorMessage = nullptr);
    void Play();
    void Pause();
    void Stop();
    bool Restart(bool playImmediately = true, std::string* errorMessage = nullptr);
    bool Seek(float distance, std::string* errorMessage = nullptr);
    bool Step(std::string* errorMessage = nullptr);
    void Tick(float deltaTime);

    bool MarkEnemyDefeated(std::string_view placementGuid, bool defeated = true);
    uint32_t DefeatActiveEnemies();
    void SignalEvent(std::string eventId);

    bool IsActive() const noexcept {
        return frame_.playback != CoursePreviewPlaybackState::Stopped &&
            frame_.playback != CoursePreviewPlaybackState::Error;
    }
    bool IsPlaying() const noexcept {
        return frame_.playback == CoursePreviewPlaybackState::Playing;
    }
    bool HasSnapshot() const noexcept { return hasSnapshot_; }
    const CourseAsset* Snapshot() const noexcept {
        return hasSnapshot_ ? &snapshot_ : nullptr;
    }
    const CoursePreviewSimulationFrame& Frame() const noexcept { return frame_; }
    const std::vector<CoursePreviewWaveState>& Waves() const noexcept { return waves_; }
    const std::vector<CoursePreviewEnemyState>& Enemies() const noexcept { return enemies_; }
    const std::vector<CoursePreviewSimulationEvent>& Events() const noexcept { return events_; }
    const CoursePreviewViewportStats& ViewportStats() const noexcept {
        return viewportStats_;
    }
    bool Configure(const CoursePreviewSimulationSettings& settings, std::string* errorMessage = nullptr);
    const CoursePreviewSimulationSettings& Settings() const noexcept { return settings_; }

private:
    void InitializeRuntimeState();
    bool RebuildAtDistance(float distance, std::string* errorMessage);
    void AdvanceFixed(float deltaTime, bool emitEvents);
    void UpdatePrewarmAndActivation(bool emitEvents);
    void UpdateActiveState(float deltaTime, bool emitEvents);
    bool CanActivateWave(std::size_t index) const;
    void PrewarmWave(std::size_t index, bool emitEvents);
    void ActivateWave(std::size_t index, bool emitEvents);
    void CompleteWave(std::size_t index, bool emitEvents);
    void RefreshFrame();
    void PushEvent(
        CoursePreviewEventType type,
        std::string subjectGuid,
        std::string message);
    float ResolveTravelSpeed() const;
    std::size_t FindWaveIndex(std::string_view guid) const;
    std::size_t FindEnemyIndex(std::string_view guid) const;

    CourseAsset snapshot_{};
    RailPath railPath_{};
    CoursePreviewSimulationSettings settings_{};
    CoursePreviewSimulationFrame frame_{};
    std::vector<CoursePreviewWaveState> waves_;
    std::vector<CoursePreviewEnemyState> enemies_;
    std::vector<CoursePreviewSimulationEvent> events_;
    std::unordered_set<std::string> signaledEvents_;
    std::unordered_set<std::string> forcedWaveActivations_;
    float accumulator_ = 0.0f;
    float requestedStartDistance_ = 0.0f;
    uint64_t nextRevision_ = 1;
    bool hasSnapshot_ = false;
    mutable CoursePreviewViewportStats viewportStats_{};
};

const char* ToString(CoursePreviewPlaybackState state);
const char* ToString(CoursePreviewWavePhase phase);
const char* ToString(CoursePreviewEnemyPhase phase);
const char* ToString(CoursePreviewEventType type);

} // namespace editor
