void TestPursuerCoordination(RegressionRunner& runner) {
    using C=RailPursuerCoordination;
    for(int fps:{30,60,120}) {
        C c;C::Input in;
        in.aircraft[0]={true,true,true,true,30,6,6,0};
        in.aircraft[1]={true,true,false,true,50,-12,10,0};
        const float dt=1.0f/fps;
        float fangLife=0,veilLife=0,lastWarning=-2;
        unsigned fangGrants=0,veilGrants=0;
        bool overlap=false,fangSolo=false,veilSolo=false,spaced=true;
        for(int i=0;i<fps*24;++i) {
            const float time=i*dt;
            fangLife=(std::max)(0.0f,fangLife-dt);veilLife=(std::max)(0.0f,veilLife-dt);
            in.fangThreat=fangLife>0;in.veilThreat=veilLife>0;
            in.aircraft[1].lineClear=time>3.0f;
            c.Tick(dt,in);
            const auto& fang=c.AircraftState(0);
            const auto& veil=c.AircraftState(1);
            if(fang.grants!=fangGrants || veil.grants!=veilGrants) {
                spaced&=time-lastWarning>=C::WarningSpacing-.001f;
                lastWarning=time;fangGrants=fang.grants;veilGrants=veil.grants;
            }
            if(c.FangCanFire() && fang.leaseAge>.75f && !in.fangThreat) {
                ++in.aircraft[0].shots;fangLife=2.7f;
            }
            if(c.VeilCanFire() && veil.leaseAge>1.25f) {c.NotifyVeilFired();veilLife=.65f;}
            overlap|=in.fangThreat && in.veilThreat;
            fangSolo|=in.fangThreat && !veil.reserved;
            veilSolo|=in.veilThreat && !fang.reserved;
            if(!in.aircraft[1].lineClear)runner.Expect(veil.grants==0,"blocked Veil cannot begin a warning");
        }
        runner.Expect(fangGrants>=2 && veilGrants>=2 && overlap && fangSolo && veilSolo && spaced,
            "the two attack clocks admit live overlaps and solo attacks with separate warning onsets");
        const auto checkpoint=c;c.Tick(0,in);
        runner.Expect(c.AircraftState(0).leaseAge==checkpoint.AircraftState(0).leaseAge &&
            c.AircraftState(1).leaseAge==checkpoint.AircraftState(1).leaseAge,"pause preserves both attack clocks");
        auto restored=checkpoint;for(int i=0;i<fps;++i){c.Tick(dt,in);restored.Tick(dt,in);}
        runner.Expect(c.AircraftState(0).grants==restored.AircraftState(0).grants &&
            c.AircraftState(1).formation.y==restored.AircraftState(1).formation.y,
            "checkpoint copies reproduce subsequent flight and attack choices");
        const auto before=c.AircraftState(0);
        c.CancelVeil();
        runner.Expect(!c.VeilCanFire() && c.VeilInterrupted() && c.AircraftState(0).reserved==before.reserved &&
            c.AircraftState(0).leaseAge==before.leaseAge,"a sensor interrupt releases only Veil");

        for(bool accelerate:{false,true}) {
            RailVeilRearBeam::Frame beam;
            beam.Begin(200,1.4f);
            const float limit=beam.end;
            PlayerHitboxRuntimeState player;player.initialized=true;
            player.distance=200;player.lateralOffset=player.previousLateralOffset=1.1f;
            player.verticalOffset=player.previousVerticalOffset=1.4f;
            RailManualBoost boost;
            float age=0;int contacts=0;
            for(int i=0;i<fps*3;++i) {
                player.previousDistance=player.distance;
                boost.SetInput(accelerate && age>=.12f,true);boost.Update(dt);
                player.distance+=dt*36.45f*boost.SpeedMultiplier();
                age+=dt;
                if(beam.Charging() && age>=RailVeilRearBeam::ChargeSeconds)
                    beam.Launch({0,3.8f,player.previousDistance-31});
                if(beam.phase==RailVeilRearBeam::Phase::Flash)contacts+=beam.Advance(dt,player)?1:0;
            }
            runner.Expect(contacts==(accelerate?0:1) && beam.front==limit && beam.end==limit,
                "the swept rail beam hits once at cruise and held acceleration can clear its fixed endpoint");
        }
    }
    // Real geometry is queried, rather than a test-only LOS flag in the host.
    RailPath rail;rail.SetControlPoints({{{0,0,0},70,36},{{0,0,1000},70,36}});
    CourseSpawnRuntime runtime;CourseEnemyActorDesc desc;desc.actorAssetId="rear_veil";desc.meshId="twin_shield_hull";desc.spawnDistance=40;
    desc.lateralOffset=-10;desc.verticalOffset=8;runtime.SpawnEnemyActor(desc);
    EnemyTargetingSystem targeting;EnemyTargetingFrameInput target;target.railPath=&rail;target.playerDistance=100;
    target.playerVerticalOffset=1.4f;target.hasCameraPosition=true;target.cameraPosition={0,9,117};target.deltaTime=.016f;targeting.Update(runtime,target);
    RailWorldRaycastInput query;query.railPath=&rail;query.spawnRuntime=&runtime;query.includeProceduralTerrain=false;
    const Vector3 player{0,1.4f,100};
    runner.Expect(RailPursuerHasShotLine(runtime.Enemies().front(),player,query),"an unobstructed live muzzle can see the player");
    CourseObstacleActorDesc wall;wall.id="pursuer-los-test";wall.spawnDistance=70;wall.lateralOffset=-5;wall.verticalOffset=4.7f;
    wall.halfExtents={4,5,2};wall.breakable=false;runtime.SpawnObstacle(wall);
    runner.Expect(!RailPursuerHasShotLine(runtime.Enemies().front(),player,query),"an actual obstacle blocks the reverse weapon-to-player ray");
    for(int fps:{30,60,120}) {
        CourseAsset course;course.railPoints={{{0,24,0},70,36.45f},{{0,24,12000},70,36.45f}};
        RailPath path;course.ApplyToRailPath(path);CourseSpawnRuntime live;CourseEventDispatcher dispatcher;RailPostBossSequence sequence;
        float distance=4000;bool overlap=false,solo=false;uint64_t shots=0;
        float chargeStart=-1,lockedAt=-1,firstShotAt=-1;
        const float dt=1.0f/fps;
        for(int i=0;i<fps*42;++i) {
            distance+=36.45f*dt;
            sequence.Update(dt,distance,true,live,dispatcher,nullptr,dt,36.45f);
            CourseEnemyFireSafetyFrameInput safety;safety.playerDistance=distance;safety.playerSpeed=36.45f;safety.deltaTime=dt;
            safety.railPath=&path;safety.hasCameraPosition=true;const auto cart=path.Evaluate(distance).position;
            safety.cameraPosition={cart.x,cart.y+7.8f,cart.z+17};safety.cameraAllowsEnemyFire=sequence.CameraWeight()>.999f;
            live.Update(dt,safety);
            // This headless fixture acknowledges the same reserved warning
            // that the host presents through its real HUD/audio pipeline.
            for(const auto& actor:live.Enemies())if(actor.attackState.tokenReserved)
                live.MarkEnemyAttackTelegraphPresented(actor.actorId,actor.attackState.intentSequence);
            PlayerHitboxRuntimeState player;player.initialized=true;player.distance=distance;
            player.previousDistance=distance-36.45f*dt;player.verticalOffset=player.previousVerticalOffset=1.4f;
            const auto beamEvents=sequence.AdvanceVeilBeam(dt,live,path,player,{});
            const auto& beam=sequence.VeilBeam();
            const float time=sequence.PhaseAge();
            if(chargeStart<0 && beam.Charging())chargeStart=time;
            if(beamEvents.locked && lockedAt<0)lockedAt=time;
            if(beamEvents.fired && firstShotAt<0)firstShotAt=time;
            bool rounds=false;
            for(const auto& b:live.Bullets())if(b.ownerActorId==sequence.ActorIds()[0] && !b.hitConsumed && b.age<b.lifetime && (b.active || b.launchDelayRemaining>0))rounds=true;
            overlap|=rounds && sequence.Coordination().AircraftState(1).reserved;
            solo|=rounds && !sequence.Coordination().AircraftState(1).reserved;
            for(const auto& a:live.Enemies())if(a.actorId==sequence.ActorIds()[0])shots=a.fireSequence;
        }
        runner.Expect(chargeStart>=0 && std::abs(lockedAt-chargeStart-1.10f)<=dt+.002f &&
            std::abs(firstShotAt-chargeStart-1.80f)<=dt+.002f,
            "the original sightline stays thin for 1.10s, then warns at full strength for .70s");
        runner.Expect(shots>0 && sequence.VeilBeamShots()>0 && overlap && solo,"the actual runtime launches both weapons and admits both overlapping and solo attacks fps="+
            std::to_string(fps)+" fang="+std::to_string(shots)+" veil="+std::to_string(sequence.VeilBeamShots())+" overlap="+std::to_string(overlap)+" solo="+std::to_string(solo));
    }
}
