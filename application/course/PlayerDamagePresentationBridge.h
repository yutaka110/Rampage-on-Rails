#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "PlayerDamageSystem.h"
#include "utils/math/Vector.h"

struct PlayerDamagePresentationSettings final {
    float flashDecayPerSecond = 3.8f;
    float hapticDurationSeconds = 0.16f;
    float baseCameraShake = 0.72f;
    float lethalCameraShake = 1.15f;
    float baseHitStopSeconds = 0.045f;
    float lethalHitStopSeconds = 0.10f;

    bool Validate(std::string* errorMessage = nullptr) const;
};

struct PlayerDamageAudioCue final {
    uint64_t resultSequence = 0;
    std::string cueId = "player_damage";
    float volume = 0.5f;
    float pitch = 1.0f;
    bool lethal = false;
};

struct PlayerDamageVfxCommand final {
    uint64_t resultSequence = 0;
    std::string effectId = "ice_impact";
    float railDistance = 0.0f;
    float lateralOffset = 0.0f;
    float verticalOffset = 4.0f;
    float radius = 1.6f;
    Vector4 color{0.68f, 0.90f, 1.0f, 1.0f};
    bool lethal = false;
};

struct PlayerDamagePresentationFrame final {
    // Retained separately from one-frame audio/VFX. A lethal notice survives
    // the Defeat -> Result transition until retry/reset.
    PlayerDamageResult lastDamage{};
    float damageNoticeRemainingSeconds = 0.0f;
    bool showDamageNotice = false;
    std::vector<PlayerDamageAudioCue> audioCues;
    std::vector<PlayerDamageVfxCommand> vfxCommands;
    float screenFlashIntensity = 0.0f;
    Vector4 screenFlashColor{1.0f, 0.22f, 0.10f, 1.0f};
    float cameraShake = 0.0f;
    float cameraPitchImpulse = 0.0f;
    float cameraYawImpulse = 0.0f;
    float hitStopSeconds = 0.0f;
    float hapticLow = 0.0f;
    float hapticHigh = 0.0f;
    float hapticRemainingSeconds = 0.0f;
    float hitPointsBefore = 0.0f;
    float hitPointsAfter = 0.0f;
    uint32_t acceptedHits = 0;
    bool lethal = false;
    uint64_t revision = 0;
};

struct PlayerDamagePresentationInput final {
    std::span<const PlayerDamageResult> results;
    float deltaTime = 0.0f;
    bool gameplayActive = true;
    bool suppressCameraFeedback = false;
    bool vehicleOwnsImpactPresentation = false;
};

// Converts authoritative PlayerDamageResult records into bounded one-frame
// presentation commands. It never changes health or collision state.
class PlayerDamagePresentationBridge final {
public:
    void Reset();
    void Update(const PlayerDamagePresentationInput& input);

    bool Configure(const PlayerDamagePresentationSettings& settings, std::string* errorMessage = nullptr);
    const PlayerDamagePresentationSettings& Settings() const noexcept {
        return settings_;
    }
    const PlayerDamagePresentationFrame& Frame() const noexcept { return frame_; }

private:
    PlayerDamagePresentationSettings settings_{};
    PlayerDamagePresentationFrame frame_{};
    float flashIntensity_ = 0.0f;
    float hapticLow_ = 0.0f;
    float hapticHigh_ = 0.0f;
    float hapticRemainingSeconds_ = 0.0f;
    PlayerDamageResult lastDamage_{};
    float damageNoticeRemainingSeconds_ = 0.0f;
    uint64_t lastPresentedSequence_ = 0;
    uint64_t revision_ = 0;
};
