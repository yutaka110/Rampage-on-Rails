#pragma once

#include "CourseSpawnRuntime.h"
#include "RailGroundBatteryAttack.h"
#include "RailMothershipPresentation.h"
#include "utils/math/MathUtils.h"
#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <string_view>

namespace RailRouteHazards {
inline constexpr std::string_view BarricadeId="route_break_barricade";
inline constexpr std::string_view SlabId="route_arch_slab";
inline constexpr std::string_view ShelfSlabId="route_shelf_slab";
inline constexpr std::string_view ArchBatteryWave="arch_inner_batteries";
using RailGroundBattery::ShelfBatteryWave;
inline constexpr float ArchPlatformTop=-2.25f;
inline constexpr std::string_view SlabShotPrefix="slab-shot:";
inline constexpr float SlabTrigger=1689.0f;
inline constexpr float BoltLaunch=.12f, BoltImpact=.39f;
inline constexpr float SupportBreak=.64f, SlipSound=.85f, GroundImpact=1.63f;
inline constexpr float LandingDustDelay=.16f, LandingDustSeconds=1.40f;
inline constexpr float BarricadeHealth=72.0f;
inline constexpr float BarricadeDebrisSeconds=.78f;
inline constexpr float BarricadeBeaconHeight=5.16f, BarricadePostOffset=.46f;

inline bool IsBarricade(const CourseObstacleActor& actor) { return actor.desc.id==BarricadeId; }
inline bool IsShelf(const CourseObstacleActor& actor) { return actor.desc.id==ShelfSlabId; }
inline bool IsSlab(const CourseObstacleActor& actor) { return actor.desc.id==SlabId || IsShelf(actor); }
inline bool IsRockfallBattery(std::string_view wave) { return wave==ArchBatteryWave || wave==ShelfBatteryWave; }

inline bool CarrierStrikesSlab(const CourseObstacleActor& actor) {
    // The original setpiece uses an empty payload. Later arches wait for a shot.
    return !IsShelf(actor) && actor.desc.payload.empty();
}

inline float SlabAdvance(const CourseObstacleActor& actor,float distance,float dt) {
    if(actor.age<=0 && (!CarrierStrikesSlab(actor) || distance<SlabTrigger))return 0;
    return IsShelf(actor)?1.55f*dt:dt;
}

inline uint64_t SlabTriggerShot(const CourseObstacleActor& actor) {
    const std::string_view payload=actor.desc.payload;
    if(!payload.starts_with(SlabShotPrefix))return 0;
    uint64_t shot=0;
    const auto parsed=std::from_chars(payload.data()+SlabShotPrefix.size(),payload.data()+payload.size(),shot);
    return parsed.ec==std::errc{} && parsed.ptr==payload.data()+payload.size()?shot:0;
}

inline bool SlabCanBeReleased(const CourseObstacleActor& actor) {
    return actor.age<BoltImpact && SlabTriggerShot(actor)==0;
}

inline void ReleaseSlab(CourseObstacleActor& actor,uint64_t shotId) {
    // The slab payload travels with the obstacle through checkpoint capture.
    actor.desc.payload=std::string(SlabShotPrefix)+std::to_string(shotId);
    actor.age=BoltImpact;
}

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
    const float drop=Ramp(age,SupportBreak,GroundImpact);
    return {{actor.desc.lateralOffset,
        std::lerp(actor.desc.verticalOffset,actor.desc.halfExtents.y,drop*drop),
        actor.desc.spawnDistance+actor.desc.distanceOffset},0};
}

inline Vector3 Strike(const CourseObstacleActor& actor) {
    return {actor.desc.lateralOffset,actor.desc.verticalOffset-1.4f,
        actor.desc.spawnDistance+actor.desc.distanceOffset-actor.desc.halfExtents.z};
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

inline constexpr float FractureStart=.40f, FractureEnd=SupportBreak;

inline Vector3 LandingContact(const CourseObstacleActor& actor) {
    const auto slab=Stone(actor,GroundImpact);
    return {slab.rail.x,0,slab.rail.z};
}

inline SlabPose VisibleStone(const CourseObstacleActor& actor,float age) {
    return Stone(actor,age);
}

inline float ChipRelease(unsigned index) {return SupportBreak+.11f*float(index);}
inline constexpr float ChipFlight=.90f;
inline Vector3 CrossingChip(const CourseObstacleActor& actor,unsigned index,float age) {
    const float release=ChipRelease(index);
    const float t=Ramp(age,release,release+ChipFlight);
    const auto origin=VisibleStone(actor,release).rail;
    const float side=index%2==0?-1.0f:1.0f;
    return {origin.x+side*(4.7f+index*1.45f+.8f*t),
        std::lerp(origin.y-4.0f,.14f,t*t),
        origin.z-2.4f-.42f*index-.52f*t};
}
}
