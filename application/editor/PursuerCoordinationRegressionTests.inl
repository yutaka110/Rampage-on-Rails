void TestPursuerCoordination(RegressionRunner& runner) {
    using C=RailPursuerCoordination;using O=C::Owner;
    const auto context=[] {
        C::Input in;in.aircraft[0]={true,true,true,true,30,6,6,0};
        in.aircraft[1]={true,true,true,true,50,-12,10,0};return in;
    };
    for(int fps:{30,60,120}) {
        const auto first=[&](float speed,float acceleration,bool clear) {
            C c;auto in=context();in.speed=speed;in.acceleration=acceleration;in.aircraft[1].lineClear=clear;
            for(int i=0;i<fps*3 && c.Reservation()==O::None;++i)c.Tick(1.0f/fps,in);
            return c.Reservation();
        };
        runner.Expect(first(36.45f,0,true)==O::Fang && first(60,12,true)==O::Veil && first(12,-18,true)==O::Veil,
            "ordinary, accelerated and braked entries produce different readable attack priorities");
        runner.Expect(first(60,12,false)==O::Fang,"an occluded Veil yields its otherwise preferred attack to the fighter");
        C c;auto in=context();in.aircraft[1].lineClear=false;
        float fangLife=0,veilLife=0;unsigned lastGrant=0;std::vector<O> order;
        bool exclusive=true,movingTogether=false;uint64_t fired=0;float previousBank=0;bool smooth=true;
        for(int i=0;i<fps*23;++i) {
            const float dt=1.0f/fps;fangLife=(std::max)(0.0f,fangLife-dt);veilLife=(std::max)(0.0f,veilLife-dt);
            in.fangThreat=fangLife>0;in.veilThreat=veilLife>0;
            in.aircraft[1].lineClear=float(i)/fps>10;
            c.Tick(dt,in);
            if(c.GrantSerial()!=lastGrant){lastGrant=c.GrantSerial();order.push_back(c.Reservation());}
            exclusive&=!(c.VeilCanFire() && in.fangThreat) && !(c.FangCanFire() && in.veilThreat);
            if(c.FangCanFire() && c.LeaseAge()>1.35f && fired!=lastGrant) {
                ++in.aircraft[0].shots;fangLife=2.25f;fired=lastGrant;
            }
            if(c.VeilCanFire() && c.LeaseAge()>1.80f) {c.NotifyVeilFired();veilLife=.21f;}
            movingTogether|=c.Reservation()==O::Fang && std::abs(c.AircraftState(1).bank)>.001f;
            smooth&=std::abs(c.AircraftState(1).bank-previousBank)<.20f;previousBank=c.AircraftState(1).bank;
        }
        runner.Expect(exclusive && movingTogether && smooth,"two aircraft keep moving independently while live shots retain the sole attack reservation");
        runner.Expect(order.size()>=3 && order[0]==O::Fang && order[1]==O::Fang &&
            std::find(order.begin(),order.end(),O::Veil)!=order.end(),"a blocked gun permits repeat fighter attacks and re-enters after its actual sight line clears");
        const auto checkpoint=c;c.Tick(0,in);
        runner.Expect(c.GrantSerial()==checkpoint.GrantSerial() && c.LeaseAge()==checkpoint.LeaseAge(),"pause consumes no flight, cooldown or reservation time");
        auto restored=checkpoint;for(int i=0;i<fps;++i){c.Tick(1.0f/fps,in);restored.Tick(1.0f/fps,in);}
        runner.Expect(c.Reservation()==restored.Reservation() && c.AircraftState(1).formation.y==restored.AircraftState(1).formation.y,
            "checkpoint copies reproduce subsequent independent flight and attack choices");
        C veil;auto boost=context();boost.speed=60;
        for(int i=0;i<fps*3 && !veil.VeilCanFire();++i)veil.Tick(1.0f/fps,boost);
        veil.CancelVeil();runner.Expect(!veil.VeilCanFire() && veil.VeilInterrupted(),"a real sensor interrupt cancels its reservation and provides reaction grace");
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
        float distance=4000;bool exclusive=true;uint64_t shots=0;
        for(int i=0;i<fps*42;++i) {
            const float dt=1.0f/fps;distance+=36.45f*dt;
            sequence.Update(dt,distance,true,live,dispatcher,nullptr,dt,36.45f);
            CourseEnemyFireSafetyFrameInput safety;safety.playerDistance=distance;safety.playerSpeed=36.45f;safety.deltaTime=dt;
            safety.railPath=&path;safety.hasCameraPosition=true;const auto cart=path.Evaluate(distance).position;
            safety.cameraPosition={cart.x,cart.y+7.8f,cart.z+17};safety.cameraAllowsEnemyFire=sequence.CameraWeight()>.999f;
            live.Update(dt,safety);
            // This headless fixture acknowledges the same reserved warning
            // that the host presents through its real HUD/audio pipeline.
            for(const auto& actor:live.Enemies())if(actor.attackState.tokenReserved)
                live.MarkEnemyAttackTelegraphPresented(actor.actorId,actor.attackState.intentSequence);
            PlayerHitboxRuntimeState player;player.distance=distance;player.verticalOffset=1.4f;
            sequence.AdvanceVeilBeam(dt,live,player);
            bool rounds=false;
            for(const auto& b:live.Bullets())if(b.ownerActorId==sequence.ActorIds()[0] && !b.hitConsumed && b.age<b.lifetime && (b.active || b.launchDelayRemaining>0))rounds=true;
            exclusive&=!(rounds && sequence.VeilBeam().phase!=RailPostBossSequence::BeamPhase::Inactive);
            for(const auto& a:live.Enemies())if(a.actorId==sequence.ActorIds()[0])shots=a.fireSequence;
        }
        runner.Expect(shots>0 && sequence.VeilBeamShots()>0 && exclusive,"the actual runtime admits both weapons and preserves mutual exclusion throughout their real warning/projectile lifetimes fps="+
            std::to_string(fps)+" fang="+std::to_string(shots)+" veil="+std::to_string(sequence.VeilBeamShots())+" exclusive="+std::to_string(exclusive));
    }
}
