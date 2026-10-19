#pragma once
#include "RailLockOnTypes.h"
#include "WeaponFireSystem.h"
#include "WeaponDamageSystem.h"
#include <span>

class CourseSpawnRuntime;
class CourseCollisionSystem;
struct CourseAsset;
class RailPath;

struct RailLockLaser final {
    RailLockTargetHandle target{};
    WeaponShot weapon{};
    Vector3 start{}, end{}, position{};
    float age=0, delay=0, duration=0.4f, endedAge=-1;
    uint64_t volley=0;
    uint32_t index=0;
    bool launched=false, resolved=false, hit=false;
    float Progress() const;
    float Opacity() const;
    Vector3 Curve(float progress) const;
};
struct RailLockVolleyResult final { uint32_t tokens=0,hits=0,maxLocks=0; };

// Owns real lock-laser travel. Damage is committed at the same endpoint and
// time submitted by the world ribbon, never at button release.
class RailLockVolley final {
public:
    static constexpr size_t Capacity=64;
    void Reset();
    size_t Enqueue(std::span<const RailLockToken> tokens,std::span<const WeaponShot> weapons,
        Vector3 muzzle,const RailPath& rail,const CourseSpawnRuntime& runtime,const RailLockSettings& settings);
    void Update(float dt,Vector3 muzzle,const RailPath& rail,CourseSpawnRuntime& runtime,
        CourseCollisionSystem& collision,const CourseAsset* course);
    bool Visible() const;
    std::span<const RailLockLaser> Lasers() const {return lasers_;}
    std::span<const DamageResult> Hits() const {return hits_;}
    std::span<const RailLockVolleyResult> Completed() const {return completed_;}
    static bool TargetPosition(RailLockTargetHandle target,const RailPath& rail,
        const CourseSpawnRuntime& runtime,Vector3& position);
private:
    struct Batch {uint64_t id=0;uint32_t tokens=0,pending=0,hits=0,maxLocks=0;};
    std::vector<RailLockLaser> lasers_;
    std::vector<Batch> batches_;
    std::vector<DamageResult> hits_;
    std::vector<RailLockVolleyResult> completed_;
    uint64_t nextId_=1;
};
