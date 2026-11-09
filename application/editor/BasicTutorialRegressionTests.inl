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
        in.dt=1.0f/fps;in.primaryHeld=false;in.primaryShots=0;in.distance=445;in.lockFormation=true;
        for(int f=0;f<fps*2;++f)guide.Update(in);
        runner.Expect(guide.Current()==Lesson::Lock && guide.LockPhase()==LockStep::Hold,"lock teaching follows the actual lock formation");
        in.lockHeld=true;guide.Update(in);runner.Expect(guide.LockPhase()==LockStep::Sweep,"held right button advances to sweep instruction");
        RailLockToken enemy;enemy.target.kind=RailLockTargetKind::Enemy;enemy.target.actorId=1;
        auto projectile=enemy;projectile.target.kind=RailLockTargetKind::Projectile;projectile.target.projectileId=7;
        std::array<RailLockToken,2> wrong{projectile,projectile},tokens{enemy,enemy};
        in.locks=wrong;guide.Update(in);runner.Expect(guide.Locks()==0,"projectile locks never masquerade as formation locks");
        in.locks=tokens;guide.Update(in);runner.Expect(guide.LockPhase()==LockStep::Release && !guide.Learned(Lesson::Lock),"capture alone is not successful release");
        in.locks={};in.release=tokens;in.lockHeld=false;guide.Update(in);
        runner.Expect(!guide.Learned(Lesson::Lock),"an unaccepted volley cannot complete lock instruction");
        in.volleyAccepted=true;guide.Update(in);runner.Expect(guide.Learned(Lesson::Lock) && guide.Success(),"two real enemy locks and an accepted release earn success");
        in.volleyAccepted=false;in.release={};in.targetVisible=false;in.distance=RailBasicTutorial::BoostGuideStart;
        for(int f=0;f<fps*2;++f)guide.Update(in);
        runner.Expect(guide.Current()==Lesson::Boost,"clear runway introduces the accelerator");
        in.normalSpeed=32;in.speed=32;in.boostHeld=true;
        for(int f=0;f<fps;++f)guide.Update(in);
        runner.Expect(!guide.Learned(Lesson::Boost),"an E input stopped by a curve cannot fake actual acceleration");
        in.speed=40;in.rewardBoost=true;
        for(int f=0;f<fps;++f)guide.Update(in);
        runner.Expect(!guide.Learned(Lesson::Boost),"automatic kill boost cannot teach the accelerator");
        runner.Expect(guide.Visible() && guide.BoostWaiting(),"early E instruction signals that automatic boost is still active");
        in.rewardBoost=false;
        in.distance=700;
        for(int f=0;f<fps;++f)guide.Update(in);
        runner.Expect(guide.Learned(Lesson::Boost),"held manual acceleration and achieved speed earn the lesson");
        in.boostHeld=false;
        for(int f=0;f<fps*2;++f)guide.Update(in);
        runner.Expect(guide.Current()==Lesson::Brake,"brake follows the accelerator");
        in.speed=20;
        for(int f=0;f<fps;++f)guide.Update(in);
        runner.Expect(!guide.Learned(Lesson::Brake),"coasting or impact slowdown never earns brake mastery");
        in.brakeHeld=true;
        for(int f=0;f<fps;++f)guide.Update(in);
        runner.Expect(guide.Learned(Lesson::Brake),"Q and actual low speed complete braking");
        const auto learned=guide.LearnedMask();guide.ResetAttempt();guide.Update(in);
        runner.Expect(guide.LearnedMask()==learned && !guide.Visible(),"checkpoint retry preserves knowledge without repeating prompts");
        guide.Reset();guide.Update(in);runner.Expect(!guide.Learned(Lesson::Primary),"new run resets tutorial knowledge");
        in.boss=true;guide.Update(in);runner.Expect(!guide.Visible(),"boss keeps only the local interception cue");
        guide.Reset();in.boss=false;in.targetVisible=in.lockFormation=true;
        in.distance=RailBasicTutorial::SpeedEnd;guide.Update(in);
        runner.Expect(!guide.Visible(),"unlearned operations stay off the HUD in live combat");
        in.distance=445;
        in.brakeHeld=false;in.locks=std::span<const RailLockToken>(tokens.data(),1);guide.Update(in);
        runner.Expect(guide.LockPhase()==LockStep::Release,"one captured enemy already permits release under normal combat rules");
        in.release=in.locks;in.locks={};in.volleyAccepted=true;guide.Update(in);
        runner.Expect(guide.Learned(Lesson::Lock),"one accepted lock shot also teaches the control without requiring a full container");
    }
}

void TestBasicTutorialLayoutAndRunway(RegressionRunner& runner) {
    RailShooterHudDefinitionAsset definition;RailShooterHudPresentationFrame hud;hud.visible=true;
    RailBasicTutorial guide;RailBasicTutorialInput action;action.active=true;action.dt=.016f;action.targetVisible=true;action.distance=260;
    guide.Update(action);
    RailShooterHudRenderInput input;input.definition=&definition;input.presentation=&hud;input.basicTutorial=&guide;
    const auto has=[](const auto& commands,std::u8string_view text) {
        const std::string value(reinterpret_cast<const char*>(text.data()),text.size());
        return std::any_of(commands.begin(),commands.end(),[&](const auto& c){return c.text.find(value)!=std::string::npos;});
    };
    for(auto size:{Vector2{1280,720},Vector2{1600,900},Vector2{1024,768},Vector2{2560,1080}}) {
        input.viewportWidth=uint32_t(size.x);input.viewportHeight=uint32_t(size.y);
        RailShooterHudRenderer renderer;renderer.Update(input);
        runner.Expect(has(renderer.Frame().commands,u8"左クリック長押しで連射") && !has(renderer.Frame().commands,u8"砲台が"),"HUD teaches operation without prescribing combat tactics");
        runner.Expect(renderer.Frame().commands.size()<=definition.maximumDrawCommands,"tutorial stays within the production HUD budget");
        for(const auto& c:renderer.Frame().commands)if(c.kind==RailShooterHudDrawCommandKind::Rectangle)
            runner.Expect(c.x>=0 && c.y>=0 && c.x+c.width<=size.x+1 && c.y+c.height<=size.y+1,"tutorial rectangles fit supported viewports");
        input.bossVisible=true;renderer.Update(input);
        runner.Expect(!has(renderer.Frame().commands,u8"左クリック長押しで連射"),"global guide yields to the boss");input.bossVisible=false;
    }
    GameSessionRuntimeState liveSession;liveSession.phase=GameSessionPhase::Playing;
    liveSession.courseDistance=RailBasicTutorial::SpeedEnd-1;
    guide.Reset();action.targetVisible=false;action.threat=true;action.distance=liveSession.courseDistance;guide.Update(action);
    input.shortRun=true;input.resultSession=&liveSession;
    RailShooterHudRenderer liveHud;liveHud.Update(input);
    runner.Expect(has(liveHud.Frame().commands,u8"Q : ブレーキ"),"the control footer remains available in the practice area");
    liveSession.courseDistance=RailBasicTutorial::SpeedEnd;
    action.distance=liveSession.courseDistance;guide.Update(action);liveHud.Update(input);
    runner.Expect(!has(liveHud.Frame().commands,u8"Q : ブレーキ"),"the live course also hides the control footer");
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
