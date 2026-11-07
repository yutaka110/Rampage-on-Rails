void TestEliteFirePresentation(RegressionRunner& runner) {
    for(int fps:{30,60,120}) {
        RailEliteFirePresentation fx;const float dt=1.0f/fps;
        fx.Begin(dt,true);
        runner.Expect(!fx.Visible() && fx.Shake()==0,"no elite emission means no extra effect or camera kick");
        runner.Expect(fx.FangLaunch(7,10,{1,2,3},{0,0,1}) && !fx.FangLaunch(7,10,{},{0,0,1}),"one actual Fang round emits exactly once");
        runner.Expect(fx.FangFired() && fx.FangRecoil(10)>=.10f && fx.Shake()<=.018f,"Fang recoils immediately while source shake stays small");
        const float paused=fx.FangRecoil(10);fx.Begin(0,true);
        runner.Expect(fx.FangRecoil(10)==paused && fx.Shake()==0 && !fx.FangLaunch(8,10,{},{}),"pause freezes the real pulse and rejects input/emission replay");
        fx.Begin(dt,true);runner.Expect(fx.FangLaunch(8,10,{},{}),"a new round can start a new bounded recoil");
        runner.Expect(fx.Shake()==0,"consecutive Fang shots never stack source camera shake");
        fx.FangContact(99,1,true);runner.Expect(fx.Shake()==0,"another enemy cannot create Fang contact feedback");
        fx.FangContact(8,1,false);runner.Expect(fx.Shake()>.018f,"a close pass is stronger than a distant launch");
        fx.Begin(dt,true);fx.FangContact(8,1,true);runner.Expect(fx.Shake()==0,"one projectile cannot replay camera feedback on both near pass and contact");
        for(int i=0;i<fps;++i)fx.Begin(dt,true);
        runner.Expect(!fx.Visible() && fx.FangRecoil(10)==0,"burst flashes and recoil settle without persistent animation");
        runner.Expect(fx.VeilLaunch(1,20,{3,2,1},{0,0,1},{0,0,10},1) && !fx.VeilLaunch(1,20,{},{},{},1),"one beam emission produces one heavy launch");
        runner.Expect(fx.LensPulse() && fx.Shake()==.035f,"Veil receives only a small source kick and a local lens pulse");
        bool shock=false;
        for(int i=0;i<int(std::ceil(.10f/dt));++i) {fx.Begin(dt,true);if(fx.ShockSound()){shock=true;runner.Expect(fx.Shake()>.10f && fx.Shake()<=.16f,"close beam shock is delayed, stronger and capped");}}
        runner.Expect(shock,"beam pressure reaches the player after the source flash");
        for(int i=0;i<fps;++i)fx.Begin(dt,true);
        runner.Expect(!fx.Visible() && !fx.LensPulse() && fx.Shake()==0,"the lens and all pressure rings end promptly");
        fx.Reset();fx.Begin(dt,true);fx.VeilLaunch(1,20,{},{},{},0);
        for(int i=0;i<fps/3;++i){fx.Begin(dt,true);runner.Expect(!fx.ShockSound() && fx.Shake()==0,"a distant beam does not shake the player with an invented nearby impact");}
        fx.Begin(dt,false);runner.Expect(!fx.Visible(),"scene change cancels pending effects");
        fx.Begin(dt,true);runner.Expect(fx.Shake()==0 && !fx.ShockSound(),"reactivation cannot replay an old beam shock");
    }
}

void TestFangRigidParts(RegressionRunner& runner) {
    using Key=std::array<long long,9>;
    const auto triangles=[](const ModelData& mesh,const Matrix4x4& matrix) {
        std::map<Key,unsigned> result;
        for(size_t i=0;i<mesh.indices.size();i+=3) {
            std::array<std::array<long long,3>,3> points{};
            for(size_t j=0;j<3;++j) {
                const auto p=mesh.vertices[mesh.indices[i+j]].position;
                for(size_t k=0;k<3;++k)points[j][k]=std::llround((p.x*matrix.m[0][k]+p.y*matrix.m[1][k]+p.z*matrix.m[2][k]+matrix.m[3][k])*100000);
            }
            std::sort(points.begin(),points.end());Key key{};
            for(size_t j=0;j<3;++j)for(size_t k=0;k<3;++k)key[j*3+k]=points[j][k];
            ++result[key];
        }
        return result;
    };
    for(int state=0;state<3;++state) {
        const bool exposed=state==1,damaged=state==2;
        const std::string suffix=exposed?"Exposed":damaged?"Damaged":"";
        const auto whole=LoadObjFile_Assimp(RailFangModel::Directory,"Fang"+suffix+".obj");
        std::map<Key,unsigned> assembled;
        for(size_t i=0;i<RailFangModel::Parts.size();++i) {
            const auto pose=RailFangModel::EvaluatePart(i,{},damaged);if(!pose.visible)continue;
            const std::string file=i==0?"FangBody"+suffix+".obj":RailFangModel::Parts[i].file;
            const auto part=LoadObjFile_Assimp(RailFangModel::Directory,file);
            runner.Expect(!part.indices.empty() && ValidateModelGeometryOrientation(part) && AuditModelClosedSurface(part).IsValid(),"Fang rigid part is closed and correctly wound: "+file);
            for(const auto& [key,count]:triangles(part,Multiply(part.rootNode.localMatrix,pose.local)))assembled[key]+=count;
        }
        runner.Expect(assembled==triangles(whole,whole.rootNode.localMatrix),"eight pivot-local Fang parts exactly reconstruct every original triangle in fitted, exposed and damaged states");
    }
    RailFangModel::Pose pose;pose.nodes[size_t(RailFangModel::Node::LeftBarrel)].translation.z=.16f;
    const auto left=RailFangModel::EvaluatePart(3,pose),right=RailFangModel::EvaluatePart(4,pose);
    runner.Expect(std::abs(left.local.m[3][2]-RailFangModel::Parts[3].pivot.z-.16f)<.0001f &&
        right.local.m[3][2]==RailFangModel::Parts[4].pivot.z,"each cannon can recoil independently around its own base");
    for(size_t i:{size_t{1},size_t{2},size_t{5},size_t{6},size_t{7}}) {
        pose={};pose.nodes[i].rotation={.2f,.3f,-.1f};const auto part=RailFangModel::EvaluatePart(i,pose);
        const auto pivot=RailFangModel::Parts[i].pivot;
        runner.Expect(part.local.m[3][0]==pivot.x && part.local.m[3][1]==pivot.y && part.local.m[3][2]==pivot.z,
            "wing, engine and sensor rotations keep their authored mounting pivot fixed");
    }
    runner.Expect(RailFangModel::EvaluatePart(3,{},true).visible && !RailFangModel::EvaluatePart(4,{},true).visible,
        "right-cannon destruction hides only the broken barrel and preserves the surviving left cannon");
}

void TestEliteFireAssets(RegressionRunner& runner) {
    for(const auto* suffix:{"","Exposed","Damaged"}) {
        const std::string tail=suffix,dir="Resources/enemies/ElitePursuers";
        const auto whole=LoadObjFile_Assimp(dir,"Fang"+tail+".obj");
        const auto hull=LoadObjFile_Assimp(dir,"FangFireHull"+tail+".obj");
        const auto barrels=LoadObjFile_Assimp(dir,"FangFireBarrels"+tail+".obj");
        runner.Expect(!hull.indices.empty() && !barrels.indices.empty() && hull.indices.size()+barrels.indices.size()==whole.indices.size(),"modular Fang preserves all original geometry across exposure and damage");
        runner.Expect(ValidateModelGeometryOrientation(hull) && ValidateModelGeometryOrientation(barrels) &&
            AuditModelClosedSurface(hull).IsValid() && AuditModelClosedSurface(barrels).IsValid(),"separated Fang hull and barrels stay closed with correct normals");
        if(tail=="Damaged")for(const auto& v:barrels.vertices)runner.Expect(v.position.x<0,"destroyed right cannon is absent from the recoiling barrel mesh");
    }
    RailVeilModelPresentation model;model.Update(.016f,true,true,0);
    runner.Expect(model.Frame().recoil>=.10f && model.Frame().flash>0,"beam firing edge has an immediate heavy recoil and flash");
    model.Update(.04f,true,true,0);runner.Expect(std::abs(model.Frame().wingKick)>0,"wings react after the cannon rather than at the same instant");
    for(int i=0;i<60;++i)model.Update(1.0f/60,false,false,0);
    runner.Expect(model.Frame().recoil==0 && model.Frame().wingKick==0 && model.Frame().flash==0,"heavy firing pose settles after recovery");
    for(const auto* asset:{"rear_fang","rear_veil","drone_scout"}) {
        CourseSpawnRuntime runtime;CourseEnemyActorDesc actor;actor.actorAssetId=asset;actor.hitPoints=100;
        actor.spawnDistance=20;runtime.SpawnEnemyActor(actor);
        RailPath rail;rail.SetControlPoints({{{0,0,0}},{{0,0,100}}});
        EnemyCombatEvent event;event.kind=EnemyCombatEventKind::AttackCommitted;event.actorId=runtime.Enemies().front().actorId;
        std::array<EnemyCombatEvent,1> events{event};
        EnemyCombatPresentationInput input;input.runtime=&runtime;input.railPath=&rail;input.events=events;input.deltaTime=.016f;
        EnemyCombatPresentationBridge bridge;bridge.Update(input);
        const auto& frame=bridge.Frame();
        runner.Expect(IsRailElitePursuer(asset)?frame.cameraShakeImpulse==0 && frame.cameraFovImpulseRadians==0:frame.cameraShakeImpulse>.2f,
            "elite firing uses the bounded emission observer without a second generic shake/FOV kick; ordinary enemies retain their presentation");
    }
}

void TestEliteFirePostGraph(RegressionRunner& runner) {
    using namespace ge3::graphics;
    auto gui=std::make_unique<AppImGuiLayer>();
    for(bool active:{false,true})for(bool editor:{false,true})for(bool boost:{false,true}) {
        PostProcessStack stack;stack.ResetToVfxDefaults();
        const auto output=stack.BuildExecutionPlan().finalOutputResource;
        RenderGraph graph;
        graph.DeclareTransientRenderTarget("SceneColor",1,DXGI_FORMAT_R8G8B8A8_UNORM);
        graph.DeclareTransientRenderTarget("VfxAccumulation",1,DXGI_FORMAT_R8G8B8A8_UNORM);
        graph.DeclarePersistentDepthTarget("SceneDepth",DXGI_FORMAT_D24_UNORM_S8_UINT);
        graph.AddPass({"World",RenderPassLayer::Geometry,
            {{"SceneColor",RenderResourceAccessType::WriteRtv},{"VfxAccumulation",RenderResourceAccessType::WriteRtv},
             {"SceneDepth",RenderResourceAccessType::WriteDepth}},"SceneDepth",{}});
        graph.AddPass({"HUD",RenderPassLayer::Ui,{{editor?output:"BackBuffer",RenderResourceAccessType::WriteRtv}},"",{}});
        AppFrameGraphBuildContext ctx;ctx.renderGraph=&graph;ctx.postProcessStack=&stack;ctx.imguiLayer=gui.get();
        ctx.railFilmBurnInEditorViewport=editor;ctx.railBoostBlurStrength=boost?.5f:0;
        ctx.railEliteLens={.55f,.4f,.07f,active?.8f:0};ctx.railEliteLensAge=.05f;
        AppSceneRenderPipeline{}.RegisterRailBoostProtectionPass(ctx);
        AppPostProcessPipeline{}.RegisterPasses(ctx);
        if(editor)graph.AddPass({"EditorUI",RenderPassLayer::Ui,{{output,RenderResourceAccessType::ReadSrv},{"BackBuffer",RenderResourceAccessType::WriteRtv}},"",{}});
        std::string error;runner.Expect(graph.Validate(&error),"elite lens graph validates: "+error);
        const auto plan=graph.BuildPassDebugInfo();
        const auto find=[&](const char* name){return std::find_if(plan.begin(),plan.end(),[&](const auto& p){return p.name==name;});};
        const auto refract=find("PostProcess.EliteMuzzleRefraction"),resolve=find("PostProcess.EliteLensResolve"),hud=find("HUD");
        if(active)runner.Expect(refract!=plan.end() && resolve!=plan.end() && hud!=plan.end() && refract->executed && resolve->executed &&
            refract->executionIndex<resolve->executionIndex && resolve->executionIndex<hud->executionIndex,
            "local colour refraction and its resolve run before the sharp HUD in editor and release, including acceleration");
        else {
            const auto targets=graph.BuildTransientRenderTargetPlan();
            runner.Expect(refract==plan.end() && resolve==plan.end() && std::none_of(targets.begin(),targets.end(),[](const auto& t){return t.name=="RailEliteLens";}),
                "no firing pulse allocates no refraction pass or target");
        }
    }
}
