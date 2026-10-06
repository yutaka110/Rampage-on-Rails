#include "PlayerDamagePresentationBridge.h"
#include "GameplaySettingsValidation.h"

#include <algorithm>
#include <cmath>

bool PlayerDamagePresentationSettings::Validate(std::string* errorMessage) const {
    return gameplay::settings::Result(
        gameplay::settings::NonNegative({flashDecayPerSecond, hapticDurationSeconds,
            baseCameraShake, lethalCameraShake, baseHitStopSeconds, lethalHitStopSeconds}),
        errorMessage, "Damage presentation values must be finite, nonnegative and bounded.");
}

bool PlayerDamagePresentationBridge::Configure(
    const PlayerDamagePresentationSettings& settings, std::string* errorMessage) {
    if (!settings.Validate(errorMessage)) return false;
    settings_ = settings;
    hapticRemainingSeconds_ = (std::min)(hapticRemainingSeconds_, settings_.hapticDurationSeconds);
    if (hapticRemainingSeconds_ == 0.0f) hapticLow_ = hapticHigh_ = 0.0f;
    frame_.hapticRemainingSeconds = hapticRemainingSeconds_;
    frame_.hapticLow = hapticLow_;
    frame_.hapticHigh = hapticHigh_;
    return true;
}

void PlayerDamagePresentationBridge::Reset() {
    frame_ = {};
    flashIntensity_ = 0.0f;
    hapticLow_ = 0.0f;
    hapticHigh_ = 0.0f;
    hapticRemainingSeconds_ = 0.0f;
    lastDamage_ = {};
    damageNoticeRemainingSeconds_ = 0.0f;
    lastPresentedSequence_ = 0;
    revision_ = 0;
}

void PlayerDamagePresentationBridge::Update(
    const PlayerDamagePresentationInput& input) {
    const float dt = std::isfinite(input.deltaTime)
        ? (std::max)(0.0f, input.deltaTime)
        : 0.0f;
    flashIntensity_ = (std::max)(
        0.0f,
        flashIntensity_ - dt * settings_.flashDecayPerSecond);
    hapticRemainingSeconds_ = (std::max)(
        0.0f,
        hapticRemainingSeconds_ - dt);
    if (hapticRemainingSeconds_ <= 0.0f) {
        hapticLow_ = 0.0f;
        hapticHigh_ = 0.0f;
    }

    if (input.gameplayActive && !lastDamage_.lethal) {
        damageNoticeRemainingSeconds_ = (std::max)(
            0.0f, damageNoticeRemainingSeconds_ - dt);
    }
    frame_ = {};
    if (input.gameplayActive) {
        for (const PlayerDamageResult& result : input.results) {
            if (!result.accepted || result.appliedDamage <= 0.0f) continue;
            if (result.sequence != 0 && result.sequence <= lastPresentedSequence_) continue;
            lastPresentedSequence_ = result.sequence;
            lastDamage_ = result;
            damageNoticeRemainingSeconds_ = 3.0f;
            const float normalizedDamage = (std::clamp)(
                result.appliedDamage / 30.0f,
                0.0f,
                1.0f);
            const float intensity = result.lethal
                ? 1.0f
                : 0.48f + normalizedDamage * 0.42f;
            const bool dedicatedVehicleImpact =
                input.vehicleOwnsImpactPresentation || (result.request.hasWorldImpact &&
                (result.request.kind == PlayerHitKind::ObstacleContact ||
                 result.request.kind == PlayerHitKind::TerrainContact));
            flashIntensity_ = (std::max)(flashIntensity_, intensity);
            if (!dedicatedVehicleImpact && !input.suppressCameraFeedback) {
                frame_.cameraShake = (std::max)(
                    frame_.cameraShake,
                    result.lethal
                        ? settings_.lethalCameraShake
                        : settings_.baseCameraShake + normalizedDamage * 0.20f);
                frame_.cameraPitchImpulse -= 0.006f + normalizedDamage * 0.006f;
                frame_.cameraYawImpulse +=
                    result.request.lateralOffset >= 0.0f ? -0.004f : 0.004f;
            }
            frame_.hitStopSeconds = (std::max)(
                frame_.hitStopSeconds,
                result.lethal
                    ? settings_.lethalHitStopSeconds
                    : settings_.baseHitStopSeconds + normalizedDamage * 0.02f);
            hapticRemainingSeconds_ = (std::max)(
                hapticRemainingSeconds_,
                settings_.hapticDurationSeconds);
            hapticLow_ = (std::max)(hapticLow_, 0.45f + intensity * 0.45f);
            hapticHigh_ = (std::max)(hapticHigh_, 0.30f + intensity * 0.55f);

            if (!dedicatedVehicleImpact) {
                PlayerDamageAudioCue audio{};
                audio.resultSequence = result.sequence;
                audio.cueId = result.lethal
                    ? "player_damage_lethal"
                    : result.request.kind == PlayerHitKind::EnemyProjectile
                        ? "player_damage_projectile"
                        : "player_damage_collision";
                audio.volume = (std::clamp)(0.42f + intensity * 0.28f, 0.0f, 1.0f);
                audio.pitch = result.lethal
                    ? 0.78f
                    : 1.04f - normalizedDamage * 0.16f;
                audio.lethal = result.lethal;
                frame_.audioCues.push_back(std::move(audio));

                PlayerDamageVfxCommand vfx{};
                vfx.resultSequence = result.sequence;
                vfx.effectId = result.request.impactEffectId.empty()
                    ? "ice_impact"
                    : result.request.impactEffectId;
                vfx.railDistance = result.request.railDistance;
                vfx.lateralOffset = result.request.lateralOffset;
                vfx.verticalOffset = result.request.verticalOffset;
                vfx.radius = result.lethal ? 2.6f : 1.5f + normalizedDamage * 0.7f;
                vfx.color = result.request.kind == PlayerHitKind::EnemyProjectile
                    ? Vector4{0.68f, 0.90f, 1.0f, 1.0f}
                    : Vector4{1.0f, 0.55f, 0.18f, 1.0f};
                vfx.lethal = result.lethal;
                frame_.vfxCommands.push_back(std::move(vfx));
            }

            frame_.hitPointsBefore = result.hitPointsBefore;
            frame_.hitPointsAfter = result.hitPointsAfter;
            ++frame_.acceptedHits;
            frame_.lethal = frame_.lethal || result.lethal;
        }
    }
    frame_.lastDamage = lastDamage_;
    frame_.damageNoticeRemainingSeconds = damageNoticeRemainingSeconds_;
    frame_.showDamageNotice = lastDamage_.accepted &&
        (lastDamage_.lethal || damageNoticeRemainingSeconds_ > 0.0f);
    frame_.screenFlashIntensity = flashIntensity_;
    frame_.screenFlashColor = frame_.lethal
        ? Vector4{1.0f, 0.08f, 0.04f, 1.0f}
        : Vector4{1.0f, 0.22f, 0.10f, 1.0f};
    frame_.hapticLow = hapticLow_;
    frame_.hapticHigh = hapticHigh_;
    frame_.hapticRemainingSeconds = hapticRemainingSeconds_;
    frame_.revision = ++revision_;
}
