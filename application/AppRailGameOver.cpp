#include "AppRunLoop.h"
#include "AppLogFile.h"
#include "course/RailShortCourse.h"
#include "AppImGuiLayer.h"
#include "AppRuntimeState.h"
#include "AppSceneResources.h"
#include <DirectXMath.h>
#if defined(GE3_ENABLE_IMGUI) && GE3_ENABLE_IMGUI
#include "../externals/imgui/imgui.h"
#endif


RailRetryFollowBindings AppRunLoop::BindRailRetryFollowers() {
    return {
        .readability=railEnemyEncounterReadabilityDirector_,
        .threat=railThreatResponseDirector_,
        .combatFeel=railShooterCombatFeelSystem_,
        .vehicleAudio=railShooterVehicleAudioBridge_,
        .vehicle=railShooterVehiclePresentation_,
        .rideDynamics=railShooterVehicleRideDynamics_,
        .trackContact=railShooterTrackContactPose_,
        .wheels=railShooterWheelContactPresentation_,
        .cameraInertia=railShooterCameraInertia_,
        .actor=railShooterVehicleActor_,
        .renderer=railShooterVehicleRenderer_,
        .combatMount=railShooterCombatMountBridge_,
        .cameraMount=railShooterCameraMountBridge_,
        .evasion=railShooterMountedEvasionPresentation_,
        .occupant=railShooterOccupantActor_,
        .camera=railShooterCameraDirector_,
        .encounterCamera=railEnemyEncounterCameraCompositionBridge_,
        .speed=railShooterSpeedDirector_,
        .ride=railShooterRideDirector_,
        .motion=railShooterRideMotionEnvelope_,
        .trackFeedback=railShooterTrackFeedbackDirector_,
        .encounters=railShooterEncounterDirector_,
        .entry=railTitleGameplayEntry_,
        .readySalvo=railReadyDroneSalvo_,
        .readyIds=railReadyDroneActorIds_,
        .mothershipReveal=railMothershipReveal_,
        .mothershipBattle=railMothershipBattle_,
        .hudRuntime=railShooterHudRuntimeModel_,
        .hud=railShooterHudPresentation_,
        .view=frameState_.viewMatrix,
        .projection=frameState_.projMatrix,
        .eye=frameState_.cameraWorldPosition,
        .cameraTranslation=runtimeState_.camera.transform.translate,
        .cameraRotation=runtimeState_.camera.transform.rotate,
        .cameraFov=runtimeState_.camera.fovY,
        .playerLateral=railShooterPlayerLateralOffset_,
        .playerVertical=railShooterPlayerVerticalOffset_,
        .frameIndex=railShooterFrameIndex_
    };
}

bool AppRunLoop::CaptureRailCheckpoint(const std::string& id) {
    std::string error;
    if(!railShooterRetryCoordinator_.CaptureCheckpoint(railShooterDistance_,id,&error)) {
        OutputDebugStringA(("[GameSessionRetry] Capture failed: "+error+"\n").c_str());return false;
    }
    const auto& checkpoint=railShooterRetryCoordinator_.Checkpoint();
    railRetryPresentation_.Capture(checkpoint.runIndex,checkpoint.revision,BindRailRetryFollowers());
    railFirstPlayableCheckpointPending_=false;
    LogRailRetryBoundary("capture");return true;
}

void AppRunLoop::LogRailRetryBoundary(const char* boundary) {
    std::ofstream log("logs/rail_retry_state.log",std::ios::app);
    if(!log) return;
    const auto& s=railShooterGameSession_.State();const auto& v=railShooterVehicleMovement_.State();
    const auto& body=railShooterVehiclePresentation_.Frame();const auto& eye=frameState_.cameraWorldPosition;
    log<<boundary<<" run="<<s.runIndex<<" attempt="<<s.attemptIndex
       <<" checkpoint="<<railShooterRetryCoordinator_.Checkpoint().revision
       <<" distance="<<railShooterDistance_<<" vehicleDistance="<<v.distance<<" speed="<<v.speed
       <<" bank="<<body.visualBankDegrees<<" pitch="<<body.visualPitchDegrees
       <<" eye="<<eye.x<<','<<eye.y<<','<<eye.z<<" entry="<<railTitleGameplayEntry_.Elapsed()
       <<" attemptTime="<<s.attemptElapsedSeconds<<'\n';
}

void AppRunLoop::UpdateRailGameOver(float deltaTime) {
    const bool focused=hwnd_==nullptr || GetForegroundWindow()==hwnd_;
    const float dt=focused && std::isfinite(deltaTime)?(std::clamp)(deltaTime,0.0f,0.05f):0.0f;
    if(!railGameOverScene_.Active()) {
        const auto prepareStart=std::chrono::steady_clock::now();
        LogRailRetryBoundary("defeat");
        railGameOverWorld_=std::make_unique<CourseSpawnRuntime>(railShooterSpawnRuntime_);
        const Vector3 forward{frameState_.viewMatrix.m[0][2],frameState_.viewMatrix.m[1][2],frameState_.viewMatrix.m[2][2]};
        const Vector3 up{frameState_.viewMatrix.m[0][1],frameState_.viewMatrix.m[1][1],frameState_.viewMatrix.m[2][1]};
        const float fov=2*std::atan(1/(std::max)(0.001f,frameState_.projMatrix.m[1][1]));
        // Keep contact geometry identical to the visible frozen terrain. No
        // worker/LOD change may move the floor while the wreck is playing.
        std::vector<TerrainGroundTriangle> triangles;
        const auto& vehicleState=railShooterVehicleMovement_.State();
        const float groundEnd=vehicleState.distance+(std::max)(100.0f,vehicleState.speed*4.0f);
        for(const auto& chunk:terrainChunkManager_.RenderChunks()) {
            if(chunk.endDistance<vehicleState.distance-16 || chunk.startDistance>groundEnd) continue;
            triangles.insert(triangles.end(),chunk.groundTriangles.begin(),chunk.groundTriangles.end());
        }
        const auto ground=std::make_shared<TerrainGroundSurface>(std::move(triangles));
        railGameOverScene_.Begin(railShooterGameSession_.State(),railShooterVehicleMovement_.State(),
            railShooterVehicleActor_.Frame(),railShooterVehicleRenderer_.Frame(),
            railShooterWheelContactPresentation_.Frame(),railShooterOccupantActor_.Frame(),
            frameState_.cameraWorldPosition,forward,railPath_,up,fov,
            &railShooterTrackMeshBake_.Result(),&railShooterTrackContactPoseSettings_,
            [ground](Vector3 position,float ceiling,TerrainGroundSurfaceHit& hit){return ground->Sample(position,hit,ceiling);});
        if(auto log=app::OpenRotatingLog("logs/rail_game_over_contact.log"))
            log<<"triangles="<<ground->TriangleCount()<<" contact="<<railGameOverScene_.Frame().groundContactTime
               <<" fade="<<railGameOverScene_.FadeStartTime()<<" prepareMs="
               <<std::chrono::duration<float,std::milli>(std::chrono::steady_clock::now()-prepareStart).count()<<'\n';
        railShooterLockOnSystem_.Reset();railBoostVfx_.Reset();
        StopRailEnemyAttackFeedback();
        audio_.Stop(railBoostIgnitionSound_);
        if(focused && railGameOverScene_.Frame().wreck) (void)audio_.Play(railVehicleCollisionSound_,0.22f);
        // Flush held gameplay buttons at entry, including a mouse shot that
        // happened on the fatal frame. Only new menu edges can retry.
        for(int key=0;key<256;++key) previousKeyDown_[key]=(GetAsyncKeyState(key)&0x8000)!=0;
        previousLeftMouseDown_=(GetAsyncKeyState(VK_LBUTTON)&0x8000)!=0;
    }
    RailGameOverInput input{};input.enabled=focused && !railTransitionDemo_.Returning();
#if defined(GE3_ENABLE_IMGUI) && GE3_ENABLE_IMGUI
    if(imguiLayer_.IsVisible() && (ImGui::GetIO().WantTextInput || ImGui::IsAnyItemActive())) input.enabled=false;
#endif
    // Poll every binding even while hidden or unfocused, never short-circuit
    // edge sampling; an old held key must not fire when focus returns.
    const bool up=WasKeyPressed(VK_UP),w=WasKeyPressed('W');
    const bool down=WasKeyPressed(VK_DOWN),s=WasKeyPressed('S');
    const bool enter=WasKeyPressed(VK_RETURN),r=WasKeyPressed('R'),t=WasKeyPressed('T');
    const bool click=WasKeyPressed(VK_LBUTTON);
    const auto gamepad=railAimGamepad_.Poll();
    input.up=up || w || gamepad.leftShoulderPressed;input.down=down || s || gamepad.rightShoulderPressed;
    input.confirm=enter || gamepad.toggleSkeletonPressed;input.retry=r;input.title=t;
    POINT cursor{},point{};uint32_t width=0,height=0;
    if(GetCursorPos(&cursor) && (hwnd_==nullptr || ScreenToClient(hwnd_,&cursor)) &&
        ResolveEditorViewportClientPoint(cursor,point,width,height)) {
        input.pointerValid=true;input.pointerX=float(point.x);input.pointerY=float(point.y);
        input.width=float(width);input.height=float(height);input.pointerPressed=click;
    }
    const float previous=railGameOverScene_.Frame().elapsed;
    const size_t previousFaultBursts=railGameOverScene_.Frame().faultBurstCount;
    const auto action=railGameOverScene_.Update(dt,railPath_,railShooterRetryCoordinator_.CanRetry(),input);
    const auto& frame=railGameOverScene_.Frame();
    if(focused && frame.wreck) for(size_t i=previousFaultBursts;i<frame.faultBurstCount;++i) {
        const auto& burst=RailGameOverScene::FaultBursts[i];
        (void)audio_.PlaySpatial(railEnemyProjectileImpactSound_,0.10f+0.12f*burst.strength,
            burst.side*0.55f,1.70f-0.25f*burst.strength);
    }
    if(focused && frame.wreck && previous<RailGameOverScene::BrakeBegin && frame.elapsed>=RailGameOverScene::BrakeBegin)
        (void)audio_.Play(railVehicleBrakeSound_,0.35f);
    if(focused && frame.wreck && previous<RailGameOverScene::DerailBegin && frame.elapsed>=RailGameOverScene::DerailBegin)
        (void)audio_.Play(railVehicleCollisionSound_,0.28f);
    if(focused && frame.groundContactTime>=0 && previous<frame.groundContactTime && frame.elapsed>=frame.groundContactTime) {
        (void)audio_.Play(railVehicleCollisionSound_,0.38f);
    }
    if(focused && frame.secondaryContactTime>=0 && previous<frame.secondaryContactTime && frame.elapsed>=frame.secondaryContactTime) {
        (void)audio_.Play(railVehicleCollisionSound_,0.58f);
        (void)audio_.PlaySpatial(railEnemyProjectileImpactSound_,0.22f,0.0f,1.40f);
    }
    if(focused && frame.shatterTime>=0 && previous<frame.shatterTime && frame.elapsed>=frame.shatterTime) {
        (void)audio_.Play(railVehicleCollisionSound_,0.85f);
        (void)audio_.PlaySpatial(railEnemyProjectileImpactSound_,1.0f,0.0f,0.55f);
    }
    if(focused && frame.shatterTime<0 && frame.recontactTime>=0 && previous<frame.recontactTime && frame.elapsed>=frame.recontactTime) {
        (void)audio_.Play(railVehicleCollisionSound_,0.60f);
        (void)audio_.Play(railVehicleStopSound_,0.30f);
    }
    UpdateRailRollingAudio((std::clamp)(deltaTime,0.0f,0.05f),
        focused && frame.wreck && (frame.elapsed<RailGameOverScene::DetachBegin || frame.grounded)?0.28f*frame.speedRatio*(1-frame.blackout):0.0f,0.15f,0.8f+0.2f*frame.speedRatio);
    const float visibleDt=previous<railGameOverScene_.FadeEndTime()?dt:0.0f;
    frameState_.deltaTime=visibleDt;
    DirectX::XMFLOAT4X4 view;
    DirectX::XMStoreFloat4x4(&view,DirectX::XMMatrixLookAtLH(
        DirectX::XMVectorSet(frame.cameraEye.x,frame.cameraEye.y,frame.cameraEye.z,1),
        DirectX::XMVectorSet(frame.cameraTarget.x,frame.cameraTarget.y,frame.cameraTarget.z,1),
        DirectX::XMVectorSet(frame.cameraUp.x,frame.cameraUp.y,frame.cameraUp.z,0)));
    for(int row=0;row<4;++row) for(int column=0;column<4;++column) frameState_.viewMatrix.m[row][column]=view.m[row][column];
    if(frame.wreck) {
        const float aspect=frameState_.projMatrix.m[0][0]>0.001f?
            frameState_.projMatrix.m[1][1]/frameState_.projMatrix.m[0][0]:16.0f/9.0f;
        frameState_.projMatrix=MakePerspectiveFovMatrix(frame.cameraFovY,aspect,0.1f,5000.0f);
    }
    frameState_.viewProjectionMatrix=Multiply(frameState_.viewMatrix,frameState_.projMatrix);
    frameState_.cameraWorldPosition=frame.cameraEye;runtimeState_.cameraWorldPosition=frame.cameraEye;
    scene_.UpdateCameraWorldPosition(frame.cameraEye);
    scene_.debugDraw.BeginFrame();scene_.debugDraw.Upload(frameState_.viewProjectionMatrix);
    scene_.enemyThreatVisualDraw.BeginFrame();scene_.enemyThreatVisualDraw.Upload(frameState_.viewProjectionMatrix);
    if(railGameOverWorld_ && visibleDt>0) {
        CourseEnemyFireSafetyFrameInput motion;
        motion.railPath=&railPath_;
        motion.ingressRoutes=railEnemyIngressRoutes_;
        motion.course=&railShooterCourse_;
        motion.terrainSettings=&runtimeState_.terrain.settings;
        motion.terrainEdits=&railShooterCourse_.terrainEditLayer;
        motion.terrainPreview=&runtimeState_.terrain.previewEditLayer;
        motion.terrainCollision=&terrainCollisionWorld_;
        motion.playerDistance=railShooterDistance_+railShooterVehicleMovement_.State().speed*frame.elapsed;
        motion.playerSpeed=railShooterVehicleMovement_.State().speed;
        motion.playerLateralOffset=railShooterPlayerLateralOffset_;
        motion.playerVerticalOffset=railShooterPlayerVerticalOffset_;
        motion.cameraPosition=frame.cameraEye;motion.hasCameraPosition=true;
        railGameOverWorld_->UpdateDefeatPresentation(visibleDt,motion);
        if(IsRailShortCourse(railShooterCoursePath_)) {
            const auto& terrain=runtimeState_.terrain.settings;
            railMothershipReveal_.Update(visibleDt,motion.playerDistance,
                terrain.openCanyonStartDistance+terrain.openCanyonTransitionLength,railPath_,true);
            railMothershipBattle_.Update(railMothershipReveal_.Current(),*railGameOverWorld_,
                railShooterGameplayWaveBridge_.Phase(RailMothership::BossWaveId),railPath_,motion.playerDistance,visibleDt);
        }
        EnemyCombatPresentationInput combat;
        combat.runtime=railGameOverWorld_.get();combat.railPath=&railPath_;
        combat.deltaTime=visibleDt;combat.gameplayActive=true;
        combat.settings=railEnemyCombatPresentationSettings_;
        combat.listenerPosition=frame.cameraEye;
        railEnemyCombatPresentationBridge_.Update(combat);
        railGameOverWorld_->SubmitPendingVfx(vfxEngine_.Runtime(),railPath_);
    }
    const Vector3 cameraRight{frameState_.viewMatrix.m[0][0],frameState_.viewMatrix.m[1][0],frameState_.viewMatrix.m[2][0]};
    DispatchEnemyProjectilePresentation(visibleDt,false,frame.cameraEye,cameraRight,frame.cameraUp);
    railPulseShots_.Update(visibleDt);railPrimaryKills_.Update(visibleDt);
    railLockVolley_.UpdateDefeatPresentation(visibleDt);
    vfxEngine_.Update(runtimeState_.vfx,visibleDt);
    GameSessionFrameInput tick{};tick.deltaTime=dt;tick.courseDistance=railShooterDistance_;
    railShooterGameSession_.Update(tick);
    if(action==RailGameOverAction::Checkpoint) {
        if(RetryRailCheckpoint()) (void)audio_.Play(railSessionRetrySound_,0.35f);
    } else if(action==RailGameOverAction::Title) {
        railTransitionDemo_.BeginGameplay();
        if(railTransitionDemo_.RequestReturn(true)) {
            railResultReturnActive_=true;railTransitionDemoEnabled_=true;railTransitionDemoHasLastUpdate_=false;
        }
    }
    // Actors and rails use frameState_ in Render. Refresh terrain with that
    // same final camera, including a checkpoint restore performed above.
    terrainChunkManager_.SyncViewProjection(frameState_.viewProjectionMatrix);
}

bool AppRunLoop::RetryRailCheckpoint() {
    if(!railShooterRetryCoordinator_.CanRetry()) return false;
    std::string retryError;
    const GameSessionRetryResult retry =
        railShooterRetryCoordinator_.Retry(&retryError);
    if (retry.succeeded) {
        railGameOverScene_.Reset();railGameOverWorld_.reset();
        railGameOverHasLastUpdate_=false;
        railShooterHasLastUpdateTime_=false;
        for(int key=0;key<256;++key) previousKeyDown_[key]=(GetAsyncKeyState(key)&0x8000)!=0;
        previousLeftMouseDown_=(GetAsyncKeyState(VK_LBUTTON)&0x8000)!=0;
        railShooterDistance_ = retry.restoredDistance;
        runtimeState_.terrain.previewDistance = retry.restoredDistance;
        railShooterPlayerVehicleMount_.Reset();
        railShooterVehicleBodyCollision_.Reset();
        railShooterVehicleCollisionFeedback_.Reset();
        railShooterCombatMountBridge_.Reset();
        railShooterCameraMountBridge_.Reset();
        railShooterOccupantClearance_.Reset();
        railShooterEvasionConstraintResolver_.Reset();
        railShooterMountedEvasionPresentation_.Reset();
        railShooterOccupantActor_.Reset();
        railShooterEvasionFeedback_.Reset();
        railShooterPlayerLateralOffset_ =
            railShooterVehicleMovement_.Definition().mounts.player.x;
        railShooterPlayerVerticalOffset_ =
            railShooterVehicleMovement_.Definition().bodyVerticalOffset +
            railShooterVehicleMovement_.Definition().mounts.player.y;
        railShooterVehiclePresentation_.Reset();
        railShooterVehicleRideDynamics_.Reset();
        railShooterTrackContactPose_.Reset();
        railShooterCameraInertia_.Reset();
        railShooterVehicleActor_.Reset();
        railShooterVehicleRenderer_.Reset();
        railShooterVehicleAudioBridge_.Reset();
        railEnemyCombatPresentationBridge_.Reset();
        ResetEnemyProjectilePresentation(false);
        railPlayerDamagePresentationBridge_.Reset();
        railEnemyAttackTelegraphSystem_.Reset();
        StopRailEnemyAttackFeedback();
        railShooterCombatFeelSystem_.Reset();
        railShooterEncounterDirector_.Reset();
        railEnemyEncounterCameraCompositionBridge_.Reset();
        railShooterCameraDirector_.Reset();
        railMothershipBattle_.Reset();railMothershipReveal_.Reset();
        railShooterLockOnSystem_.Reset(); railLockVolley_.Reset();
        railShooterSpeedDirector_.Reset();
        railShooterRideDirector_.Reset();
        railShooterTrackFeedbackDirector_.Reset();
        railShooterRideMotionEnvelope_.Reset();
        const auto& checkpoint=railShooterRetryCoordinator_.Checkpoint();
        if(railRetryPresentation_.Restore(checkpoint.runIndex,checkpoint.revision,BindRailRetryFollowers())) {
            frameState_.viewProjectionMatrix=Multiply(frameState_.viewMatrix,frameState_.projMatrix);
            runtimeState_.cameraWorldPosition=frameState_.cameraWorldPosition;
            scene_.UpdateCameraWorldPosition(frameState_.cameraWorldPosition);
            LogRailRetryBoundary("restore");
        } else {
            OutputDebugStringA("[GameSessionRetry] Follow-state snapshot unavailable; followers were reset.\n");
        }
        PrimeRailRetrySessionPresentation(railShooterSessionPresentation_,railShooterGameSession_,railShooterSessionPresentationSettings_);
        railPulseShots_.Reset(); railPrimaryKills_.Reset(); railBoostVfx_.Reset(); audio_.Stop(railBoostIgnitionSound_);
        OutputDebugStringA(("[GameSessionRetry] " + retry.message + "\n").c_str());
    } else {
        OutputDebugStringA(("[GameSessionRetry] " + retryError + "\n").c_str());
    }
    return retry.succeeded;
}
