void TestBasicTutorialActions(RegressionRunner& runner) {
    using Lesson=RailBasicTutorial::Lesson;using LockStep=RailBasicTutorial::LockStep;
    for(int fps:{30,60,120}) {
        RailBasicTutorial guide;RailBasicTutorialInput in;
        in.dt=1.0f/fps;in.active=true;in.distance=260;in.targetVisible=true;
        guide.Update(in);runner.Expect(guide.Current()==Lesson::Primary && guide.Visible(),"first target introduces only the primary weapon");
        in.primaryHeld=true;
        for(int f=0;f<fps;++f)guide.Update(in);
        runner.Expect(!guide.Learned(Lesson::Primary),"holding a blocked weapon without real shots cannot complete the lesson");
        in.primaryShots=1;
        for(int f=0;f<fps/2;++f)guide.Update(in);
        runner.Expect(guide.Learned(Lesson::Primary) && guide.Success(),"accepted sustained primary fire earns confirmation");
        const float age=guide.Age();in.dt=0;guide.Update(in);
        runner.Expect(guide.Age()==age,"pause freezes confirmation and input progress");
        in.dt=1.0f/fps;in.primaryHeld=false;in.primaryShots=0;in.distance=445;in.lockTargetAvailable=true;
        for(int f=0;f<fps*2;++f)guide.Update(in);
        runner.Expect(guide.Current()==Lesson::Lock && guide.LockPhase()==LockStep::Hold,"lock teaching follows the actual lock formation");
        in.lockHeld=true;guide.Update(in);runner.Expect(guide.LockPhase()==LockStep::Sweep,"held right button advances to sweep instruction");
        RailLockToken enemy;enemy.target.kind=RailLockTargetKind::Enemy;enemy.target.actorId=1;
        auto projectile=enemy;projectile.target.kind=RailLockTargetKind::Projectile;projectile.target.projectileId=7;
        std::array<RailLockToken,2> wrong{projectile,projectile},tokens{enemy,enemy};
        in.locks=wrong;guide.Update(in);runner.Expect(guide.Locks()==0,"projectile locks never masquerade as formation locks");
        in.locks=tokens;guide.Update(in);runner.Expect(guide.LockPhase()==LockStep::Release && !guide.Learned(Lesson::Lock),"capture alone is not successful release");
        in.distance=RailBasicTutorial::BoostGuideStart+1;
        guide.Update(in);
        runner.Expect(guide.Current()==Lesson::Lock && guide.LockPhase()==LockStep::Release,
            "crossing the Q start cannot replace a usable lock exercise");
        in.lockTargetAvailable=false;
        for(int f=0;f<fps/5;++f)guide.Update(in);
        runner.Expect(guide.Current()==Lesson::Lock && guide.Visible(),"a brief loss of sight keeps the lock instruction");
        in.lockTargetAvailable=true;guide.Update(in);
        in.locks={};in.release=tokens;in.lockHeld=false;guide.Update(in);
        runner.Expect(!guide.Learned(Lesson::Lock),"an unaccepted volley cannot complete lock instruction");
        in.volleyAccepted=true;guide.Update(in);runner.Expect(guide.Learned(Lesson::Lock) && guide.Success(),"two real enemy locks and an accepted release earn success");
        in.volleyAccepted=false;in.release={};in.targetVisible=false;in.lockTargetAvailable=false;
        in.normalSpeed=32;in.speed=40;in.rewardBoost=true;in.boostHeld=true;
        float confirmationTime=guide.Age();
        while(guide.Current()==Lesson::Lock && confirmationTime<1.5f) {
            runner.Expect(guide.Visible() && guide.Success(),"the lock launch confirmation remains visible before Q");
            guide.Update(in);confirmationTime+=in.dt;
        }
        runner.Expect(confirmationTime>=1.25f-.0001f && confirmationTime<=1.25f+in.dt+.0001f,
            "Q follows the full 1.25 second lock launch confirmation");
        runner.Expect(guide.Current()==Lesson::Brake && guide.Visible() && !guide.Learned(Lesson::Boost),
            "Q is offered first even with an active kill boost and E already held");
        in.distance=RailBasicTutorial::SpeedEnd+1;in.speed=20;
        for(int f=0;f<fps;++f)guide.Update(in);
        runner.Expect(!guide.Learned(Lesson::Brake) && guide.Visible(),
            "speed alone cannot complete Q, and crossing the runway boundary does not discard the prompt");
        in.brakeHeld=true;
        for(int f=0;f<int(fps*.75f);++f)guide.Update(in);
        runner.Expect(guide.Learned(Lesson::Brake) && guide.Current()==Lesson::Brake && guide.Success(),
            "actual braking completes Q during the kill boost");
        in.brakeHeld=false;in.speed=40;
        for(int f=0;f<fps;++f)guide.Update(in);
        runner.Expect(guide.Visible() && guide.WaitingForRewardBoost() && guide.Current()==Lesson::None &&
            !guide.Learned(Lesson::Boost),"the HUD explains why E is waiting for the active reward");
        guide.ResetAttempt();in.rewardBoost=false;in.targetVisible=true;in.speed=32;guide.Update(in);
        runner.Expect(guide.Current()==Lesson::Boost && guide.Visible(),
            "E resumes after the reward ends beyond the runway, including with enemies on screen");
        for(int f=0;f<fps;++f)guide.Update(in);
        runner.Expect(!guide.Learned(Lesson::Boost),"holding E without actual acceleration does not teach it");
        in.speed=40;
        for(int f=0;f<int(fps*.75f);++f)guide.Update(in);
        runner.Expect(guide.Learned(Lesson::Boost) && guide.Visible() && guide.Success(),
            "accepted manual acceleration completes the sequence");
        const auto learned=guide.LearnedMask();guide.ResetAttempt();guide.Update(in);
        runner.Expect(guide.LearnedMask()==learned && !guide.Visible(),"checkpoint retry preserves knowledge without repeating prompts");
        guide.Reset();guide.Update(in);runner.Expect(!guide.Learned(Lesson::Primary),"new run resets tutorial knowledge");
        in.boss=true;guide.Update(in);runner.Expect(!guide.Visible(),"boss keeps only the local interception cue");
        guide.Reset();in.boss=false;in.targetVisible=true;
        in.distance=RailBasicTutorial::SpeedEnd;guide.Update(in);
        runner.Expect(guide.Current()==Lesson::Brake && guide.Visible(),
            "Q appears even if enemies obscured the original practice stretch and firearm lessons are incomplete");
        in.distance=445;
        in.brakeHeld=false;in.lockTargetAvailable=true;in.locks=std::span<const RailLockToken>(tokens.data(),1);guide.Update(in);
        runner.Expect(guide.LockPhase()==LockStep::Release,"one captured enemy already permits release under normal combat rules");
        in.release=in.locks;in.locks={};in.volleyAccepted=true;guide.Update(in);
        runner.Expect(guide.Learned(Lesson::Lock),"one accepted lock shot also teaches the control without requiring a full container");

        RailBasicTutorial skipped;RailBasicTutorialInput pass;
        pass.active=true;pass.dt=in.dt;pass.distance=445;pass.lockTargetAvailable=true;
        skipped.Update(pass);
        pass.distance=RailBasicTutorial::BoostGuideStart+1;pass.lockTargetAvailable=false;
        for(int f=0;f<fps/2+1;++f)skipped.Update(pass);
        runner.Expect(skipped.Current()==Lesson::Brake && skipped.Visible() && !skipped.Learned(Lesson::Lock),
            "losing all lock targets hands off to Q without requiring a shot");
        pass.lockTargetAvailable=true;pass.normalSpeed=32;pass.speed=20;pass.brakeHeld=true;
        for(int f=0;f<fps*2;++f)skipped.Update(pass);
        runner.Expect(skipped.Current()==Lesson::Boost && skipped.Learned(Lesson::Brake),
            "a returning target cannot interrupt the Q to E sequence");
        pass.brakeHeld=false;pass.boostHeld=true;pass.speed=40;pass.distance=1320;
        for(int f=0;f<fps*2;++f)skipped.Update(pass);
        runner.Expect(skipped.Learned(Lesson::Boost) && skipped.Current()==Lesson::Lock && !skipped.Learned(Lesson::Lock),
            "an available later practice target can reopen the skipped lesson after E");
    }
}

void TestBasicTutorialLayoutAndRunway(RegressionRunner& runner) {
    RailShooterHudDefinitionAsset definition;RailShooterHudPresentationFrame hud;hud.visible=true;
    RailBasicTutorial guide;RailBasicTutorialInput action;action.active=true;action.dt=.016f;action.targetVisible=true;action.distance=260;
    guide.Update(action);
    RailBasicTutorial brakeGuide;
    auto speedAction=action;
    speedAction.distance=RailBasicTutorial::BoostGuideStart;
    brakeGuide.Update(speedAction);
    auto boostGuide=brakeGuide;
    speedAction.dt=.1f;speedAction.brakeHeld=true;speedAction.speed=10;speedAction.normalSpeed=20;
    for(int i=0;i<14;++i)boostGuide.Update(speedAction);
    runner.Expect(brakeGuide.Current()==RailBasicTutorial::Lesson::Brake &&
        boostGuide.Current()==RailBasicTutorial::Lesson::Boost,"Q and E layout fixtures follow the live lesson sequence");
    RailShooterHudRenderInput input;input.definition=&definition;input.presentation=&hud;input.basicTutorial=&guide;
    const auto has=[](const auto& commands,std::u8string_view text) {
        const std::string value(reinterpret_cast<const char*>(text.data()),text.size());
        return std::any_of(commands.begin(),commands.end(),[&](const auto& c){return c.text.find(value)!=std::string::npos;});
    };
    for(auto size:{Vector2{1280,720},Vector2{1600,900},Vector2{1024,768},Vector2{2560,1080}}) {
        input.viewportWidth=uint32_t(size.x);input.viewportHeight=uint32_t(size.y);
        RailShooterHudRenderer renderer;renderer.Update(input);
        runner.Expect(has(renderer.Frame().commands,u8"左クリック長押しで連射") && !has(renderer.Frame().commands,u8"砲台が"),"HUD teaches operation without prescribing combat tactics");
        const auto primary=std::find_if(renderer.Frame().commands.begin(),renderer.Frame().commands.end(),
            [](const auto& command){return command.text=="左クリック長押しで連射";});
        const auto primaryText=primary!=renderer.Frame().commands.end()?*primary:RailShooterHudDrawCommand{};
        const auto checkSpeedCard=[&](const RailBasicTutorial& speedGuide,const char* instruction,const char* key) {
            input.basicTutorial=&speedGuide;renderer.Update(input);
            const auto& commands=renderer.Frame().commands;
            runner.Expect(std::any_of(commands.begin(),commands.end(),[&](const auto& c) {
                return c.text==instruction && c.x==primaryText.x && c.y==primaryText.y && c.fontScale==primaryText.fontScale;
            }) && std::any_of(commands.begin(),commands.end(),[&](const auto& c) {
                return c.text==key && c.x<primaryText.x && c.y<primaryText.y+16*primaryText.fontScale;
            }),"speed lesson uses the primary tutorial's upper card and shows its keycap");
        };
        checkSpeedCard(brakeGuide,"Q 長押しで減速","Q");
        checkSpeedCard(boostGuide,"E 長押しで加速","E");
        input.basicTutorial=&guide;renderer.Update(input);
        runner.Expect(renderer.Frame().commands.size()<=definition.maximumDrawCommands,"tutorial stays within the production HUD budget");
        for(const auto& c:renderer.Frame().commands)if(c.kind==RailShooterHudDrawCommandKind::Rectangle)
            runner.Expect(c.x>=0 && c.y>=0 && c.x+c.width<=size.x+1 && c.y+c.height<=size.y+1,"tutorial rectangles fit supported viewports");
        input.bossVisible=true;renderer.Update(input);
        runner.Expect(!has(renderer.Frame().commands,u8"左クリック長押しで連射"),"global guide yields to the boss");input.bossVisible=false;
    }
    GameSessionRuntimeState liveSession;liveSession.phase=GameSessionPhase::Playing;
    liveSession.courseDistance=RailBasicTutorial::SpeedEnd-1;
    guide.Reset();action.targetVisible=true;action.distance=liveSession.courseDistance;guide.Update(action);
    input.shortRun=true;input.resultSession=&liveSession;
    RailShooterHudRenderer liveHud;liveHud.Update(input);
    const auto hasControls=[&](const auto& commands) {
        return has(commands,u8"通常攻撃") && has(commands,u8"長押しでロックオン／離して発射") &&
            has(commands,u8"長押しでブレーキ") && has(commands,u8"長押しで加速");
    };
    runner.Expect(hasControls(liveHud.Frame().commands),"all four controls are visible in the practice area");
    runner.Expect(has(liveHud.Frame().commands,u8"Q 長押しで減速"),"the Q card and permanent controls are visible together");
    guide.Reset();
    liveSession.courseDistance=RailBasicTutorial::SpeedEnd;
    action.distance=liveSession.courseDistance;guide.Update(action);liveHud.Update(input);
    runner.Expect(hasControls(liveHud.Frame().commands),"the controls remain available after the tutorial runway");
    input.bossVisible=true;
    input.viewportWidth=640;input.viewportHeight=360;
    liveHud.Update(input);
    runner.Expect(hasControls(liveHud.Frame().commands),"all four controls remain visible in a small boss view");
    CourseAsset course;std::string error;runner.Expect(course.LoadFromFile(RailShortCoursePath,&error),"tutorial course loads");
    const auto cooked=CourseWaveRuntimeCompiler{}.Compile(course,{});runner.Expect(cooked.succeeded,"retimed teaching course compiles");
    for(const auto& wave:cooked.program.waves)if(wave.waveGuid=="short_intercept" || wave.waveGuid=="short_finale") {
        runner.Expect(wave.triggerRailDistance>=RailBasicTutorial::SpeedEnd,"new waves stay after the speed lesson runway");
        for(auto index:wave.actorIndices) {
            const auto& actor=cooked.program.actors[index].actor;
            const float ahead=actor.spawnDistance+actor.distanceOffset-wave.triggerRailDistance;
            runner.Expect(ahead>=45 && ahead<=120,"retimed aircraft remain ahead of their real trigger");
        }
    }
    for(float d=RailBasicTutorial::SpeedStart;d<RailBasicTutorial::SpeedEnd;d+=20)
        runner.Expect(!ShortRunAllowsEarlyWave(d) && !ShortRunAllowsAdaptivePatrol(d),"runway excludes promoted formations and random patrols");
}
