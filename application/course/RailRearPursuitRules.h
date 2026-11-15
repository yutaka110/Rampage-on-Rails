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
inline constexpr float kFangHullCounterDamageMultiplier = 1.5f;
inline constexpr float kVeilSensorHitPoints = 24.0f;
inline constexpr uint32_t kVeilSensorHitSource = UINT32_MAX - 2;
inline constexpr const char* kVeilBeamSource = "rear_veil_beam";
inline Vector3 RearSubsystemWorldCenter(const TwinShieldDronePose& pose, Vector3 local) {
    const auto world=BuildTwinShieldDroneWorldMatrix(pose);
    return {local.x*world.m[0][0]+local.y*world.m[1][0]+local.z*world.m[2][0]+world.m[3][0],
        local.x*world.m[0][1]+local.y*world.m[1][1]+local.z*world.m[2][1]+world.m[3][1],
        local.x*world.m[0][2]+local.y*world.m[1][2]+local.z*world.m[2][2]+world.m[3][2]};
}
inline Vector3 VeilSensorWorldCenter(const TwinShieldDronePose& pose) {
    return RearSubsystemWorldCenter(pose,{-1.10f,.38f,-.68f});
}
inline bool IsRearSubsystemHit(uint32_t source) {
    return source==kVeilSensorHitSource;
}
inline constexpr uint32_t kRearPanelSourceBase = UINT32_MAX - 10;
inline constexpr std::array<Vector3,3> kRearPanelAnchors{{
    {-1.64f,.31f,-.65f}, {0,.84f,-.32f}, {1.64f,.31f,-.65f}
}};
inline bool IsRearPanelHit(uint32_t source) {
    return source>=kRearPanelSourceBase && source-kRearPanelSourceBase<kRearPanelAnchors.size();
}
inline bool IsFangWingPanel(std::string_view actorAssetId,size_t panel) {
    return actorAssetId=="rear_fang" && (panel==0 || panel==2);
}

struct RailRearArmor {
    static constexpr float RebuildSeconds=6.0f;
    static constexpr float FangWingRebuildSeconds=9.0f;
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
    bool Break(size_t index,bool fangWing=false) {
        if(!HasPanel(index)) return false;
        if(intact==FullMask) rebuildRemaining=RebuildSeconds;
        if(fangWing) rebuildRemaining=FangWingRebuildSeconds;
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
inline constexpr char kFangAimVolleyRound[] = "enemy_fang_aim_volley";
inline constexpr char kFangAimVolleyVisual[] = "visual_fang_aim_volley";
// Lane offsets are in rail coordinates, with positive lateral on screen left
// while the camera is looking back at the pursuing aircraft.
inline constexpr std::array<const char*,3> kFangAimVolleyRoles{
    "fang_aim_left","fang_aim_high","fang_aim_right"
};
inline constexpr std::array<Vector2,3> kFangAimVolleyExtents{{
    {4.9f,.5f},{0,4.7f},{-4.9f,.5f}
}};
inline int FangAimSalvo(std::string_view role) {
    for(size_t i=0;i<kFangAimVolleyRoles.size();++i)
        if(role==kFangAimVolleyRoles[i])return int(i);
    return -1;
}
inline Vector2 FangAimVolleyOffset(int salvo,float progress) {
    const float arc=4.0f*progress*(1.0f-progress);
    const auto extent=kFangAimVolleyExtents[salvo];
    return {extent.x*arc*arc,extent.y*arc*arc};
}
inline bool IsFangChaseRound(std::string_view id) { return id=="enemy_fang_chase" || id==kFangAimVolleyRound; }
inline constexpr char kFangCrossfireRole[] = "fang_crossfire";
inline constexpr float kFangCrossfireHalfWidth = .70f;
inline constexpr float kElitePursuerModelScale = 3.0f;
inline float RailEnemyEngagementSign(std::string_view actorAssetId) {
    return IsRailRearPursuer(actorAssetId)?-1.0f:1.0f;
}
