// Included inside the core runner's namespace after the shared gameplay fixtures.
void TestKillBoostCountAndActivation(RegressionRunner& runner) {
    const auto defeat=[](unsigned id) {
        EnemyCombatEvent e;e.kind=EnemyCombatEventKind::Defeated;e.actorId=id;e.shotId=1000+id;
        e.actorAssetId="short_scout";e.waveId="short_fire";e.appliedDamage=24;e.feedbackKind=HitFeedbackKind::Destroyed;
        return e;
    };
    for(int fps:{30,60,120}) {
        const float dt=1.0f/fps;RailKillBoost boost;
        for(unsigned id=1;id<=5;++id)runner.Expect(boost.ObserveDefeat(defeat(id)),"five unique player kills accumulate charge");
        boost.Update(dt);runner.Expect(boost.Charge()==5 && !boost.Active(),"five kills cannot start the reward");
        runner.Expect(!boost.ObserveDefeat(defeat(1)) && boost.Charge()==5,"the same defeated enemy never counts twice");
        for(int invalid=0;invalid<5;++invalid) {
            auto e=defeat(99);
            if(invalid==0)e.kind=EnemyCombatEventKind::Retired;
            if(invalid==1)e.shotId=0;
            if(invalid==2)e.appliedDamage=0;
            if(invalid==3)e.actorAssetId="short_mothership_heavy";
            if(invalid==4)e.feedbackKind=HitFeedbackKind::NormalHit;
            runner.Expect(!boost.ObserveDefeat(e) && boost.Charge()==5,"cleanup, boss parts and nonlethal contacts do not charge the reward");
        }
        runner.Expect(boost.ObserveDefeat(defeat(6)),"sixth real defeat fills the reward");
        boost.Update(dt,true,false);runner.Expect(boost.Charge()==6 && !boost.Active(),"an introduction holds a full charge until control resumes");
        auto retry=boost;boost.Update(dt);runner.Expect(boost.Active() && boost.Charge()==0 && boost.Serial()==1,"six kills automatically start one boost");
        const float age=boost.Age();boost.Update(1,false);runner.Expect(boost.Age()==age,"pause preserves reward timing");
        runner.Expect(!retry.ObserveDefeat(defeat(6)),"checkpoint copies preserve kill deduplication");
        retry.Update(dt);runner.Expect(retry.Serial()==1 && retry.Charge()==0,"retry restores a pending reward exactly once");
        for(unsigned id=7;id<=30;++id)boost.ObserveDefeat(defeat(id));
        runner.Expect(boost.Charge()==6,"kills during a boost retain at most one next activation");
        for(int frame=0;frame<int(std::ceil((RailKillBoost::TotalSeconds+.1f)*fps));++frame)boost.Update(dt);
        runner.Expect(boost.Serial()==2 && boost.Active() && boost.Charge()==0,"one banked boost begins after the release, without speed stacking");
        boost.Reset();runner.Expect(boost.TotalKills()==0 && boost.Charge()==0 && !boost.Active(),"a fresh run clears reward and counted IDs");
    }
}

__declspec(noinline) void TestKillBoostFeedback(RegressionRunner& runner) {
    ge3::core::ShaderCompiler compiler;
    runner.Expect(compiler.Initialize(),"reward overlay initializes the production shader compiler");
    const auto shader=compiler.CompileFromFile(L"Resources/RailHudAtlas.PS.hlsl",L"main",L"ps_6_0",{L"-O3",L"-WX"});
    runner.Expect(shader && shader->GetBufferSize()>0,"native reward overlay, ordinary HUD and ring compile together in Release");
    for(const char* file:{"Resources/audio/rail_boost_ignition.wav","Resources/audio/rail_kill_boost_release.wav"}) {
        std::ifstream wave(file,std::ios::binary);std::vector<char> bytes((std::istreambuf_iterator<char>(wave)),{});
        runner.Expect(bytes.size()>16000 && bytes.size()<60000 && std::string(bytes.begin(),bytes.begin()+(std::min)(bytes.size(),size_t(4)))=="RIFF",
            "short original boost onset/release sounds are present as native PCM waves");
    }
    for(int fps:{30,60,120}) {
        RailSpeedDirector director;const float dt=1.0f/fps;
        const auto fill=[&] {
            for(unsigned id=1;id<=6;++id) {
                EnemyCombatEvent e;e.kind=EnemyCombatEventKind::Defeated;e.actorId=id;e.shotId=id;
                e.actorAssetId="short_scout";e.appliedDamage=1;e.feedbackKind=HitFeedbackKind::Destroyed;
                director.KillBoost().ObserveDefeat(e);
            }
        };
        fill();RailBoostPresentation exhaust;RailVehicleAudioBridge audio;
        RailVehicleActorFrame actor;actor.active=true;RailVehiclePresentationFrame presentation;presentation.speedNormalized=.7f;
        unsigned ignitions=0,releases=0;bool holdVisible=false,windVisible=false;
        for(int frame=0;frame<6*fps;++frame) {
            const float previousAge=director.KillBoost().Age();director.KillBoost().Update(dt);
            const float speed=32*director.BoostMultiplier();
            const float strength=director.BoostPresentationStrength(speed,32);
            const auto feedback=BuildRailKillBoostFeedback(director.KillBoost(),strength,previousAge,true,dt);
            ignitions+=feedback.ignitionSound;releases+=feedback.releaseSound;
            if(director.KillBoost().Age()>1 && director.KillBoost().Age()<3) {
                holdVisible|=feedback.visible && std::abs(feedback.strength-1)<.001f;
                RailVehicleAudioInput ai;ai.actor=&actor;ai.presentation=&presentation;ai.deltaTime=dt;ai.manualBoostStrength=strength;audio.Update(ai);
                runner.Expect(audio.Frame().rollingLoopPitch>1.25f && audio.Frame().rollingLoopGain>1.39f,
                    "timed reward raises the continuous rolling sound even with the manual key released");
            }
            RailBoostPresentationInput vi;vi.deltaTime=dt;vi.gameplayActive=true;vi.speed=speed;
            vi.distance=float(frame)*dt*speed;vi.ground={0,0,vi.distance};vi.strength=strength;vi.ignition=feedback.ignition;
            exhaust.Update(vi);windVisible|=exhaust.Frame().ribbonCount>8;
            runner.Expect(!BuildRailKillBoostFeedback(director.KillBoost(),strength,previousAge,true,0).ignitionSound &&
                !BuildRailKillBoostFeedback(director.KillBoost(),strength,previousAge,true,0).releaseSound,
                "pause or repeated rendering cannot replay either transition sound");
        }
        runner.Expect(ignitions==1 && releases==1 && holdVisible && windVisible,"one reward produces one onset, continuous speed feedback and one release at every frame rate");
        runner.Expect(director.BoostPresentationStrength(64,32)==0 &&
            !BuildRailKillBoostFeedback(director.KillBoost(),1,-1,true,dt).visible,
            "ordinary high speed after expiry cannot leave green light or reward audio behind");
        director.KillBoost().Reset();fill();director.KillBoost().Update(dt);
        const auto stalled=BuildRailKillBoostFeedback(director.KillBoost(),director.BoostPresentationStrength(32,32),-1,false,dt);
        runner.Expect(stalled.strength==0 && stalled.ignition==0 && !stalled.ignitionSound,
            "a speed-limited cart cannot promise acceleration with light or an ignition sound");
        director.KillBoost().Update(dt,true,false);
        const auto blocked=BuildRailKillBoostFeedback(director.KillBoost(),1,0,true,dt);
        runner.Expect(!blocked.visible && blocked.strength==0 && !blocked.releaseSound,
            "scripted control handovers suppress reward effects while retaining its state");
    }
}

struct KillBoostTravelFixture final {
    CourseAsset course;RailPath rail;CourseRuntime travel;RailVehicleMovementSystem vehicle;
    RailSpeedDirector speed;RailRideDirector ride;RailRideMotionEnvelope envelope;
    explicit KillBoostTravelFixture(float base=32) {
        course.railPoints={{{0,0,0},40,base},{{0,0,6000},40,base}};
        course.ApplyToRailPath(rail);travel.Bind(&course);travel.Reset(100);vehicle.Reset(100,base,&rail);
        auto moving=vehicle.State();moving.movementEnabled=true;vehicle.RestoreState(moving,&rail);
        RailSpeedDirectorSettings settings;settings.enabled=false;speed.Configure(settings);
    }
    void Charge() {
        for(unsigned id=1;id<=6;++id) {
            EnemyCombatEvent e;e.kind=EnemyCombatEventKind::Defeated;e.actorId=id;e.shotId=id;e.actorAssetId="short_scout";
            e.appliedDamage=24;e.feedbackKind=HitFeedbackKind::Destroyed;speed.KillBoost().ObserveDefeat(e);
        }
    }
    void Step(float dt) {
        RailSpeedDirectorFrameInput si;si.course=&course;si.railPath=&rail;si.distance=travel.Distance();si.deltaTime=dt;
        const auto policy=speed.Evaluate(si);
        RailRideDirectorInput ri;ri.course=&course;ri.railPath=&rail;ri.distance=travel.Distance();ri.baseRequestedSpeed=policy.requestedSpeed;
        const auto& intent=ride.Evaluate(ri);
        RailRideMotionEnvelopeInput ei;ei.vehicleDefinition=&vehicle.Definition();ei.vehicleState=&vehicle.State();ei.ride=&intent;ei.railPath=&rail;
        ei.normalSpeedScale=speed.Settings().normalSpeedScale;ei.manualBoostMultiplier=speed.BoostMultiplier();
        ei.rewardBoostAttack=speed.KillBoost().Accelerating()?1.0f:0.0f;
        const auto& limits=envelope.Evaluate(ei);
        RailVehicleMovementInput mi;mi.deltaTime=dt;mi.courseRuntime=&travel;mi.railPath=&rail;mi.requestedSpeed=limits.requestedSpeed;
        mi.manualBoostMultiplier=limits.manualBoostMultiplier;mi.motionEnvelopeActive=limits.active;mi.accelerationLimit=limits.accelerationLimit;
        mi.brakingLimit=limits.brakingLimit;mi.jerkLimit=limits.jerkLimit;mi.movementEnabled=true;
        speed.NotifyCourseEvents(vehicle.Update(mi).triggeredEvents);
    }
    std::shared_ptr<const RailTravelPrediction> Forecast(float dt,float horizon)const {
        RailTravelPredictionInput input;input.vehicle=&vehicle;input.speedDirector=&speed;input.courseRuntime=&travel;
        input.course=&course;input.railPath=&rail;input.simulationStepSeconds=dt;return BuildRailTravelPrediction(input,horizon);
    }
};

__declspec(noinline) void TestKillBoostFloatingTurn(RegressionRunner& runner) {
    const auto at=[](float age) {
        RailKillBoost boost;
        for(unsigned id=1;id<=6;++id) {
            EnemyCombatEvent e;e.kind=EnemyCombatEventKind::Defeated;e.actorId=id;e.shotId=id;
            e.actorAssetId="short_scout";e.appliedDamage=1;e.feedbackKind=HitFeedbackKind::Destroyed;boost.ObserveDefeat(e);
        }
        boost.ActivatePending();
        while(boost.Age()+.000001f<age)boost.Update((std::min)(.01f,age-boost.Age()));
        return boost;
    };
    const auto launch=at(.20f),middle=at(.50f),aligned=at(.72f),landed=at(.81f);
    runner.Expect(launch.Blend()>=.94f && launch.Blend()<1 && middle.Blend()>launch.Blend() && middle.Blend()<1 && landed.Blend()==1,
        "a strong first 0.2s launch keeps a small real acceleration tail through the complete float-and-turn");
    runner.Expect(middle.SpinDegrees()>200 && middle.SpinDegrees()<300 && std::abs(aligned.SpinDegrees()-360)<.01f && !landed.Spinning(),
        "the body keeps turning beyond the old half-second limit, aligns before landing and finishes the full 0.8s sequence");
    for(float boundary:{.02f,.06f,.18f,.50f,.60f,.72f,.80f}) {
        const auto left=at(boundary-.0001f),right=at(boundary+.0001f);
        runner.Expect(std::abs(std::remainder(right.SpinDegrees()-left.SpinDegrees(),360.0f))<.2f &&
            std::abs(right.Lift()-left.Lift())<.002f && std::abs(right.Blend()-left.Blend())<.002f,
            "ignition, angular ramps, floating hold and landing meet without a pose or speed jump");
    }
    std::ofstream metrics("logs/kill_boost_floating_turn.csv");metrics<<"fps,age,turn,angular_speed,lift,speed_blend\n";
    for(int fps:{30,60,120}) {
        auto reward=at(0);RailBoostPresentation exhaust;
        float previous=0,peakAngular=0,highest=0,lowest=0;bool steady=false,landing=false;
        const Vector3 pivot{3,4,5};const auto stable=MakeAffineMatrix(Vector3{1,1,1},Vector3{},pivot);
        const auto wheel=MakeAffineMatrix(Vector3{1,1,1},Vector3{},Vector3{5,3,8});
        for(int frame=1;frame<=fps;++frame) {
            reward.Update(1.0f/fps);
            const float turn=reward.SpinDegrees(),lift=reward.Lift();
            if(reward.Spinning()) {
                runner.Expect(turn>=previous-.001f && turn<=360.001f,"the float performs exactly one forward turn with no unwinding");
                const float angular=(turn-previous)*fps;peakAngular=(std::max)(peakAngular,angular);
                if(reward.Age()>.24f && reward.Age()<.55f) {
                    steady=true;runner.Expect(std::abs(angular-666.6667f)<.15f,
                        "the middle angular speed stays uniform instead of concentrating the turn into a fast flick");
                }
                metrics<<fps<<','<<reward.Age()<<','<<turn<<','<<angular<<','<<lift<<','<<reward.Blend()<<'\n';
                previous=turn;
            }
            highest=(std::max)(highest,lift);lowest=(std::min)(lowest,lift);landing|=lift<-.005f;
            const auto body=RailKillBoostPose::Apply(stable,pivot,{0,1,0},turn,lift);
            const auto spunWheel=RailKillBoostPose::Apply(wheel,pivot,{0,1,0},turn,lift);
            const auto localWheel=rail_boost_detail::Point({2,-1,3},body);
            runner.Expect(std::abs(spunWheel.m[3][0]-localWheel.x)+std::abs(spunWheel.m[3][1]-localWheel.y)+
                std::abs(spunWheel.m[3][2]-localWheel.z)<.0001f,"wheels float, turn and settle as one rigid body without sliding away from the cart");
            RailBoostPresentationInput input;input.deltaTime=1.0f/fps;input.gameplayActive=true;
            input.speed=32;input.strength=1;input.bodyWorld=body;exhaust.Update(input);
            const auto root=rail_boost_detail::Point({-RailBoostExhaust::RootX,RailBoostExhaust::RootY,RailBoostExhaust::RootZ-.025f},body);
            runner.Expect(exhaust.Frame().ribbonCount>=2 &&
                std::abs(exhaust.Frame().ribbons[0].head.x-root.x)+std::abs(exhaust.Frame().ribbons[0].head.y-root.y)+
                std::abs(exhaust.Frame().ribbons[0].head.z-root.z)<.0001f,"exhaust roots stay on the same floating and rotating chassis throughout the full stunt");
        }
        runner.Expect(steady && peakAngular>660 && peakAngular<675 && highest>.399f && lowest>=-.036f && landing && reward.Lift()==0,
            "float, uniform spin and a small non-oscillating landing settle complete at each frame rate");
        runner.Expect(stable.m[3][0]==pivot.x && stable.m[3][1]==pivot.y && stable.m[2][2]==1,
            "the spin never changes the stable pose supplied to gameplay aim, camera framing or hitboxes");
    }
}

__declspec(noinline) void TestKillBoostRapidTravelAndSurge(RegressionRunner& runner) {
    std::ofstream metrics("logs/kill_boost_rapid_travel.csv");metrics<<"fps,base,manual,time,speed,target,spin,forecast_error\n";
    for(int fps:{30,60,120})for(float base:{18.9f,32.0f,64.0f})for(bool manual:{false,true}) {
        const float dt=1.0f/fps;KillBoostTravelFixture live(base);
        live.speed.SetManualBoostInput(manual,true);
        for(int i=0;i<fps;++i)live.Step(dt);
        const float before=live.vehicle.State().speed;
        live.Charge();live.speed.KillBoost().ActivatePending();
        const float target=RailManualBoost::RequestedSpeed(base,RailKillBoost::MaximumMultiplier);
        const auto forecast=live.Forecast(dt,7);
        for(int frame=1;frame<=fps*7;++frame) {
            live.Step(dt);const auto& boost=live.speed.KillBoost();const float time=frame*dt;
            const float error=std::abs(live.travel.Distance()-forecast->DistanceAt(time));
            runner.Expect(error<.06f,"rapid acceleration and release match the production enemy travel forecast");
            if(frame==fps/5) {
                runner.Expect(live.vehicle.State().speed>=before+.90f*(target-before) && live.vehicle.State().speed<=target+.001f,
                    "at least ninety percent of the real speed gap is reached at 0.2s without overshoot: base="+std::to_string(base)+" speed="+std::to_string(live.vehicle.State().speed));
                runner.Expect(boost.Spinning() && boost.SpinDegrees()>30 && boost.SpinDegrees()<180,
                    "real acceleration is already strong while the independent float-and-turn sequence is still in progress");
            }
            if(time>RailKillBoost::SpinSeconds+.05f)runner.Expect(!boost.Spinning(),"the fast launch does not shorten or repeat the float-and-turn sequence");
            if(time<.8f && frame%(fps/30)==0)metrics<<fps<<','<<base<<','<<manual<<','<<time<<','<<live.vehicle.State().speed<<','<<target<<','<<boost.SpinDegrees()<<','<<error<<'\n';
        }
        runner.Expect(std::abs(live.vehicle.State().speed-RailManualBoost::RequestedSpeed(base,manual?1.3f:1.0f))<.03f,
            "the independent fast attack still releases back to the current ordinary or held-assist request");
    }
    for(int fps:{30,60,120})for(bool focus:{false,true}) {
        KillBoostTravelFixture live;live.Charge();live.speed.KillBoost().ActivatePending();
        RailVehicleCameraInertiaBridge base,burst;float peak=0,peakFov=0,lastLag=0;
        for(int frame=1;frame<=fps;++frame) {
            live.Step(1.0f/fps);const auto state=live.vehicle.State();
            RailVehicleCameraInertiaInput ci;ci.definition=&live.vehicle.Definition();ci.state=&state;
            ci.deltaTime=1.0f/fps;ci.aimFocusActive=focus;
            ci.manualBoostStrength=live.speed.BoostPresentationStrength(state.speed,32);
            ci.manualBoostIgnition=live.speed.BoostIgnitionPulse();
            const auto ordinary=base.Update(ci);
            ci.rewardBoostSurge=live.speed.KillBoost().SurgePulse()*(std::clamp)(ci.manualBoostStrength*3,0.0f,1.0f);
            const auto enhanced=burst.Update(ci);
            const float lag=ordinary.gameplayPositionOffset.z-enhanced.gameplayPositionOffset.z;
            peak=(std::max)(peak,lag);peakFov=(std::max)(peakFov,enhanced.fovOffsetRadians-ordinary.fovOffsetRadians);lastLag=lag;
            runner.Expect(lag>=-.0001f && lag<=(focus?1.351f:3.201f) &&
                std::abs(enhanced.gameplayPositionOffset.x-ordinary.gameplayPositionOffset.x)<.0001f && enhanced.rollOffsetRadians==ordinary.rollOffsetRadians,
                "the onset creates one bounded backward camera catch-up, with no lateral aim drift or roll shake");
            if(frame==fps/5) {
                ci.deltaTime=0;const auto paused=burst.Update(ci);
                runner.Expect(paused.gameplayPositionOffset.z==enhanced.gameplayPositionOffset.z,"pause cannot integrate the surge spring again");
            }
        }
        runner.Expect(peak>(focus?.7f:1.7f) && peakFov>0 && peakFov<=(focus?1.251f:2.501f)*.01745329252f && lastLag<.001f,
            "camera catch-up is perceptible on ignition and fully settles during sustained boost, including lock focus");
        burst.Reset();runner.Expect(!burst.Frame().active,"retry clears the transient camera history");
    }
}

__declspec(noinline) void TestKillBoostNearFieldFlow(RegressionRunner& runner) {
    ge3::core::ShaderCompiler compiler;
    runner.Expect(compiler.Initialize(),"near-ground grit initializes the native shader compiler");
    const auto shader=compiler.CompileFromFile(L"Resources/TitleDust.PS.hlsl",L"main",L"ps_6_0",{L"-O3",L"-WX"});
    runner.Expect(shader && shader->GetBufferSize()>0,"new grounded grit compiles with existing smoke/dust/impact materials in Release");
    for(int fps:{30,60,120}) {
        RailBoostPresentation effect;RailBoostPresentationInput in;
        in.deltaTime=1.0f/fps;in.gameplayActive=true;in.speed=35.78f;in.strength=in.nearFlow=1;
        for(int frame=0;frame<fps;++frame) {
            in.distance+=in.speed*in.deltaTime;in.ground={0,0,in.distance};effect.Update(in);
        }
        const auto before=effect.Frame();
        runner.Expect(before.groundMarkCount>8 && before.groundMarkCount<=32 && before.ribbonCount<=before.ribbons.size(),
            "ground grit and side flow have bounded live geometry at 30/60/120 fps");
        bool left=false,right=false,sideWind=false;
        for(size_t i=0;i<before.groundMarkCount;++i) {
            const auto& mark=before.groundMarks[i];left|=mark.center.x<0;right|=mark.center.x>0;
            runner.Expect(std::abs(mark.center.x)>=3.9f && std::abs(mark.center.y-.06f)<.001f && mark.horizontal.y==0 && mark.vertical.y==0 &&
                mark.opacity>=0 && mark.opacity<=.46f,"ground marks stay low and outside the combat lane instead of covering the reticle");
        }
        for(size_t i=0;i<before.ribbonCount;++i)if(!before.ribbons[i].propulsion)
            sideWind|=std::abs(before.ribbons[i].head.x)>5.5f && before.ribbons[i].head.y>1.6f;
        runner.Expect(left && right && sideWind,"both sides of the ground and the side air provide close passing reference points");
        in.distance+=.25f;in.ground.z+=.25f;effect.Update(in);
        bool fixed=false;
        for(size_t i=0;i<before.groundMarkCount && !fixed;++i)for(size_t j=0;j<effect.Frame().groundMarkCount;++j) {
            const auto& a=before.groundMarks[i];const auto& b=effect.Frame().groundMarks[j];
            fixed|=a.seed==b.seed && a.center.x==b.center.x && a.center.z==b.center.z;
        }
        runner.Expect(fixed,"already emitted grit stays in world space as the vehicle passes, never tracking the camera like HUD lines");
        const auto count=effect.Frame().groundMarkCount;in.deltaTime=0;effect.Update(in);
        runner.Expect(effect.Frame().groundMarkCount==count,"pause freezes close ground geometry");
        in.deltaTime=1.0f/fps;in.strength=in.nearFlow=0;
        for(int frame=0;frame<fps*2;++frame)effect.Update(in);
        runner.Expect(effect.Frame().groundMarkCount==0 && !effect.Visible(),"expired boost removes close flow without a permanent dusty overlay");
        effect.Reset();runner.Expect(effect.Frame().groundMarkCount==0,"retry/title clears every close-flow slot");
    }
}

__declspec(noinline) void TestKillBoostImmediateOnset(RegressionRunner& runner) {
    const auto defeat=[](unsigned id) {
        EnemyCombatEvent e;e.kind=EnemyCombatEventKind::Defeated;e.actorId=id;e.shotId=id;
        e.actorAssetId="short_scout";e.waveId="short_fire";e.appliedDamage=24;e.feedbackKind=HitFeedbackKind::Destroyed;
        return e;
    };
    RailKillBoostControlInput control{true,true,true,false,false};
    for(int blocked=0;blocked<5;++blocked) {
        auto locked=control;
        if(blocked==0)locked.gameplayActive=false;
        if(blocked==1)locked.playerInputEnabled=false;
        if(blocked==2)locked.vehicleMounted=false;
        if(blocked==3)locked.startupBlocked=true;
        if(blocked==4)locked.previewOwnsRail=true;
        RailKillBoost pending;for(unsigned i=1;i<=6;++i)pending.ObserveDefeat(defeat(i));
        runner.Expect(!pending.ActivatePending(true,locked.AllowsActivation()) && pending.Charge()==6 && !pending.Active(),
            "pause, lost player control, unmounted mode, startup and editor preview retain the full charge");
        runner.Expect(pending.ActivatePending(true,control.AllowsActivation()) && pending.Age()==0 && pending.Charge()==0,
            "restoring real player control starts a deferred reward without spending another simulation tick");
    }
    runner.Expect(RailBoostHasAcceleration(32,32.02f,32.02f,0),
        "reaching this frame's rising speed request still authorizes ignition instead of suppressing it after movement");
    runner.Expect(!RailBoostHasAcceleration(32,32,32,0) && !RailBoostHasAcceleration(32,31,30,0) &&
        !RailBoostHasAcceleration(32,33,48,.2f),"a stopped speed ramp, braking curve or impact slowdown cannot promise acceleration");
    for(int fps:{30,60,120}) {
        const float dt=1.0f/fps;KillBoostTravelFixture live;live.Step(dt);
        RailMothership::Reveal flyby;
        const auto intro=flyby.Update(dt,live.travel.Distance(),live.travel.Distance(),live.rail,true,true);
        runner.Expect(intro.introductionActive && intro.cameraWeight==0 && control.AllowsActivation(),
            "the actual passive carrier entrance keeps its staging while player-controlled reward activation remains allowed");
        auto& reward=live.speed.KillBoost();
        for(unsigned id=1;id<=6;++id) {
            runner.Expect(reward.ObserveDefeat(defeat(id)),"the causal onset accepts each unique real defeat payload");
            const bool started=reward.ActivatePending(true,control.AllowsActivation());
            runner.Expect(started==(id==6),"exactly the sixth credit starts the reward in its own event-processing frame");
        }
        runner.Expect(reward.Active() && reward.Age()==0 && reward.Serial()==1 && reward.IgnitionPulse()==1 && reward.SpinDegrees()==0,
            "sixth defeat immediately owns ignition, with no pending READY frame or instant rotation jump");
        const auto feedback=BuildRailKillBoostFeedback(reward,0,-1,true,dt);
        runner.Expect(feedback.ignitionSound && feedback.ignition==1 && feedback.strength==0,
            "the sixth-defeat frame plays the point ignition once, without faking a speed already achieved");
        RailBoostPresentation exhaust;RailBoostPresentationInput vfx;
        vfx.deltaTime=dt;vfx.gameplayActive=true;vfx.speed=32;vfx.distance=live.travel.Distance();vfx.ignition=feedback.ignition;
        exhaust.Update(vfx);
        runner.Expect(exhaust.Frame().ignition==1 && exhaust.Frame().ribbonCount==2 && exhaust.Frame().lights[0].w>2,
            "both real exhaust roots and the localized white ignition light are submitted in the defeat frame");
        const auto forecast=live.Forecast(dt,1);
        const float before=live.vehicle.State().speed,startDistance=live.travel.Distance();
        bool turned=false;unsigned sounds=1;
        for(int frame=1;frame<=fps;++frame) {
            const float previousAge=reward.Age();live.Step(dt);
            if(reward.Age()<RailKillBoost::IgnitionLeadSeconds)
                runner.Expect(reward.SpinDegrees()==0 && reward.IgnitionPulse()>0,"a visible point ignition leads the turn by sixty milliseconds without a dead frame");
            turned|=reward.SpinDegrees()>30;
            const auto f=BuildRailKillBoostFeedback(reward,live.speed.BoostPresentationStrength(live.vehicle.State().speed,32),previousAge,true,dt);
            sounds+=f.ignitionSound;
            runner.Expect(std::abs(live.travel.Distance()-forecast->DistanceAt(frame*dt))<.02f,
                "the enemy travel forecast includes the same end-of-frame reward activation as real movement");
        }
        runner.Expect(live.vehicle.State().speed>before+20 && live.travel.Distance()>startDistance+40 && turned && sounds==1,
            "ignition flows into the complete turn and real speed ramp, with a single sound at each tested frame rate");
        for(unsigned id=7;id<=12;++id) {reward.ObserveDefeat(defeat(id));reward.ActivatePending(true,true);}
        runner.Expect(reward.Serial()==1 && reward.Charge()==6,"rapid batch defeats can bank the next six without restarting the current point ignition");
    }
}

__declspec(noinline) void TestKillBoostTravelPrediction(RegressionRunner& runner) {
    std::ofstream metrics("logs/kill_boost_travel.csv");metrics<<"fps,time,speed,boost,forecast_error\n";
    for(int fps:{30,60,120}) {
        const float dt=1.0f/fps;KillBoostTravelFixture live;live.Charge();
        const auto forecast=live.Forecast(dt,8);
        runner.Expect(forecast && !live.speed.KillBoost().Active() && live.speed.KillBoost().Charge()==6,
            "future reward simulation does not consume the real pending six kills");
        float onset=0,hold=0,final=0;
        for(int frame=1;frame<=8*fps;++frame) {
            live.Step(dt);const float time=frame*dt;
            const float error=std::abs(live.travel.Distance()-forecast->DistanceAt(time));
            runner.Expect(error<.06f,"shared forecast matches the actual reward onset, sustained speed and release: "+std::to_string(error));
            if(frame==fps/2)onset=live.vehicle.State().speed;
            if(frame==3*fps)hold=live.vehicle.State().speed;
            if(frame==8*fps)final=live.vehicle.State().speed;
            if(frame%(fps/10)==0)metrics<<fps<<','<<time<<','<<live.vehicle.State().speed<<','<<live.speed.BoostMultiplier()<<','<<error<<'\n';
        }
        runner.Expect(onset>32*1.5f && std::abs(hold-32*1.8f)<.03f && std::abs(final-32)<.03f,
            "the real cart kicks forward within half a second, sustains 1.8x, then settles to cruise: onset="+std::to_string(onset));
        live.speed.SetManualBoostInput(true,true);
        for(int frame=0;frame<fps;++frame)live.Step(dt);
        runner.Expect(std::abs(live.speed.BoostMultiplier()-1.3f)<.001f,"held assist is 1.3x after the reward ends");
        live.speed.KillBoost().Reset();live.Charge();
        for(int frame=0;frame<fps;++frame)live.Step(dt);
        runner.Expect(std::abs(live.speed.BoostMultiplier()-1.8f)<.001f,"manual and reward boosts select the stronger multiplier instead of multiplying");
    }
    // Exact future travel must survive a shot launched across the timed release.
    CourseAsset production;std::string error;runner.Expect(production.LoadFromFile(RailShortCoursePath,&error),"reward prediction loads actual enemies");
    const auto cooked=CourseWaveRuntimeCompiler{}.Compile(production,{});
    for(const std::string pattern:{"assault_magenta_single","sniper_predictive_single","combat_expansion_homing_single"}) {
        const auto actorRecord=std::find_if(cooked.program.actors.begin(),cooked.program.actors.end(),[&](const auto& a){return a.actor.bulletPatternId==pattern;});
        runner.Expect(actorRecord!=cooked.program.actors.end(),"actual reward encounter pattern exists");
        if(actorRecord==cooked.program.actors.end())continue;
        for(int fps:{30,60,120})for(float launchAge:{.15f,RailKillBoost::SpinSeconds+RailKillBoost::HoldSeconds-.20f,RailKillBoost::SpinSeconds+RailKillBoost::HoldSeconds+.25f}) {
            const float dt=1.0f/fps;KillBoostTravelFixture live;live.Charge();
            for(int frame=0;frame<int(std::round(launchAge*fps));++frame)live.Step(dt);
            const auto forecast=live.Forecast(dt,7);
            CourseEnemyActor actor;actor.actorId=7;actor.desc=actorRecord->actor;actor.behaviorDefinition=actor.desc.behaviorDefinition;
            actor.desc.spawnDistance=live.travel.Distance()+65;actor.desc.distanceOffset=0;actor.desc.lateralOffset=18;actor.desc.verticalOffset=13;
            actor.desc.bulletCount=1;actor.desc.bulletLateralSpreadSpeed=actor.desc.bulletVerticalSpreadSpeed=0;
            actor.attackState.intentSequence=11;actor.attackState.tokenId=17;
            actor.targetingState.solutionLocked=actor.targetingState.compensatesForwardTravel=true;
            actor.targetingState.attackIntentSequence=11;actor.targetingState.attackTokenId=17;
            actor.targetingState.targetLateralOffset=0;actor.targetingState.targetVerticalOffset=4;
            actor.targetingState.forwardTravelReferenceDistance=live.travel.Distance();actor.targetingState.forwardTravelPrediction=forecast;
            actor.targetingState.playerForwardVelocity=live.vehicle.State().speed;
            std::vector<EnemyProjectileRuntimeState> bullets;EnemyProjectileSystem projectiles;
            runner.Expect(projectiles.SpawnVolley(actor,bullets)==1,"reward phase produces a reachable actual enemy shot");
            if(bullets.empty())continue;
            EnemyProjectileFrameInput pi;pi.deltaTime=dt;pi.playerDistance=live.travel.Distance();pi.playerVerticalOffset=4;
            if(bullets[0].frameEndLaunch)projectiles.Update(bullets,pi);
            PlayerHitboxSystem hitbox;PlayerHitboxFrameInput player;player.distance=live.travel.Distance();player.verticalOffset=4;hitbox.Update(player);
            bool hit=false;
            for(int frame=0;frame<6*fps && !hit;++frame) {
                live.Step(dt);pi.playerDistance=live.travel.Distance();projectiles.Update(bullets,pi);
                player.distance=live.travel.Distance();hitbox.Update(player);
                hit=hitbox.EvaluateProjectile(bullets[0]).kind==PlayerProjectileContactKind::Hit;
            }
            runner.Expect(hit,"normal/sniper/homing fire still reaches a passive cart during ignition or release: "+pattern+" age="+std::to_string(launchAge));
        }
    }
}

void TestKillBoostRealDefeats(RegressionRunner& runner) {
    CourseAsset course;std::string error;runner.Expect(course.LoadFromFile(RailShortCoursePath,&error),"kill credit loads real course");
    const auto cooked=CourseWaveRuntimeCompiler{}.Compile(course,{});runner.Expect(cooked.succeeded,"kill credit compiles authored actors");
    CourseSpawnRuntime runtime;CourseCollisionSystem collision;RailKillBoost boost,immediate;
    for(unsigned i=0;i<6;++i) {
        auto desc=cooked.program.actors[i].actor;desc.spawnDistance=100;desc.establishedEntrance=true;runtime.SpawnEnemyActor(desc);
    }
    auto saved=runtime.CaptureCheckpoint();
    for(auto& actor:saved.enemies) {actor.combatState.phase=EnemyCombatPhase::Engaging;actor.combatState.canReceiveDamage=true;actor.combatState.canBeTargeted=true;}
    runner.Expect(runtime.RestoreCheckpoint(saved,true,&error),"damageable real enemies restore");
    runtime.EnemyCombat().ConsumeEvents();
    unsigned count=0;
    for(const auto& actor:runtime.Enemies()) {
        WeaponHitRequest shot;shot.shotId=100+count;shot.targetActorId=actor.actorId;shot.hitKind=RailAimHitKind::Enemy;
        shot.damageType=WeaponDamageType::Energy;shot.lockOnContact=count%2==1;shot.baseDamage=10000;
        shot.rayDirection={0,0,1};shot.hitNormal={0,0,-1};
        const auto result=collision.ApplyWeaponHit(runtime,&course,shot);
        runner.Expect(result.damageApplied && result.destroyed,"real primary/lock contact defeats one actor");
        for(const auto& event:runtime.EnemyCombat().ConsumeEvents()) {
            boost.ObserveDefeat(event);
            if(immediate.ObserveDefeat(event))immediate.ActivatePending(true,true);
        }
        runner.Expect(boost.Charge()==++count,"accepted primary and lock defeats both contribute exactly one charge");
        collision.ApplyWeaponHit(runtime,&course,shot);
        for(const auto& event:runtime.EnemyCombat().ConsumeEvents())boost.ObserveDefeat(event);
        runner.Expect(boost.Charge()==count,"replayed weapon damage never grants another kill");
    }
    runner.Expect(immediate.Active() && immediate.Age()==0 && immediate.Serial()==1 && immediate.IgnitionPulse()==1,
        "real normal and travelling-lock damage credits activate ignition in the sixth-contact frame");
    boost.Update(1.0f/60);runner.Expect(boost.Active() && boost.Serial()==1,"six real player damage results activate the reward once");
}

__declspec(noinline) void TestKillBoostSpinAndMuzzle(RegressionRunner& runner) {
    std::string error;
    for(int fps:{30,60,120}) {
        KillBoostTravelFixture live;live.Charge();
        RailVehicleCombatMountBridge mounts;CourseCollisionSystem collision;
        runner.Expect(collision.LoadWeaponDefinitions("Resources/weapons",&error),"spin uses the production normal gun");
        const auto* weapon=collision.WeaponDefinitions().Find(RailWeaponIds::PulseCannon);
        runner.Expect(weapon!=nullptr,"spin resolves the authored normal-gun muzzle offset");
        // LastShot traces carry the real muzzle, independent of a display turn.
        CourseSpawnRuntime empty;float previousTurn=0;bool sidePose=false,shot=false;
        for(int frame=0;frame<=fps;++frame) {
            live.Step(1.0f/fps);const auto& reward=live.speed.KillBoost();
            const auto& vehicle=live.vehicle.State();
            RailVehicleCombatMountInput mi;mi.vehicleDefinition=&live.vehicle.Definition();mi.vehicleState=&vehicle;mounts.Update(mi);
            const auto mount=mounts.Frame();
            Matrix4x4 stable=MakeAffineMatrix(Vector3{1,1,1},Vector3{0,0,0},vehicle.position);
            const auto original=stable;
            const auto visual=RailKillBoostPose::Apply(stable,vehicle.position,vehicle.up,reward.SpinDegrees(),reward.Lift());
            runner.Expect(stable.m[0][0]==original.m[0][0] && stable.m[3][2]==original.m[3][2],
                "spin never mutates the stable pose used by framing and collision");
            if(reward.Spinning()) {
                runner.Expect(reward.SpinDegrees()>=previousTurn && reward.SpinDegrees()<=360,"onset makes one forward full turn");
                previousTurn=reward.SpinDegrees();sidePose|=std::abs(visual.m[0][2])>.8f;
            }
            runner.Expect(std::abs(visual.m[3][0]-vehicle.position.x)<.001f &&
                std::abs(visual.m[3][2]-vehicle.position.z)<.001f && std::abs(visual.m[3][1]-vehicle.position.y)<=RailKillBoost::MaximumLift+.001f,
                "body spin adds only a small vertical lift, never lateral travel or a camera rotation");
            RailAimState aim;aim.valid=true;aim.worldRayOrigin=mount.playerWorldPosition;
            aim.worldRayDirection={0,0,1};aim.worldAimPoint={0,4,vehicle.distance+90};
            CourseCollisionFrameInput input;input.deltaTime=1.0f/fps;input.railPath=&live.rail;input.worldAim=&aim;input.combatMount=&mount;
            input.player.distance=vehicle.distance;input.player.verticalOffset=4;input.weapon.enabled=input.weapon.triggerHeld=true;
            collision.Update(empty,input);
            for(const auto& trace:collision.PlayerShotTraces()) {
                const auto actual=RailMothership::Add(mount.weaponWorldPosition,RailMothership::Scale(mount.forward,weapon->muzzleForwardOffset));
                const float x=trace.start.x-actual.x,y=trace.start.y-actual.y,z=trace.start.z-actual.z;
                runner.Expect(x*x+y*y+z*z<.00001f,"normal flash and pulse begin at the same actual mount while the cart spins");
                runner.Expect(std::abs(actual.x-mount.weaponWorldPosition.x)<.001f && std::abs(actual.y-mount.weaponWorldPosition.y)<.001f,
                    "a presentation spin does not rotate the mounted firing origin sideways");
                shot=true;
            }
            runner.Expect(vehicle.up.y>.99f && mount.forward.z>.99f,"aim and physical up remain stable through the full spin");
        }
        runner.Expect(sidePose && shot && !live.speed.KillBoost().Spinning(),"the full turn is visible, permits firing and completes within the ignition phase");
    }
    // Wheels use the same rigid render turn about the cart pivot.
    const Vector3 pivot{10,4,20};const auto wheel=MakeAffineMatrix(Vector3{1,1,1},Vector3{0,0,0},{12,3,23});
    const auto turned=RailKillBoostPose::Apply(wheel,pivot,{0,1,0},90,.2f);
    runner.Expect(std::abs(turned.m[3][0]-13)<.001f && std::abs(turned.m[3][2]-18)<.001f &&
        std::abs(turned.m[3][1]-3.2f)<.001f,"rendered wheels rotate and lift with the body without altering rail contact authority");
}
