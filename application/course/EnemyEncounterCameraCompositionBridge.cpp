#include "EnemyEncounterCameraCompositionBridge.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "CourseSpawnRuntime.h"
#include "../terrain/RailPath.h"

namespace {
Vector3 Add(Vector3 a, Vector3 b) { return {a.x+b.x,a.y+b.y,a.z+b.z}; }
Vector3 Sub(Vector3 a, Vector3 b) { return {a.x-b.x,a.y-b.y,a.z-b.z}; }
Vector3 Scale(Vector3 v, float s) { return {v.x*s,v.y*s,v.z*s}; }
float Dot(Vector3 a, Vector3 b) { return a.x*b.x+a.y*b.y+a.z*b.z; }
float Smooth(float value,float target,float rate,float dt) {
    return value+(target-value)*(1.0f-std::exp(-rate*dt));
}
float PhaseWeight(EnemyEncounterBeatPhase phase) {
    switch (phase) {
    case EnemyEncounterBeatPhase::Establish: return 0.72f;
    case EnemyEncounterBeatPhase::Threaten: return 1.00f;
    case EnemyEncounterBeatPhase::Attack: return 0.86f;
    case EnemyEncounterBeatPhase::Recovery: return 0.48f;
    case EnemyEncounterBeatPhase::ExitResolve: return 0.58f;
    default: return 0.0f;
    }
}
bool Living(const CourseEnemyActor& actor) {
    return !actor.stationaryDormant && actor.desc.hitPoints>0 && !actor.entranceExitState.exitComplete &&
        actor.entranceExitState.phase!=EnemyEntranceExitPhase::Exited &&
        (!actor.combatState.initialized || (actor.combatState.currentHitPoints>0 &&
         actor.combatState.phase!=EnemyCombatPhase::Dying && actor.combatState.phase!=EnemyCombatPhase::Retired));
}
}

void EnemyEncounterCameraCompositionBridge::Reset() {
    frame_ = {};
    blend_ = 0.0f;
    focusOffset_ = {0,2,42};
    previousDistance_ = 0;
    hasRailHistory_ = false;
    revision_ = 0;
}

const EnemyEncounterCameraCompositionFrame& EnemyEncounterCameraCompositionBridge::Update(
    const EnemyEncounterCameraCompositionInput& input) {
    // Pause/result must preserve composition, including an in-progress release.
    if (!input.gameplayActive) return frame_;
    EnemyEncounterCameraCompositionFrame next = frame_;
    next.revision = ++revision_;
    const bool requested = input.pacing && input.pacing->active && input.pacing->cameraCompositionRequested;
    const float dt = std::isfinite(input.deltaTime) ? (std::clamp)(input.deltaTime,0.0f,0.25f) : 0.0f;
    blend_ = Smooth(blend_,requested?1.0f:0.0f,requested?6.5f:3.8f,dt);
    next.blend = blend_;
    next.active = requested || blend_>0.001f;
    const float phase = requested ? PhaseWeight(input.pacing->phase) : 0.0f;
    if (requested) {
        next.phase = input.pacing->phase;
        next.beatGuid = input.pacing->activeBeatGuid;
        next.preferredCameraShotId = input.pacing->definition.cameraShotId;
    }
    // Blend every output, not only the request envelope. A phase switch or an
    // empty pacing frame used to replace focus/FOV/back offsets in one tick.
    const auto* definition = requested ? &input.pacing->definition : nullptr;
    next.preferredCameraShotWeight = Smooth(frame_.preferredCameraShotWeight,
        definition?definition->cameraWeight*phase*blend_:0.0f,5.0f,dt);
    next.focusWeight = Smooth(frame_.focusWeight,
        definition?(std::clamp)(definition->cameraFocusWeight*phase*blend_,0.0f,1.0f):0.0f,5.0f,dt);
    next.fovOffsetRadians = Smooth(frame_.fovOffsetRadians,
        definition?definition->cameraFovOffsetDegrees*0.01745329252f*phase*blend_:0.0f,5.0f,dt);
    next.backDistanceOffset = Smooth(frame_.backDistanceOffset,
        definition?definition->cameraBackDistanceOffset*phase*blend_:0.0f,5.0f,dt);
    next.focusActorCount = 0;
    next.hasFocusWorld = false;
    if (input.railPath && input.railPath->Length()>0) {
        const auto anchor = input.railPath->Evaluate(input.playerDistance);
        Vector3 desired{0,2,42}, center{};
        if (requested && input.runtime) {
            for (const auto& actor:input.runtime->Enemies()) {
                if (actor.desc.waveId!=input.pacing->waveGuid || !Living(actor)) continue;
                const auto sample = input.railPath->EvaluateExtended(actor.desc.spawnDistance+actor.desc.distanceOffset);
                center = Add(center,Add(sample.position,Add(Scale(sample.right,actor.desc.lateralOffset),
                    Scale(sample.up,actor.desc.verticalOffset))));
                ++next.focusActorCount;
            }
            if (next.focusActorCount>0) {
                const auto delta = Sub(Scale(center,1.0f/next.focusActorCount),anchor.position);
                desired = {(std::clamp)(Dot(delta,anchor.right),-14.0f,14.0f),
                    (std::clamp)(Dot(delta,anchor.up),-10.0f,12.0f),
                    (std::clamp)(Dot(delta,anchor.tangent),22.0f,95.0f)};
            }
        }
        // Store offsets in the moving rail frame: smoothing world coordinates
        // drags the gaze behind a fast cart. Seek/reset clears only this history.
        if (!hasRailHistory_ || std::abs(input.playerDistance-previousDistance_)>80.0f) focusOffset_={0,2,42};
        Vector3 correction = Scale(Sub(desired,focusOffset_),1.0f-std::exp(-5.0f*dt));
        const float length = std::sqrt(Dot(correction,correction));
        if (length>24.0f*dt && length>0) correction=Scale(correction,24.0f*dt/length);
        focusOffset_=Add(focusOffset_,correction);
        next.focusWorld=Add(anchor.position,Add(Scale(anchor.right,focusOffset_.x),
            Add(Scale(anchor.up,focusOffset_.y),Scale(anchor.tangent,focusOffset_.z))));
        next.hasFocusWorld=true;
        previousDistance_=input.playerDistance;
        hasRailHistory_=true;
    }
    if (!next.active) {
        next.focusWeight=next.preferredCameraShotWeight=next.fovOffsetRadians=next.backDistanceOffset=0;
        next.preferredCameraShotId.clear();
    }
    frame_=std::move(next);
    return frame_;
}
