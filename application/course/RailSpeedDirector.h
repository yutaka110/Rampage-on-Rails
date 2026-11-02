#pragma once

#include <string>
#include <vector>

#include "CourseAsset.h"
#include "RailManualBoost.h"
#include "RailKillBoost.h"
#include "../terrain/RailPath.h"

enum class RailSpeedZoneMode {
    Cruise,
    Combat,
    HighSpeed,
    Tunnel,
    Boss,
    Setpiece,
    Cinematic,
    Recovery,
};

struct RailSpeedDirectorSettings {
    bool enabled = true;
    // Applied by the shared motion envelope after authored ride overrides.
    float normalSpeedScale = 1.0f;
    float minSpeed = 10.0f;
    float maxSpeed = 64.0f;
    float cruiseMultiplier = 1.0f;
    float combatMultiplier = 0.82f;
    float highSpeedMultiplier = 1.26f;
    float tunnelMultiplier = 0.72f;
    float bossMultiplier = 0.76f;
    float setpieceMultiplier = 0.84f;
    float cinematicMultiplier = 0.72f;
    float recoveryMultiplier = 1.04f;
    float eventSlowMultiplier = 0.76f;
    float eventBoostMultiplier = 1.12f;
    float eventBlendDuration = 1.10f;

    bool Validate(std::string* errorMessage = nullptr) const;
};

struct RailSpeedDirectorFrameInput {
    const CourseAsset* course = nullptr;
    const RailPath* railPath = nullptr;
    const CourseSection* section = nullptr;
    const CourseRideProfileDefinition* rideProfile = nullptr;
    float distance = 0.0f;
    float deltaTime = 0.016f;
    bool killBoostAllowed = true;
};

struct RailSpeedDirectorFrame {
    float distance = 0.0f;
    float baseSpeed = 0.0f;
    // Policy output only. RailVehicleMovementSystem is the sole owner of
    // acceleration, braking, curve limiting and physical distance advance.
    float requestedSpeed = 0.0f;
    float zoneMultiplier = 1.0f;
    float eventMultiplier = 1.0f;
    RailSpeedZoneMode mode = RailSpeedZoneMode::Cruise;
    std::string modeName = "Cruise";
    std::string sectionName = "-";
    std::string reason = "rail";
    bool enabled = true;
};

class RailSpeedDirector {
public:
    bool Configure(
        const RailSpeedDirectorSettings& settings,
        bool preservePolicyState = false,
        std::string* errorMessage = nullptr);
    void Reset();
    void SetManualBoostInput(bool held, bool enabled) { manualBoost_.SetInput(held, enabled); }
    const RailManualBoost& ManualBoost() const { return manualBoost_; }
    void SetManualBoostMaximum(float value) { manualBoost_.SetHeldMaximumMultiplier(value); }
    RailKillBoost& KillBoost() { return killBoost_; }
    const RailKillBoost& KillBoost() const { return killBoost_; }
    float BoostMultiplier() const {
        const float manual=manualBoost_.SpeedMultiplier();
        // Start a reward from the achieved manual tier, so holding E does not
        // introduce a dead portion in its launch. This never stacks multipliers.
        return manual+(RailKillBoost::MaximumMultiplier-manual)*killBoost_.Blend();
    }
    float BoostPresentationStrength(float speed,float normalSpeed) const {
        if(!std::isfinite(speed) || !std::isfinite(normalSpeed) || normalSpeed<1)return 0;
        const float gain=RailManualBoost::RequestedSpeed(normalSpeed,RailKillBoost::MaximumMultiplier)-normalSpeed;
        return (std::min)((BoostMultiplier()-1)/(RailKillBoost::MaximumMultiplier-1),
            (std::clamp)((speed-normalSpeed)/(std::max)(1.0f,gain),0.0f,1.0f));
    }
    float BoostIgnitionPulse()const{return (std::max)(manualBoost_.IgnitionPulse()*.45f,killBoost_.IgnitionPulse());}
    float ManualPresentationStrength(float speed,float normalSpeed) const {
        // Normalize against the manual request, rather than the stronger reward.
        // Achieved travel still gates every effect through curves and collisions.
        if(!std::isfinite(speed) || !std::isfinite(normalSpeed) || normalSpeed<1)return 0;
        const float gain=RailManualBoost::RequestedSpeed(normalSpeed,manualBoost_.SpeedMultiplier())-normalSpeed;
        if(gain<.001f)return 0;
        return .82f*(std::min)(manualBoost_.Blend(),(std::clamp)((speed-normalSpeed)/gain,0.0f,1.0f));
    }
    float TravelPresentationStrength(float speed,float normalSpeed)const {
        const float cruise=settings_.enabled && settings_.normalSpeedScale>=1.30f &&
            std::isfinite(speed) && std::isfinite(normalSpeed) && normalSpeed>=1?
            .40f*(std::clamp)(speed/normalSpeed,0.0f,1.0f):0;
        return (std::max)({cruise,BoostPresentationStrength(speed,normalSpeed),ManualPresentationStrength(speed,normalSpeed)});
    }
    bool BoostStartedThisFrame()const{return manualBoost_.StartedThisFrame() || killBoost_.StartedThisFrame();}
    void NotifyCourseEvents(const std::vector<CourseEventMarker>& events);
    RailSpeedDirectorFrame Evaluate(const RailSpeedDirectorFrameInput& input);

    const RailSpeedDirectorSettings& Settings() const { return settings_; }
    const RailSpeedDirectorFrame& LastFrame() const { return lastFrame_; }

private:
    RailSpeedZoneMode ResolveMode(
        const CourseRideProfileDefinition* rideProfile,
        const CourseSection* section) const;
    float MultiplierForMode(RailSpeedZoneMode mode) const;

    RailSpeedDirectorSettings settings_{};
    RailManualBoost manualBoost_{};
    RailKillBoost killBoost_{};
    RailSpeedDirectorFrame lastFrame_{};
    float eventSlowTimer_ = 0.0f;
    float eventBoostTimer_ = 0.0f;
};

const char* ToRailSpeedZoneModeString(RailSpeedZoneMode mode);
