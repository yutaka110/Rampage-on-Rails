#pragma once

#include "CourseSpawnRuntime.h"
#include "RailMothershipPresentation.h"
#include "utils/math/MathUtils.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <string_view>

namespace RailRouteHazards {
inline constexpr std::string_view BarricadeId="route_break_barricade";
inline constexpr std::string_view SlabId="route_arch_slab";
inline constexpr float SlabTrigger=1689.0f;
inline constexpr float BoltLaunch=.12f, BoltImpact=.39f;
inline constexpr float SupportBreak=.98f, SlipSound=1.52f, GroundImpact=2.30f;
inline constexpr float LandingDustDelay=.16f, LandingDustSeconds=1.40f;
inline constexpr Vector3 ShoulderOpeningOffset{-2.2f,-1.5f,-.70f};
inline constexpr float BarricadeHealth=72.0f;
inline constexpr float BarricadeDebrisSeconds=.78f;

inline bool IsBarricade(const CourseObstacleActor& actor) { return actor.desc.id==BarricadeId; }
inline bool IsSlab(const CourseObstacleActor& actor) { return actor.desc.id==SlabId; }

inline void OpenBarricade(CourseObstacleActor& actor) {
    actor.desc.hitPoints=0;
    actor.desc.lifetime=BarricadeDebrisSeconds;
    actor.age=0;
}

inline float Ramp(float time,float start,float end) {
    return (std::clamp)((time-start)/(end-start),0.0f,1.0f);
}

inline float Ease(float time,float start,float end) {
    const float u=Ramp(time,start,end);
    return u*u*(3-2*u);
}

struct SlabPose {
    Vector3 rail{}; // lateral, vertical, distance
    float roll=0;
};

inline SlabPose Stone(const CourseObstacleActor& actor,float age) {
    const float crack=Ease(age,BoltImpact,1.39f);
    const float crossing=Ease(age,1.77f,2.34f);
    const float settle=Ease(age,2.23f,2.66f);
    return {{actor.desc.lateralOffset-.42f*crack-15.0f*crossing,
        actor.desc.verticalOffset-.44f*crack-4.75f*Ease(age,1.52f,1.99f)-5.2f*settle,
        actor.desc.spawnDistance+actor.desc.distanceOffset},
        -.23f*crack-.78f*crossing};
}

inline Vector3 Strike(const CourseObstacleActor& actor) {
    // Front face of the lower shear joint in RouteArchRoots.
    return {actor.desc.lateralOffset+1.90f+ShoulderOpeningOffset.x,
        actor.desc.verticalOffset-2.01f+ShoulderOpeningOffset.y,
        actor.desc.spawnDistance+actor.desc.distanceOffset-2.92f+ShoulderOpeningOffset.z};
}

inline constexpr float ArchDistance=1755.0f;
inline constexpr Vector3 HeavyMount{20.0f,-11.8f,-62.0f};
inline constexpr float HeavyGunScale=3.85f;

inline RailMothership::Frame CollapseCarrier(const RailPath& path) {
    RailMothership::Frame carrier;
    carrier.visible=true;
    carrier.railDistance=ArchDistance+133.0f;
    carrier.railVertical=50.0f;
    const auto sample=path.EvaluateExtended(carrier.railDistance);
    carrier.right=sample.right;carrier.up=sample.up;carrier.forward=sample.tangent;
    carrier.center=RailMothership::Add(sample.position,
        RailMothership::Add(RailMothership::Scale(sample.right,21.0f),RailMothership::Scale(sample.up,carrier.railVertical)));
    return carrier;
}

inline float HeavyPassWeight(float distance) {
    return Ease(distance,1638,1681)*(1-Ease(distance,1771,1865));
}

inline float HeavyCharge(float distance,float age) {
    if(age>BoltLaunch)return 0;
    return .87f*Ease(distance,1660,SlabTrigger)+.13f*Ramp(age,0,BoltLaunch);
}

inline Vector3 HeavyGunRotation(const RailMothership::Frame& carrier,Vector3 target) {
    const auto mount=carrier.World(HeavyMount);
    const Vector3 delta{target.x-mount.x,target.y-mount.y,target.z-mount.z};
    const auto dot=[&](Vector3 axis){return delta.x*axis.x+delta.y*axis.y+delta.z*axis.z;};
    const float x=dot(carrier.right),y=dot(carrier.up),z=dot(carrier.forward);
    return {std::atan2(y,std::hypot(x,z)),std::atan2(-x,-z),0};
}

inline Vector3 HeavyGunLocal(Vector3 rotation,Vector3 point) {
    const auto gun=MakeAffineMatrix(Vector3{HeavyGunScale,HeavyGunScale,HeavyGunScale},rotation,HeavyMount);
    return {point.x*gun.m[0][0]+point.y*gun.m[1][0]+point.z*gun.m[2][0]+gun.m[3][0],
        point.x*gun.m[0][1]+point.y*gun.m[1][1]+point.z*gun.m[2][1]+gun.m[3][1],
        point.x*gun.m[0][2]+point.y*gun.m[1][2]+point.z*gun.m[2][2]+gun.m[3][2]};
}

inline Vector3 HeavyMuzzle(const RailMothership::Frame& carrier,Vector3 target) {
    return carrier.World(HeavyGunLocal(HeavyGunRotation(carrier,target),{0,.32f,-2.10f}));
}

inline constexpr float FractureStart=.46f, FractureEnd=.91f;
inline constexpr float ShoulderLandingRoll=.58f, ShelfHeight=-.83f;

inline Vector2 RotateRockPoint(float x,float y,float roll) {
    const float s=std::sin(roll),c=std::cos(roll);
    return {x*c-y*s,x*s+y*c};
}

inline Vector3 LandingContact(const CourseObstacleActor& actor) {
    const auto slab=Stone(actor,GroundImpact);
    // Lowest vertex of the authored shoulder at impact. The shelf is part
    // of the stationary canyon mesh.
    const auto edge=RotateRockPoint(-1.028f,-1.481f,ShoulderLandingRoll);
    return {slab.rail.x+edge.x,ShelfHeight,slab.rail.z-.905f};
}

inline SlabPose VisibleStone(const CourseObstacleActor& actor,float age) {
    auto slab=Stone(actor,age);
    // Hinge on the surviving lower lip until it tears, then continue the
    // same rotation through the fall. Stone() remains the vehicle sweep.
    const float tip=.23f*Ease(age,1.08f,1.70f);
    const auto lip=RotateRockPoint(1.9f,-1.6f,tip);
    const float free=Ease(age,1.57f,1.86f);
    slab.rail.x=std::lerp(actor.desc.lateralOffset+1.9f-lip.x,slab.rail.x,free);
    slab.rail.y=std::lerp(actor.desc.verticalOffset-1.6f-lip.y,slab.rail.y,free);
    // Start in the arch opening and blend onto the established crossing.
    slab.rail.x+=ShoulderOpeningOffset.x*(1-Ease(age,1.84f,2.10f));
    slab.rail.y+=ShoulderOpeningOffset.y*(1-Ease(age,1.63f,1.94f));
    slab.rail.z+=ShoulderOpeningOffset.z*(1-Ease(age,1.75f,2.18f));
    slab.roll=tip+(ShoulderLandingRoll-.23f)*Ease(age,1.59f,GroundImpact);
    if(age>=GroundImpact) {
        slab.roll+=.11f*Ease(age,GroundImpact,2.50f)-.035f*Ease(age,2.50f,2.72f);
    }
    const auto edge=RotateRockPoint(-1.028f,-1.481f,slab.roll);
    const float floor=ShelfHeight-edge.y;
    // Carry downward speed into contact, then let the shelf arrest the fall.
    const float drop=Ramp(age,2.12f,GroundImpact);
    slab.rail.y=std::lerp(slab.rail.y,floor,drop*drop);
    return slab;
}

inline float ChipRelease(unsigned index) {return .98f+.15f*float(index);}
inline constexpr float ChipFlight=.79f;
inline Vector3 CrossingChip(const CourseObstacleActor& actor,unsigned index,float age) {
    const float release=ChipRelease(index);
    const float t=Ramp(age,release,release+ChipFlight);
    const auto origin=VisibleStone(actor,release).rail;
    return {origin.x-.50f-(.55f+.43f*index)*t,
        std::lerp(origin.y-.99f,.14f,t*t),
        origin.z-1.85f-.45f*index-.52f*t};
}
}
