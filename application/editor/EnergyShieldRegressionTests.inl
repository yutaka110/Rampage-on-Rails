// Included in the core runner namespace, using actual runtime damage/volley paths.
struct EnergyShieldTestFixture {
    CourseAsset course;RailPath rail;CourseSpawnRuntime runtime;CourseCollisionSystem collision;
    explicit EnergyShieldTestFixture(RegressionRunner& runner) {
        course.railPoints={{{0,0,0},40,30},{{0,0,1000},40,30}};course.ApplyToRailPath(rail);
        CourseEnemyActorDesc desc;desc.actorAssetId="short_shield_scout";desc.meshId="twin_shield_hull";
        desc.spawnDistance=100;desc.verticalOffset=4;desc.radius=1;desc.hitPoints=28;desc.lifetime=90;
        desc.energyShield.enabled=true;desc.energyShield.maximumHitPoints=48;
        desc.combatDefinition=EnemyCombatDefinition::LegacyCompatible();desc.combatDefinition.definitionId="shield-test";
        runtime.SpawnEnemyActor(desc);
        auto saved=runtime.CaptureCheckpoint();auto& actor=saved.enemies.front();
        actor.combatState.canReceiveDamage=actor.combatState.canBeTargeted=true;
        actor.entranceExitState.initialized=true;actor.entranceExitState.targetable=true;
        actor.weaponMount.ready=true;actor.weaponMount.dronePose.visible=true;
        actor.weaponMount.dronePose.position={0,4,100};actor.weaponMount.dronePose.scale={1,1,1};
        std::string error;runner.Expect(runtime.RestoreCheckpoint(saved,true,&error),"shield fixture restores a real damageable actor");
        runtime.EnemyCombat().ConsumeEvents();
        runner.Expect(collision.LoadWeaponDefinitions("Resources/weapons",&error),"shield uses actual weapon definitions");
    }
    WeaponHitRequest Request(uint64_t id,bool lock=false,bool rear=false)const {
        WeaponHitRequest r;r.shotId=id;r.targetActorId=runtime.Enemies().front().actorId;
        r.hitKind=RailAimHitKind::Enemy;r.lockOnContact=lock;
        r.damageType=lock?WeaponDamageType::Ice:WeaponDamageType::Energy;r.baseDamage=lock?34.0f:12.0f;
        r.rayOrigin={0,4,rear?110.0f:10.0f};r.rayDirection={0,0,rear?-1.0f:1.0f};
        r.hitPoint={0,4,100};r.hitNormal={0,0,rear?1.0f:-1.0f};r.hitDistance=rear?10.0f:90.0f;return r;
    }
};

void TestEnergyShieldAuthority(RegressionRunner& runner) {
    EnergyShieldTestFixture f(runner);RailKillBoost reward;
    for(uint64_t id=1;id<=8;++id) {
        const auto hit=f.collision.ApplyWeaponHit(f.runtime,&f.course,f.Request(id,true));
        runner.Expect(hit.shieldHit && hit.blocked && !hit.damageApplied && !hit.destroyed && hit.shieldDamage==0 &&
            hit.remainingHitPoints==28 && hit.remainingShield==48,"eight real lock contacts are absorbed without hull damage or shield depletion");
        const auto& feedback=f.collision.WeaponFeedback().LastAcceptedEvent();
        runner.Expect(feedback.shieldHit && !feedback.lockOnContact && feedback.impactEffectName.empty() &&
            feedback.worldPosition.z<99 && feedback.hitStopSeconds==0,"absorbed lock reports the membrane contact without an ice/hull explosion");
        for(const auto& event:f.runtime.EnemyCombat().ConsumeEvents())reward.ObserveDefeat(event);
    }
    runner.Expect(reward.TotalKills()==0,"shield hits cannot grant defeat reward or restart the enemy lifecycle");
    for(uint64_t id=9;id<=12;++id) {
        const auto hit=f.collision.ApplyWeaponHit(f.runtime,&f.course,f.Request(id));
        runner.Expect(hit.shieldHit && hit.shieldDamage==12 && !hit.damageApplied && hit.remainingHitPoints==28 &&
            hit.shieldBroken==(id==12),"four ordinary pulses break only the shield, with no overflow into the hull");
        const float hp=f.runtime.Enemies().front().energyShield.hitPoints;
        const auto replay=f.collision.ApplyWeaponHit(f.runtime,&f.course,f.Request(id));
        runner.Expect(replay.duplicate && f.runtime.Enemies().front().energyShield.hitPoints==hp,"replayed shot cannot drain or break the shield twice");
    }
    const auto body=f.collision.ApplyWeaponHit(f.runtime,&f.course,f.Request(13,true));
    runner.Expect(body.damageApplied && body.destroyed && !body.shieldHit,"an arriving lock reaches the newly exposed hull");
    unsigned deaths=0;for(const auto& event:f.runtime.EnemyCombat().ConsumeEvents()) {deaths+=event.kind==EnemyCombatEventKind::Defeated;reward.ObserveDefeat(event);}
    runner.Expect(deaths==1 && reward.TotalKills()==1,"only the actual hull defeat produces one death and one boost credit");
    EnergyShieldTestFixture rear(runner);const auto flank=rear.collision.ApplyWeaponHit(rear.runtime,&rear.course,rear.Request(1,false,true));
    runner.Expect(flank.damageApplied && !flank.shieldHit && rear.runtime.Enemies().front().energyShield.hitPoints==48,
        "a frontal shield does not falsely protect the exposed rear");
}

void TestEnergyShieldVolley(RegressionRunner& runner) {
    for(int fps:{30,60,120})for(bool moving:{false,true}) {
        EnergyShieldTestFixture f(runner);RailLockVolley volley;RailLockToken token;
        token.target.actorId=f.runtime.Enemies().front().actorId;
        WeaponShot shot;shot.shotId=100;shot.damageType=WeaponDamageType::Ice;shot.damage=34;shot.range=120;
        runner.Expect(volley.Enqueue(std::span(&token,1),std::span(&shot,1),{0,4,10},f.rail,f.runtime,{})==1,"shield remains a valid lock target and launches an actual travelling shot");
        bool absorbed=false;for(int i=0;i<fps;++i) {
            if(moving) {
                auto checkpoint=f.runtime.CaptureCheckpoint();const float age=float(i)/fps;
                checkpoint.enemies.front().weaponMount.dronePose.position={2*std::sin(age*8),4+2*std::sin(age*12),100+5*std::sin(age*7)};
                runner.Expect(f.runtime.RestoreCheckpoint(checkpoint,true),"moving shield keeps its real authoritative pose");
            }
            volley.Update(1.0f/fps,{0,4,10},f.rail,f.runtime,f.collision,&f.course);
            if(!volley.Completed().empty()) {
                const auto& result=volley.Completed().front();
                absorbed=result.hits==0 && result.defeats==0 && volley.Hits().empty();
                runner.Expect(!volley.Lasers().empty() && volley.Lasers().front().end.z<
                    f.runtime.Enemies().front().weaponMount.dronePose.position.z-1 && volley.Contacts().empty(),
                    "the real lock tail terminates at the shield without creating a hull-impact contact");break;
            }
        }
        runner.Expect(absorbed && f.runtime.Enemies().front().desc.hitPoints==28,"absorbed travel has no hidden instant damage at any tested frame rate");
        if(moving) {
            auto checkpoint=f.runtime.CaptureCheckpoint();checkpoint.enemies.front().weaponMount.dronePose.position={0,4,100};
            runner.Expect(f.runtime.RestoreCheckpoint(checkpoint,true),"moving target settles before the hull-break followup");
        }
        for(uint64_t id=1;id<=4;++id)f.collision.ApplyWeaponHit(f.runtime,&f.course,f.Request(id));
        shot.shotId=101;volley.Enqueue(std::span(&token,1),std::span(&shot,1),{0,4,10},f.rail,f.runtime,{});
        bool killed=false;for(int i=0;i<fps;++i) {
            volley.Update(1.0f/fps,{0,4,10},f.rail,f.runtime,f.collision,&f.course);
            for(const auto& hit:volley.Hits())killed|=hit.destroyed;
        }
        runner.Expect(killed,"the same real lock path kills the exposed hull after the generator breaks");
    }
}

void TestEnergyShieldPoseAndRetry(RegressionRunner& runner) {
    {
        EnergyShieldTestFixture f(runner);RailAimState aim;aim.valid=true;aim.worldRayOrigin={1.25f,4,10};
        aim.worldRayDirection={0,0,1};aim.maxDistance=100;
        RailWorldRaycastInput query;query.aim=&aim;query.railPath=&f.rail;query.spawnRuntime=&f.runtime;
        query.includeProceduralTerrain=false;
        const auto hit=RailWorldRaycast::Query(query);
        runner.Expect(hit.hit && hit.kind==RailAimHitKind::Enemy && hit.actorId==f.runtime.Enemies().front().actorId && hit.position.z<99,
            "primary aim hits the visible membrane outside the smaller hull sphere");
        CourseObstacleActorDesc obstacle;obstacle.spawnDistance=50;obstacle.verticalOffset=4;obstacle.halfExtents={3,3,3};
        f.runtime.SpawnObstacle(obstacle);const auto blocked=RailWorldRaycast::Query(query);
        runner.Expect(blocked.hit && blocked.kind==RailAimHitKind::Obstacle,"the shield cannot steal a primary ray through foreground cover");
    }
    for(float bank:{-.7f,0.0f,.7f})for(float scale:{.65f,1.0f,2.2f}) {
        TwinShieldDronePose pose;pose.visible=true;pose.position={17,9,120};pose.scale={scale,scale,scale};pose.rotation={.3f,1.4f,0};pose.flightBank=bank;
        const auto m=BuildTwinShieldDroneWorldMatrix(pose);const auto center=EnemyEnergyShield::Point(m,EnemyEnergyShield::Local({},true));
        const auto front=Normalize(Vector3{-m.m[2][0],-m.m[2][1],-m.m[2][2]});
        const auto hit=EnemyEnergyShield::Intersect(pose,EnemyEnergyShield::Add(center,EnemyEnergyShield::Scale(front,10)),EnemyEnergyShield::Scale(front,-1),12);
        runner.Expect(hit.hit && std::abs(hit.distance-10)<.001f && std::abs(hit.uv.x)+std::abs(hit.uv.y)<.001f,
            "collision uses the same banked/scaled six facets as the displayed barrier");
        const auto miss=EnemyEnergyShield::Intersect(pose,EnemyEnergyShield::Add(center,EnemyEnergyShield::Scale(front,-10)),front,12);
        runner.Expect(!miss.hit,"rear-facing travel never hits the front-only barrier");
        EnemyEnergyShieldDefinition def;def.enabled=true;EnemyEnergyShieldState state;state.Initialize(def);state.Contact(7,{.1f,.2f},true,0);
        size_t vertices=0;EnemyEnergyShield::Draw(pose,state,def,1,1,[&](Vector3 p,Vector2 uv,Vector4 color) {
            ++vertices;runner.Expect(std::isfinite(p.x+p.y+p.z+uv.x+uv.y+color.w) && color.w>=0,"shield geometry remains finite through roll/scale");
        });runner.Expect(vertices<=108 && vertices>=36,"world shield and its local contacts have a fixed bounded vertex budget");
    }
    EnergyShieldTestFixture f(runner);f.collision.ApplyWeaponHit(f.runtime,&f.course,f.Request(1));
    const auto saved=f.runtime.CaptureCheckpoint();CourseSpawnRuntime retry;std::string error;
    runner.Expect(retry.RestoreCheckpoint(saved,true,&error) && retry.Enemies().front().energyShield.hitPoints==36,"checkpoint retains shield durability and local impact clocks");
    auto state=retry.Enemies().front().energyShield;state.Update(0);
    runner.Expect(state.impacts[0].age==0,"pause does not advance the shield contact or fracture clocks");
    CourseCollisionSystem retryDamage;const auto duplicate=retryDamage.ApplyWeaponHit(retry,&f.course,f.Request(1));
    runner.Expect(duplicate.duplicate && retry.Enemies().front().energyShield.hitPoints==36,"checkpoint's last shield shot survives receiver reset");
    auto bad=saved;bad.enemies.front().energyShield.hitPoints=49;
    runner.Expect(!retry.RestoreCheckpoint(bad,true,&error) && retry.Enemies().front().energyShield.hitPoints==36,"invalid shield checkpoint is rejected transactionally");
    for(uint64_t id=2;id<=4;++id)f.collision.ApplyWeaponHit(f.runtime,&f.course,f.Request(id));
    auto broken=f.runtime.CaptureCheckpoint();runner.Expect(retry.RestoreCheckpoint(broken,true,&error),"broken shield checkpoint restores");
    state=retry.Enemies().front().energyShield;state.Update(60);
    runner.Expect(!state.Active(retry.Enemies().front().desc.energyShield),"waiting or retry cannot regenerate a broken generator");
}

void TestEnergyShieldAuthoring(RegressionRunner& runner) {
    CourseAsset course;std::string error;runner.Expect(course.LoadFromFile(RailShortCoursePath,&error),"shield loads the complete course");
    const auto cooked=CourseWaveRuntimeCompiler{}.Compile(course,{});runner.Expect(cooked.succeeded,"shield assets compile with the existing wave compiler");
    size_t shieldCount=0;for(const auto& actor:cooked.program.actors)if(actor.actor.energyShield.enabled) {
        ++shieldCount;runner.Expect(actor.actor.meshId=="twin_shield_hull" && actor.actor.energyShield.maximumHitPoints==48,
            "the two shield variants retain the original chassis and explicit authored durability");
    }
    runner.Expect(shieldCount==2,"main course introduces exactly two shield enemies without increasing population");
    runner.Expect(cooked.program.SaveToFile("logs/energy_shield_roundtrip.coursebin",&error),"shield program writes current binary version");
    CourseRuntimeProgramAsset loaded;runner.Expect(loaded.LoadFromFile("logs/energy_shield_roundtrip.coursebin",&error),"shield program reloads its authored damage contract");
    size_t restored=0;for(const auto& actor:loaded.actors)restored+=actor.actor.energyShield.enabled;
    runner.Expect(restored==shieldCount,"cooked runtime preserves shield enablement rather than relying on actor IDs");
    ge3::core::ShaderCompiler compiler;runner.Expect(compiler.Initialize(),"shield initializes native shader compiler");
    const auto shader=compiler.CompileFromFile(L"Resources/TitleTracer.PS.hlsl",L"main",L"ps_6_0",{L"-O3",L"-WX"});
    runner.Expect(shader && shader->GetBufferSize()>0,"membrane, impacts and fracture compile in the actual depth-tested world shader");
    const auto backing=compiler.CompileFromFile(L"Resources/TitleDust.PS.hlsl",L"main",L"ps_6_0",{L"-O3",L"-WX"});
    runner.Expect(backing && backing->GetBufferSize()>0,"the shield contrast backing compiles in the actual alpha-blended depth-tested pass");
}

void TestEnergyShieldDefenseReadability(RegressionRunner& runner) {
    for(int fps:{30,60,120}) {
        EnergyShieldTestFixture f(runner);const auto def=f.runtime.Enemies().front().desc.energyShield;
        const auto pose=f.runtime.Enemies().front().weaponMount.dronePose;
        auto count=[&](const EnemyEnergyShieldState& state) {
            std::array<size_t,3> result{};
            EnemyEnergyShield::Draw(pose,state,def,1,1,[&](Vector3,Vector2 uv,Vector4 color) {
                ++result[0];result[1]+=uv.x>=72 && uv.x<=73;result[2]+=uv.x>=76 && uv.x<=77;
                runner.Expect(std::isfinite(color.w) && color.w>=0,"defense cue alpha stays finite and nonnegative");
            });return result;
        };
        runner.Expect(count(f.runtime.Enemies().front().energyShield)[0]==30,"an idle shield has no phantom guard flash or dissipating sparks");
        for(uint64_t id=1;id<=8;++id)f.collision.ApplyWeaponHit(f.runtime,&f.course,f.Request(id,true));
        const auto& feedback=f.collision.WeaponFeedback().LastAcceptedEvent();
        runner.Expect(feedback.audioCueId=="energy_shield_lock_block" && feedback.hudDuration>=.20f &&
            feedback.shieldHit && !feedback.destroyed && !feedback.lockOnContact && feedback.feedbackKind==HitFeedbackKind::Blocked,
            "absorbed lock selects its distinct guard cue and readable HUD duration");
        auto state=f.runtime.Enemies().front().energyShield;
        size_t backing=0;EnemyEnergyShield::DrawBacking(pose,state,def,1,[&](Vector3,Vector2 uv,Vector4 color) {
            ++backing;runner.Expect(uv.x>=3000 && uv.x<=3001 && color.z<.1f,"the contrast rim uses its dark alpha-pass material");
        });runner.Expect(backing==18,"six world facets carry the contrast backing");
        for(int i=0;i<=fps;++i) {
            const float time=float(i)/fps;const auto vertices=count(state);
            runner.Expect(vertices[0]<=108,"eight simultaneous lock blocks collapse into one bounded visible guard response");
            if(time<.44f)runner.Expect(vertices[1]==18,"the accepted lock contact sends one wave across the actual shield facets");
            if(time>.48f)runner.Expect(vertices[1]==0 && vertices[2]==0,"the defense cue ends promptly instead of turning into a permanent aura");
            runner.Expect(state.hitPoints==48 && f.runtime.Enemies().front().desc.hitPoints==28,"guard presentation never changes shield or hull damage");
            state.Update(1.0f/fps);
        }
        for(uint64_t id=9;id<=12;++id)f.collision.ApplyWeaponHit(f.runtime,&f.course,f.Request(id));
        backing=0;EnemyEnergyShield::DrawBacking(pose,f.runtime.Enemies().front().energyShield,def,1,[&](Vector3,Vector2,Vector4){++backing;});
        runner.Expect(backing==0 && count(f.runtime.Enemies().front().energyShield)[1]==0,"a broken shield loses both the protective outline and guard-wave state");
    }
}
