#include "RailEnemyIngress.h"
#include "CourseAsset.h"
#include "../terrain/TerrainVolumeField.h"
#include <algorithm>
#include <cmath>

namespace {
Vector3 Mix(Vector3 a, Vector3 b, float t) {
    return {std::lerp(a.x,b.x,t),std::lerp(a.y,b.y,t),std::lerp(a.z,b.z,t)};
}
bool Finite(Vector3 p) { return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z); }
}

std::vector<RailEnemyIngressRoute> BuildRailEnemyIngressRoutes(
        CourseAsset& course,const RailPath& rail,const TerrainGenerationSettings& terrain) {
    // Remove the obsolete generated launch structures. Existing authored
    // scenery and the terrain surface are never changed to make an entrance.
    std::erase_if(course.terrainPlacements,[](const auto& p){return p.id.starts_with("enemy_ingress_");});
    std::vector<RailEnemyIngressRoute> routes;
    TerrainVolumeField field(rail,terrain,&course.terrainEditLayer);
    for(float distance=220;distance<(std::min)(rail.Length()-140,3530.0f);distance+=8) {
        const auto sample=rail.Evaluate(distance);
        const auto local=[&](float angle,float radius) {
            const auto w=field.SurfacePoint(distance,angle);
            const Vector3 d{w.x-sample.position.x,w.y-sample.position.y,w.z-sample.position.z};
            return Vector3{(d.x*sample.right.x+d.y*sample.right.y+d.z*sample.right.z)*radius,
                (d.x*sample.up.x+d.y*sample.up.y+d.z*sample.up.z)*radius,distance};
        };
        for(float side:{-1.0f,1.0f}) {
            unsigned variant=0;
            for(float angle:{0.52f,0.64f,0.72f,0.80f,0.94f,1.18f,1.43f}) {
                const float gate=side>0?angle:3.14159265f-angle;
                float opening=2;
                for(float along:{-3.0f,0.0f,3.0f}) for(float angular:{-0.025f,0.0f,0.025f})
                    opening=(std::min)(opening,field.OpeningMask(distance+along,gate+angular));
                if(opening<0.435f) continue; // Runtime/render cutout threshold is 0.42.
                RailEnemyIngressRoute route;
                route.id="terrain_edge_"+std::to_string(int(distance))+(side<0?"_left_":"_right_")+std::to_string(variant++);
                route.distance=distance;route.side=side;
                route.kind=field.OpenCanyonBlend(distance)<0.55f?RailEnemyIngressKind::SideOpening:
                    angle>0.85f?RailEnemyIngressKind::Cliff:RailEnemyIngressKind::Rock;
                // The launch is genuinely outside the original wall. The
                // approach stays outside until it reaches an existing cutout.
                route.start=local(side>0?0.04f:3.10159265f,1.34f);
                route.departure=local(gate,1.16f);
                route.merge=local(gate,0.52f);
                // Approach from behind the skyline rather than rocketing all
                // the way up from the canyon floor before diving back down.
                route.start.y=route.departure.y*0.48f;
                route.start.x=side*((std::max)(std::abs(route.start.x),std::abs(route.departure.x))+28.0f);
                const auto outside=field.SampleLocal(distance,route.start.x,route.start.y);
                if(outside.sdf<0.12f) continue;
                routes.push_back(std::move(route));
            }
        }
    }
    return routes;
}

namespace {
constexpr float Pi=3.14159265359f;
Vector3 Sub(Vector3 a,Vector3 b) {return {a.x-b.x,a.y-b.y,a.z-b.z};}
float Length(Vector3 p) {return std::sqrt(p.x*p.x+p.y*p.y+p.z*p.z);}
Vector3 World(const RailPath* rail,Vector3 p) {
    if(!rail) return p;
    const auto s=rail->Evaluate(p.z);
    return {s.position.x+s.right.x*p.x+s.up.x*p.y,
        s.position.y+s.right.y*p.x+s.up.y*p.y,s.position.z+s.right.z*p.x+s.up.z*p.y};
}
float Smooth(float x,float a,float b) {
    const float t=(std::clamp)((x-a)/(b-a),0.0f,1.0f);return t*t*(3-2*t);
}
float AngleDelta(float a,float b) {return std::remainder(b-a,2*Pi);}
// Integrate a cosine acceleration, cruise and longer braking phase. The
// derivative is continuous; neither end introduces a sudden velocity change.
float TravelFraction(float t) {
    constexpr float rise=0.12f,fall=0.30f,area=1-(rise+fall)*0.5f;
    if(t<rise) return rise*0.5f*(t/rise-std::sin(Pi*t/rise)/Pi)/area;
    if(t<1-fall) return (t-rise*0.5f)/area;
    const float q=(t-(1-fall))/fall;
    return (1-fall-rise*0.5f+fall*0.5f*(q+std::sin(Pi*q)/Pi))/area;
}
std::array<Vector3,6> Controls(const RailEnemyIngressState& state) {
    constexpr float split=.42f;
    const auto& gate=state.route.departure;const auto& inner=state.route.merge;
    const Vector3 tangent{(inner.x-gate.x)*.25f,(inner.y-gate.y)*.25f,0};
    return {state.route.start,
        Vector3{std::lerp(state.route.start.x,gate.x,.28f),std::lerp(state.route.start.y,gate.y,.80f),gate.z},
        Vector3{gate.x-tangent.x*split/(1-split),gate.y-tangent.y*split/(1-split),gate.z},
        gate,Vector3{gate.x+tangent.x,gate.y+tangent.y,gate.z},inner};
}
Vector3 Curve(const RailEnemyIngressState& state,float u,Vector3 target) {
    const auto controls=state.worldPrepared?state.worldControls:Controls(state);
    const auto bezier=[](Vector3 p0,Vector3 p1,Vector3 p2,Vector3 p3,float q) {
        return Mix(Mix(Mix(p0,p1,q),Mix(p1,p2,q),q),Mix(Mix(p1,p2,q),Mix(p2,p3,q),q),q);
    };
    if(u<.42f) return bezier(controls[0],controls[1],controls[2],controls[3],u/.42f);
    return bezier(controls[3],controls[4],controls[5],target,(u-.42f)/.58f);
}
Vector3 ProjectToRail(const RailPath& rail,Vector3 world,float seed) {
    // The cinematic curve lives in world space. Rail distance is a gameplay
    // coordinate, not a flight interpolator: its segment parameterization can
    // change scale abruptly at a control point.
    float distance=(std::clamp)(seed,0.0f,rail.Length());
    const auto along=[&](float at) {const auto f=rail.Evaluate(at);const auto d=Sub(world,f.position);
        return d.x*f.tangent.x+d.y*f.tangent.y+d.z*f.tangent.z;};
    for(int i=0;i<10;++i) {
        const float error=along(distance);if(std::abs(error)<.0005f) break;
        const float lo=(std::max)(0.0f,distance-.25f),hi=(std::min)(rail.Length(),distance+.25f);
        const float derivative=(along(hi)-along(lo))/(std::max)(.001f,hi-lo);
        if(std::abs(derivative)<.01f) break;
        distance=(std::clamp)(distance-(std::clamp)(error/derivative,-20.0f,20.0f),0.0f,rail.Length());
    }
    // An exterior flight can be far from its seed on a steep bend. A bounded
    // Newton step may stop on the wrong cross-section; bracket the normal
    // plane before projecting, so rendering and hit testing share a position.
    if(std::abs(along(distance))>0.005f) {
        for(float radius=32.0f;radius<=rail.Length()*2.0f;radius*=2.0f) {
            float lo=(std::max)(0.0f,distance-radius);
            float hi=(std::min)(rail.Length(),distance+radius);
            float left=along(lo),right=along(hi);
            if((left<=0 && right>=0) || (left>=0 && right<=0)) {
                for(int i=0;i<32;++i) {
                    const float mid=(lo+hi)*0.5f,value=along(mid);
                    distance=mid;
                    if(std::abs(value)<0.0005f || hi-lo<0.0005f) break;
                    if((left<=0 && value<=0) || (left>=0 && value>=0)) {lo=mid;left=value;}
                    else hi=mid;
                }
                break;
            }
            if(lo==0.0f && hi==rail.Length()) break;
        }
    }
    const auto f=rail.Evaluate(distance);const auto d=Sub(world,f.position);
    return {d.x*f.right.x+d.y*f.right.y+d.z*f.right.z,
        d.x*f.up.x+d.y*f.up.y+d.z*f.up.z,distance};
}
float ArcParameter(const RailEnemyIngressState& state,float fraction) {
    if(!state.pathPrepared || state.pathLength<=0.001f) return fraction;
    const auto& distances=state.arcDistances;
    const float distance=fraction*state.pathLength;
    const auto end=std::lower_bound(distances.begin()+1,distances.end(),distance);
    const size_t i=(std::min)(size_t(end-distances.begin()-1),distances.size()-2);
    const float width=(std::max)(0.0001f,distances[i+1]-distances[i]);
    const float q=(std::clamp)((distance-distances[i])/width,0.0f,1.0f);
    constexpr float step=1.0f/64;
    const auto slope=[&](size_t k) {
        const float before=step/(std::max)(0.0001f,distances[(std::max)(size_t(1),k)]-distances[k>0?k-1:0]);
        const float after=step/(std::max)(0.0001f,distances[(std::min)(k+1,size_t(64))]-distances[(std::min)(k,size_t(63))]);
        return 2*before*after/(before+after);
    };
    // Monotone cubic inverse avoids speed steps at the table boundaries.
    return (2*q*q*q-3*q*q+1)*(float(i)*step)+(q*q*q-2*q*q+q)*width*slope(i)+
        (-2*q*q*q+3*q*q)*(float(i+1)*step)+(q*q*q-q*q)*width*slope(i+1);
}
}

void PrepareRailEnemyIngress(RailEnemyIngressState& state,float playerDistance,float playerSpeed,const RailPath* rail) {
    state.pathPrepared=false;
    state.worldPrepared=rail!=nullptr;
    state.worldControls=Controls(state);
    if(rail) for(auto& p:state.worldControls) p=World(rail,p);
    state.duration=3.0f;
    for(int iteration=0;iteration<5;++iteration) {
        state.targetDistance=playerDistance+(std::max)(0.0f,playerSpeed)*(state.duration+state.delay)+state.forward;
        const auto target=World(rail,{state.lateral,state.vertical,state.targetDistance});
        auto previous=Curve(state,0,target);
        state.arcDistances[0]=0;
        for(size_t i=1;i<state.arcDistances.size();++i) {
            const auto next=Curve(state,float(i)/64,target);
            state.arcDistances[i]=state.arcDistances[i-1]+Length(Sub(next,previous));previous=next;
        }
        state.pathLength=state.arcDistances.back();
        if(iteration<4) state.duration=(std::clamp)(state.pathLength/(78.0f*0.79f),2.6f,4.2f);
    }
    state.pathPrepared=true;
}

RailEnemyIngressFlight EvaluateRailEnemyIngress(const RailEnemyIngressState& state,float playerDistance,const RailPath* rail) {
    RailEnemyIngressFlight flight;
    const float t=(std::clamp)((state.elapsed-state.delay)/state.duration,0.0f,1.0f);
    const float u=ArcParameter(state,TravelFraction(t));
    const Vector3 target{state.lateral,state.vertical,playerDistance+state.forward};
    if(state.worldPrepared && rail) {
        flight.worldPosition=Curve(state,u,World(rail,target));
        flight.hasWorldPosition=true;
        const float q=(std::max)(0.0f,(u-.42f)/.58f);
        const float seed=std::lerp(state.route.distance,target.z,q*q*q);
        flight.railPosition=ProjectToRail(*rail,flight.worldPosition,seed);
    } else {
        auto local=state;local.worldPrepared=false;
        flight.railPosition=Curve(local,u,target);
    }
    flight.progress=t;flight.released=t>=1;
    return flight;
}

Vector3 ResolveRailEnemyIngressRotation(const RailEnemyIngressState& state,const RailPath& rail,
        float playerDistance,float playerSpeed,Vector3 cameraPosition,bool hasCameraPosition) {
    constexpr float h=0.003f;
    auto before=state,after=state;before.elapsed=(std::max)(0.0f,state.elapsed-h);after.elapsed+=h;
    const auto current=EvaluateRailEnemyIngress(state,playerDistance,&rail);
    const auto earlier=EvaluateRailEnemyIngress(before,playerDistance-playerSpeed*h,&rail);
    const auto later=EvaluateRailEnemyIngress(after,playerDistance+playerSpeed*h,&rail);
    const auto a=earlier.hasWorldPosition?earlier.worldPosition:World(&rail,earlier.railPosition);
    const auto b=later.hasWorldPosition?later.worldPosition:World(&rail,later.railPosition);
    const auto frame=rail.Evaluate(current.railPosition.z);
    const auto local=[&](Vector3 d) {return Vector3{
        d.x*frame.right.x+d.y*frame.right.y+d.z*frame.right.z,
        d.x*frame.up.x+d.y*frame.up.y+d.z*frame.up.z,
        d.x*frame.tangent.x+d.y*frame.tangent.y+d.z*frame.tangent.z};};
    auto velocity=local(Sub(b,a));
    if(Length(velocity)<0.00001f) velocity={-state.route.side,0,0};
    Vector3 rotation{std::atan2(velocity.y,std::sqrt(velocity.x*velocity.x+velocity.z*velocity.z)),
        std::atan2(-velocity.x,-velocity.z),0};
    const auto position=current.hasWorldPosition?current.worldPosition:World(&rail,current.railPosition);
    const auto eye=hasCameraPosition?cameraPosition:World(&rail,{0,5,playerDistance-24});
    const auto aim=local(Sub(eye,position));
    const float facing=Smooth(current.progress,0.58f,0.97f);
    rotation.x=std::lerp(rotation.x,std::atan2(aim.y,std::sqrt(aim.x*aim.x+aim.z*aim.z)),facing);
    rotation.y+=AngleDelta(rotation.y,std::atan2(-aim.x,-aim.z))*facing;
    rotation.z=state.route.side*0.30f*std::sin(Pi*current.progress)*(1-facing);
    return rotation;
}

bool ValidateRailEnemyIngress(const RailEnemyIngressState& state) {
    if(!state.waiting && !state.initialized) return true;
    return Finite(state.route.start) && Finite(state.route.departure) && Finite(state.route.merge) &&
        std::isfinite(state.elapsed) && state.elapsed>=0 && std::isfinite(state.delay) && state.delay>=0 &&
        std::isfinite(state.duration) && state.duration>=0.25f && std::isfinite(state.forward) &&
        std::isfinite(state.lateral) && std::isfinite(state.vertical) &&
        std::isfinite(state.route.distance) && (state.route.side==-1 || state.route.side==1) &&
        state.route.kind<=RailEnemyIngressKind::SideOpening &&
        (!state.worldPrepared || std::all_of(state.worldControls.begin(),state.worldControls.end(),Finite)) &&
        (!state.pathPrepared || (std::isfinite(state.targetDistance) && std::isfinite(state.pathLength) &&
            state.pathLength>0 && state.arcDistances.front()==0 &&
            state.arcDistances.back()==state.pathLength &&
            std::all_of(state.arcDistances.begin(),state.arcDistances.end(),[](float x){return std::isfinite(x) && x>=0;}) &&
            std::is_sorted(state.arcDistances.begin(),state.arcDistances.end())));
}
