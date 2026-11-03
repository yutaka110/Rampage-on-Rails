// Integration checks for the boss-relative bridge and actual rear weapons.
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

void TestRearPursuitCombat(RegressionRunner& runner) {
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
        runner.Expect(sequence.Complete() && sequence.CameraWeight()==0,"actual primary and lock defeats return the camera and release the clear gate");
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
    RailRetryFollowState live;CourseSpawnRuntime runtime;CourseEventDispatcher dispatcher;
    live.postBoss.Update(.1f,4400,true,runtime,dispatcher);
    for(int i=0;i<40;++i)live.postBoss.Update(.1f,4770,true,runtime,dispatcher);
    runner.Expect(live.postBoss.CheckpointPending(),"bridge entry creates a dedicated checkpoint before the rear battle");
    live.postBoss.MarkCheckpointCaptured(true);
    RailRetryPresentationCheckpoint checkpoint;checkpoint.Capture(1,1,BindRailRetryFollowState(live));
    live.postBoss.Reset();runner.Expect(checkpoint.Restore(1,1,BindRailRetryFollowState(live)) && live.postBoss.Started() && live.postBoss.Anchor()==4400 && !live.postBoss.CheckpointPending(),
        "the ordinary retry follower transaction restores post-boss timing and relative scenery");
}
