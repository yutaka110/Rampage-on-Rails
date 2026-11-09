// Integration checks for the boss-relative bridge and actual rear weapons.
void TestBridgeEscapeInteraction(RegressionRunner& runner) {
    GameSessionSystem session;session.Initialize(GameSessionDefinition::RailShooterDefaults(),10000);session.Start(5000);
    runner.Expect(session.SetCourseLength(5400) && session.RegisterCheckpoint(5138,"BRIDGE ESCAPE") && session.State().checkpointDistance==5138,
        "runtime-authored escape checkpoints retain their actual distance beyond the former short-run finish");
    runner.Expect(!session.SetCourseLength(3800),"resolving a new finish cannot invalidate an existing checkpoint");
    for(int fps:{30,60,120}) {
        const float dt=1.0f/fps;
        CourseSpawnRuntime runtime;RailBridgeEscape escape;escape.Begin(1000,{});
        bool sawRecoilBeforeImpact=false;
        bool sawImpactAfterRecoil=false;
        RailVeilModelPresentation gun;
        TwinShieldDronePose pose;pose.visible=true;pose.position={-10,10,990};pose.scale={3,3,3};
        const Vector3 impact{0,0,1160};
        for(int i=0;i<fps*3 && !escape.GapOpen();++i) {
            escape.Tick(dt,1048,runtime);
            gun.UpdateBridge(pose,impact,escape.Age(),RailBridgeEscape::FireSeconds);
            if(gun.Frame().recoil>.30f && !escape.GapOpen() && escape.RearWeight()>0)
                sawRecoilBeforeImpact=true;
            if(escape.GapOpen())sawImpactAfterRecoil=sawRecoilBeforeImpact && escape.RearWeight()==0 && escape.ImpactCutFlash()>.3f;
        }
        runner.Expect(sawImpactAfterRecoil,"bridge shot holds on the visible recoil before the flash cut opens the gap");
        for(int i=0;i<fps*6 && !escape.Aiming();++i)escape.Tick(dt,1048,runtime);
        runner.Expect(escape.Aiming() && escape.Target()!=0 && runtime.Obstacles().size()==1,"escape presents one real release clamp after the camera turn");
        runner.Expect(escape.TimeScale()==.22f && !escape.OwnsControls(),"slow world time leaves aiming under player control");
        const auto frozen=escape;escape.Tick(0,1100,runtime);
        runner.Expect(std::abs(RailBridgeStructure::RampHeight(escape,1)+2.4f)<.001f,"the held deck visibly sags before the clamp is destroyed");
        const auto attached=RailBridgeStructure::Span(escape,0);
        runner.Expect(!attached.attached && attached.height<-15,"the main span sinks, tears, and falls before aiming begins");
        runner.Expect(escape.Age()==frozen.Age(),"paused escape freezes its authored clock");
        CourseActorDamageReceiver damage;WeaponHitRequest request;
        request.shotId=1;request.targetActorId=escape.Target();request.hitKind=RailAimHitKind::Obstacle;
        request.rayOrigin={0,0,0};request.rayDirection={0,0,1};request.hitPoint={0,0,10};
        request.hitNormal={0,0,-1};request.hitDistance=10;request.baseDamage=34;
        runner.Expect(!damage.Apply(runtime,nullptr,request).damageApplied,"primary fire cannot accidentally open the lock-only clamp");
        escape.ObserveRelease(escape.Target());
        runner.Expect(escape.Aiming() && !escape.Arc().enabled,"release alone never starts a jump or claims a clear");
        request.shotId=2;request.lockOnContact=true;const auto hit=damage.Apply(runtime,nullptr,request);
        runner.Expect(hit.destroyed && escape.AcceptLockHit(hit.targetActorId,hit.destroyed),"a real confirmed lock impact opens the route");
        runner.Expect(escape.TimeScale()==.55f,"clamp impact starts from the current time scale");
        auto beforeLaunch=escape.View();
        while(escape.Current()==RailBridgeEscape::Phase::Release) {
            beforeLaunch=escape.View();
            escape.Tick(dt,1050,runtime);
        }
        const auto launchView=escape.View();
        runner.Expect(std::abs(launchView.fovY-beforeLaunch.fovY)<.001f &&
            std::abs(launchView.positionOffset.y-beforeLaunch.positionOffset.y)<.001f && escape.TimeScale()==1,
            "ramp deployment hands off to the launch without a camera cut or time scale step");
        runner.Expect(escape.LaunchPresentation().IgnitionPulse()==1 && escape.RequestedSpeed()<20,
            "ignition starts at ramp handoff as the requested speed begins to ramp");
        float furthestTurn=0;
        for(int i=0;i<fps;++i) {
            escape.Tick(dt,1050,runtime);
            furthestTurn=(std::max)(furthestTurn,escape.LaunchPresentation().SpinDegrees());
        }
        runner.Expect(furthestTurn>=359 && !escape.LaunchPresentation().Spinning(),
            "the shared boost spin completes a single turn before the cart leaves the ramp");
        runner.Expect(std::abs(RailBridgeStructure::RampHeight(escape,1)-escape.Arc().rise)<.001f,"releasing the real pin lifts the sagging deck into the authoritative launch curve");
        runner.Expect(escape.Arc().enabled && escape.RequestedSpeed()>68 && !escape.Complete(),"the opened ramp enables actual acceleration without an early clear");
        escape.Tick(dt,escape.Arc().Launch()+1,runtime);
        runner.Expect(escape.Current()==RailBridgeEscape::Phase::Flight && !escape.Complete(),"takeoff waits for real traveled distance");
        escape.Tick(dt,escape.Arc().Landing()+1,runtime);
        for(int i=0;i<fps*2;++i)escape.Tick(dt,escape.Arc().Landing()+1,runtime);
        runner.Expect(escape.Complete(),"only landing and settling confirm the escape");
        auto missed=frozen;for(int i=0;i<fps*6;++i)missed.Tick(dt,1050,runtime);
        runner.Expect(missed.TakeRetry() && !missed.Complete(),"missed release requests a short checkpoint retry");
        missed=frozen;missed.ObserveRelease(missed.Target());for(int i=0;i<fps*5;++i)missed.Tick(dt,1050,runtime);
        runner.Expect(missed.TakeRetry(),"a failed laser flight cannot stall the cinematic forever");
    }
    RailPath rail;rail.SetControlPoints({{{0,0,0},18,32},{{0,0,1000},18,32}});
    CourseAsset course;CourseRuntime runtime;runtime.Bind(&course);
    RailVehicleMovementSystem vehicle;vehicle.Initialize(RailVehicleDefinition::MineCartDefaults());
    RailVehicleRideArc arc;arc.enabled=true;arc.start=100;
    RailVehicleTrackContactPoseSolver solver;
    for(float d:{110.0f,145.0f,180.0f,230.0f,245.0f}) {
        vehicle.Reset(d,60,&rail);runtime.Reset(d);RailVehicleMovementInput input;
        input.deltaTime=1.0f/60;input.requestedSpeed=60;input.railPath=&rail;input.courseRuntime=&runtime;input.rideArc=&arc;
        const auto& state=vehicle.Update(input).state;
        runner.Expect(std::abs(state.position.y-1.4f-arc.Height(state.distance))<.001f,"authoritative vehicle and weapon mounts follow the same jump path");
        RailVehicleTrackContactPoseInput contact;contact.definition=&vehicle.Definition();contact.state=&state;contact.railPath=&rail;contact.rideArc=&arc;
        const auto& frame=solver.Solve(contact);
        runner.Expect(frame.valid && frame.allWheelsSupported!=arc.Airborne(state.distance),"contact solver preserves lifted wheel geometry and reports unsupported flight");
    }
    for(const char* name:{"BridgeEscapeRamp.obj","BridgeEscapeClamp.obj","BridgeEscapeDebris.obj","BridgeEscapeSpan.obj","BridgeEscapeLeaf.obj","BridgeEscapeLink.obj","BridgeEscapePylon.obj","BridgeEscapeTornJoint.obj"}) {
        const auto mesh=LoadObjFile_Assimp("Resources/enemies/ElitePursuers",name);
        runner.Expect(!mesh.indices.empty() && ValidateModelGeometryOrientation(mesh) && AuditModelClosedSurface(mesh).IsValid(),std::string("escape mesh imports closed and consistently wound: ")+name);
    }
    for(int fps:{30,60,120})for(float entry:{0.0f,36.0f,78.0f}) {
        RailBridgeEscape escape;CourseSpawnRuntime actors;escape.Begin(400,{},350,entry);
        auto definition=RailVehicleDefinition::MineCartDefaults();definition.maximumSpeed=100;
        RailVehicleMovementSystem cart;cart.Initialize(definition);cart.Reset(400,entry,&rail);runtime.Reset(400);
        float highest=0;bool safe=true,reachable=true,hit=false;
        for(int i=0;i<fps*18 && !escape.Complete();++i) {
            RailVehicleMovementInput input;input.deltaTime=escape.TimeScale()/fps;input.requestedSpeed=escape.RequestedSpeed();
            input.manualBoostMultiplier=2;input.motionEnvelopeActive=true;input.accelerationLimit=50;input.brakingLimit=42;input.jerkLimit=240;
            input.courseRuntime=&runtime;input.railPath=&rail;input.rideArc=&escape.Arc();
            const auto& frame=cart.Update(input);escape.Tick(1.0f/fps,frame.state.distance,actors);
            if(!escape.Arc().enabled)safe&=frame.state.distance<escape.Arc().start;
            if(escape.Aiming() && escape.PhaseAge()<.05f)reachable&=escape.TargetRail().z-frame.state.distance+22<=120;
            if(escape.Aiming() && escape.PhaseAge()>=4.8f && !hit) {hit=true;escape.AcceptLockHit(escape.Target(),true);}
            highest=(std::max)(highest,escape.Arc().Height(frame.state.distance));
        }
        runner.Expect(safe && reachable && highest>14 && escape.Complete(),"braked, normal and boosted entries reserve stopping room, keep the pin in lock range, and complete a late-input jump at every frame rate");
    }
}

void TestPostBossTiming(RegressionRunner& runner) {
    for(int fps:{30,60,120})for(float speed:{36.45f,43.74f,51.03f})for(float clear:{3500.0f,6200.0f}) {
        RailPostBossSequence sequence;CourseSpawnRuntime runtime;CourseEventDispatcher dispatcher;
        float d=clear;
        for(int i=0;i<fps*4;++i){sequence.Update(1.0f/fps,d,false,runtime,dispatcher);d+=speed/fps;}
        runner.Expect(!sequence.Started() && runtime.Enemies().empty(),"a living miniboss never starts the bridge or rear pair");
        sequence.Update(1.0f/fps,d,true,runtime,dispatcher);const float anchor=sequence.Anchor();
        runner.Expect(std::abs(sequence.Scenery().front().distance-anchor-360)<.01f,"the bridge is placed relative to the actual early/late clear");
        const auto floor=sequence.TerrainPreview(TerrainEditLayer{}).Evaluate(anchor+600,4.712389f).radialOffset;
        runner.Expect(floor>8 && floor<32 && sequence.TerrainPreview(TerrainEditLayer{}).Evaluate(anchor+600,0).radialOffset>16,
            "the derived valley opens laterally with a bounded gentle floor, without editing the source course");
        const auto frozen=sequence;sequence.Update(0,d+1000,true,runtime,dispatcher);
        runner.Expect(sequence.Age()==frozen.Age() && sequence.ActorIds()==frozen.ActorIds(),"pause advances neither the sequence nor its actors");
        bool notice=false;for(int i=0;i<fps*25;++i) {
            d+=speed/fps;sequence.Update(1.0f/fps,d,true,runtime,dispatcher);
            if(sequence.CurrentPhase()==RailPostBossSequence::Phase::Notice) {
                notice=true;runner.Expect(sequence.Age()>=12 && d-anchor>=490,"normal, E and reward speeds all retain the minimum breather and bridge entry");
            }
            if(sequence.CurrentPhase()==RailPostBossSequence::Phase::Combat)break;
        }
        runner.Expect(notice && runtime.Enemies().size()==2 && sequence.CameraWeight()==1,"exactly two real pursuers enter after the vista, bridge and camera turn");
        runner.Expect(!sequence.Complete(),"merely travelling through the new section never claims a combat clear");
        const auto checkpoint=sequence;const auto ids=sequence.ActorIds();
        sequence.Update(1,d+speed,true,runtime,dispatcher);sequence=checkpoint;
        sequence.Update(1.0f/fps,d,true,runtime,dispatcher);
        runner.Expect(sequence.ActorIds()==ids && runtime.Enemies().size()==2 && sequence.Anchor()==anchor,"restoring sequence history preserves the bridge origin and never respawns its pair");
    }
}

void TestVeilModularModel(RegressionRunner& runner) {
    size_t triangles=0;
    for(const auto& part:RailVeilModel::Parts) {
        const auto model=LoadObjFile_Assimp(RailVeilModel::Directory,part.file);
        runner.Expect(!model.vertices.empty() && model.indices.size()>0 && ValidateModelGeometryOrientation(model),
            std::string("Veil rigid part imports with consistent normals: ")+part.id);
        runner.Expect(AuditModelClosedSurface(model).IsValid(),std::string("Veil rigid part has a closed surface: ")+part.id);
        triangles+=model.indices.size()/3;
    }
    runner.Expect(triangles<12000,"the complete ten-part Veil remains within a modest gameplay mesh budget");
    for(size_t index:{size_t{4},size_t{5}}) {
        const auto model=LoadObjFile_Assimp(RailVeilModel::Directory,RailVeilModel::Parts[index].file);
        RailVeilAttackFrame deployed;deployed.opening=1;
        const auto closed=RailVeilModel::EvaluatePart(index,{},false,false).local;
        const auto open=RailVeilModel::EvaluatePart(index,deployed,false,false).local;
        float closedFront=100,openFront=100;
        for(const auto& vertex:model.vertices) {
            const auto& p=vertex.position;
            const auto depth=[&](const auto& m){return p.x*m.m[0][2]+p.y*m.m[1][2]+p.z*m.m[2][2]+m.m[3][2];};
            closedFront=(std::min)(closedFront,depth(closed));openFront=(std::min)(openFront,depth(open));
        }
        runner.Expect(closedFront<RailVeilModel::Muzzle.z-.04f && openFront>RailVeilModel::Muzzle.z+.15f,
            "closed jaw protects the nose; deployed armour retracts behind the beam's real muzzle");
    }
    const auto panel=LoadObjFile_Assimp(RailVeilModel::Directory,"VeilBoostPanel.obj");
    runner.Expect(!panel.indices.empty() && ValidateModelGeometryOrientation(panel) && AuditModelClosedSurface(panel).IsValid(),
        "Veil's breakable panel keeps a closed readable badge in the fighter's own material palette");
    const auto sensorPart=RailVeilModel::Parts[6].pivot;
    TwinShieldDronePose pose;pose.position={7,12,-25};pose.rotation={.2f,.7f,-.1f};pose.flightBank=.65f;pose.scale={3,3,3};
    const auto sensorWorld=RearSubsystemWorldCenter(pose,sensorPart);
    const auto hitWorld=VeilSensorWorldCenter(pose);
    runner.Expect(std::abs(sensorWorld.x-hitWorld.x)+std::abs(sensorWorld.y-hitWorld.y)+std::abs(sensorWorld.z-hitWorld.z)<.001f,
        "sensor geometry uses the actual banked gameplay hit anchor");
    RailVeilModelPresentation lastShot;
    pose.visible=true;
    const Vector3 storedMuzzle{-22,12,990};
    lastShot.UpdateBridge(pose,{0,0,1160},RailBridgeEscape::FireSeconds,RailBridgeEscape::FireSeconds,&storedMuzzle);
    const auto& wreck=lastShot.Frame();
    const auto wreckMuzzle=RailVeilModel::MuzzleWorld(wreck.bridgePose,wreck);
    runner.Expect(wreck.bridgeWreck && std::abs(wreckMuzzle.x-storedMuzzle.x)+std::abs(wreckMuzzle.y-storedMuzzle.y)+
        std::abs(wreckMuzzle.z-storedMuzzle.z)<.001f,"the wreck reaches the stored muzzle at the first instant of fire");
    for(int fps:{30,60,120}) {
        RailVeilModelPresentation visual;const float dt=1.0f/fps;
        for(int i=0;i<fps;++i)visual.Update(dt,true,false,1.8f-float(i)*dt);
        runner.Expect(visual.Frame().opening==1 && visual.Frame().charge>0,"the beam clock opens the armour and charges the optical core");
        const auto before=visual.Frame();visual.Update(0,false,true,0);
        runner.Expect(visual.Frame().opening==before.opening && visual.Frame().charge==before.charge,"pause freezes the model's charging pose");
        const auto top=RailVeilModel::EvaluatePart(4,visual.Frame(),true,false);
        const auto bottom=RailVeilModel::EvaluatePart(5,visual.Frame(),true,false);
        runner.Expect(top.local.m[1][2]*bottom.local.m[1][2]<0,"armour petals rotate in opposite directions around separate hinges");
        runner.Expect(std::abs(RailVeilModel::EvaluatePart(7,visual.Frame(),false,false).local.m[1][2])<.001f,
            "the sensor cover stays closed outside its real damage window even while the main gun charges");
        visual.Update(dt,true,true,0);visual.Update(dt,true,true,0);
        runner.Expect(visual.Frame().recoil>.02f,"the real fire edge recoils the main cannon");
        const auto cannon=RailVeilModel::EvaluatePart(3,visual.Frame(),false,false);
        const auto parent=BuildTwinShieldDroneWorldMatrix(pose);
        const auto cannonWorld=Multiply(cannon.local,parent);
        const Vector3 lipLocal{0,0,-.92f}; // muzzle - cannon pivot
        Vector3 lip{};for(int i=0;i<3;++i) {
            const float value=lipLocal.x*cannonWorld.m[0][i]+lipLocal.y*cannonWorld.m[1][i]+lipLocal.z*cannonWorld.m[2][i]+cannonWorld.m[3][i];
            if(i==0)lip.x=value;else if(i==1)lip.y=value;else lip.z=value;
        }
        const auto muzzle=RailVeilModel::MuzzleWorld(pose,visual.Frame());
        runner.Expect(std::abs(lip.x-muzzle.x)+std::abs(lip.y-muzzle.y)+std::abs(lip.z-muzzle.z)<.001f,
            "beam origin follows the actual moving gun lip under bank, heading and scale");
        runner.Expect(!RailVeilModel::EvaluatePart(6,visual.Frame(),true,true).visible &&
            !RailVeilModel::EvaluatePart(7,visual.Frame(),true,true).visible,"broken sensor removes only the independent lens and its cover");
        for(int i=0;i<fps;++i)visual.Update(dt,false,false,0);
        runner.Expect(visual.Frame().opening==0 && visual.Frame().recoil<.001f,"interruption or recovery closes the shroud and settles recoil without another fire event");
    }
}

void TestVeilSensorAim(RegressionRunner& runner) {
    for(const bool damaged:{false,true}) {
        CourseSpawnRuntime runtime;
        CourseEnemyActorDesc desc;
        desc.actorAssetId="rear_veil";
        desc.spawnDistance=80;
        desc.bulletCount=1;
        desc.behaviorDefinition=EnemyBehaviorDefinition::Commercial(EnemyBehaviorArchetype::Assault);
        std::string error;
        runner.Expect(desc.projectileDefinition.LoadFromFile(
            "Resources/courses/projectiles/enemy_rear_pursuit.projectile",&error),
            "Veil sensor test loads the live rear projectile");
        desc.projectileDefinitionId=desc.projectileDefinition.id;
        if(!runtime.SpawnEnemyActor(desc,&error)) {
            runner.Expect(false,"Veil sensor test spawns the live actor");
            continue;
        }

        auto ready=runtime.Enemies().front();
        EnemyCombatSystem::AdvancePoseForPrediction(ready,ready.combatDefinition.spawnDurationSeconds+.01f);
        RestoreEnemyFixture(runner,runtime,ready);
        const uint32_t id=runtime.Enemies().front().actorId;
        runtime.SetRearPursuerFormation(id,32,-9,8,0,true);
        EnemyTargetingSystem targeting;
        EnemyTargetingFrameInput input;
        input.deltaTime=1.0f/60;
        input.playerDistance=110;
        input.playerLateralOffset=-2;
        input.playerVerticalOffset=4;
        targeting.Update(runtime,input);
        if(damaged) {
            CourseCollisionSystem collision;
            for(uint64_t shot=1;shot<=2;++shot) {
                WeaponHitRequest hit;
                hit.shotId=shot;
                hit.hitKind=RailAimHitKind::Enemy;
                hit.targetActorId=id;
                hit.sourceIndex=kVeilSensorHitSource;
                hit.hitNormal={0,0,-1};
                hit.damageType=WeaponDamageType::Energy;
                hit.baseDamage=12;
                const auto result=collision.ApplyWeaponHit(runtime,nullptr,hit);
                runner.Expect(result.damageApplied,
                    "a primary contact removes sensor HP during Veil's descent, reject="+
                    std::to_string(int(result.rejectReason)));
            }
        }

        runtime.SetRearPursuerFormation(id,32,-9,8,0);
        auto actor=runtime.Enemies().front();
        runner.Expect(actor.veilSensorHitPoints==(damaged?0:kVeilSensorHitPoints) &&
            actor.desc.hitPoints==desc.hitPoints,"sensor damage leaves Veil's hull intact");
        actor.attackState.tokenReserved=true;
        actor.attackState.intentSequence=17;
        actor.attackState.tokenId=41;
        RestoreEnemyFixture(runner,runtime,actor);
        input.playerLateralOffset=3;
        targeting.Update(runtime,input);
        const auto launch=ResolveEnemyProjectileScheduledLaunch(runtime.Enemies().front(),0,0);
        runner.Expect(launch.projectile.active &&
            std::abs(launch.projectile.lockedTargetLateralOffset-(damaged?-2.0f:3.0f))<.001f,
            "Veil's live round tracks the later lane with an intact sensor and the descent lane with a damaged sensor");
    }
}

void TestFangArmourImpact(RegressionRunner& runner) {
    CourseAsset course;
    course.railPoints = {{{0,24,0},70,36.45f},{{0,24,200},70,36.45f}};
    RailPath rail;
    course.ApplyToRailPath(rail);
    const auto vehicle = RailVehicleDefinition::MineCartDefaults();
    RailVehicleRuntimeState state;
    state.initialized = true;
    state.previousDistance = 100.0f;
    state.distance = 105.0f;
    RailVehicleBodyCollisionInput body;
    body.vehicleDefinition = &vehicle;
    body.vehicleState = &state;
    body.railPath = &rail;

    CourseSpawnRuntime runtime;
    auto spawn = [&](uint64_t id, const char* definition, float lateral, float travel) {
        CourseBulletActor round;
        round.initialized = true;
        round.projectileId = id;
        round.definitionId = definition;
        round.spawnDistance = 91.0f;
        round.distanceOffset = travel;
        round.previousLateralOffset = round.lateralOffset = lateral;
        round.previousVerticalOffset = round.verticalOffset = vehicle.bodyVerticalOffset;
        round.radius = 0.45f;
        round.damage = 6.0f;
        runner.Expect(runtime.SpawnProjectile(round), "Fang impact fixture spawns each round");
    };
    for (uint64_t id : {1, 2, 3}) spawn(id, "enemy_fang_chase", 1.9f, 10.0f);
    spawn(4, "enemy_fang_chase", 0.0f, 4.0f); // The cart pulls away.
    spawn(5, "enemy_rear_pursuit", 1.9f, 10.0f);

    CourseCollisionSystem collision;
    CourseCollisionFrameInput input;
    input.railPath = &rail;
    input.player.distance = state.distance;
    input.player.verticalOffset = vehicle.bodyVerticalOffset;
    input.vehicleBody = &body;
    const auto stats = collision.Update(runtime, input);
    runner.Expect(stats.enemyBulletHits == 1 && stats.playerHitsRejected == 2 &&
        std::abs(stats.playerDamage - 6.0f) < .01f,
        "three Fang armour strikes still deal damage only once during invulnerability");

    EnemyProjectilePresentationInput presentationInput;
    presentationInput.runtime = &runtime;
    presentationInput.railPath = &rail;
    presentationInput.projectileContacts = collision.ProjectileContacts();
    presentationInput.playerDamageResults = collision.PlayerDamageResults();
    EnemyProjectilePresentationBridge presentation;
    presentation.Update(presentationInput);
    const auto& frame = presentation.Frame();
    const float fraction = (9.0f - vehicle.collisionHalfExtents.z - 0.45f) / 5.0f;
    const auto hull = rail.Evaluate(std::lerp(state.previousDistance, state.distance, fraction));
    const Vector3 rearPlate = {
        hull.position.x + 1.9f, hull.position.y + vehicle.bodyVerticalOffset,
        hull.position.z - vehicle.collisionHalfExtents.z};
    unsigned impacts = 0;
    for (const auto& event : frame.events) {
        if (event.kind != EnemyProjectilePresentationEventKind::Impacted) continue;
        ++impacts;
        runner.Expect(event.projectileId >= 1 && event.projectileId <= 3 &&
            !event.surfaceImpact && event.impactNormal.z < -.99f &&
            std::abs(event.worldPosition.x - rearPlate.x) < .01f &&
            std::abs(event.worldPosition.y - rearPlate.y) < .01f &&
            std::abs(event.worldPosition.z - rearPlate.z) < .01f,
            "the burst is on the moving rear armour face, including invulnerable impacts");
    }
    runner.Expect(impacts == 3 && frame.projectiles.size() == 2 &&
        std::all_of(frame.projectiles.begin(), frame.projectiles.end(),
            [](const auto& projectile) { return projectile.projectileId == 4 || projectile.projectileId == 5; }),
        "the impact frame shows one explosion per stopped Fang round, while the outrun and Veil rounds survive");
}

void TestRearPursuitCombat(RegressionRunner& runner) {
    TestFangArmourImpact(runner);
    TestVeilSensorAim(runner);
    for(int fps:{30,60,120}) {
        CourseAsset course;course.railPoints={{{0,24,0},70,36.45f},{{0,24,12000},70,36.45f}};
        RailPath rail;course.ApplyToRailPath(rail);CourseSpawnRuntime runtime;CourseEventDispatcher dispatcher;
        RailPostBossSequence sequence;float distance=4000;const float dt=1.0f/fps;
        for(int i=0;i<fps*22;++i) {
            const float speed=i<fps*18?36.45f:51.03f;distance+=speed*dt;
            sequence.Update(dt,distance,true,runtime,dispatcher);
            CourseEnemyFireSafetyFrameInput input;input.playerDistance=distance;input.playerSpeed=speed;
            input.railPath=&rail;input.hasCameraPosition=true;const auto cart=rail.Evaluate(distance).position;
            input.cameraPosition={cart.x,cart.y+7.8f,cart.z+17};
            input.cameraAllowsEnemyFire=sequence.CameraWeight()>.999f;input.deltaTime=dt;
            runtime.Update(dt,input);
        }
        runner.Expect(runtime.Enemies().size()==2,"the full runtime keeps both rear combat actors alive until defeated");
        const auto cart=rail.Evaluate(distance).position;
        const Vector3 muzzle{cart.x,cart.y+4,cart.z};
        RailTargetRegistry registry;RailTargetRegistryFrameInput query;
        query.spawnRuntime=&runtime;query.railPath=&rail;query.playerDistance=distance;
        query.cameraPosition={cart.x,cart.y+7.8f,cart.z+17};query.viewRelativeTargeting=true;query.viewForward={0,0,-1};
        registry.Build(query);
        runner.Expect(registry.Anchors().size()==8,"rear camera registers two hulls and six independent panels");
        for(const auto& actor:runtime.Enemies()) {
            const float rear=distance-actor.desc.spawnDistance-actor.desc.distanceOffset;
            runner.Expect(rear>=22.5f && rear<=101 && actor.combatState.canReceiveDamage && !actor.startupFlightControlled,
                "speed changes retain a damageable rear band without forward relocation or cinematic invulnerability");
        }
        CourseCollisionSystem collision;std::string error;runner.Expect(collision.LoadWeaponDefinitions("Resources/weapons",&error),"rear prototype loads production weapon definitions");
        const auto ids=sequence.ActorIds();
        const auto armorCheckpoint=runtime.CaptureCheckpoint();
        RailLockVolley panelVolley;
        std::vector<RailLockToken> panelTokens;
        for(const auto& anchor:registry.Anchors()) {
            if(anchor.target.kind!=RailLockTargetKind::RearPanel || anchor.target.actorId!=ids[0]) continue;
            RailLockToken token;
            token.target=anchor.target;
            panelTokens.push_back(token);
        }
        if(panelTokens.size()==3) {
            std::array<WeaponShot,3> shots;
            for(size_t i=0;i<shots.size();++i) {
                shots[i].shotId=1000+i;
                shots[i].damageType=WeaponDamageType::Ice;
                shots[i].damage=34;
                shots[i].range=120;
            }
            CourseCollisionSystem panelCollision;
            runner.Expect(panelVolley.Enqueue(panelTokens,shots,muzzle,rail,runtime,{})==3,
                "three independent panel locks launch together");
            auto boostSequence=sequence;
            unsigned breaks=0;
            for(int i=0;i<fps*2;++i) {
                panelVolley.Update(dt,muzzle,rail,runtime,panelCollision,&course);
                if(boostSequence.OnPanelBreaks(panelVolley.Hits())) {
                    for(const auto& hit:panelVolley.Hits()) breaks+=hit.rearPanelBroken?1:0;
                    runner.Expect(boostSequence.PanelBoostMultiplier()>=1.85f,
                        "the first panel contact commands boost without the kill threshold");
                }
            }
            const auto& fang=runtime.Enemies().front();
            runner.Expect(breaks==3 && fang.rearArmor.intact==0 && fang.desc.hitPoints==120 &&
                fang.rearArmor.exposedRemaining==1.2f,"breaking the full set exposes the hull without killing it");
            WeaponHitRequest body;
            body.shotId=1100;
            body.targetActorId=ids[0];
            body.hitKind=RailAimHitKind::Enemy;
            body.hitNormal={0,0,-1};
            body.baseDamage=10;
            const auto exposed=panelCollision.ApplyWeaponHit(runtime,&course,body);
            runner.Expect(std::abs(exposed.appliedDamage-17.0f)<.01f,"the exposure multiplies actual hull damage");
            Vector3 position{};
            runner.Expect(!RailLockVolley::TargetPosition(panelTokens.front().target,rail,runtime,position),
                "absent panels cannot be locked or receive a late volley");
            runtime.UpdateRearArmor(1.21f);
            runner.Expect(runtime.Enemies().front().rearArmor.intact==0 &&
                runtime.Enemies().front().rearArmor.exposedRemaining==0,"exposure ends before the rearm");
            runtime.UpdateRearArmor(4.80f);
            runner.Expect(runtime.Enemies().front().rearArmor.intact==7 &&
                !RailLockVolley::TargetPosition(panelTokens.front().target,rail,runtime,position),
                "rearm restores three panels but rejects the previous generation");
            registry.Build(query);
            const auto replacement=std::find_if(registry.Anchors().begin(),registry.Anchors().end(),[&](const auto& anchor){
                return anchor.target.kind==RailLockTargetKind::RearPanel && anchor.target.actorId==ids[0];
            });
            if(replacement!=registry.Anchors().end()) {
                RailLockToken token;
                token.target=replacement->target;
                shots[0].shotId=1200;
                runner.Expect(panelVolley.Enqueue(std::span(&token,1),std::span(&shots[0],1),muzzle,rail,runtime,{})==1,
                    "a rebuilt panel accepts a new lock");
                for(int i=0;i<fps*2;++i)panelVolley.Update(dt,muzzle,rail,runtime,panelCollision,&course);
                runner.Expect(runtime.Enemies().front().rearArmor.intact!=7,"the rebuilt panel breaks again");
                runtime.UpdateRearArmor(6.01f);
                runner.Expect(runtime.Enemies().front().rearArmor.intact==7,"repeat break rearms again");
            } else runner.Expect(false,"rearm registers new lock targets");
        } else runner.Expect(false,"Fang registers three panel locks");
        runner.Expect(runtime.RestoreCheckpoint(armorCheckpoint,true,&error),"restore the original fight before hull tests");
        for(uint64_t i=1;i<=10;++i) {
            CourseEnemyFireSafetyFrameInput settle;settle.playerDistance=distance;settle.playerSpeed=0;
            settle.railPath=&rail;settle.hasCameraPosition=true;settle.cameraPosition={cart.x,cart.y+7.8f,cart.z+17};
            settle.cameraAllowsEnemyFire=false;
            for(int frame=0;frame<fps/4;++frame)runtime.Update(dt,settle);
            WeaponHitRequest hit;hit.shotId=i;hit.hitKind=RailAimHitKind::Enemy;hit.targetActorId=ids[0];
            hit.baseDamage=12;hit.damageType=WeaponDamageType::Energy;hit.rayOrigin=muzzle;
            const auto& actor=runtime.Enemies().front();
            const auto sample=rail.Evaluate(actor.desc.spawnDistance+actor.desc.distanceOffset);
            const Vector3 center{sample.position.x+actor.desc.lateralOffset,sample.position.y+actor.desc.verticalOffset,sample.position.z};
            RailAimState aim;aim.valid=true;aim.worldRayOrigin=hit.rayOrigin;
            const Vector3 delta{center.x-hit.rayOrigin.x,center.y-hit.rayOrigin.y,center.z-hit.rayOrigin.z};
            const float length=std::sqrt(delta.x*delta.x+delta.y*delta.y+delta.z*delta.z);
            aim.worldRayDirection={delta.x/length,delta.y/length,delta.z/length};aim.maxDistance=120;
            RailWorldRaycastInput trace;trace.aim=&aim;trace.railPath=&rail;trace.spawnRuntime=&runtime;trace.includeProceduralTerrain=false;
            const auto contact=RailWorldRaycast::Query(trace);
            runner.Expect(contact.hit && contact.actorId==ids[0],"the actual primary world ray resolves the following enemy behind the cart: shot="+
                std::to_string(i)+" id="+std::to_string(actor.actorId)+" expected="+std::to_string(ids[0])+" hit="+std::to_string(contact.actorId)+
                " hp="+std::to_string(actor.desc.hitPoints)+" targetable="+std::to_string(actor.combatState.canBeTargeted)+
                " entrance="+std::to_string(actor.entranceExitState.targetable)+" depth="+std::to_string(length));
            hit.rayDirection=aim.worldRayDirection;hit.hitPoint=contact.position;hit.hitNormal=contact.normal;hit.hitDistance=contact.distance;
            const auto result=collision.ApplyWeaponHit(runtime,&course,hit);
            runner.Expect(result.damageApplied,"ordinary primary contacts damage the rear enemy through the shared receiver");
        }
        sequence.Notify(runtime.EnemyCombat().ConsumeEvents());
        RailLockVolley volley;RailLockToken token;token.target.actorId=ids[1];
        WeaponShot shot;shot.shotId=100;shot.damageType=WeaponDamageType::Ice;shot.damage=34;shot.range=120;
        for(int burst=0;burst<2;++burst) {
            shot.shotId=100+burst;
            runner.Expect(volley.Enqueue(std::span(&token,1),std::span(&shot,1),muzzle,rail,runtime,{})==1,"a real travelling volley launches behind the moving cart");
            for(int i=0;i<fps*2;++i)volley.Update(dt,muzzle,rail,runtime,collision,&course);
        }
        sequence.Notify(runtime.EnemyCombat().ConsumeEvents());
        for(int i=0;i<fps*4;++i)sequence.Update(dt,distance,true,runtime,dispatcher);
        runner.Expect(sequence.EscapeActive() && !sequence.Complete(),"actual primary and lock defeats qualify escape without claiming a clear before the bridge jump");
    }
}

void TestPostBossCameraAndRetry(RegressionRunner& runner) {
    CourseAsset course;std::string error;
    runner.Expect(course.LoadFromFile(RailShortCoursePath,&error) && RailPostBossSequence::Enabled(course),"the production short course opts into the boss-relative extension");
    auto fixture=course;fixture.waveDefinitions.clear();
    runner.Expect(!RailPostBossSequence::Enabled(fixture),"isolated shield and combat fixtures do not wait for a nonexistent boss");
    RailPath rail;course.ApplyToRailPath(rail);RailCameraDirector director;RailCameraDirectorFrameInput input;
    input.course=&course;input.railPath=&rail;input.distance=4400;input.deltaTime=1.0f/60;
    input.viewportWidth=1600;input.viewportHeight=900;input.sequenceView=RailPostBossSequence::RearView();
    for(int i=0;i<=120;++i) {
        input.sequenceViewWeight=float(i)/120;
        const auto frame=director.Evaluate(input);
        const float length=frame.gameplayForward.x*frame.gameplayForward.x+frame.gameplayForward.y*frame.gameplayForward.y+frame.gameplayForward.z*frame.gameplayForward.z;
        runner.Expect(std::isfinite(length) && length>.99f && length<1.01f,"the 180 degree blend never creates a zero aim direction");
        if(i==120)runner.Expect(frame.gameplayForward.z<-.98f && frame.viewRelativeTargeting && !frame.hideVehicle,
            "rear combat uses the real stable rear camera and keeps the cart visible");
    }
    input.stableAimPresentation=true;director.AddFeedbackImpulse(3,.04f,.05f);
    runner.Expect(director.Evaluate(input).shakeAmount==0,"precision escape aiming suppresses residual impact and authored camera shake");
    RailRetryFollowState live;CourseSpawnRuntime runtime;CourseEventDispatcher dispatcher;
    live.postBoss.Update(.1f,4400,true,runtime,dispatcher);
    for(int i=0;i<40;++i)live.postBoss.Update(.1f,4770,true,runtime,dispatcher);
    runner.Expect(live.postBoss.CheckpointPending(),"bridge entry creates a dedicated checkpoint before the rear battle");
    live.postBoss.MarkCheckpointCaptured(true);
    RailRetryPresentationCheckpoint checkpoint;checkpoint.Capture(1,1,BindRailRetryFollowState(live));
    live.postBoss.Reset();runner.Expect(checkpoint.Restore(1,1,BindRailRetryFollowState(live)) && live.postBoss.Started() && live.postBoss.Anchor()==4400 && !live.postBoss.CheckpointPending(),
        "the ordinary retry follower transaction restores post-boss timing and relative scenery");
}
