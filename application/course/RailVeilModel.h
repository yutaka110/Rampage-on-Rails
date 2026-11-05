#pragma once
#include "RailRearPursuitRules.h"
#include <array>

struct RailVeilAttackFrame final {
    float opening=0,charge=0,recoil=0;
};

// A visual follower of the existing beam clock. It cannot delay a shot, change
// the locked aim, or grant a different sensor hit window.
class RailVeilModelPresentation final {
public:
    void Reset(){*this={};}
    const RailVeilAttackFrame& Frame()const{return frame_;}
    void Update(float dt,bool active,bool firing,float secondsToFire) {
        if(!std::isfinite(dt) || dt<=0)return;
        dt=(std::min)(dt,.1f);
        const float target=active?1.0f:0.0f;
        frame_.opening+=(std::clamp)(target-frame_.opening,-dt*2.5f,dt*5);
        frame_.charge=active?(std::clamp)(1-secondsToFire/1.8f,0.0f,1.0f):0;
        if(firing && !wasFiring_)recoilAge_=0;
        else recoilAge_+=dt;
        frame_.recoil=.23f*(1-std::exp(-recoilAge_*100))*std::exp(-recoilAge_*10);
        wasFiring_=firing;
    }
private:
    RailVeilAttackFrame frame_{};
    bool wasFiring_=false;
    float recoilAge_=10;
};

namespace RailVeilModel {
inline constexpr const char* Directory="Resources/enemies/VeilInterceptor";
inline constexpr Vector3 Muzzle{0,.10f,-1.64f};
inline constexpr float ShroudAngle=.64f,ShroudRetraction=.42f;
struct Part {
    const char* id;
    const char* file;
    Vector3 pivot;
};
inline constexpr std::array<Part,10> Parts{{
    {"veil_hull","VeilHull.obj",{0,0,0}},
    {"veil_wing_left","VeilWingLeft.obj",{-1.02f,0,.08f}},
    {"veil_wing_right","VeilWingRight.obj",{1.02f,0,.08f}},
    {"veil_cannon","VeilCannon.obj",{0,.10f,-.72f}},
    {"veil_armour_top","VeilArmourTop.obj",{0,.64f,-.70f}},
    {"veil_armour_bottom","VeilArmourBottom.obj",{0,-.44f,-.70f}},
    {"veil_sensor","VeilSensor.obj",{-1.10f,.38f,-.68f}},
    {"veil_sensor_cover","VeilSensorCover.obj",{-1.10f,.65f,-.57f}},
    {"veil_cannon_glow","VeilCannonGlow.obj",{0,.10f,-.72f}},
    {"veil_engine_glow","VeilEngineGlow.obj",{0,0,0}},
}};
struct PartPose {Matrix4x4 local=MakeIdentity4x4();bool visible=true;};
inline PartPose EvaluatePart(size_t index,const RailVeilAttackFrame& attack,bool sensorExposed,bool sensorDamaged) {
    PartPose result;if(index>=Parts.size()){result.visible=false;return result;}
    Vector3 rotate{},offset=Parts[index].pivot;
    if(index==1 || index==2)rotate.y=(index==1?1.0f:-1.0f)*attack.opening*.12f;
    if(index==3 || index==8)offset.z+=attack.recoil;
    if(index==4 || index==5) {
        rotate.x=(index==4?1.0f:-1.0f)*attack.opening*ShroudAngle;
        offset.z+=attack.opening*ShroudRetraction;
    }
    if(index==6)result.visible=!sensorDamaged;
    if(index==7) {
        rotate.x=sensorExposed?1.45f:0.0f;
        result.visible=!sensorDamaged;
    }
    result.local=MakeAffineMatrix({1,1,1},rotate,offset);
    return result;
}
inline Vector3 MuzzleWorld(const TwinShieldDronePose& pose,const RailVeilAttackFrame& attack) {
    auto local=Muzzle;local.z+=attack.recoil;return RearSubsystemWorldCenter(pose,local);
}
}
