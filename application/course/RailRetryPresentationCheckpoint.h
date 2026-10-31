#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include "RailMinibossCombatDirector.h"
#include "GameSessionPresentationBridge.h"
#include "EnemyEncounterReadabilityDirector.h"
#include "ThreatResponseDirector.h"
#include "PlayerCombatFeelSystem.h"
#include "RailVehicleAudioBridge.h"
#include "RailVehiclePresentationBridge.h"
#include "RailVehicleRideDynamicsSystem.h"
#include "RailVehicleTrackContactPoseSolver.h"
#include "RailVehicleWheelContactPresentationBridge.h"
#include "RailVehicleCameraInertiaBridge.h"
#include "RailVehicleActor.h"
#include "RailVehicleRenderer.h"
#include "RailVehicleCombatMountBridge.h"
#include "RailVehicleCameraMountBridge.h"
#include "RailVehicleMountedEvasionPresentationBridge.h"
#include "RailVehicleOccupantActor.h"
#include "RailCameraDirector.h"
#include "EnemyEncounterCameraCompositionBridge.h"
#include "RailSpeedDirector.h"
#include "RailRideDirector.h"
#include "RailRideMotionEnvelope.h"
#include "RailTrackFeedbackDirector.h"
#include "EncounterDirector.h"
#include "RailGateBridgeFlow.h"
#include "RailRidgeCombatFlow.h"
#include "RailTitleScene.h"
#include "RailMothershipPresentation.h"
#include "RailMothershipBattle.h"
#include "RailShooterHudRuntimeModel.h"
#include "RailShooterHudPresentationBridge.h"

// Copy the complete follower histories, including spring velocities and entry
// clocks. Saving only the current render pose cannot reproduce the next frame.
struct RailRetryFollowState final {
    EnemyEncounterReadabilityDirector readability{};
    ThreatResponseDirector threat{};
    PlayerCombatFeelSystem combatFeel{};
    RailVehicleAudioBridge vehicleAudio{};
    RailVehiclePresentationBridge vehicle{};
    RailVehicleRideDynamicsSystem rideDynamics{};
    RailVehicleTrackContactPoseSolver trackContact{};
    RailVehicleWheelContactPresentationBridge wheels{};
    RailVehicleCameraInertiaBridge cameraInertia{};
    RailVehicleActor actor{};
    RailVehicleRenderer renderer{};
    RailVehicleCombatMountBridge combatMount{};
    RailVehicleCameraMountBridge cameraMount{};
    RailVehicleMountedEvasionPresentationBridge evasion{};
    RailVehicleOccupantActor occupant{};
    RailCameraDirector camera{};
    EnemyEncounterCameraCompositionBridge encounterCamera{};
    RailSpeedDirector speed{};
    RailRideDirector ride{};
    RailRideMotionEnvelope motion{};
    RailTrackFeedbackDirector trackFeedback{};
    EncounterDirector encounters{};
    RailGateBridgeFlow gateBridge{};
    RailRidgeCombatFlow ridge{};
    RailTitleGameplayEntry entry{};
    RailReadyDroneSalvo readySalvo{};
    std::array<uint32_t,kRailReadyDroneCount> readyIds{};
    RailMothership::Reveal mothershipReveal{};
    RailMothership::Battle mothershipBattle{};
    RailMinibossCombatDirector minibossCombat{};
    RailShooterHudRuntimeModel hudRuntime{};
    RailShooterHudPresentationBridge hud{};
    Matrix4x4 view{};
    Matrix4x4 projection{};
    Vector3 eye{};
    Vector3 cameraTranslation{};
    Vector3 cameraRotation{};
    float cameraFov{};
    float playerLateral{};
    float playerVertical{};
    uint32_t frameIndex{};
};

struct RailRetryFollowBindings final {
    EnemyEncounterReadabilityDirector& readability;
    ThreatResponseDirector& threat;
    PlayerCombatFeelSystem& combatFeel;
    RailVehicleAudioBridge& vehicleAudio;
    RailVehiclePresentationBridge& vehicle;
    RailVehicleRideDynamicsSystem& rideDynamics;
    RailVehicleTrackContactPoseSolver& trackContact;
    RailVehicleWheelContactPresentationBridge& wheels;
    RailVehicleCameraInertiaBridge& cameraInertia;
    RailVehicleActor& actor;
    RailVehicleRenderer& renderer;
    RailVehicleCombatMountBridge& combatMount;
    RailVehicleCameraMountBridge& cameraMount;
    RailVehicleMountedEvasionPresentationBridge& evasion;
    RailVehicleOccupantActor& occupant;
    RailCameraDirector& camera;
    EnemyEncounterCameraCompositionBridge& encounterCamera;
    RailSpeedDirector& speed;
    RailRideDirector& ride;
    RailRideMotionEnvelope& motion;
    RailTrackFeedbackDirector& trackFeedback;
    EncounterDirector& encounters;
    RailGateBridgeFlow& gateBridge;
    RailRidgeCombatFlow& ridge;
    RailTitleGameplayEntry& entry;
    RailReadyDroneSalvo& readySalvo;
    std::array<uint32_t,kRailReadyDroneCount>& readyIds;
    RailMothership::Reveal& mothershipReveal;
    RailMothership::Battle& mothershipBattle;
    RailMinibossCombatDirector& minibossCombat;
    RailShooterHudRuntimeModel& hudRuntime;
    RailShooterHudPresentationBridge& hud;
    Matrix4x4& view;
    Matrix4x4& projection;
    Vector3& eye;
    Vector3& cameraTranslation;
    Vector3& cameraRotation;
    float& cameraFov;
    float& playerLateral;
    float& playerVertical;
    uint32_t& frameIndex;
};

inline RailRetryFollowBindings BindRailRetryFollowState(RailRetryFollowState& state) {
    return {
        .readability=state.readability,
        .threat=state.threat,
        .combatFeel=state.combatFeel,
        .vehicleAudio=state.vehicleAudio,
        .vehicle=state.vehicle,
        .rideDynamics=state.rideDynamics,
        .trackContact=state.trackContact,
        .wheels=state.wheels,
        .cameraInertia=state.cameraInertia,
        .actor=state.actor,
        .renderer=state.renderer,
        .combatMount=state.combatMount,
        .cameraMount=state.cameraMount,
        .evasion=state.evasion,
        .occupant=state.occupant,
        .camera=state.camera,
        .encounterCamera=state.encounterCamera,
        .speed=state.speed,
        .ride=state.ride,
        .motion=state.motion,
        .trackFeedback=state.trackFeedback,
        .encounters=state.encounters,
        .gateBridge=state.gateBridge,
        .ridge=state.ridge,
        .entry=state.entry,
        .readySalvo=state.readySalvo,
        .readyIds=state.readyIds,
        .mothershipReveal=state.mothershipReveal,
        .mothershipBattle=state.mothershipBattle,
        .minibossCombat=state.minibossCombat,
        .hudRuntime=state.hudRuntime,
        .hud=state.hud,
        .view=state.view,
        .projection=state.projection,
        .eye=state.eye,
        .cameraTranslation=state.cameraTranslation,
        .cameraRotation=state.cameraRotation,
        .cameraFov=state.cameraFov,
        .playerLateral=state.playerLateral,
        .playerVertical=state.playerVertical,
        .frameIndex=state.frameIndex};
}

class RailRetryPresentationCheckpoint final {
public:
    void Reset() { state_.reset();run_=revision_=0; }
    bool Matches(uint64_t run,uint64_t revision) const { return state_ && run_==run && revision_==revision; }
    void Capture(uint64_t run,uint64_t revision,const RailRetryFollowBindings& live) {
        auto saved=std::make_unique<RailRetryFollowState>();
        saved->readability=live.readability;
        saved->threat=live.threat;
        saved->combatFeel=live.combatFeel;
        saved->vehicleAudio=live.vehicleAudio;
        saved->vehicle=live.vehicle;
        saved->rideDynamics=live.rideDynamics;
        saved->trackContact=live.trackContact;
        saved->wheels=live.wheels;
        saved->cameraInertia=live.cameraInertia;
        saved->actor=live.actor;
        saved->renderer=live.renderer;
        saved->combatMount=live.combatMount;
        saved->cameraMount=live.cameraMount;
        saved->evasion=live.evasion;
        saved->occupant=live.occupant;
        saved->camera=live.camera;
        saved->encounterCamera=live.encounterCamera;
        saved->speed=live.speed;
        saved->ride=live.ride;
        saved->motion=live.motion;
        saved->trackFeedback=live.trackFeedback;
        saved->encounters=live.encounters;
        saved->gateBridge=live.gateBridge;
        saved->ridge=live.ridge;
        saved->entry=live.entry;
        saved->readySalvo=live.readySalvo;
        saved->readyIds=live.readyIds;
        saved->mothershipReveal=live.mothershipReveal;
        saved->mothershipBattle=live.mothershipBattle;
        saved->minibossCombat=live.minibossCombat;
        saved->hudRuntime=live.hudRuntime;
        saved->hud=live.hud;
        saved->view=live.view;
        saved->projection=live.projection;
        saved->eye=live.eye;
        saved->cameraTranslation=live.cameraTranslation;
        saved->cameraRotation=live.cameraRotation;
        saved->cameraFov=live.cameraFov;
        saved->playerLateral=live.playerLateral;
        saved->playerVertical=live.playerVertical;
        saved->frameIndex=live.frameIndex;
        state_=std::move(saved);run_=run;revision_=revision;
    }
    bool Restore(uint64_t run,uint64_t revision,RailRetryFollowBindings live) const {
        if(!Matches(run,revision)) return false;
        live.readability=state_->readability;
        live.threat=state_->threat;
        live.combatFeel=state_->combatFeel;
        live.vehicleAudio=state_->vehicleAudio;
        live.vehicle=state_->vehicle;
        live.rideDynamics=state_->rideDynamics;
        live.trackContact=state_->trackContact;
        live.wheels=state_->wheels;
        live.cameraInertia=state_->cameraInertia;
        live.actor=state_->actor;
        live.renderer=state_->renderer;
        live.combatMount=state_->combatMount;
        live.cameraMount=state_->cameraMount;
        live.evasion=state_->evasion;
        live.occupant=state_->occupant;
        live.camera=state_->camera;
        live.encounterCamera=state_->encounterCamera;
        live.speed=state_->speed;
        live.ride=state_->ride;
        live.motion=state_->motion;
        live.trackFeedback=state_->trackFeedback;
        live.encounters=state_->encounters;
        live.gateBridge=state_->gateBridge;
        live.ridge=state_->ridge;
        live.entry=state_->entry;
        live.readySalvo=state_->readySalvo;
        live.readyIds=state_->readyIds;
        live.mothershipReveal=state_->mothershipReveal;
        live.mothershipBattle=state_->mothershipBattle;
        live.minibossCombat=state_->minibossCombat;
        live.hudRuntime=state_->hudRuntime;
        live.hud=state_->hud;
        live.view=state_->view;
        live.projection=state_->projection;
        live.eye=state_->eye;
        live.cameraTranslation=state_->cameraTranslation;
        live.cameraRotation=state_->cameraRotation;
        live.cameraFov=state_->cameraFov;
        live.playerLateral=state_->playerLateral;
        live.playerVertical=state_->playerVertical;
        live.frameIndex=state_->frameIndex;
        return true;
    }
private:
    std::unique_ptr<RailRetryFollowState> state_;
    uint64_t run_=0,revision_=0;
};

// Defeat and retry happened while this bridge was frozen by the wreck scene.
// Consume that history boundary without replaying old flashes or adding a
// retry-only camera impulse to the restored follower springs.
inline void PrimeRailRetrySessionPresentation(GameSessionPresentationBridge& bridge,
    const GameSessionSystem& session,const GameSessionPresentationSettings& settings={}) {
    const auto& history=session.EventHistory();
    bridge.Reset(history.empty()?0:history.back().sequence);
    GameSessionPresentationInput input;
    input.definition=&session.Definition();input.state=&session.State();input.eventHistory=&history;input.settings=settings;
    bridge.Update(input);
}

