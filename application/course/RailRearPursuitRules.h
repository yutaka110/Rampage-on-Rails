#pragma once
#include "TwinShieldDronePose.h"
#include <string_view>
#include <array>

// A rear escort uses the same positive engagement-band distances, measured
// behind the cart. Ordinary actors retain their existing forward contract.
inline bool IsRailRearPursuer(std::string_view actorAssetId) {
    return actorAssetId=="short_rear_pursuer" || actorAssetId=="rear_fang" || actorAssetId=="rear_veil";
}
inline bool IsRailElitePursuer(std::string_view id) { return id=="rear_fang" || id=="rear_veil"; }
inline constexpr float kFangCannonHitPoints = 36.0f;
inline constexpr uint32_t kFangCannonHitSource = UINT32_MAX - 1;
inline constexpr float kVeilSensorHitPoints = 24.0f;
inline constexpr uint32_t kVeilSensorHitSource = UINT32_MAX - 2;
inline constexpr const char* kVeilBeamSource = "rear_veil_beam";
inline Vector3 RearSubsystemWorldCenter(const TwinShieldDronePose& pose, Vector3 local) {
    const auto world=BuildTwinShieldDroneWorldMatrix(pose);
    return {local.x*world.m[0][0]+local.y*world.m[1][0]+local.z*world.m[2][0]+world.m[3][0],
        local.x*world.m[0][1]+local.y*world.m[1][1]+local.z*world.m[2][1]+world.m[3][1],
        local.x*world.m[0][2]+local.y*world.m[1][2]+local.z*world.m[2][2]+world.m[3][2]};
}
inline Vector3 FangCannonWorldCenter(const TwinShieldDronePose& pose) {
    return RearSubsystemWorldCenter(pose,{1.02f,.22f,-.78f});
}
inline Vector3 VeilSensorWorldCenter(const TwinShieldDronePose& pose) {
    return RearSubsystemWorldCenter(pose,{-1.10f,.38f,-.68f});
}
inline bool IsRearSubsystemHit(uint32_t source) {
    return source==kFangCannonHitSource || source==kVeilSensorHitSource;
}
inline constexpr uint32_t kRearPanelSourceBase = UINT32_MAX - 10;
inline constexpr std::array<Vector3,3> kRearPanelAnchors{{
    {-1.64f,.31f,-.65f}, {0,.84f,-.32f}, {1.64f,.31f,-.65f}
}};
inline bool IsRearPanelHit(uint32_t source) {
    return source>=kRearPanelSourceBase && source-kRearPanelSourceBase<kRearPanelAnchors.size();
}

struct RailRearArmor {
    static constexpr float RebuildSeconds=6.0f;
    static constexpr float ExposedSeconds=1.2f;
    static constexpr uint8_t FullMask=0b111;
    bool enabled=false;
    uint8_t intact=FullMask;
    std::array<uint32_t,3> generation{1,1,1};
    float rebuildRemaining=0;
    float exposedRemaining=0;
    bool HasPanel(size_t index) const {
        return enabled && index<generation.size() && (intact & (1u<<index))!=0;
    }
    bool Break(size_t index) {
        if(!HasPanel(index)) return false;
        if(intact==FullMask) rebuildRemaining=RebuildSeconds;
        intact&=static_cast<uint8_t>(~(1u<<index));
        ++generation[index];
        if(intact==0) {
            exposedRemaining=ExposedSeconds;
            rebuildRemaining=(std::max)(rebuildRemaining,ExposedSeconds);
        }
        return true;
    }
    void Update(float dt) {
        enabled=true;
        exposedRemaining=(std::max)(0.0f,exposedRemaining-dt);
        if(intact==FullMask) return;
        rebuildRemaining=(std::max)(0.0f,rebuildRemaining-dt);
        if(rebuildRemaining==0) intact=FullMask;
    }
};
inline constexpr float kFangShotInterval = .16f;
inline constexpr char kFangCrossfireRole[] = "fang_crossfire";
inline constexpr float kFangCrossfireHalfWidth = .70f;
inline constexpr float kElitePursuerModelScale = 3.0f;
inline float RailEnemyEngagementSign(std::string_view actorAssetId) {
    return IsRailRearPursuer(actorAssetId)?-1.0f:1.0f;
}
