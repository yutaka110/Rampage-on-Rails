#include "RailTitleScene.h"
#include "../ModelLoaderAssimp.h"
#include "CourseSpawnRuntime.h"
#include "RailWorldRaycast.h"
#include "../terrain/TerrainVolumeField.h"
#include "EnemyEncounterReadabilityDirector.h"
#include <algorithm>
#include <cmath>

namespace {
constexpr float Pi = 3.14159265358979323846f;
Vector3 Add(Vector3 a, Vector3 b) { return {a.x+b.x,a.y+b.y,a.z+b.z}; }
Vector3 Scale(Vector3 a, float s) { return {a.x*s,a.y*s,a.z*s}; }
float Dot(Vector3 a, Vector3 b) { return a.x*b.x+a.y*b.y+a.z*b.z; }
Vector3 Cross(Vector3 a, Vector3 b) {
    return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};
}
// The title has one static cave and one cinematic wall shot. Cache the imported
// geometry, and intersect it only when Start is confirmed, never every frame.
bool TraceTitleTunnel(Vector3 origin, Vector3 end, Vector3& contact, Vector3& normal) {
    static const auto mesh=LoadObjFile_Assimp("Resources/course_meshes/TitleLandscape","TitleTunnel.obj");
    const auto direction=Add(end,Scale(origin,-1));
    float nearest=1.0f;
    bool found=false;
    for(size_t i=0;i+2<mesh.indices.size();i+=3) {
        const auto point=[&](uint32_t index) {
            const auto& p=mesh.vertices[index].position;
            return Vector3{p.x,p.y,p.z};
        };
        const auto a=point(mesh.indices[i]), b=point(mesh.indices[i+1]), c=point(mesh.indices[i+2]);
        const auto e1=Add(b,Scale(a,-1)), e2=Add(c,Scale(a,-1));
        const auto p=Cross(direction,e2);
        const float determinant=Dot(e1,p);
        if(std::abs(determinant)<0.000001f) continue;
        const auto offset=Add(origin,Scale(a,-1));
        const float u=Dot(offset,p)/determinant;
        if(u<0 || u>1) continue;
        const auto q=Cross(offset,e1);
        const float v=Dot(direction,q)/determinant;
        if(v<0 || u+v>1) continue;
        const float t=Dot(e2,q)/determinant;
        if(t<0 || t>nearest) continue;
        nearest=t; found=true;
        normal=Normalize(Cross(e1,e2));
        if(Dot(normal,direction)>0) normal=Scale(normal,-1);
    }
    if(found) contact=Add(origin,Scale(direction,nearest));
    return found;
}
float CollapseRandom(uint32_t seed, uint32_t index, uint32_t channel) {
    uint32_t bits=seed*747796405u+index*2891336453u+channel*277803737u;
    bits=((bits>>((bits>>28u)+4u))^bits)*277803737u;
    return float((bits>>22u)^bits)/float(UINT32_MAX);
}

}

bool RailTitleScene::Initialize() {
    travel_ = 0.0;
    startTravel_ = 0.0; startDistance_ = 0.0f;
    starting_ = false; startTime_ = age_ = departureAge_ = 0.0f;
    shots_.clear(); attackCues_.clear();
    dustClouds_.clear();
    railImpact_ = {}; departureShotFired_ = false;
    departureWallHit_=false;
    sandCollapse_={}; renderScenery_={};
    attackTime_ = 0.0; nextShotTime_ = 1.2;
    shotSequence_ = 0; burstShot_ = 0; muzzleFlashTime_ = 0.0f;
    constexpr int segments = 24;
    constexpr float radius = 70.0f;
    const float handle = radius * (4.0f/3.0f) * std::tan(Pi/(2.0f*segments));
    std::vector<RailPathControlPoint> points;
    // Repeat the closed path so the contact solver can sample both sides of
    // the wrap. Only one local rail window is rendered, avoiding overlap.
    for (int i=0;i<=segments*3;++i) {
        const float angle = (i%segments) * (2.0f*Pi/segments);
        RailPathControlPoint p;
        p.position = {radius*std::cos(angle),0.0f,radius*std::sin(angle)};
        p.speed = Speed;
        p.tangentMode = RailPathTangentMode::Mirrored;
        p.outgoingTangent = {-handle*std::sin(angle),0.0f,handle*std::cos(angle)};
        p.incomingTangent = Scale(p.outgoingTangent,-1.0f);
        points.push_back(p);
    }
    path_.SetControlPoints(std::move(points));
    lapStart_ = path_.SegmentStartDistance(segments);
    lapLength_ = path_.SegmentStartDistance(segments*2)-lapStart_;
    auto trackDefinition = CourseRailTrackDefinitionAsset::MineCartDefaults();
    trackDefinition.assetId = "title_loop";
    trackDefinition.renderAheadDistance = 220.0f;
    trackDefinition.renderBehindDistance = 150.0f;
    trackDefinition.nearDetailDistance = 120.0f;
    // Align all authored detail to a complete lap as well as the path itself.
    trackDefinition.bakeSegmentLength = lapLength_ / 120.0f;
    trackDefinition.sleeperSpacing = lapLength_ / 192.0f;
    trackDefinition.supportSpacing = lapLength_ / 48.0f;
    trackDefinition.maximumVisibleInstances = 768;
    if (!track_.Bake(trackDefinition,path_)) return false;
    scenery_ = {};
    const auto place = [this](const char* id, const char* mesh, float fraction,
        float lateral, float vertical, Vector3 scale, float yaw = 0.0f) {
        CourseTerrainPlacement p;
        p.id = id;
        p.meshId = mesh;
        p.distance = lapStart_ + lapLength_*fraction;
        p.lateralOffset = lateral;
        p.verticalOffset = vertical;
        p.scale = scale;
        p.rotation.y = yaw;
        // Static world scenery must not pop when the title distance wraps.
        p.cullAheadDistance = p.cullBehindDistance = 5000.0f;
        scenery_.terrainPlacements.push_back(p);
    };
    // The mesh origin is the centre of the circular course. One connected
    // ground surface replaces the overlapping wall tiles beneath the rails.
    place("title_ground", "title_ground", 0.0f, -70.0f, 0.0f, {1,1,1});
    place("title_central_cliff", "title_cliff", 0.0f, -70.0f, -1.5f, {33,25,30}, 0.35f);
    place("title_distant_cliff_a", "title_cliff", 0.24f, 120.0f, -2.0f, {38,32,29}, 0.8f);
    // Keep this vista behind the longer cave approach at every departure angle.
    place("title_distant_cliff_b", "title_cliff", 0.68f, 220.0f, -8.0f, {48,26,35}, -0.6f);
    // Broad gaps and modest scale differences; never a ring of repeated rocks.
    place("title_rock_a", "title_boulder", 0.07f, -26.0f, -0.8f, {3.4f,3.0f,2.7f}, 0.3f);
    place("title_rock_b", "title_boulder", 0.32f, -22.0f, -0.8f, {2.4f,2.0f,3.6f}, 1.0f);
    place("title_rock_c", "title_boulder", 0.57f, -24.0f, -0.8f, {4.0f,3.6f,3.0f}, -0.5f);
    place("title_rock_d", "title_boulder", 0.78f, 42.0f, -1.4f, {5.0f,3.2f,4.2f}, 0.7f);
    place("title_rock_e", "title_boulder", 0.92f, 55.0f, -1.4f, {3.8f,2.4f,5.0f}, -0.8f);
    // Camera-side foreground markers provide parallax without changing travel
    // speed. A complete, fixed ring avoids spawning/popping at the lap seam.
    for(int i=0;i<24;++i) {
        const std::string id="title_roadside_stake_"+std::to_string(i);
        const float variation=float((i*7)%11)/10.0f;
        place(id.c_str(),"title_stake",(i+0.35f)/24.0f,9.6f+variation*1.0f,-0.35f,
            {0.85f+variation*0.18f,0.85f+variation*0.20f,0.90f},0.12f*std::sin(float(i)*1.7f));
        scenery_.terrainPlacements.back().rotation.z=0.035f*std::sin(float(i)*2.3f);
    }
    for(int i=0;i<36;++i) {
        const std::string id="title_roadside_rock_"+std::to_string(i);
        const float variation=float((i*13)%17)/16.0f;
        const float fraction=(i+0.55f+0.20f*std::sin(float(i)*2.1f))/36.0f;
        place(id.c_str(),"title_boulder",fraction,12.0f+5.0f*variation,-0.40f,
            {0.35f+variation*0.40f,0.28f+variation*0.30f,0.30f+(1-variation)*0.50f},float(i)*2.399963f);
    }
    state_ = {};
    state_.initialized = true;
    state_.vehicleId = definition_.vehicleId;
    state_.hitPoints = definition_.maximumHitPoints;
    state_.speed = Speed;
    Update(0.0f);
    return true;
}

void RailTitleScene::Update(float deltaTime) {
    if (lapLength_ <= 0.0f) return;
    const float dt = std::isfinite(deltaTime) ? (std::clamp)(deltaTime,0.0f,0.05f) : 0.0f;
    if(starting_) {
        startTime_ = (std::min)(StartDuration,startTime_+dt);
        // Keep the idle rolling speed throughout the approach.
        travel_=startTravel_+Speed*double(startTime_);
    } else travel_ += dt*Speed;
    age_ += dt;
    state_.speed=CurrentSpeed();
    state_.previousDistance = state_.distance;
    state_.distance = lapStart_ + static_cast<float>(std::fmod(travel_,double(lapLength_)));
    // Continue from the retained curve onto its straight tangent extension.
    if(starting_) state_.distance=startDistance_+static_cast<float>(travel_-startTravel_);
    const auto sample = path_.Evaluate(state_.distance);
    state_.position = sample.position;
    state_.forward = sample.tangent;
    state_.right = sample.right;
    state_.up = sample.up;
    ++state_.revision;
    RailVehicleTrackContactPoseInput solve;
    solve.definition = &definition_; solve.state = &state_; solve.railPath = &path_;
    const auto& contact = contacts_.Solve(solve);
    RailVehiclePresentationFrame presentation;
    presentation.visible = true;
    presentation.visualPosition = contact.visualPosition;
    presentation.forward = contact.forward;
    presentation.right = contact.right;
    presentation.up = contact.up;
    // Unwrapped travel prevents wheel phase snapping at the loop boundary.
    presentation.wheelRotationRadians = static_cast<float>(std::fmod(travel_/0.62,2.0*Pi));
    presentation.speedNormalized = (std::min)(1.0f,state_.speed/definition_.maximumSpeed);
    presentation.revision = state_.revision;
    presentation.sourceVehicleRevision = state_.revision;
    actor_.Update({&definition_,&state_,&presentation});
    cameraPosition_ = Add(sample.position,Add(Scale(sample.tangent,13.0f),Scale(sample.right,15.0f)));
    cameraPosition_.y += 8.0f;
    // Shift the focal point left in camera space, leaving the cart on the right.
    const Vector3 screenRight = { -sample.tangent.z*13.0f-sample.right.z*15.0f,0.0f,
                                  sample.tangent.x*13.0f+sample.right.x*15.0f };
    cameraTarget_ = Add(contact.visualPosition,Scale(screenRight,-5.5f/std::sqrt(394.0f)));
    cameraTarget_.y += 0.6f;
    if(starting_) {
        const float t = StartProgress();
        const float blend = t*t*t*(t*(t*6.0f-15.0f)+10.0f);
        // Orbit outside the cart rather than interpolating through its body.
        const float angle = 0.8567f + (Pi-0.8567f)*blend;
        // Settle into a close, fixed chase shot without a speed-dependent zoom.
        const auto departureRig=DepartureCameraRig();
        const float radius = 19.8494f + (departureRig.backDistance-19.8494f)*blend;
        cameraPosition_ = Add(sample.position,Add(Scale(sample.tangent,std::cos(angle)*radius),
            Scale(sample.right,std::sin(angle)*radius)));
        cameraPosition_.y += 8.0f+(departureRig.verticalOffset-8.0f)*blend;
        // Keep the distant cave centered along the straight departure rail.
        const float remaining=TunnelLead-static_cast<float>(travel_-startTravel_);
        const float lookAhead=(std::clamp)(remaining,departureRig.lookAheadDistance,38.0f);
        const Vector3 chaseTarget = Add(path_.Evaluate(state_.distance+lookAhead).position,
            Vector3{0,departureRig.lookUpOffset,0});
        cameraTarget_ = Add(Scale(cameraTarget_,1-blend),Scale(chaseTarget,blend));
    }
    renderer_.Update({&actor_.Frame(),cameraPosition_});
    vehicleFrame_=renderer_.Frame();
    // Local suspension motion affects the title body only. Keep the camera,
    // rail contacts and separately rendered wheels on their solved poses.
    // Distance is unwrapped and double precision, so phase never resets at a lap
    // boundary and does not depend on frame rate or accumulate while unfocused.
    const auto phase=[this](double wavelength) {
        return static_cast<float>(std::fmod(travel_/wavelength,1.0)*2.0*double(Pi));
    };
    const float jointPhase=phase(5.4),vibrationPhase=phase(3.2),swayPhase=phase(13.0);
    const float strength=1.0f-Blackout();
    const float bounce=(0.026f*std::sin(jointPhase)+0.012f*std::sin(vibrationPhase+0.6f))*strength;
    const float lateral=0.015f*std::sin(swayPhase)*strength;
    const float roll=(0.70f*std::sin(swayPhase)+0.12f*std::sin(vibrationPhase))*Pi/180.0f*strength;
    const float pitch=0.26f*std::sin(jointPhase+0.9f)*Pi/180.0f*strength;
    const Matrix4x4 bodySway=MakeAffineMatrix(Vector3{1,1,1},Vector3{pitch,0,roll},Vector3{lateral,bounce,0});
    vehicleFrame_.worldMatrix=Multiply(bodySway,vehicleFrame_.worldMatrix);
    wheels_.Update({&contact,&presentation});
    // Sample the motion rail, including its retained curve after BeginStart().
    // Never use the straight scenery coordinate frame for the pursuing actor.
    // Cut slightly inside the bend so the silhouette stays clear of the logo.
    const float gap = 32.0f + 3.0f*std::sin(age_*0.55f);
    const auto behind = path_.Evaluate(state_.distance-gap);
    pursuer_.position = Add(behind.position,Scale(behind.right,-10.0f+2.4f*std::sin(age_*0.8f)));
    pursuer_.position.y += 3.6f+0.65f*std::sin(age_*1.7f);
    if(starting_) pursuer_.position=DepartureDronePosition(startTime_);
    // Lead the moving cart, then deliberately miss onto the flat sand corridor.
    // Alternate sides of the rail, keeping every impact away from the sleepers.
    constexpr float lanes[]{-5.0f,5.5f,-4.5f};
    const auto aim = path_.Evaluate(state_.distance+Speed*0.78f-2.0f+burstShot_*1.2f);
    pursuerAimTarget_ = Add(aim.position,Scale(aim.right,lanes[burstShot_]));
    pursuerAimTarget_.y = -0.27f; // Ground top is y=-0.30 in this corridor.
    if(starting_) {
        pursuerAimTarget_=DepartureImpactTarget();
    }
    const Vector3 direction = Add(pursuerAimTarget_,Scale(pursuer_.position,-1.0f));
    const float length = std::sqrt(direction.x*direction.x+direction.y*direction.y+direction.z*direction.z);
    // Imported model points along -Z. Aim at the cart rather than the camera.
    const Vector3 desiredRotation{std::asin((std::clamp)(direction.y/(std::max)(length,0.001f),-1.0f,1.0f)),
        std::atan2(-direction.x,-direction.z),starting_ ? 0.05f*std::sin(startTime_*3.0f) : 0.16f*std::sin(age_*0.8f)};
    if(state_.revision<=1 || (starting_ && startTime_>=OrbitDuration)) pursuer_.rotation=desiredRotation;
    else {
        const float blend=1.0f-std::exp(-14.0f*dt);
        pursuer_.rotation.x+=(desiredRotation.x-pursuer_.rotation.x)*blend;
        pursuer_.rotation.y+=std::remainder(desiredRotation.y-pursuer_.rotation.y,2.0f*Pi)*blend;
        pursuer_.rotation.z+=(desiredRotation.z-pursuer_.rotation.z)*blend;
    }
    pursuer_.scale = {1.25f,1.25f,1.25f};
    pursuer_.alpha = 1.0f-Blackout();
    pursuer_.visible = pursuer_.alpha > 0.001f;
    UpdateAttack(dt);
    UpdateSandCollapse();
}

void RailTitleScene::UpdateSandCollapse() {
    if(!starting_) return;
    sandCollapse_.active=railImpact_.seed!=0 && startTime_>=CollapseBegin;
    sandCollapse_.seed=railImpact_.seed;
    sandCollapse_.age=(std::max)(0.0f,startTime_-CollapseBegin);
    // Reuse the fixed scenery coordinate frame. Dynamic clods never enter the
    // playable terrain/collision system or modify the authored placements.
    renderScenery_.terrainPlacements.resize(scenery_.terrainPlacements.size());
    if(!sandCollapse_.active) return;
    const auto basis=sceneryPath_.Evaluate(400.0f);
    for(uint32_t i=0;i<CollapseClodCount;++i) {
        const auto clod=EvaluateSandClod(sandCollapse_,i);
        if(!clod.visible) continue;
        const Vector3 delta=Add(clod.position,Scale(basis.position,-1));
        CourseTerrainPlacement placement;
        placement.id="title_collapse_clod_"+std::to_string(i);
        placement.meshId="title_sand_clod";
        placement.distance=400.0f;
        placement.forwardOffset=delta.x*basis.tangent.x+delta.z*basis.tangent.z;
        placement.lateralOffset=delta.x*basis.right.x+delta.z*basis.right.z;
        placement.verticalOffset=delta.y;
        placement.rotation=clod.rotation; placement.scale=clod.scale;
        placement.cullAheadDistance=placement.cullBehindDistance=5000.0f;
        renderScenery_.terrainPlacements.push_back(std::move(placement));
    }
}

RailTitleSandClod RailTitleScene::EvaluateSandClod(const RailTitleSandCollapse& collapse, uint32_t index) {
    RailTitleSandClod clod;
    if(!collapse.active || index>=CollapseClodCount) return clod;
    const auto rnd=[&](uint32_t channel){return CollapseRandom(collapse.seed,index,channel);};
    const float band=float(index/8);
    const float side=index%2==0 ? -1.0f : 1.0f;
    const float birth=0.105f*band+(side>0 ? 0.075f : 0.0f)+rnd(0)*0.13f;
    const float age=collapse.age-birth;
    clod.emissionAge=age;
    const float size=index%9==0 ? 0.50f+0.16f*rnd(1) : 0.20f+0.30f*rnd(1);
    const float height=9.0f+1.2f*rnd(2), speed=14.0f+size*4.5f;
    const float floorHeight=size*1.14f;
    const float landing=(std::sqrt(speed*speed+19.6f*(height-floorHeight))-speed)/9.8f;
    const float flight=(std::clamp)(age,0.0f,landing);
    const float across=side*(3.95f+2.2f*rnd(3));
    const float along=-4.5f+3.0f*band+(rnd(4)-0.5f)*2.2f;
    // Five staggered ceiling sections collapse along both shoulders. Heavy
    // pieces stay clear of the cart, while fine sand fills the central air.
    const Vector3 horizontal=Add(Scale(collapse.right,-side*(0.25f+0.40f*rnd(5))),
        Scale(collapse.forward,(rnd(6)-0.5f)*1.2f));
    const Vector3 source=Add(collapse.origin,Add(Scale(collapse.right,across),Scale(collapse.forward,along)));
    clod.impactPosition=Add(source,Scale(horizontal,landing));
    clod.impactAge=age-landing;
    clod.position=Add(source,Add(Scale(horizontal,flight),{0,height-speed*flight-4.9f*flight*flight,0}));
    clod.velocity=age<landing ? Add(horizontal,{0,-speed-9.8f*flight,0}) : Vector3{};
    clod.rotation={0.16f*std::sin(rnd(7)*6.28f+flight*3.0f),rnd(8)*6.28f+flight*(rnd(9)-0.5f)*4.0f,
        0.22f*std::sin(rnd(10)*6.28f+flight*4.0f)};
    // On contact the cohesive lump breaks down into the smaller grain burst.
    // Dissolve its volume over 160ms; never bounce or hang above the ground.
    const float crush=(std::clamp)(clod.impactAge/0.16f,0.0f,1.0f);
    const float remaining=1.0f-crush*crush*(3.0f-2.0f*crush);
    clod.scale={size*remaining,size*(0.80f+0.20f*rnd(11))*remaining,size*(0.85f+0.25f*rnd(12))*remaining};
    if(clod.impactAge>=0.0f) {
        clod.position.y=collapse.origin.y+floorHeight*remaining;
        clod.rotation.x*=remaining; clod.rotation.z*=remaining;
    }
    clod.visible=age>=0.0f && remaining>0.01f;
    return clod;
}

RailTitleSandGrain RailTitleScene::EvaluateCollapseGrain(const RailTitleSandCollapse& collapse, uint32_t index) {
    RailTitleSandGrain grain;
    if(!collapse.active || index>=CollapseGrainCount) return grain;
    const auto clod=EvaluateSandClod(collapse,index/CollapseGrainsPerClod);
    const auto rnd=[&](uint32_t channel){return CollapseRandom(collapse.seed,index+113,channel);};
    const bool falling=index%CollapseGrainsPerClod<6;
    const float age=falling ? clod.emissionAge-0.035f*rnd(0) : clod.impactAge-0.035f*rnd(0);
    if(age<0.0f) return grain;
    const float angle=rnd(1)*2*Pi;
    const float speed=falling ? 1.3f+1.0f*rnd(2) : 3.5f+4.5f*rnd(2);
    const float lift=falling ? -3.0f-3.5f*rnd(3) : 1.4f+2.2f*rnd(3);
    const float height=falling ? 8.0f+2.0f*rnd(4) : 0.04f;
    const float landing=(lift+std::sqrt(lift*lift+19.6f*height))/9.8f;
    const float flight=(std::min)(age,landing), drag=std::exp(-3.4f*flight);
    const float spread=speed*(1.0f-drag)/3.4f;
    const Vector3 scatter=Add(Scale(collapse.right,std::cos(angle)*spread+0.35f*flight),
        Scale(collapse.forward,std::sin(angle)*spread));
    Vector3 source=clod.impactPosition;
    if(falling) {
        const auto relative=Add(source,Scale(collapse.origin,-1));
        const float across=relative.x*collapse.right.x+relative.z*collapse.right.z;
        source=Add(source,Add(Scale(collapse.right,-across*rnd(6)*0.95f),
            Scale(collapse.forward,(rnd(7)-0.5f)*3.0f)));
    }
    grain.position=Add(source,Add(scatter,{0,(std::max)(0.0f,height+lift*flight-4.9f*flight*flight),0}));
    grain.velocity=age<landing ? Add(Scale(collapse.right,std::cos(angle)*speed*drag+0.35f),
        Add(Scale(collapse.forward,std::sin(angle)*speed*drag),{0,lift-9.8f*flight,0})) : Vector3{};
    grain.radius=0.010f+0.018f*rnd(5);
    grain.opacity=0.85f*(1.0f-(std::clamp)((age-landing)/0.18f,0.0f,1.0f));
    return grain;
}

RailTitleCollapsePuff RailTitleScene::EvaluateCollapsePuff(const RailTitleSandCollapse& collapse, uint32_t index) {
    RailTitleCollapsePuff puff;
    if(!collapse.active || index>=CollapsePuffCount) return puff;
    const auto clod=EvaluateSandClod(collapse,index/CollapsePuffsPerClod);
    const auto rnd=[&](uint32_t channel){return CollapseRandom(collapse.seed,index+397,channel);};
    const uint32_t layer=index%CollapsePuffsPerClod;
    // Suspended powder sheds from the ceiling after the first grains; lower
    // billows still wait for ground contact. This gives the storm vertical depth.
    const bool ceiling=layer==3;
    const float age=ceiling ? clod.emissionAge-0.16f-0.035f*rnd(0) :
        clod.impactAge-0.10f-0.045f*float(layer)-0.035f*rnd(0);
    if(age<=0.0f || age>=2.3f) return puff;
    const float growth=1.0f-std::exp(-age*2.6f);
    const float angle=rnd(1)*2*Pi;
    const float spread=(1.0f+1.7f*rnd(2))*growth;
    const auto relative=Add(clod.impactPosition,Scale(collapse.origin,-1));
    const float side=relative.x*collapse.right.x+relative.z*collapse.right.z<0 ? -1.0f : 1.0f;
    const float inward=-side*(1.2f+1.2f*rnd(6))*growth;
    puff.position=Add(clod.impactPosition,Add(Scale(collapse.right,std::cos(angle)*spread+inward),
        Add(Scale(collapse.forward,std::sin(angle)*spread-age*1.8f),{0,0.18f+(0.7f+0.90f*float(layer))*growth,0})));
    if(ceiling) puff.position.y=collapse.origin.y+8.0f-age*2.0f;
    puff.radius=0.24f+(2.0f+1.25f*rnd(3))*growth;
    puff.aspect=layer==0 ? 0.42f : 0.85f+0.18f*rnd(4);
    puff.rotation=(rnd(5)-0.5f)*1.2f+age*0.13f;
    puff.opacity=(1.0f-std::exp(-age*12.0f))*std::pow(1.0f-age/2.3f,1.3f)*(ceiling ? 0.10f : layer==0 ? 0.18f : 0.10f+0.025f*float(layer));
    return puff;
}

float RailTitleScene::CollapseDustStrength() const {
    if(!sandCollapse_.active) return 0.0f;
    float density=0.0f;
    for(uint32_t i=0;i<CollapsePuffCount;++i) {
        const auto puff=EvaluateCollapsePuff(sandCollapse_,i);
        const auto delta=Add(puff.position,Scale(cameraPosition_,-1));
        const float distance=std::sqrt(delta.x*delta.x+delta.y*delta.y+delta.z*delta.z);
        density+=puff.opacity*(std::clamp)((8.0f+puff.radius-distance)/6.0f,0.0f,1.0f);
    }
    // A broad airborne veil builds as the expanding world clouds reach the eye.
    // The existing burn still owns blackout, including the fully black endpoint.
    return (std::min)(0.68f,1.0f-std::exp(-density*0.35f));
}

void RailTitleScene::UpdateAttack(float deltaTime) {
    attackCues_.clear();
    if(railImpact_.active) {
        railImpact_.age=(std::max)(0.0f,startTime_-DepartureImpactTime);
        if(railImpact_.age>=0.72f) railImpact_.active=false;
    }
    for(auto& cloud:dustClouds_) cloud.age+=deltaTime;
    std::erase_if(dustClouds_,[](const auto& cloud){return cloud.age>=RailTitleDustCloud::Lifetime;});
    muzzleFlashTime_ = (std::max)(0.0f,muzzleFlashTime_-deltaTime);
    // Existing shots continue in world space while the cart/camera moves.
    for(auto& shot : shots_) {
        shot.age = shot.railStrike ? (std::max)(0.0f,startTime_-DepartureShotTime) : shot.age+deltaTime;
        const float t = (std::clamp)(shot.age/shot.duration,0.0f,1.0f);
        shot.position = Add(Scale(shot.origin,1-t),Scale(shot.target,t));
        if(shot.railStrike && !shot.nearMissNotified && startTime_>=DepartureNearMissTime) {
            shot.nearMissNotified=true;
            attackCues_.push_back({RailTitleAttackCueKind::NearMiss,shot.position});
        }
        if(t>=1.0f) {
            if(shot.railStrike) {
                railImpact_={shot.target,(std::max)(0.0f,startTime_-DepartureImpactTime),shot.id,true};
                railImpact_.wall=departureWallHit_;
                railImpact_.normal=departureWallNormal_;
                attackCues_.push_back({departureWallHit_ ? RailTitleAttackCueKind::WallImpact :
                    RailTitleAttackCueKind::RailImpact,shot.target});
            } else {
                attackCues_.push_back({RailTitleAttackCueKind::GroundImpact,shot.target});
                dustClouds_.push_back({shot.target,0.0f,shot.id});
            }
        }
    }
    std::erase_if(shots_,[](const auto& shot){return shot.age>=shot.duration;});
    if(Blackout()>=0.2f) { shots_.clear(); attackCues_.clear(); dustClouds_.clear(); }
    attackTime_ += deltaTime;
    if(starting_ && !departureShotFired_ && deltaTime>0.0f && startTime_>=DepartureShotTime) {
        departureShotFired_=true;
        RailTitleShot shot;
        shot.id=++shotSequence_; shot.railStrike=true;
        shot.duration=DepartureImpactTime-DepartureShotTime;
        shot.target=DepartureImpactTarget();
        // Evaluate the authored firing pose at its exact clock time. Neither
        // the muzzle nor the target depends on the frame that crosses it.
        const Vector3 position=DepartureDronePosition(DepartureShotTime);
        const Vector3 delta=Add(shot.target,Scale(position,-1));
        const float length=std::sqrt(delta.x*delta.x+delta.y*delta.y+delta.z*delta.z);
        const Vector3 rotation{std::asin(delta.y/length),std::atan2(-delta.x,-delta.z),
            0.05f*std::sin(DepartureShotTime*3.0f)};
        const auto world=MakeAffineMatrix(pursuer_.scale,rotation,position);
        const Vector3 local{0.30f,-0.61f,-0.99f};
        shot.origin={local.x*world.m[0][0]+local.y*world.m[1][0]+local.z*world.m[2][0]+world.m[3][0],
            local.x*world.m[0][1]+local.y*world.m[1][1]+local.z*world.m[2][1]+world.m[3][1],
            local.x*world.m[0][2]+local.y*world.m[1][2]+local.z*world.m[2][2]+world.m[3][2]};
        if(departureWallHit_) shot.origin=departureShotOrigin_;
        shot.age=startTime_-DepartureShotTime;
        const float t=shot.age/shot.duration;
        shot.position=Add(Scale(shot.origin,1-t),Scale(shot.target,t));
        shots_.push_back(shot);
        attackCues_.push_back({RailTitleAttackCueKind::Muzzle,shot.origin});
        muzzleFlashTime_=0.12f;
    }
    if(!starting_ && deltaTime>0.0f && attackTime_+0.000001>=nextShotTime_) {
        RailTitleShot shot;
        shot.id = ++shotSequence_;
        // Alternate the two authored muzzles; transform the local barrel tip.
        const Vector3 local{shotSequence_%2 ? -0.30f : 0.30f,-0.61f,-0.99f};
        const Matrix4x4 world = MakeAffineMatrix(pursuer_.scale,pursuer_.rotation,pursuer_.position);
        shot.origin = {local.x*world.m[0][0]+local.y*world.m[1][0]+local.z*world.m[2][0]+world.m[3][0],
            local.x*world.m[0][1]+local.y*world.m[1][1]+local.z*world.m[2][1]+world.m[3][1],
            local.x*world.m[0][2]+local.y*world.m[1][2]+local.z*world.m[2][2]+world.m[3][2]};
        shot.target = pursuerAimTarget_;
        shot.age = static_cast<float>((std::max)(0.0,attackTime_-nextShotTime_));
        const float t = shot.age/shot.duration;
        shot.position = Add(Scale(shot.origin,1-t),Scale(shot.target,t));
        shots_.push_back(shot);
        attackCues_.push_back({RailTitleAttackCueKind::Muzzle,shot.origin});
        muzzleFlashTime_ = 0.12f;
        ++burstShot_;
        if(burstShot_==3) { burstShot_=0; nextShotTime_+=4.24; }
        else nextShotTime_+=0.28;
    }
    const float charge = !starting_ ? (std::clamp)(1.0f-static_cast<float>(nextShotTime_-attackTime_)/0.40f,0.0f,1.0f) : 0.0f;
    pursuer_.charge = (std::max)(charge,muzzleFlashTime_/0.12f);
}

RailTitleSandGrain RailTitleScene::EvaluateSandGrain(const RailTitleDustCloud& cloud, uint32_t index) {
    auto random=[&](uint32_t channel) {
        uint32_t bits=cloud.seed*747796405u+index*2891336453u+channel*277803737u;
        bits=((bits>>((bits>>28u)+4u))^bits)*277803737u;
        return float((bits>>22u)^bits)/float(UINT32_MAX);
    };
    const float angle=2.0f*Pi*random(0);
    const float speed=2.5f+5.0f*random(1), lift=1.4f+3.3f*random(2);
    const float age=(std::max)(0.0f,cloud.age-random(3)*0.055f);
    const float landing=(lift+std::sqrt(lift*lift+2.0f*9.8f*0.035f))/9.8f;
    const float flight=(std::min)(age,landing), drag=std::exp(-2.2f*flight);
    const float spread=speed*(1.0f-drag)/2.2f;
    RailTitleSandGrain grain;
    grain.position=Add(cloud.origin,{std::cos(angle)*spread,
        (std::max)(0.035f+lift*flight-4.9f*flight*flight,0.0f),std::sin(angle)*spread});
    grain.velocity=age<landing ? Vector3{std::cos(angle)*speed*drag,lift-9.8f*age,std::sin(angle)*speed*drag} : Vector3{};
    grain.radius=0.018f+0.025f*random(4);
    grain.opacity=cloud.age>=random(3)*0.055f ? 0.90f*(1.0f-(std::clamp)((age-landing)/0.22f,0.0f,1.0f)) : 0.0f;
    return grain;
}

RailTitleSandGrain RailTitleScene::EvaluateRailSpark(const RailTitleRailImpact& impact, uint32_t index) {
    const auto random=[&](uint32_t channel) {
        uint32_t bits=impact.seed*747796405u+index*2891336453u+channel*277803737u;
        bits=((bits>>((bits>>28u)+4u))^bits)*277803737u;
        return float((bits>>22u)^bits)/float(UINT32_MAX);
    };
    const float age=(std::max)(0.0f,impact.age);
    const float life=0.34f+0.005f*float(index%RailImpactSparkCount);
    const float angle=2.0f*Pi*random(0);
    const float speed=3.8f+4.2f*random(1),lift=7.0f+4.5f*random(2);
    const float drag=std::exp(-1.8f*age),spread=speed*(1-drag)/1.8f;
    const float fade=(std::clamp)((age-0.08f)/(life-0.08f),0.0f,1.0f);
    RailTitleSandGrain spark;
    // Metal fragments leave the contact together, then separate ballistically.
    // Ground dust retains its slower, staggered emission in EvaluateSandGrain.
    spark.position=Add(impact.position,{std::cos(angle)*spread,
        (std::max)(0.04f+lift*age-4.9f*age*age,0.04f),std::sin(angle)*spread});
    spark.velocity={std::cos(angle)*speed*drag,lift-9.8f*age,std::sin(angle)*speed*drag};
    if(impact.wall) {
        // Eject into the cave air, rather than spreading along a ground plane.
        const auto side=Normalize(Cross(std::abs(impact.normal.y)<0.9f ? Vector3{0,1,0} : Vector3{1,0,0},impact.normal));
        const auto vertical=Cross(impact.normal,side);
        const auto velocity=Add(Scale(impact.normal,3.8f+4.0f*random(4)),
            Add(Scale(side,std::cos(angle)*speed),Scale(vertical,std::sin(angle)*speed)));
        spark.position=Add(impact.position,Add(Scale(impact.normal,0.07f),
            Add(Scale(velocity,(1-drag)/1.8f),Vector3{0,-4.9f*age*age,0})));
        spark.velocity=Add(Scale(velocity,drag),Vector3{0,-9.8f*age,0});
    }
    spark.radius=0.060f+0.024f*random(3);
    spark.opacity=impact.active && age<life ? 0.95f*(1-fade*fade*(3-2*fade)) : 0.0f;
    return spark;
}

int RailTitleScene::HitTest(float x,float y,float width,float height) {
    const float s = (std::min)(width/1600.0f,height/900.0f);
    if (s<=0.0f) return -1;
    x = (x-(width-1600.0f*s)*0.5f)/s;
    y = (y-(height-900.0f*s)*0.5f)/s;
    if (x<96.0f || x>560.0f) return -1;
    for (int i=0;i<3;++i) if (y>=480.0f+i*82.0f && y<=548.0f+i*82.0f) return i;
    return -1;
}

void RailTitleScene::BeginStart() {
    if(starting_) return;
    const auto departure=path_.Evaluate(state_.distance);
    const float departureYaw=std::atan2(departure.tangent.x,departure.tangent.z);
    // Split the current Bezier segment exactly. Keep every earlier control
    // point so rails behind the cart do not change shape when start is pressed.
    uint32_t segment=0;
    while(segment+1<path_.SegmentCount() && path_.SegmentStartDistance(segment+1)<=state_.distance) ++segment;
    const float t=(std::clamp)((state_.distance-path_.SegmentStartDistance(segment))/path_.SegmentLength(segment),0.0f,1.0f);
    auto retained=path_.ControlPoints();
    const Vector3 p0=retained[segment].position;
    const Vector3 p1=Add(p0,retained[segment].outgoingTangent);
    const Vector3 p3=retained[segment+1].position;
    const Vector3 p2=Add(p3,retained[segment+1].incomingTangent);
    const auto lerp=[t](Vector3 a,Vector3 b){return Add(Scale(a,1-t),Scale(b,t));};
    const Vector3 a=lerp(p0,p1),b=lerp(p1,p2),c=lerp(p2,p3);
    const Vector3 d=lerp(a,b),e=lerp(b,c),cut=lerp(d,e);
    retained.resize(segment+1);
    RailPathControlPoint join;
    join.position=cut;join.speed=Speed;join.tangentMode=RailPathTangentMode::Broken;
    join.incomingTangent=Add(d,Scale(cut,-1));
    if(t>0.000001f) {
        retained.back().tangentMode=RailPathTangentMode::Broken;
        retained.back().outgoingTangent=Add(a,Scale(p0,-1));
        retained.push_back(join);
    }
    // At an exact control point, keep the original incoming curve handle.
    retained.back().tangentMode=RailPathTangentMode::Broken;
    retained.back().outgoingTangent=Scale(departure.tangent,400.0f/3.0f);
    const auto joinIndex=static_cast<uint32_t>(retained.size()-1);
    RailPathControlPoint endPoint;
    endPoint.position=Add(departure.position,Scale(departure.tangent,400.0f));
    endPoint.speed=Speed;endPoint.tangentMode=RailPathTangentMode::Broken;
    endPoint.incomingTangent=Scale(departure.tangent,-400.0f/3.0f);
    retained.push_back(endPoint);
    // Preserve scenery in world space before replacing the looping rail.
    for(auto& p:scenery_.terrainPlacements) {
        const auto old=path_.Evaluate(p.distance);
        const Vector3 world=Add(old.position,Add(Scale(old.right,p.lateralOffset),
            Scale(old.tangent,p.forwardOffset)));
        const Vector3 delta=Add(world,Scale(departure.position,-1));
        p.distance=400.0f;
        p.forwardOffset=delta.x*departure.tangent.x+delta.z*departure.tangent.z;
        p.lateralOffset=delta.x*departure.right.x+delta.z*departure.right.z;
        p.rotation.y+=std::atan2(old.tangent.x,old.tangent.z)-departureYaw;
    }
    std::vector<RailPathControlPoint> straight;
    constexpr float end=400.0f;
    for(float offset:{-end,0.0f,end}) {
        RailPathControlPoint p;
        p.position=Add(departure.position,Scale(departure.tangent,offset));
        p.speed=Speed;
        p.tangentMode=RailPathTangentMode::Mirrored;
        p.incomingTangent=Scale(departure.tangent,-end/3.0f);
        p.outgoingTangent=Scale(departure.tangent,end/3.0f);
        straight.push_back(p);
    }
    // A separate straight coordinate frame keeps scenery fixed in world space,
    // including scenery behind the join where the motion rail is still curved.
    sceneryPath_.SetControlPoints(std::move(straight));
    path_.SetControlPoints(std::move(retained));
    auto trackDefinition=track_.Result().definition;
    trackDefinition.assetId="title_departure";
    track_.Bake(trackDefinition,path_);
    state_.distance=state_.previousDistance=path_.SegmentStartDistance(joinIndex);
    starting_ = true;
    departureAge_=age_;
    shots_.clear(); railImpact_={}; departureShotFired_=false;
    startTime_ = 0.0f;
    startTravel_=travel_; startDistance_=state_.distance;
    CourseTerrainPlacement tunnel;
    tunnel.id="title_start_tunnel"; tunnel.meshId="title_tunnel";
    tunnel.distance=400.0f+TunnelLead;
    tunnel.scale={1,1,1};
    tunnel.cullAheadDistance=tunnel.cullBehindDistance=5000.0f;
    scenery_.terrainPlacements.push_back(tunnel);
    PrepareDepartureWallShot();
    renderScenery_=scenery_;
    const auto collapseSite=sceneryPath_.Evaluate(400.0f+TunnelLead+14.0f);
    sandCollapse_={Add(collapseSite.position,{0,-0.30f,0}),
        collapseSite.right,collapseSite.tangent,0.0f,0,false};
}
float RailTitleScene::StartProgress() const { return (std::clamp)(startTime_/OrbitDuration,0.0f,1.0f); }
RailTitleColors RailTitleScene::Colors(Vector4 gameplaySunColor) const {
    RailTitleColors result;
    const auto luminance=[](Vector3 c) {return c.x*0.2126f+c.y*0.7152f+c.z*0.0722f;};
    // Begin after the menu disappears, arrive as the camera finishes its orbit.
    // Darkness is owned solely by Blackout(), never by this color transition.
    const float t=(std::clamp)((startTime_-0.35f)/1.10f,0.0f,1.0f);
    result.transition=t*t*(3.0f-2.0f*t);
    Vector3 target{gameplaySunColor.x,gameplaySunColor.y,gameplaySunColor.z};
    if(!std::isfinite(target.x) || !std::isfinite(target.y) || !std::isfinite(target.z) ||
        target.x<0 || target.y<0 || target.z<0 || luminance(target)<0.001f) return result;
    const Vector3 original{result.light.x,result.light.y,result.light.z};
    target=Scale(target,luminance(original)/luminance(target));
    // Use the opening course's warm hue already at the menu, retaining outdoor
    // luminance instead of copying the much darker canyon exposure.
    const float blend=0.85f+0.15f*result.transition;
    const Vector3 light=Add(Scale(original,1-blend),Scale(target,blend));
    result.light={light.x,light.y,light.z,1};
    // Match the title shader's haze tint, preserving the old background luminance.
    const Vector3 tinted{result.background.x*light.x/original.x,
        result.background.y*light.y/original.y,result.background.z*light.z/original.z};
    result.background=Scale(tinted,luminance(result.background)/luminance(tinted));
    return result;
}
float RailTitleScene::MenuOpacity() const {
    const float t=(std::clamp)(startTime_/0.35f,0.0f,1.0f);
    return 1.0f-t*t*(3.0f-2.0f*t);
}
float RailTitleScene::Blackout() const {
    return starting_ ? SandCoverageAtAge(startTime_-CollapseBegin) : 0.0f;
}
float RailTitleScene::SandCoverageAtAge(float age) {
    // Wait until the chase camera has crossed the mouth, not just the cart.
    // Preserve the readable spread at its existing speed. Once the screen is
    // mostly covered, finish the peripheral pockets without a long black hold.
    const float coverDelay=(TunnelLead+DepartureCameraRig().backDistance)/Speed+FilmBurnDelay-CollapseBegin;
    const float elapsed=age-coverDelay;
    const float tailStart=FilmBurnDuration*FilmBurnTailBegin;
    if(elapsed<=tailStart)
        return (std::clamp)(elapsed/FilmBurnDuration,0.0f,FilmBurnTailBegin);
    const float tail=(std::clamp)((elapsed-tailStart)/FilmBurnTailDuration,0.0f,1.0f);
    return FilmBurnTailBegin+(1.0f-FilmBurnTailBegin)*tail;
}
float RailTitleScene::TunnelCameraDepth() const {
    return starting_ ? static_cast<float>(travel_-startTravel_)-TunnelLead-ChaseDistance() : -TunnelLead;
}
float RailTitleScene::TunnelShade() const {
    if(!starting_) return 0.0f;
    const float t=(std::clamp)((static_cast<float>(travel_-startTravel_)-TunnelLead)/12.0f,0.0f,1.0f);
    return t*t*(3.0f-2.0f*t);
}
float RailTitleScene::AmbienceGain() const {
    const float t=(std::clamp)(age_/0.8f,0.0f,1.0f);
    return t*t*(3.0f-2.0f*t)*(1.0f-Blackout());
}
float RailTitleScene::RollingGain() const {
    const float t=(std::clamp)(age_/0.8f,0.0f,1.0f);
    // Mechanical motion survives the blackout; the same voice plays in-game.
    return t*t*(3.0f-2.0f*t);
}

Vector2 RailTitleScene::LightSweep() const {
    if(!railImpact_.active) return {};
    const float p=railImpact_.age/LightSweepDuration;
    if(p<=0.0f || p>=1.0f) return {};
    const float pulse=std::sin(Pi*p);
    return {-8.0f+16.0f*p,pulse*pulse};
}
float RailTitleScene::LightSweepIllumination(Vector3 worldPosition) const {
    const auto sweep=LightSweep();
    if(sweep.y<=0.0f) return 0.0f;
    const auto sample=path_.Evaluate(state_.distance);
    const auto delta=Add(worldPosition,Scale(sample.position,-1));
    const auto dot=[](Vector3 a,Vector3 b) { return a.x*b.x+a.y*b.y+a.z*b.z; };
    const float along=dot(delta,sample.tangent);
    const float across=dot(delta,sample.right);
    const float height=dot(delta,sample.up);
    const float sheet=(across+0.28f*along-0.80f*height-sweep.x)/1.4f;
    return sweep.y*std::exp(-sheet*sheet)*std::exp(-along*along/200.0f);
}
float RailTitleScene::RailReflectionOpacity() const {
    if(!railImpact_.active) return 0.0f;
    const float p=(railImpact_.age-ReflectionDelay)/ReflectionDuration;
    if(p<=0.0f || p>=1.0f) return 0.0f;
    // The reflection precedes the burn, so it is not hidden by its mask.
    const float pulse=std::sin(Pi*p);
    return 1.50f*pulse*pulse;
}
float RailTitleScene::RailReflectionDistance() const {
    const float p=(std::clamp)((railImpact_.age-ReflectionDelay)/ReflectionDuration,0.0f,1.0f);
    return DepartureImpactAhead-(DepartureImpactAhead-3.0f)*p;
}
float RailTitleScene::RailImpactLight() const {
    if(!railImpact_.active) return 0.0f;
    const float decayAge=(std::max)(0.0f,railImpact_.age-0.04f);
    return 2.9f*std::exp(-decayAge*11.0f)*
        (1.0f-(std::clamp)(railImpact_.age/0.38f,0.0f,1.0f));
}
float RailTitleScene::LensLeakAge() const {
    return starting_ && railImpact_.seed != 0
        ? (std::max)(0.0f,startTime_-DepartureImpactTime) : 0.0f;
}
float RailTitleScene::LensLeakStrength() const {
    // Only a confirmed scripted impact can expose the lens. Its afterimage
    // persists beyond the world spark lifetime, on the same presentation clock.
    if(!starting_ || railImpact_.seed == 0) return 0.0f;
    const float age=LensLeakAge();
    const float onset=(std::clamp)(age/0.04f,0.0f,1.0f);
    return onset*onset*(3.0f-2.0f*onset)*std::exp(-age*1.55f)*
        (1.0f-0.55f*TunnelShade());
}
Vector3 RailTitleScene::DepartureDronePosition(float elapsed) const {
    const float age=departureAge_+elapsed;
    const float gap=32.0f+3.0f*std::sin(age*0.55f);
    const auto behind=path_.Evaluate(startDistance_+Speed*elapsed-gap);
    Vector3 position=Add(behind.position,Scale(behind.right,-10.0f+2.4f*std::sin(age*0.8f)));
    position.y+=3.6f+0.65f*std::sin(age*1.7f);
    // Ease into the entrance corridor before firing at the opposite left wall.
    // Keep the retained rail's longitudinal motion and all existing hover.
    const auto entrance=sceneryPath_.Evaluate(400.0f+TunnelLead);
    const float lateral=Dot(Add(position,Scale(entrance.position,-1)),entrance.right);
    const float t=(std::clamp)(elapsed/1.45f,0.0f,1.0f);
    position=Add(position,Scale(entrance.right,-lateral*t*t*(3.0f-2.0f*t)));
    return position;
}
Vector3 RailTitleScene::DepartureImpactTarget() const {
    if(departureWallHit_) return departureWallTarget_;
    const auto sample=path_.Evaluate(startDistance_+Speed*DepartureImpactTime+DepartureImpactAhead);
    const auto& track=track_.Result().definition;
    return Add(sample.position,Add(Scale(sample.right,-track.trackGauge*0.5f),
        Scale(sample.up,track.railHeadVerticalOffset+track.railHeadHeight*0.5f+0.012f)));
}
void RailTitleScene::PrepareDepartureWallShot() {
    departureWallHit_=false;
    const auto frame=sceneryPath_.Evaluate(400.0f+TunnelLead);
    const auto toWorld=[&](Vector3 p) {
        return Add(frame.position,Add(Scale(frame.right,p.x),Add(Scale(frame.up,p.y),Scale(frame.tangent,p.z))));
    };
    const auto toLocal=[&](Vector3 p) {
        const auto d=Add(p,Scale(frame.position,-1));
        return Vector3{Dot(d,frame.right),Dot(d,frame.up),Dot(d,frame.tangent)};
    };
    // Aim beyond the upper left wall. The first mesh intersection owns the
    // contact, including the irregular mouth, regardless of title lap phase.
    Vector3 target=toWorld({-12.0f,7.0f,50.0f});
    const auto position=DepartureDronePosition(DepartureShotTime);
    for(int iteration=0;iteration<5;++iteration) {
        const auto delta=Add(target,Scale(position,-1));
        const float length=std::sqrt(Dot(delta,delta));
        const Vector3 rotation{std::asin(delta.y/length),std::atan2(-delta.x,-delta.z),
            0.05f*std::sin(DepartureShotTime*3.0f)};
        const auto world=MakeAffineMatrix(Vector3{1.25f,1.25f,1.25f},rotation,position);
        const Vector3 local{0.30f,-0.61f,-0.99f};
        const Vector3 origin{local.x*world.m[0][0]+local.y*world.m[1][0]+local.z*world.m[2][0]+world.m[3][0],
            local.x*world.m[0][1]+local.y*world.m[1][1]+local.z*world.m[2][1]+world.m[3][1],
            local.x*world.m[0][2]+local.y*world.m[1][2]+local.z*world.m[2][2]+world.m[3][2]};
        Vector3 contact{},normal{};
        const auto end=Add(origin,Scale(Add(target,Scale(origin,-1)),1.05f));
        if(!TraceTitleTunnel(toLocal(origin),toLocal(end),contact,normal)) return;
        target=toWorld(contact);
        departureShotOrigin_=origin;
        departureWallTarget_=target;
        departureWallNormal_=Normalize(Add(Scale(frame.right,normal.x),
            Add(Scale(frame.up,normal.y),Scale(frame.tangent,normal.z))));
    }
    departureWallHit_=true;
}
CourseCameraKey RailTitleScene::DepartureCameraRig() {
    CourseCameraKey rig{};
    rig.backDistance=8.0f;
    rig.verticalOffset=5.2f;
    rig.lateralOffset=0.0f;
    rig.lookAheadDistance=14.0f;
    rig.lookUpOffset=2.0f;
    rig.lookForwardOffset=0.0f;
    rig.fovY=0.70f;
    rig.roll=0.0f;
    return rig;
}

void RailTitleGameplayEntry::Reset() {
    elapsed_=0.0;
    begun_=false;
    frame_={};
    pendingEvents_.clear();
}
RailReadyDroneFlight EvaluateRailReadyDroneFlight(float elapsed) {
    const float t=std::isfinite(elapsed) ? (std::max)(0.0f,elapsed) : 0.0f;
    const auto smooth=[&](float from,float to) {
        const float u=(std::clamp)((t-from)/(to-from),0.0f,1.0f);
        return u*u*(3.0f-2.0f*u);
    };
    const auto hermite=[&](float from,float to,float a,float b,float va,float vb) {
        const float u=(std::clamp)((t-from)/(to-from),0.0f,1.0f);
        const float u2=u*u,u3=u2*u;
        return (2*u3-3*u2+1)*a+(u3-2*u2+u)*(to-from)*va+
            (-2*u3+3*u2)*b+(u3-u2)*(to-from)*vb;
    };
    const auto smoothSpeed=[&](float from,float to) {
        const float u=(std::clamp)((t-from)/(to-from),0.0f,1.0f);
        return 6.0f*u*(1.0f-u)/(to-from);
    };
    const auto hermiteSpeed=[&](float from,float to,float a,float b,float va,float vb) {
        const float u=(std::clamp)((t-from)/(to-from),0.0f,1.0f),u2=u*u;
        return ((6*u2-6*u)*a+(-6*u2+6*u)*b)/(to-from)+
            (3*u2-4*u+1)*va+(3*u2-2*u)*vb;
    };
    constexpr float turnRadius=6.5f,turnOuterSide=9.0f;
    constexpr float turnDuration=RailTitleGameplayEntry::DroneTurnEnd-RailTitleGameplayEntry::DroneTurnBegin;
    constexpr float brakeDuration=RailTitleGameplayEntry::DroneBrakeEnd-RailTitleGameplayEntry::DroneTurnEnd;
    constexpr float turnSpeed=Pi*turnRadius/turnDuration;
    constexpr float brakeRelativeSpeed=turnSpeed+RailTitleScene::Speed;
    constexpr float turnExitForward=34.0f+brakeRelativeSpeed*brakeDuration*0.5f;
    constexpr float turnEntryForward=turnExitForward+RailTitleScene::Speed*turnDuration;
    RailReadyDroneFlight flight;
    // Start as a distant pursuer in the rail lane. The lateral clearance is
    // needed only for the brief pass, not for the whole approach.
    if(t<0.64f) flight.forward=-66.0f;
    else if(t<2.15f) flight.forward=hermite(0.64f,2.15f,-66.0f,14.0f,0.0f,36.0f);
    else if(t<RailTitleGameplayEntry::DroneTurnBegin)
        flight.forward=hermite(2.15f,RailTitleGameplayEntry::DroneTurnBegin,
            14.0f,turnEntryForward,36.0f,turnSpeed-RailTitleScene::Speed);
    const float pass=smooth(1.15f,2.05f);
    const float widen=smooth(2.15f,RailTitleGameplayEntry::DroneTurnBegin);
    flight.lateral=1.2f+4.7f*pass+3.1f*widen;
    flight.vertical=7.0f;
    Vector3 velocity{4.7f*smoothSpeed(1.15f,2.05f)+
        3.1f*smoothSpeed(2.15f,RailTitleGameplayEntry::DroneTurnBegin),0,RailTitleScene::Speed};
    if(t>=0.64f && t<2.15f)
        velocity.z+=hermiteSpeed(0.64f,2.15f,-66.0f,14.0f,0.0f,36.0f);
    else if(t>=2.15f && t<RailTitleGameplayEntry::DroneTurnBegin)
        velocity.z+=hermiteSpeed(2.15f,RailTitleGameplayEntry::DroneTurnBegin,
            14.0f,turnEntryForward,36.0f,turnSpeed-RailTitleScene::Speed);
    // Define a moving semicircle in world travel, subtracting cart travel
    // only when writing the relative rail distance. Nose and bank belong to
    // this same curve; the drone actually flies back before braking to hover.
    const float turn=(std::clamp)((t-RailTitleGameplayEntry::DroneTurnBegin)/turnDuration,0.0f,1.0f);
    if(t>=RailTitleGameplayEntry::DroneTurnBegin) {
        const float angle=Pi*turn;
        flight.lateral=turnOuterSide-turnRadius+turnRadius*std::cos(angle);
        flight.forward=turnEntryForward+turnRadius*std::sin(angle)-
            RailTitleScene::Speed*(std::min)(t-RailTitleGameplayEntry::DroneTurnBegin,turnDuration);
        flight.vertical=7.0f-turn*turn*(3.0f-2.0f*turn);
        velocity={-turnSpeed*std::sin(angle),-6.0f*turn*(1.0f-turn)/turnDuration,
            turnSpeed*std::cos(angle)};
    }
    float yaw=std::atan2(-velocity.x,-velocity.z);
    if(yaw<0.0f) yaw+=2.0f*Pi;
    flight.rotation={std::atan2(velocity.y,std::sqrt(velocity.x*velocity.x+velocity.z*velocity.z)),yaw,0};
    const float bankIn=smooth(RailTitleGameplayEntry::DroneTurnBegin-0.18f,RailTitleGameplayEntry::DroneTurnBegin);
    const float bankOut=1.0f-smooth(RailTitleGameplayEntry::DroneTurnEnd,RailTitleGameplayEntry::DroneBrakeEnd);
    // Lean into the right-hand lane change, then counter-bank as it settles.
    // Two bounded weight transfers give the approach inertia without idle wobble.
    const float approachBank=0.19f*(smooth(1.06f,1.32f)-2.0f*smooth(1.57f,1.84f)+smooth(1.96f,2.17f));
    const float outwardBank=0.10f*(smooth(2.15f,2.40f)-smooth(2.70f,3.02f));
    flight.rotation.z=(approachBank+outwardBank)*(1.0f-bankIn)-
        (0.55f+0.08f*std::sin(Pi*turn))*bankIn*bankOut;
    if(t>=RailTitleGameplayEntry::DroneTurnEnd) {
        const float remaining=1.0f-(std::clamp)((t-RailTitleGameplayEntry::DroneTurnEnd)/brakeDuration,0.0f,1.0f);
        flight.forward=34.0f+brakeRelativeSpeed*brakeDuration*0.5f*remaining*remaining;
        flight.rotation.x=flight.rotation.y=0.0f;
    }
    return flight;
}

RailReadyDroneFlight EvaluateRailReadyDroneFlight(float elapsed,const RailPath& rail,
        float playerDistance,float playerSpeed) {
    auto flight=EvaluateRailReadyDroneFlight(elapsed);
    if(!std::isfinite(elapsed) || !std::isfinite(playerDistance) || !std::isfinite(playerSpeed) ||
            rail.Length()<=0.0f || elapsed>=RailTitleGameplayEntry::DroneTurnEnd) return flight;
    constexpr float h=0.002f;
    const auto position=[&](float step) {
        const auto adjacent=EvaluateRailReadyDroneFlight(elapsed+step);
        const float distance=playerDistance+playerSpeed*step+adjacent.forward;
        const auto sample=rail.Evaluate(distance);
        return Add(sample.position,Add(Scale(sample.tangent,(std::min)(0.0f,distance)),
            Add(Scale(sample.right,adjacent.lateral),Scale(sample.up,adjacent.vertical))));
    };
    const auto velocity=Scale(Add(position(h),Scale(position(-h),-1.0f)),0.5f/h);
    if(Dot(velocity,velocity)<0.0001f) return flight;
    const auto frame=rail.Evaluate(playerDistance+flight.forward);
    const Vector3 local{Dot(velocity,frame.right),Dot(velocity,frame.up),Dot(velocity,frame.tangent)};
    flight.rotation.x=std::atan2(local.y,std::sqrt(local.x*local.x+local.z*local.z));
    flight.rotation.y=std::atan2(-local.x,-local.z);
    if(flight.rotation.y<0.0f) flight.rotation.y+=2.0f*Pi;
    return flight;
}

TwinShieldDronePose BuildRailReadyDronePose(const RailPath& rail,float railDistance,
        float lateral,float vertical,Vector3 rotation,float radius) {
    const auto sample=rail.Evaluate(railDistance);
    TwinShieldDronePose pose;
    pose.visible=true;
    pose.position=Add(sample.position,Add(Scale(sample.tangent,(std::min)(0.0f,railDistance)),
        Add(Scale(sample.right,lateral),Scale(sample.up,vertical))));
    pose.rotation=ResolveTwinShieldDroneFacing(sample.tangent,pose.position,{},rotation,{},true);
    pose.flightBank=pose.rotation.z; pose.rotation.z=0;
    const float size=radius*kTwinShieldGameplayModelScale;
    pose.scale={size,size,size};
    return pose;
}

void RailReadyDroneSalvo::Reset() {
    planAttempted_={}; planned_={}; targets_={}; durations_={};
    attempted_={}; launched_={}; normals_={}; impacts_={}; shots_.clear(); cues_.clear(); muzzleFlash_=0; muzzlePosition_={};
}

Vector3 RailReadyDroneSalvo::Muzzle(const TwinShieldDronePose& pose,uint32_t barrelIndex) {
    const auto world=BuildTwinShieldDroneWorldMatrix(pose);
    const Vector3 lip{barrelIndex==0?-0.30f:0.30f,-0.61f,-0.94f};
    return {lip.x*world.m[0][0]+lip.y*world.m[1][0]+lip.z*world.m[2][0]+world.m[3][0],
        lip.x*world.m[0][1]+lip.y*world.m[1][1]+lip.z*world.m[2][1]+world.m[3][1],
        lip.x*world.m[0][2]+lip.y*world.m[1][2]+lip.z*world.m[2][2]+world.m[3][2]};
}

RailReadyDroneFlight RailReadyDroneSalvo::Flight(float elapsed,float playerDistance,
        const RailPath& rail,float radius) const {
    auto flight=EvaluateRailReadyDroneFlight(elapsed,rail,playerDistance,RailTitleScene::Speed);
    if((!planned_[0] && !planned_[1]) || !std::isfinite(elapsed)) return flight;
    const auto smooth=[](float t,float a,float b) {
        const float u=(std::clamp)((t-a)/(b-a),0.0f,1.0f); return u*u*(3-2*u);
    };
    const float first=planned_[0]?ShotTimes[0]:ShotTimes[1];
    const float last=planned_[1]?ShotTimes[1]:ShotTimes[0];
    const float weight=smooth(elapsed,first-0.28f,first-0.07f)*
        (1-smooth(elapsed,last+0.08f,last+0.30f));
    if(weight<=0) return flight;
    const float mix=planned_[0] && planned_[1] ? smooth(elapsed,ShotTimes[0],ShotTimes[1]) :
        (planned_[1]?1.0f:0.0f);
    const auto target=planned_[0] && planned_[1] ? Add(Scale(targets_[0],1-mix),Scale(targets_[1],mix)) :
        targets_[planned_[1]?1:0];
    const auto frame=rail.Evaluate(playerDistance+flight.forward);
    const auto base=flight.rotation;
    // Aim the rigid aircraft briefly while it continues its flight path. The
    // same pose drives the rendered hull, the gun lips and the actual shot.
    for(int iteration=0;iteration<4;++iteration) {
        const auto pose=BuildRailReadyDronePose(rail,playerDistance+flight.forward,
            flight.lateral,flight.vertical,flight.rotation,radius);
        const auto origin=Add(Scale(Muzzle(pose,0),1-mix),Scale(Muzzle(pose,1),mix));
        const auto direction=Normalize(Add(target,Scale(origin,-1)));
        const float x=Dot(direction,frame.right),y=Dot(direction,frame.up),z=Dot(direction,frame.tangent);
        const float yaw=std::atan2(-x,-z);
        const float pitch=std::atan2(y,std::sqrt(x*x+z*z));
        // Partial weight always blends from the flight tangent, not recursively.
        flight.rotation.x=base.x+(pitch-base.x)*weight;
        flight.rotation.y=base.y+std::remainder(yaw-base.y,2*Pi)*weight;
    }
    return flight;
}

bool RailReadyDroneSalvo::TraceLeftWall(const RailWorldRaycastInput& terrain,Vector3 origin,
        float launchDistance,uint32_t index,Vector3& contact,Vector3& normal) {
    if(!terrain.railPath || !terrain.terrainSettings || index>=ShotTimes.size()) return false;
    const auto& rail=*terrain.railPath;
    const TerrainVolumeField field(rail,*terrain.terrainSettings,terrain.terrainEdits,terrain.terrainPreview);
    for(float ahead:{45.0f,60.0f,78.0f}) {
        const float d=launchDistance+ahead+8.0f*float(index);
        Vector3 outward;
        const auto surface=field.SurfacePoint(d,Pi-0.22f,&outward);
        const auto target=Add(surface,Scale(outward,3.0f));
        RailAimState aim;
        aim.valid=true; aim.worldRayOrigin=origin;
        aim.worldRayDirection=Normalize(Add(target,Scale(origin,-1))); aim.maxDistance=180;
        auto query=terrain;
        query.aim=&aim; query.playerDistance=launchDistance;
        query.spawnRuntime=nullptr; query.course=nullptr; query.includeVisualColumns=false;
        const auto hit=RailWorldRaycast::Query(query);
        const auto frame=rail.Evaluate(d);
        if(!hit.hit || hit.kind!=RailAimHitKind::ProceduralTerrain || hit.distance<6 ||
                Dot(hit.normal,frame.right)<0.15f) continue;
        RailTitleShot test; test.origin=origin; test.target=hit.position;
        test.duration=std::sqrt(Dot(Add(hit.position,Scale(origin,-1)),Add(hit.position,Scale(origin,-1))))/ShotSpeed;
        if(!ClearsVehicle(test,rail,launchDistance)) continue;
        contact=hit.position; normal=hit.normal; return true;
    }
    return false;
}

bool RailReadyDroneSalvo::ClearsVehicle(const RailTitleShot& shot,const RailPath& rail,float playerDistanceAtLaunch) {
    // Sweep the moving cart corridor, including passenger height and a margin
    // around the projectile. Never rely on disabling damage to hide a hit.
    for(int i=0;i<=96;++i) {
        const float u=float(i)/96.0f;
        const auto cart=rail.Evaluate(playerDistanceAtLaunch+RailTitleScene::Speed*shot.duration*u);
        const auto point=Add(shot.origin,Scale(Add(shot.target,Scale(shot.origin,-1)),u));
        const auto delta=Add(point,Scale(cart.position,-1));
        if(std::abs(Dot(delta,cart.right))<4.4f && std::abs(Dot(delta,cart.tangent))<7.0f &&
            Dot(delta,cart.up)>-2.0f && Dot(delta,cart.up)<5.4f) return false;
    }
    return true;
}

void RailReadyDroneSalvo::Update(float elapsed,float playerDistance,const RailPath& rail,
        float radius,const WallTrace& trace) {
    cues_.clear(); muzzleFlash_=0;
    if(!std::isfinite(elapsed) || !std::isfinite(playerDistance) || !std::isfinite(radius) || radius<=0 || rail.Length()<=0) return;
    // Cache wall contacts before the shot, so the aircraft has time to aim.
    // Terrain is queried at most twice per shot, never for every READY frame.
    for(size_t i=0;i<ShotTimes.size();++i) {
        if(planAttempted_[i] || elapsed<ShotTimes[i]-0.28f) continue;
        planAttempted_[i]=true;
        if(elapsed>ShotTimes[i] || !trace) continue;
        const float d=playerDistance+RailTitleScene::Speed*(ShotTimes[i]-elapsed);
        const auto f=EvaluateRailReadyDroneFlight(ShotTimes[i],rail,d,RailTitleScene::Speed);
        const auto pose=BuildRailReadyDronePose(rail,d+f.forward,f.lateral,f.vertical,f.rotation,radius);
        planned_[i]=trace(Muzzle(pose,static_cast<uint32_t>(i)),d,static_cast<uint32_t>(i),targets_[i],normals_[i]);
    }
    for(size_t i=0;i<ShotTimes.size();++i) {
        const float age=elapsed-ShotTimes[i];
        if(age>=0 && age<0.09f && launched_[i]) muzzleFlash_=(std::max)(muzzleFlash_,1.0f-age/0.09f);
        if(attempted_[i] || elapsed<ShotTimes[i]) continue;
        attempted_[i]=true;
        if(elapsed>=RailTitleGameplayEntry::Duration || age>0.10f || !trace || !planned_[i]) continue;
        const float launchDistance=playerDistance-RailTitleScene::Speed*age;
        auto flight=Flight(ShotTimes[i],launchDistance,rail,radius);
        auto pose=BuildRailReadyDronePose(rail,launchDistance+flight.forward,
            flight.lateral,flight.vertical,flight.rotation,radius);
        auto origin=Muzzle(pose,static_cast<uint32_t>(i));
        Vector3 target{},normal{};
        if(!trace(origin,launchDistance,static_cast<uint32_t>(i),target,normal)) continue;
        const auto delta=Add(target,Scale(origin,-1));
        if(Dot(delta,delta)<16 || !std::isfinite(Dot(delta,delta)) || Dot(normal,normal)<0.5f) continue;
        // Reconcile the small muzzle displacement caused by aiming the body.
        targets_[i]=target;
        flight=Flight(ShotTimes[i],launchDistance,rail,radius);
        pose=BuildRailReadyDronePose(rail,launchDistance+flight.forward,
            flight.lateral,flight.vertical,flight.rotation,radius);
        origin=Muzzle(pose,static_cast<uint32_t>(i));
        RailTitleShot shot;
        shot.id=static_cast<uint32_t>(i+1); shot.origin=shot.position=origin; shot.target=target;
        shot.duration=std::sqrt(Dot(Add(target,Scale(origin,-1)),Add(target,Scale(origin,-1))))/ShotSpeed;
        shot.age=age;
        if(!ClearsVehicle(shot,rail,launchDistance)) continue;
        normals_[i]=normal; durations_[i]=shot.duration; launched_[i]=true; shots_.push_back(shot);
        cues_.push_back({RailTitleAttackCueKind::Muzzle,origin}); muzzleFlash_=1;
    }
    for(auto& shot:shots_) {
        shot.age=elapsed-ShotTimes[shot.id-1];
        const float u=(std::clamp)(shot.age/shot.duration,0.0f,1.0f);
        shot.position=Add(shot.origin,Scale(Add(shot.target,Scale(shot.origin,-1)),u));
        if(shot.age>=shot.duration) {
            auto& impact=impacts_[shot.id-1];
            impact={shot.target,shot.age-shot.duration,8041u+shot.id,true,normals_[shot.id-1],true};
            cues_.push_back({RailTitleAttackCueKind::WallImpact,shot.target});
        }
    }
    std::erase_if(shots_,[](const auto& shot){return shot.age>=shot.duration;});
    for(size_t i=0;i<impacts_.size();++i) if(impacts_[i].seed) {
        impacts_[i].age=elapsed-ShotTimes[i]-durations_[i];
        impacts_[i].active=impacts_[i].age<1.5f;
    }
    if(muzzleFlash_>0) {
        const auto flight=Flight(elapsed,playerDistance,rail,radius);
        const auto pose=BuildRailReadyDronePose(rail,playerDistance+flight.forward,
            flight.lateral,flight.vertical,flight.rotation,radius);
        muzzlePosition_=Muzzle(pose,launched_[1]?1:0);
    }
}

RailReadyCameraShot EvaluateRailReadyCameraShot(float elapsed) {
    const float t=std::isfinite(elapsed) ? (std::max)(0.0f,elapsed) : 0.0f;
    const auto smooth=[&](float a,float b) {
        const float u=(std::clamp)((t-a)/(b-a),0.0f,1.0f);
        return u*u*(3.0f-2.0f*u);
    };
    RailReadyCameraShot shot;
    const float reveal=smooth(0.0f,0.64f);
    const float angle=-2.65f*reveal*(1.0f-smooth(1.65f,2.75f));
    const float radius=8.0f+6.0f*reveal+8.0f*smooth(1.85f,2.85f);
    shot.eyeOffset={std::sin(angle)*radius,
        5.2f+2.0f*reveal+2.8f*smooth(1.80f,2.85f),-std::cos(angle)*radius};
    const auto flight=EvaluateRailReadyDroneFlight(t);
    shot.droneOffset={flight.lateral,flight.vertical,flight.forward};
    shot.focusBlend=reveal;
    shot.weight=1.0f-smooth(2.50f,RailTitleGameplayEntry::CameraSettleEnd);
    shot.fovY=0.70f+0.28f*reveal;
    return shot;
}

CourseEnemyActorDesc BuildRailReadyDrone(float startDistance) {
    CourseEnemyActorDesc desc;
    desc.waveId = desc.actorAssetId = "start_sentinel";
    desc.meshId = "twin_shield_hull";
    desc.spawnDistance = startDistance;
    const auto flight=EvaluateRailReadyDroneFlight(0.0f);
    desc.distanceOffset = flight.forward;
    desc.lateralOffset = flight.lateral;
    desc.verticalOffset = flight.vertical;
    desc.radius = 1.65f;
    desc.hitPoints = 32.0f;
    desc.lifetime = 3600.0f;
    desc.firstShotDelay = 1.0f;
    desc.fireInterval = 1.5f;
    desc.bulletRadius = 0.42f;
    desc.localRotation = flight.rotation;
    desc.combatDefinition = EnemyCombatDefinition::CommercialStandard();
    desc.combatDefinition.maximumHitPoints = desc.hitPoints;
    desc.combatDefinition.spawnDurationSeconds = 0.0f;
    desc.combatDefinition.engageDurationSeconds = 0.4f;
    desc.behaviorDefinition = EnemyBehaviorDefinition::Commercial(EnemyBehaviorArchetype::Assault);
    desc.behaviorDefinition.entryDurationSeconds = 0.0f;
    desc.behaviorDefinition.maintainForwardEngagementBand = true;
    desc.behaviorDefinition.lateralAmplitude = 1.4f;
    desc.behaviorDefinition.verticalAmplitude = 0.45f;
    desc.formationDefinition = EnemyFormationDefinition::CommercialDefault("start_sentinel");
    desc.establishedEntrance = true;
    return desc;
}
void RailTitleGameplayEntry::Begin() {
    if(begun_) return;
    begun_=true;
    elapsed_=0.0;
    frame_={};
    frame_.blackout=1.0f;
    frame_.cameraHandoffWeight=1.0f;
    frame_.reticleProgress=frame_.gaugeProgress=frame_.informationOpacity=frame_.rollingHandoff=0.0f;
    frame_.blockingGameplay=true;
    frame_.enemyFireAllowed=false;
    frame_.active=true;
}
void RailTitleGameplayEntry::Update(float deltaTime) {
    frame_.reticleStarted=frame_.gaugesStarted=false;
    frame_.readyStarted=frame_.goStarted=false;
    frame_.travelDeltaTime=0.0f;
    if(!begun_ || !frame_.active) return;
    const double previous=elapsed_;
    if(std::isfinite(deltaTime)) elapsed_=(std::min)(double(GoEnd),
        elapsed_+double((std::clamp)(deltaTime,0.0f,0.05f)));
    frame_.travelDeltaTime=static_cast<float>((std::min)(elapsed_,double(Duration))-
        (std::min)(previous,double(Duration)));
    const auto ramp=[&](double from,double to) {
        const float t=static_cast<float>((std::clamp)((elapsed_-from)/(to-from),0.0,1.0));
        return t*t*(3.0f-2.0f*t);
    };
    frame_.blackout=1.0f-static_cast<float>((std::clamp)(
        (elapsed_-RevealBegin)/double(RevealEnd-RevealBegin),0.0,1.0));
    // Begin while the falling veil is still dense. Camera recovery and rail
    // motion overlap the reveal rather than waiting for a clear, frozen scene.
    frame_.cameraHandoffWeight=1.0f-ramp(double(CameraBegin),double(CameraEnd));
    frame_.reticleProgress=ramp(0.82,1.38);
    frame_.gaugeProgress=ramp(1.08,1.74);
    frame_.informationOpacity=ramp(1.60,2.25);
    frame_.rollingHandoff=ramp(0.12,double(Duration));
    frame_.reticleStarted=previous<0.82 && elapsed_>=0.82;
    frame_.gaugesStarted=previous<1.08 && elapsed_>=1.08;
    frame_.readyProgress=ramp(double(ReadyBegin),double(Duration));
    frame_.readyOpacity=ramp(double(ReadyBegin),double(ReadyBegin+0.18f))*(1.0f-ramp(double(Duration-0.20f),double(Duration)));
    frame_.goOpacity=elapsed_>=double(Duration) ? 1.0f-ramp(double(Duration+0.18f),double(Duration+0.50f)) : 0.0f;
    frame_.droneTurnProgress=ramp(double(DroneTurnBegin),double(DroneTurnEnd));
    frame_.readyStarted=previous<double(ReadyBegin) && elapsed_>=double(ReadyBegin);
    frame_.goStarted=previous<double(Duration) && elapsed_>=double(Duration);
    frame_.blockingGameplay=elapsed_<double(Duration);
    frame_.enemyFireAllowed=elapsed_>=double(EnemyFireBegin);
    frame_.active=elapsed_<double(GoEnd);
}
std::vector<CourseEventMarker> RailTitleGameplayEntry::DispatchCourseEvents(
    const std::vector<CourseEventMarker>& crossed, bool gameplayEnabled) {
    pendingEvents_.insert(pendingEvents_.end(),crossed.begin(),crossed.end());
    if(!gameplayEnabled) return {};
    std::vector<CourseEventMarker> dispatched;
    dispatched.swap(pendingEvents_);
    return dispatched;
}

void RailTransitionDemo::BeginGameplay() {
    phase_=Phase::Playing; playTime_=transitionTime_=0.0; travelDeltaTime_=0.0f;
}
void RailTransitionDemo::Update(float deltaTime, bool gameplayStarted) {
    travelDeltaTime_=0.0f;
    const double dt=std::isfinite(deltaTime) ? double((std::clamp)(deltaTime,0.0f,0.05f)) : 0.0;
    if(phase_==Phase::Playing && gameplayStarted) playTime_+=dt;
    if(phase_==Phase::Covering) {
        const double previous=transitionTime_;
        transitionTime_=(std::min)(double(CoverDuration),transitionTime_+dt);
        travelDeltaTime_=static_cast<float>(transitionTime_-previous);
    } else if(phase_==Phase::Revealing) {
        transitionTime_=(std::min)(double(RevealDuration),transitionTime_+dt);
        if(transitionTime_>=double(RevealDuration)) phase_=Phase::Title;
    }
}
bool RailTransitionDemo::RequestReturn() {
    if(!ButtonVisible()) return false;
    phase_=Phase::Covering; transitionTime_=0.0; travelDeltaTime_=0.0f;
    return true;
}
bool RailTransitionDemo::NeedsTitleSwap() const {
    return phase_==Phase::Covering && transitionTime_>=double(CoverDuration) && Coverage()>=0.999f;
}
void RailTransitionDemo::CommitTitleSwap() {
    if(!NeedsTitleSwap()) return;
    phase_=Phase::Revealing; transitionTime_=0.0; travelDeltaTime_=0.0f;
}
bool RailTransitionDemo::Returning() const { return phase_==Phase::Covering || phase_==Phase::Revealing; }
bool RailTransitionDemo::RevealingTitle() const { return phase_==Phase::Revealing; }
bool RailTransitionDemo::ButtonVisible() const { return phase_==Phase::Playing && playTime_>=double(ButtonDelay); }
float RailTransitionDemo::ButtonOpacity() const {
    if(!ButtonVisible()) return 0.0f;
    const float t=static_cast<float>((std::clamp)((playTime_-ButtonDelay)/0.25,0.0,1.0));
    return t*t*(3.0f-2.0f*t);
}
float RailTransitionDemo::Coverage() const {
    if(phase_==Phase::Covering) return RailTitleScene::SandCoverageAtAge(static_cast<float>(transitionTime_));
    if(phase_==Phase::Revealing) return 1.0f-static_cast<float>((std::clamp)(
        (transitionTime_-RailTitleGameplayEntry::RevealBegin)/
        double(RailTitleGameplayEntry::RevealEnd-RailTitleGameplayEntry::RevealBegin),0.0,1.0));
    return 0.0f;
}
float RailTransitionDemo::SandAge() const {
    return phase_==Phase::Revealing || phase_==Phase::Title
        ? CoverDuration+static_cast<float>(transitionTime_) : static_cast<float>(transitionTime_);
}
float RailTransitionDemo::DustStrength() const {
    if(phase_==Phase::Revealing) return 0.68f*(std::clamp)(Coverage()/0.65f,0.0f,1.0f);
    if(phase_!=Phase::Covering) return 0.0f;
    const float t=(std::clamp)((static_cast<float>(transitionTime_)-0.10f)/1.10f,0.0f,1.0f);
    return 0.68f*t*t*(3.0f-2.0f*t);
}
bool RailTransitionDemo::HitTest(float x,float y,float width,float height) {
    const float s=(std::min)(width/1600.0f,height/900.0f);
    if(s<=0.0f) return false;
    return std::abs(x-width*0.5f)<=180.0f*s && std::abs(y-height*0.5f)<=46.0f*s;
}

RailSandTransitionAudioFrame RailSandTransitionAudioClock::Update(bool active,float age,bool focused) {
    RailSandTransitionAudioFrame frame;
    if(!std::isfinite(age) || age<0) active=false;
    if(active && (!wasActive_ || age+0.001f<previousAge_)) started_=false;
    if(active && focused && !started_) {frame.start=true;started_=true;}
    frame.paused=!focused;
    frame.pauseChanged=frame.paused!=paused_;
    paused_=frame.paused;
    previousAge_=active ? age : 0.0f;
    wasActive_=active;
    return frame;
}
