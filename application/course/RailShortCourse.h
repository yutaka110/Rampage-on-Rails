#pragma once
#include <string_view>
#include <algorithm>
#include "GameSessionSystem.h"
#include "CourseGameplayWaveRuntimeBridge.h"

inline constexpr const char* RailShortCoursePath="Resources/courses/ShortCompleteRun.course";
inline bool IsRailShortCourse(std::string_view path) { return path==RailShortCoursePath; }
inline bool UsesCanyonPresentation(std::string_view path) {
    return IsRailShortCourse(path) || path=="Resources/courses/CanyonAssaultRoute01.course";
}
inline GameSessionDefinition ShortRunSessionDefinition() {
    auto definition=GameSessionDefinition::RailShooterDefaults();
    definition.sessionId="rail_shooter.short_complete_run";
    definition.resultDelaySeconds=1.0f;
    definition.timeLimitPerAttempt=true;
    definition.timeLimitSeconds=210.0f; // bounded failure rather than a stranded finish
    definition.startingRetries=2;
    return definition;
}
inline float ShortRunFinishDistance(float railLength) {
    return (std::min)(3800.0f, railLength);
}
inline float ShortRunTravelEndDistance(float railLength) {
    // The result gate and the track end are separate. Continue pursuing live
    // enemies beyond the gate; session truth decides when to show the result.
    return (std::max)(ShortRunFinishDistance(railLength),railLength-180.0f);
}
inline CourseGameplayWaveRuntimeSettings ShortRunWaveSettings() {
    CourseGameplayWaveRuntimeSettings settings;
    settings.earlyClearAdvanceEnabled = true;
    settings.earlyClearRecoverySeconds = 1.0f;
    settings.maximumEarlyAdvanceDistance = 180.0f;
    return settings;
}
inline bool ShortRunAllowsEarlyWave(float distance) {
    // A gate wave at 1080m must not be promoted into the preceding canyon.
    return distance < 900.0f || distance >= 3350.0f;
}
inline bool ShortRunAllowsAdaptivePatrol(float distance) {
    // READY, the carrier portrait, ignition showcase and boss approach keep
    // their authored breathing room. Ridge combat also protects its crest.
    return distance >= 160.0f && distance < 3270.0f &&
        !(distance >= 420.0f && distance < 620.0f) &&
        !(distance >= 950.0f && distance < 1950.0f) &&
        !(distance >= 2440.0f && distance < 2560.0f);
}
inline bool ShortRunPrimaryEncounterDue(const CourseGameplayWaveRuntimeBridge& waves,
    float distance, float speed) {
    // The opening wave has no preceding all-clear gate, so a side patrol cannot
    // starve it. Do not reserve several empty seconds before the first fight.
    if (!waves.IsBound() || waves.Stats().activeWaves > 0 || waves.Stats().completedWaves == 0) return false;
    const float lookahead = ShortRunAllowsEarlyWave(distance)
        ? waves.Settings().maximumEarlyAdvanceDistance : (std::max)(0.0f, speed) * 2.5f;
    for (const auto& wave : waves.Program()->waves) {
        const auto phase = waves.Phase(wave.waveGuid);
        if (phase == CourseGameplayWavePhase::Disabled || phase == CourseGameplayWavePhase::Completed) continue;
        return wave.triggerRailDistance <= distance + lookahead;
    }
    return false;
}
inline bool IsRunResult(const GameSessionRuntimeState& state) {
    return state.phase==GameSessionPhase::Result;
}

enum class RailRunResultAction { None, Restart, Checkpoint, Title };
struct RailRunResultButton {
    float x=0,y=0,width=0,height=0;
    RailRunResultAction action=RailRunResultAction::None;
    bool Contains(float px,float py) const {
        return px>=x && px<=x+width && py>=y && py<=y+height;
    }
};
inline RailRunResultButton RunResultButton(float width,float height,int index) {
    const float s=(std::min)(width/1600.0f,height/900.0f);
    return {width*0.5f-280*s,height*0.5f+(58+index*62)*s,560*s,50*s,
        index==0?RailRunResultAction::Restart:(index==1?RailRunResultAction::Checkpoint:RailRunResultAction::Title)};
}
inline RailRunResultAction HitRunResult(float x,float y,float width,float height,bool canCheckpoint) {
    for(int i=0;i<3;++i) if((i!=1 || canCheckpoint) && RunResultButton(width,height,i).Contains(x,y))
        return RunResultButton(width,height,i).action;
    return RailRunResultAction::None;
}
