// Included inside the regression runner's private namespace. This is a
// diagnostics export complete encounters; focused cases verify corrected contracts.
void TestOrdinaryAimedIntercept(RegressionRunner& runner) {
    CourseAsset course;std::string error;runner.Expect(course.LoadFromFile(RailShortCoursePath,&error),"aimed interception loads real course");
    const auto cooked=CourseWaveRuntimeCompiler{}.Compile(course,{});runner.Expect(cooked.succeeded,"aimed interception compiles production assets");
    for(const std::string id:{"sniper_predictive_single","combat_expansion_homing_single","short_intercept_fan"}) {
        const auto found=std::find_if(cooked.program.actors.begin(),cooked.program.actors.end(),[&](const auto& p){return p.actor.bulletPatternId==id;});
        runner.Expect(found!=cooked.program.actors.end(),"production aimed actor exists: "+id);
        if(found==cooked.program.actors.end())continue;
        for(int fps:{30,60,120})for(float speed:{0.0f,30.0f,64.0f,73.0f})for(bool forecast:{false,true}) {
            const float dt=1.0f/fps;CourseSpawnRuntime runtime;auto desc=found->actor;
            desc.spawnDistance=200;desc.distanceOffset=0;desc.lateralOffset=20;desc.verticalOffset=14;
            desc.bulletCount=1;desc.bulletLateralSpreadSpeed=desc.bulletVerticalSpreadSpeed=0;
            runtime.SpawnEnemyActor(desc);EnemyTargetingSystem targeting;
            EnemyTargetingFrameInput input;input.deltaTime=dt;input.playerDistance=100-speed*dt;input.playerVerticalOffset=4;
            targeting.Update(runtime,input);auto actor=runtime.Enemies().front();
            actor.attackState.tokenReserved=true;actor.attackState.intentSequence=9;actor.attackState.tokenId=11;
            actor.behaviorState.engagementBandVelocity=speed;actor.behaviorState.attackTimeRemaining=1.7f;
            RestoreEnemyFixture(runner,runtime,actor);input.playerDistance=100;
            if(forecast) {
                auto prediction=std::make_shared<RailTravelPrediction>();prediction->sourceSpeed=speed;
                for(int sample=0;sample<=12*fps;++sample)prediction->samples.push_back({sample*dt,100+speed*sample*dt});
                input.travelPrediction=prediction;
            }
            targeting.Update(runtime,input);actor=runtime.Enemies().front();
            runner.Expect(actor.targetingState.compensatesForwardTravel && actor.targetingState.forwardInterceptReachable &&
                std::abs(actor.targetingState.targetDistance-100-speed*(1.7f+actor.targetingState.predictedFlightSeconds))<.003f,
                "sniper and homing warnings include launch delay and full forward interception");
            auto frozen=actor.targetingState;input.deltaTime=0;input.playerLateralOffset=8;targeting.Update(runtime,input);
            runner.Expect(runtime.Enemies().front().targetingState.targetDistance==frozen.targetDistance &&
                runtime.Enemies().front().targetingState.targetLateralOffset==frozen.targetLateralOffset,"pause cannot retarget an aimed warning");
            // Actual launch uses the current muzzle. Sideways input after this
            // point must not turn a predictive sniper shot into a homing shot.
            actor.behaviorState.attackTimeRemaining=0;RestoreEnemyFixture(runner,runtime,actor);
            input.deltaTime=dt;input.playerLateralOffset=0;input.playerDistance+=speed*dt;targeting.Update(runtime,input);actor=runtime.Enemies().front();
            const auto aim=ResolveEnemyProjectileScheduledLaunch(actor,actor.desc.bulletCount/2,0);
            EnemyProjectileSystem projectiles;std::vector<EnemyProjectileRuntimeState> bullets;projectiles.SpawnVolley(actor,bullets);
            runner.Expect(bullets.size()==static_cast<size_t>(actor.desc.bulletCount) &&
                std::all_of(bullets.begin(),bullets.end(),[](const auto& p){return p.frameEndLaunch;}),"aimed ordinary shot is born at the current frame boundary: "+id+
                " fps="+std::to_string(fps)+" speed="+std::to_string(speed)+" forecast="+std::to_string(forecast)+
                " bullets="+std::to_string(bullets.size())+" reachable="+std::to_string(aim.projectile.active)+
                " compensated="+std::to_string(actor.targetingState.compensatesForwardTravel));
            if(bullets.empty())continue;
            const size_t center=bullets.size()/2; // The authored fan retains its side-round spread.
            EnemyProjectileFrameInput step;step.deltaTime=dt;step.playerDistance=input.playerDistance;step.playerVerticalOffset=4;
            projectiles.Update(bullets,step);runner.Expect(bullets[center].age==0,"birth frame does not advance the projectile ahead of the cart");
            const auto saved=bullets;projectiles.RebuildFromProjectiles(bullets);
            runner.Expect(bullets[0].automaticForwardIntercept==saved[0].automaticForwardIntercept,"retry preserves the homing crossing contract");
            const float start=input.playerDistance;float closest=1000;
            const auto initial=bullets[center];
            for(int frame=1;frame<=int(std::ceil(aim.flightSeconds*fps));++frame) {
                step.playerDistance=start+speed*frame*dt;projectiles.Update(bullets,step);
                const auto& p=bullets[center];
                const Vector3 a{p.previousLateralOffset,p.previousVerticalOffset-4,p.previousDistanceOffset-(start+speed*(frame-1)*dt)};
                const Vector3 b{p.lateralOffset-a.x,p.verticalOffset-4-a.y,p.distanceOffset-step.playerDistance-a.z};
                const float n=b.x*b.x+b.y*b.y+b.z*b.z;
                const float t=n>1e-8f?(std::clamp)(-(a.x*b.x+a.y*b.y+a.z*b.z)/n,0.0f,1.0f):0;
                closest=(std::min)(closest,std::sqrt((a.x+t*b.x)*(a.x+t*b.x)+(a.y+t*b.y)*(a.y+t*b.y)+(a.z+t*b.z)*(a.z+t*b.z)));
                const float magnitude=std::sqrt(p.forwardSpeed*p.forwardSpeed+p.lateralSpeed*p.lateralSpeed+p.verticalSpeed*p.verticalSpeed);
                runner.Expect(magnitude<=p.maximumSpeed+.001f,"homing acceleration respects authored speed cap");
            }
            runner.Expect(closest<.08f,"aimed shot intersects neutral moving cart: "+id+" fps="+std::to_string(fps)+" speed="+std::to_string(speed)+" miss="+std::to_string(closest));
            auto dodge=initial;std::vector<EnemyProjectileRuntimeState> dodging{dodge};step.playerDistance=start+speed*dt;step.playerLateralOffset=8;projectiles.Update(dodging,step);
            const auto& turned=dodging[0];
            if(initial.trajectory==EnemyProjectileTrajectory::Homing) {
                const float n0=std::sqrt(initial.forwardSpeed*initial.forwardSpeed+initial.lateralSpeed*initial.lateralSpeed+initial.verticalSpeed*initial.verticalSpeed);
                const float n1=std::sqrt(turned.forwardSpeed*turned.forwardSpeed+turned.lateralSpeed*turned.lateralSpeed+turned.verticalSpeed*turned.verticalSpeed);
                const float agreement=(initial.forwardSpeed*turned.forwardSpeed+initial.lateralSpeed*turned.lateralSpeed+initial.verticalSpeed*turned.verticalSpeed)/(n0*n1);
                runner.Expect(initial.automaticForwardIntercept && std::acos((std::clamp)(agreement,-1.0f,1.0f))<=initial.homingTurnRateRadians*dt+.001f,
                    "homing lateral reacquisition remains limited by the authored turn rate");
            } else runner.Expect(std::abs(turned.forwardSpeed-initial.forwardSpeed)<.0001f &&
                std::abs(turned.lateralSpeed-initial.lateralSpeed)<.0001f && std::abs(turned.verticalSpeed-initial.verticalSpeed)<.0001f,
                "sniper remains straight after commitment when player moves sideways");
        }
    }
}

void TestPersistentOrdinaryEncounterPacing(RegressionRunner& runner) {
    CourseAsset course;std::string error;runner.Expect(course.LoadFromFile(RailShortCoursePath,&error),"pursuit pacing loads real course");
    const auto cooked=CourseWaveRuntimeCompiler{}.Compile(course,{});runner.Expect(cooked.succeeded,"pursuit pacing compiles real actors");
    const auto definition=course.encounterBeats.front();course.encounterBeats={definition};
    for(int fps:{30,60,120}) {
        const float dt=1.0f/fps;CourseSpawnRuntime runtime;
        for(auto index:cooked.program.waves.front().actorIndices)runtime.SpawnEnemyActor(cooked.program.actors[index].actor);
        EnemyEncounterReadabilityFrame readable;readable.truth.safeToResolveSession=true;
        for(const auto& actor:runtime.Enemies()) {EnemyEncounterActorReadability entry;entry.actorId=actor.actorId;entry.screenReadable=entry.onScreen=entry.targetable=true;readable.actors.push_back(entry);}
        EnemyEncounterPacingDirector pacing;EnemyEncounterPacingInput input;input.course=&course;input.runtime=&runtime;input.readability=&readable;input.deltaTime=dt;
        bool previousAttack=false;unsigned cycles=0;bool afterEnd=false;
        for(int step=0;step<20*fps;++step) {
            input.currentDistance=definition.triggerRailDistance+64*step*dt;
            const auto& frame=pacing.Update(input);const bool attack=frame.attackWindowOpen;
            if(attack && !previousAttack)++cycles;
            afterEnd|=attack && input.currentDistance>definition.endRailDistance;
            runner.Expect(frame.phase!=EnemyEncounterBeatPhase::ExitResolve && !frame.resolveStalled,
                "living pursuing scouts never enter distance-only resolve or a stalled no-attack state");
            previousAttack=attack;
        }
        runner.Expect(afterEnd && cycles>=2,"survivors receive repeated real attack windows after leaving the scenery interval");
        auto saved=pacing.CaptureCheckpoint();input.gameplayActive=false;input.deltaTime=1;pacing.Update(input);
        runner.Expect(pacing.CaptureCheckpoint().phaseElapsedSeconds==saved.phaseElapsedSeconds,"pause retains pursuit pacing timers");
        input.gameplayActive=true;input.deltaTime=dt;saved.phase=EnemyEncounterBeatPhase::ExitResolve;saved.phaseElapsedSeconds=4;
        runner.Expect(pacing.RestoreCheckpoint(saved,course,&error),"legacy resolve checkpoint restores");
        pacing.Update(input);runner.Expect(pacing.Frame().phase==EnemyEncounterBeatPhase::Threaten,
            "a retry restored beyond the old end resumes a full warning cycle");
        for(const auto& actor:runtime.Enemies())runner.Expect(runtime.DefeatEnemy(runtime.EnemyCombat(),actor.actorId),"defeating a survivor uses combat authority");
        for(int step=0;step<4*fps;++step)pacing.Update(input);
        runner.Expect(!pacing.Frame().active,"defeated formations still finish and release encounter ownership");
        CourseSpawnRuntime stationary;CourseEnemyActorDesc desc=cooked.program.actors.front().actor;desc.actorAssetId="pacing_fixed_control";
        desc.meshId="siege_turret";desc.behaviorDefinition.maintainForwardEngagementBand=false;stationary.SpawnEnemyActor(desc);
        EnemyEncounterPacingDirector control;input.runtime=&stationary;input.currentDistance=definition.endRailDistance+10;
        for(int step=0;step<2*fps;++step)control.Update(input);
        runner.Expect(!control.Frame().attackWindowOpen,"non-pursuing enemies retain their authored distance boundary");
    }
}

__declspec(noinline) void TestOrdinaryEnemyHighSpeedAudit(RegressionRunner& runner) {
    CourseAsset course;RailVehicleControlDefinitionAsset controls;std::string error;
    runner.Expect(course.LoadFromFile(RailShortCoursePath,&error) &&
        controls.LoadFromFile("Resources/rail_vehicle/mine_cart_standard.railvehicle",&error),"ordinary audit loads production assets");
    RailPath rail;course.ApplyToRailPath(rail);TerrainGenerationSettings terrain;
    const auto ingress=BuildRailEnemyIngressRoutes(course,rail,terrain);
    TerrainCollisionWorld world;world.BuildSynchronously(rail,terrain,&course.terrainEditLayer,nullptr,0,rail.Length());
    const auto cooked=CourseWaveRuntimeCompiler{}.Compile(course,{});
    runner.Expect(cooked.succeeded,"ordinary audit compiles the real placements");
    char responseSetting[8]{};GetEnvironmentVariableA("GE3_ORDINARY_AUDIT_RESPONSE",responseSetting,sizeof(responseSetting));
    const bool respond=responseSetting[0]=='1';
    struct Scenario {std::string id;float start;int primary=-1;CourseEventMarker event;};
    std::vector<Scenario> scenarios;
    for(size_t i=0;i<cooked.program.waves.size();++i) {
        const auto& wave=cooked.program.waves[i];
        if(wave.waveGuid==RailMothership::BossWaveId)continue;
        scenarios.push_back({wave.waveGuid,wave.triggerRailDistance,int(i),{}});
    }
    const auto add=[&](std::string id,float at,std::string wave) {
        CourseEventMarker e;e.distance=at;e.type="enemy_wave";e.id=wave;e.payload="route:support";e.editorGuid=id;
        scenarios.push_back({id,at,-1,e});
    };
    add("authored-patrol",390,"route_patrol_left");
    add("bridge-lookout",1730,"route_sniper_right");
    add("ridge-left",2560,"ridge_dive_left");add("ridge-right",2600,"ridge_dive_right");
    add("final-pair",3100,"route_scout_sniper_pair");
    add("adaptive-left-probe",600,"route_patrol_left");add("adaptive-right-probe",2900,"route_patrol_right");
    add("support-sniper-left-probe",3040,"route_sniper_left");
    std::ofstream actors(respond?"logs/ordinary_enemy_high_speed_response_actors.csv":"logs/ordinary_enemy_high_speed_actors.csv"),
        attacks(respond?"logs/ordinary_enemy_high_speed_response_attacks.csv":"logs/ordinary_enemy_high_speed_attacks.csv");
    actors<<"scenario,start,fps,mode,actor,asset,pattern,first_warning,first_fire,first_contact,first_damage,warning_to_contact,shots,contacts,accepted_damage,closest_surface,compensated,peak_speed,onscreen_seconds,entry_finished,last_gate,simulation_seconds,killed,first_response,player_shots,player_hits\n";
    attacks<<"scenario,fps,mode,actor,asset,pattern,intent,warning,visible_seconds,fire,contact,accepted_damage,closest_surface,shots,contacts,source_lead,flight,fire_gap,fire_speed,compensated,censored\n";
    struct Attack {float warning=-1,visible=0,fire=-1,contact=-1,damage=0,closest=1000000,lead=0,flight=0,gap=0,speed=0;unsigned shots=0,contacts=0;bool compensated=false;};
    struct Actor {std::string asset,pattern,gate;float screen=0,firstDamage=-1;uint64_t firstIntent=0;bool entered=false,killed=false;float response=-1;};
    const auto cross=[](Vector3 a,Vector3 b){return Vector3{a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};};
    const auto unit=[](Vector3 v){const float n=std::sqrt(v.x*v.x+v.y*v.y+v.z*v.z);return RailMothership::Scale(v,1/(std::max)(.0001f,n));};
    unsigned scenarioCount=0,actorCount=0,shotCount=0;
    for(const auto& scenario:scenarios)for(int fps:respond?std::vector<int>{30}:std::vector<int>{30,60,120})for(int mode:{0,1,2}) {
        const float dt=1.0f/fps;const bool boosted=mode==2;const float constantSpeed=mode==0?30.0f:64.0f;
        CourseSpawnRuntime runtime;CourseGameplayWaveRuntimeBridge waves;CourseEventDispatcher dispatch;
        if(scenario.primary>=0) {
            waves.Configure(ShortRunWaveSettings());runner.Expect(waves.Bind(&cooked.program,&runtime,160,&error),"audit wave binding");
            auto saved=waves.CaptureCheckpoint();
            for(size_t w=0;w<saved.wavePhases.size();++w) {
                saved.wavePhases[w]=int(w)==scenario.primary?CourseGameplayWavePhase::Active:CourseGameplayWavePhase::Completed;
                if(int(w)!=scenario.primary)for(auto index:cooked.program.waves[w].actorIndices)saved.defeatedPlacementGuids.push_back(cooked.program.actors[index].placementGuid);
            }
            saved.playerDistance=scenario.start;runner.Expect(waves.RestoreCheckpoint(saved,&error),"authored group materializes without synthetic attacks");
        } else dispatch.Dispatch({scenario.event},runtime,scenario.start);
        runner.Expect(!runtime.Enemies().empty(),"audit scenario contains enemies");
        std::map<uint32_t,Actor> tracked;
        for(const auto& actor:runtime.Enemies())tracked[actor.actorId]={actor.desc.actorAssetId,actor.desc.bulletPatternId,{}};
        std::map<std::pair<uint32_t,uint64_t>,Attack> records;std::unordered_set<uint64_t> observedShots,contactedShots;
        CourseRuntime travel;travel.Bind(&course);travel.Reset(scenario.start);
        RailVehicleMovementSystem vehicle;vehicle.Initialize(controls.vehicle);vehicle.Reset(scenario.start,boosted?64:constantSpeed,&rail);
        RailSpeedDirector speedPolicy;speedPolicy.Configure(controls.speedPolicy);speedPolicy.SetManualBoostInput(boosted,true);
        RailRideDirector ride;RailRideMotionEnvelope envelope;RailTravelPredictionInput forecast;
        forecast.vehicle=&vehicle;forecast.speedDirector=&speedPolicy;forecast.courseRuntime=&travel;forecast.course=&course;forecast.railPath=&rail;forecast.simulationStepSeconds=dt;
        RailVehicleCombatMountBridge mount;RailVehicleCameraMountBridge cameraMount;cameraMount.Initialize(controls.camera);
        RailCameraDirector camera;EnemyEncounterReadabilityDirector readable;EnemyEncounterPacingDirector pacing;
        EnemyEncounterCameraCompositionBridge composition;EnemyAttackTelegraphSystem warnings;
        EnemyAttackLaneTelegraphRenderer lanes;RailAttackWarningPresentation warningPresentation;
        RailMothership::Reveal reveal;RailMothership::Battle carrierBattle;CourseCollisionSystem collision;PlayerHitboxSystem contactProbe;
        runner.Expect(collision.LoadWeaponDefinitions("Resources/weapons",&error),"audit loads actual player weapon cadence and range");
        float peakSpeed=0,endTime=0;unsigned peakSources=0;
        unsigned playerShots=0,playerHits=0;bool previousTrigger=false;
        for(int step=0;step<20*fps;++step) {
            const float t=step*dt;endTime=t;
            if(boosted && step>0) {
                RailSpeedDirectorFrameInput si;si.course=&course;si.railPath=&rail;si.section=travel.CurrentSection();si.rideProfile=course.FindRideProfile(travel.Distance());si.distance=travel.Distance();si.deltaTime=dt;
                const auto& request=speedPolicy.Evaluate(si);RailRideDirectorInput ri;ri.course=&course;ri.railPath=&rail;ri.distance=travel.Distance();ri.baseRequestedSpeed=request.requestedSpeed;
                const auto& profile=ride.Evaluate(ri);RailRideMotionEnvelopeInput ei;ei.vehicleDefinition=&vehicle.Definition();ei.vehicleState=&vehicle.State();ei.ride=&profile;ei.railPath=&rail;
                ei.normalSpeedScale=speedPolicy.Settings().normalSpeedScale;ei.manualBoostMultiplier=speedPolicy.BoostMultiplier();const auto& limits=envelope.Evaluate(ei);
                RailVehicleMovementInput movement;movement.deltaTime=dt;movement.requestedSpeed=limits.requestedSpeed;movement.manualBoostMultiplier=limits.manualBoostMultiplier;movement.movementEnabled=true;
                movement.motionEnvelopeActive=limits.active;movement.accelerationLimit=limits.accelerationLimit;movement.brakingLimit=limits.brakingLimit;movement.jerkLimit=limits.jerkLimit;movement.courseRuntime=&travel;movement.railPath=&rail;
                speedPolicy.NotifyCourseEvents(vehicle.Update(movement).triggeredEvents);
            }
            const float at=boosted?travel.Distance():scenario.start+constantSpeed*t;
            if(at>=ShortRunTravelEndDistance(rail.Length())-1)break;
            if(!boosted)vehicle.Reset(at,constantSpeed,&rail);
            const float velocity=vehicle.State().speed;peakSpeed=(std::max)(peakSpeed,velocity);
            RailWorldRaycast::FrameCacheScope cache(&world);
            RailVehicleCombatMountInput mi;mi.vehicleDefinition=&controls.vehicle;mi.vehicleState=&vehicle.State();mount.Update(mi);
            RailVehicleCameraMountInput cm;cm.vehicleDefinition=&controls.vehicle;cm.vehicleState=&vehicle.State();cameraMount.Update(cm);
            const auto ship=carrierBattle.Update(reveal.Update(dt,at,600,rail,true,true),runtime,CourseGameplayWavePhase::Pending,rail,at,dt);
            runtime.StageCarrierDeployments(at,ship);
            EnemyEncounterPacingInput pi;pi.course=&course;pi.runtime=&runtime;pi.readability=&readable.Frame();pi.currentDistance=at;pi.deltaTime=dt;
            if(scenario.primary>=0)pi.waveRuntime=&waves;
            const auto& beat=pacing.Update(pi);EnemyEncounterCameraCompositionInput co;co.pacing=&beat;co.runtime=&runtime;co.railPath=&rail;co.playerDistance=at;co.deltaTime=dt;const auto& focus=composition.Update(co);
            RailCameraDirectorFrameInput ci;ci.course=&course;ci.railPath=&rail;ci.spawnRuntime=&runtime;ci.section=course.FindSection(at);ci.distance=at;ci.deltaTime=dt;ci.railSpeed=velocity;ci.viewportWidth=1600;ci.viewportHeight=900;
            ci.authoredEncounterCompositionActive=focus.active;ci.authoredEncounterHasFocusWorld=focus.hasFocusWorld;ci.authoredEncounterFocusWorld=focus.focusWorld;
            ci.authoredEncounterCompositionWeight=focus.blend;ci.authoredEncounterFocusWeight=focus.focusWeight;ci.authoredEncounterFovOffsetRadians=focus.fovOffsetRadians;ci.authoredEncounterBackDistanceOffset=focus.backDistanceOffset;
            const auto& mf=cameraMount.Frame();ci.mountedCameraActive=mf.active;ci.mountedCameraAnchor=mf.anchorWorldPosition;ci.mountedCameraOffset=mf.cameraOffset;ci.mountedTargetOffset=mf.targetOffset;ci.mountedCameraBlend=mf.anchorBlend;ci.mountedMaximumCorrection=mf.maximumAnchorCorrection;
            ci.vehicleFramingActive=true;auto half=controls.vehicle.collisionHalfExtents;half.y+=.6f;
            for(size_t i=0;i<8;++i){const auto& v=vehicle.State();ci.vehicleBoundsCorners[i]=RailMothership::Add(v.position,RailMothership::Add(RailMothership::Scale(v.right,(i&1)?half.x:-half.x),RailMothership::Add(RailMothership::Scale(v.up,(i&2)?half.y:-half.y),RailMothership::Scale(v.forward,(i&4)?half.z:-half.z))));}
            const auto view=camera.Evaluate(ci);const auto right=unit(cross(view.gameplayUp,view.gameplayForward)),up=cross(view.gameplayForward,right);auto cameraWorld=MakeIdentity4x4();
            const Vector3 rows[]={right,up,view.gameplayForward,view.gameplayPosition};for(int row=0;row<4;++row){cameraWorld.m[row][0]=rows[row].x;cameraWorld.m[row][1]=rows[row].y;cameraWorld.m[row][2]=rows[row].z;}
            const auto vp=Multiply(Inverse(cameraWorld),MakePerspectiveFovMatrix(view.fovY,16.0f/9,.1f,2000));
            const float playerY=controls.vehicle.bodyVerticalOffset+controls.vehicle.mounts.player.y;
            const bool allow=view.allowEnemyFire && view.stableForAiming && !view.hardTransition && !ship.introductionActive;
            CourseEnemyFireSafetyFrameInput safety;safety.railPath=&rail;safety.course=&course;safety.terrainSettings=&terrain;safety.terrainEdits=&course.terrainEditLayer;safety.terrainCollision=&world;safety.ingressRoutes=ingress;
            safety.playerDistance=at;safety.playerSpeed=velocity;safety.playerVerticalOffset=playerY;safety.hasCameraPosition=true;safety.cameraPosition=view.gameplayPosition;safety.viewProjection=&vp;safety.readability=&readable;
            safety.cameraAllowsEnemyFire=allow;safety.cameraStableForAiming=view.stableForAiming;safety.cameraHardTransition=view.hardTransition;if(boosted)safety.travelPredictionInput=&forecast;
            runtime.Update(dt,safety);
            PlayerHitboxFrameInput hp;hp.distance=at;hp.verticalOffset=playerY;contactProbe.Update(hp);
            for(const auto& actor:runtime.Enemies()) {
                auto& meta=tracked[actor.actorId];meta.gate=actor.fireSafetyReason;
                meta.entered|=actor.carrierDeployment.finished || actor.terrainIngress.finished || (actor.entranceExitState.initialized && actor.entranceExitState.phase==EnemyEntranceExitPhase::Active);
                if(const auto* seen=readable.FindActor(actor.actorId);seen && seen->onScreen)meta.screen+=dt;
            }
            for(const auto& bullet:runtime.Bullets())if(bullet.active && !bullet.hitConsumed) {
                auto key=std::make_pair(bullet.ownerActorId,bullet.attackIntentSequence);auto& record=records[key];auto& meta=tracked[bullet.ownerActorId];
                if(observedShots.insert(bullet.projectileId).second) {
                    ++record.shots;++shotCount;if(record.fire<0){record.fire=t;record.speed=velocity;}
                    if(!meta.firstIntent)meta.firstIntent=bullet.attackIntentSequence;
                    for(const auto& actor:runtime.Enemies())if(actor.actorId==bullet.ownerActorId) {
                        record.compensated=actor.targetingState.compensatesForwardTravel;record.lead=actor.behaviorDefinition.attackLeadSeconds;
                        record.flight=actor.targetingState.predictedFlightSeconds;record.gap=actor.desc.spawnDistance+actor.desc.distanceOffset-at;
                    }
                }
                const auto contact=contactProbe.EvaluateProjectile(bullet);record.closest=(std::min)(record.closest,contact.surfaceSeparation);
                if(contact.kind==PlayerProjectileContactKind::Hit && contactedShots.insert(bullet.projectileId).second) {
                    ++record.contacts;if(record.contact<0)record.contact=t;
                }
            }
            EnemyAttackTelegraphFrameInput wi;wi.spawnRuntime=&runtime;wi.railPath=&rail;wi.viewProjection=&vp;wi.course=&course;wi.terrainSettings=&terrain;wi.terrainEdits=&course.terrainEditLayer;wi.cameraPosition=view.gameplayPosition;
            wi.playerDistance=at;wi.deltaTime=dt;wi.viewportWidth=1600;wi.viewportHeight=900;wi.cameraAllowsAttack=allow;warnings.Update(wi);
            EnemyAttackLaneTelegraphRenderInput laneInput;laneInput.telegraph=&warnings.Frame();laneInput.railPath=&rail;laneInput.elapsedTime=t;laneInput.settings.effectRuntimeEnabled=false;lanes.Update(laneInput);
            warningPresentation.Update(warnings.Frame(),lanes.Frame(),runtime,rail,t);peakSources=(std::max)(peakSources,unsigned(warningPresentation.Sources().size()));
            for(const auto& source:warningPresentation.Sources())if(!source.fired && lanes.WasSubmitted(source.actorId,source.intent)) {
                auto& record=records[{source.actorId,source.intent}];if(record.warning<0)record.warning=t;record.visible+=dt;
                runtime.MarkEnemyAttackTelegraphPresented(source.actorId,source.intent);
            }
            EnemyEncounterReadabilityInput readableInput;readableInput.runtime=&runtime;readableInput.railPath=&rail;readableInput.viewProjection=&vp;readableInput.telegraph=&warnings.Frame();readableInput.viewportWidth=1600;readableInput.viewportHeight=900;readableInput.playerDistance=at;readableInput.deltaTime=dt;
            readable.Update(readableInput);
            RailAimState responseAim;bool trigger=false;
            if(respond) {
                EditorViewportCoordinateService coordinates;coordinates.Update({{0,0,1600,900},1600,900,vp});
                for(const auto& actor:runtime.Enemies())if(actor.desc.hitPoints>0 && actor.combatState.canBeTargeted) {
                    bool eligible=false;for(const auto& [key,record]:records)if(key.first==actor.actorId && record.warning>=0 && t-record.warning>=.30f){eligible=true;break;}
                    if(!eligible)continue;
                    const auto point=ResolveEnemyProjectileWorldPosition(rail,{actor.desc.lateralOffset,actor.desc.verticalOffset,actor.desc.spawnDistance+actor.desc.distanceOffset});
                    const auto projected=coordinates.ProjectWorld(point);if(!projected.onscreen)continue;
                    RailAimRayBuildInput ray;ray.viewportWidth=1600;ray.viewportHeight=900;ray.gameplayViewProjection=vp;ray.gameplayCameraPosition=view.gameplayPosition;ray.pixelPosition={projected.render.x,projected.render.y};ray.maxDistance=120;
                    auto aim=BuildRailAimState(ray);RailWorldRaycastInput query;query.aim=&aim;query.railPath=&rail;query.spawnRuntime=&runtime;query.course=&course;query.terrainSettings=&terrain;query.terrainCollision=&world;query.terrainEdits=&course.terrainEditLayer;query.playerDistance=at;
                    const auto hit=RailWorldRaycast::Query(query);if(!hit.hit || hit.kind!=RailAimHitKind::Enemy)continue;
                    ApplyRailAimHit(aim,hit);responseAim=aim;trigger=true;
                    if(tracked[actor.actorId].response<0)tracked[actor.actorId].response=t;
                    break;
                }
            }
            CourseCollisionFrameInput input;input.railPath=&rail;input.course=&course;input.deltaTime=dt;input.player.distance=at;input.player.verticalOffset=playerY;input.player.hitPoints=collision.Player().hitPoints;input.combatMount=&mount.Frame();input.externalBodyCollisionAuthority=true;
            input.worldAim=&responseAim;input.primaryTerrainSettings=&terrain;input.weapon.enabled=respond;input.weapon.triggerHeld=trigger;input.weapon.triggerPressed=trigger&&!previousTrigger;previousTrigger=trigger;
            const auto collisionStats=collision.Update(runtime,input);playerShots+=collisionStats.playerShotsFired;playerHits+=collisionStats.playerShotEnemyHits;
            for(const auto& event:runtime.EnemyCombat().ConsumeEvents())if(event.kind==EnemyCombatEventKind::Defeated)tracked[event.actorId].killed=true;
            for(const auto& damage:collision.PlayerDamage().ResultsThisFrame())if(damage.accepted) {
                auto& record=records[{damage.request.sourceActorId,damage.request.attackIntentSequence}];record.damage+=damage.appliedDamage;
                auto& meta=tracked[damage.request.sourceActorId];if(meta.firstDamage<0)meta.firstDamage=t;
            }
            if(t>8) {
                bool all=true,firstLive=false;
                for(const auto& [id,meta]:tracked)all&=meta.firstIntent!=0;
                for(const auto& bullet:runtime.Bullets())if(bullet.active && !bullet.hitConsumed && tracked[bullet.ownerActorId].firstIntent==bullet.attackIntentSequence)firstLive=true;
                if(all && !firstLive)break;
            }
        }
        for(const auto& [id,meta]:tracked) {
            const auto found=records.find({id,meta.firstIntent});Attack first=found==records.end()?Attack{}:found->second;
            if(!meta.firstIntent)for(const auto& [key,record]:records)if(key.first==id && record.warning>=0 && (first.warning<0 || record.warning<first.warning))first=record;
            actors<<scenario.id<<','<<scenario.start<<','<<fps<<','<<mode<<','<<id<<','<<meta.asset<<','<<meta.pattern<<','<<first.warning<<','<<first.fire<<','<<first.contact<<','<<meta.firstDamage<<','
                <<(first.warning>=0 && first.contact>=0?first.contact-first.warning:-1)<<','<<first.shots<<','<<first.contacts<<','<<first.damage<<','<<(first.closest<999999?first.closest:-1)<<','<<first.compensated<<','<<peakSpeed<<','<<meta.screen<<','<<meta.entered<<','<<std::quoted(meta.gate)<<','<<endTime<<','<<meta.killed<<','<<meta.response<<','<<playerShots<<','<<playerHits<<std::endl;
            ++actorCount;
        }
        for(const auto& [key,record]:records) {
            bool censored=false;for(const auto& bullet:runtime.Bullets())if(bullet.active && !bullet.hitConsumed && bullet.ownerActorId==key.first && bullet.attackIntentSequence==key.second)censored=true;
            const auto& meta=tracked[key.first];
            attacks<<scenario.id<<','<<fps<<','<<mode<<','<<key.first<<','<<meta.asset<<','<<meta.pattern<<','<<key.second<<','<<record.warning<<','<<record.visible<<','<<record.fire<<','<<record.contact<<','<<record.damage<<','<<(record.closest<999999?record.closest:-1)<<','<<record.shots<<','<<record.contacts<<','<<record.lead<<','<<record.flight<<','<<record.gap<<','<<record.speed<<','<<record.compensated<<','<<censored<<std::endl;
        }
        runner.Expect(peakSources<=RailAttackWarningPresentation::MaxSources,"audit uses the real source-warning budget");
        ++scenarioCount;
    }
    runner.Expect(scenarioCount==scenarios.size()*(respond?3u:9u) && actorCount>0 && (respond || shotCount>0),"ordinary audit completed all scenarios and observed real projectiles");
}
