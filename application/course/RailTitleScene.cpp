#include "RailTitleScene.h"
#include <algorithm>
#include <cmath>

namespace {
constexpr float Pi = 3.14159265358979323846f;
Vector3 Add(Vector3 a, Vector3 b) { return {a.x+b.x,a.y+b.y,a.z+b.z}; }
Vector3 Scale(Vector3 a, float s) { return {a.x*s,a.y*s,a.z*s}; }

}

bool RailTitleScene::Initialize() {
    travel_ = 0.0;
    startTravel_ = 0.0; startDistance_ = 0.0f;
    starting_ = false; startTime_ = age_ = 0.0f;
    shots_.clear(); attackCues_.clear();
    dustClouds_.clear();
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
        const float radius = 19.8494f + (ChaseDistance()-19.8494f)*blend;
        cameraPosition_ = Add(sample.position,Add(Scale(sample.tangent,std::cos(angle)*radius),
            Scale(sample.right,std::sin(angle)*radius)));
        cameraPosition_.y += 8.0f+(5.2f-8.0f)*blend;
        // Keep the distant cave centered along the straight departure rail.
        const float remaining=TunnelLead-static_cast<float>(travel_-startTravel_);
        const float lookAhead=(std::clamp)(remaining,14.0f,38.0f);
        const Vector3 chaseTarget = Add(path_.Evaluate(state_.distance+lookAhead).position,Vector3{0,2.0f,0});
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
    // Lead the moving cart, then deliberately miss onto the flat sand corridor.
    // Alternate sides of the rail, keeping every impact away from the sleepers.
    constexpr float lanes[]{-5.0f,5.5f,-4.5f};
    const auto aim = path_.Evaluate(state_.distance+Speed*0.78f-2.0f+burstShot_*1.2f);
    pursuerAimTarget_ = Add(aim.position,Scale(aim.right,lanes[burstShot_]));
    pursuerAimTarget_.y = -0.27f; // Ground top is y=-0.30 in this corridor.
    const Vector3 direction = Add(pursuerAimTarget_,Scale(pursuer_.position,-1.0f));
    const float length = std::sqrt(direction.x*direction.x+direction.y*direction.y+direction.z*direction.z);
    // Imported model points along -Z. Aim at the cart rather than the camera.
    const Vector3 desiredRotation{std::asin((std::clamp)(direction.y/(std::max)(length,0.001f),-1.0f,1.0f)),
        std::atan2(-direction.x,-direction.z),0.16f*std::sin(age_*0.8f)};
    if(state_.revision<=1) pursuer_.rotation=desiredRotation;
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
}

void RailTitleScene::UpdateAttack(float deltaTime) {
    attackCues_.clear();
    for(auto& cloud:dustClouds_) cloud.age+=deltaTime;
    std::erase_if(dustClouds_,[](const auto& cloud){return cloud.age>=RailTitleDustCloud::Lifetime;});
    muzzleFlashTime_ = (std::max)(0.0f,muzzleFlashTime_-deltaTime);
    // Existing shots continue in world space while the cart/camera moves.
    for(auto& shot : shots_) {
        shot.age += deltaTime;
        const float t = (std::clamp)(shot.age/shot.duration,0.0f,1.0f);
        shot.position = Add(Scale(shot.origin,1-t),Scale(shot.target,t));
        if(t>=1.0f) {
            attackCues_.push_back({RailTitleAttackCueKind::GroundImpact,shot.target});
            dustClouds_.push_back({shot.target,0.0f,shot.id});
        }
    }
    std::erase_if(shots_,[](const auto& shot){return shot.age>=shot.duration;});
    if(Blackout()>=0.2f) { shots_.clear(); attackCues_.clear(); dustClouds_.clear(); }
    attackTime_ += deltaTime;
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
    startTime_ = 0.0f;
    startTravel_=travel_; startDistance_=state_.distance;
    CourseTerrainPlacement tunnel;
    tunnel.id="title_start_tunnel"; tunnel.meshId="title_tunnel";
    tunnel.distance=400.0f+TunnelLead;
    tunnel.scale={1,1,1};
    tunnel.cullAheadDistance=tunnel.cullBehindDistance=5000.0f;
    scenery_.terrainPlacements.push_back(tunnel);
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
    // Wait until the chase camera has crossed the mouth, not just the cart.
    const float t=(std::clamp)((TunnelCameraDepth()-2.0f)/2.5f,0.0f,1.0f);
    return t*t*(3.0f-2.0f*t);
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
