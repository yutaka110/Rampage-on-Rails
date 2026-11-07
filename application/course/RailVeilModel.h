#pragma once
#include "RailRearPursuitRules.h"
#include <array>

struct RailVeilAttackFrame final {
    float opening=0,charge=0,recoil=0,wingKick=0,bodyKick=0,flash=0,brace=0;
};

// A visual follower of the existing beam clock. It cannot delay a shot, change
// the locked aim, or grant a different sensor hit window.
class RailVeilModelPresentation final {
public:
    void Reset(){*this={};}
    const RailVeilAttackFrame& Frame()const{return frame_;}
    void Update(float dt,bool active,bool firing,float secondsToFire,bool maximumOutput=false) {
        if(!std::isfinite(dt) || dt<=0)return;
        dt=(std::min)(dt,.1f);
        const float target=active?1.0f:0.0f;
        frame_.opening+=(std::clamp)(target-frame_.opening,-dt*2.5f,dt*5);
        frame_.charge=active?(std::clamp)(1-secondsToFire/1.8f,0.0f,1.0f):0;
        const float brace=maximumOutput && !firing?std::sqrt(frame_.charge):0;
        frame_.brace+=(brace-frame_.brace)*(1-std::exp(-dt*12));
        if(firing && !wasFiring_)recoilAge_=0;
        else recoilAge_+=dt;
        const float t=recoilAge_;
        frame_.recoil=t<.025f?.10f+.13f*t/.025f:.23f*std::pow(1-(std::clamp)((t-.025f)/.30f,0.0f,1.0f),2.0f);
        frame_.flash=t<.075f?std::exp(-t*34):0;
        const float lag=(std::max)(0.0f,t-.035f);
        frame_.wingKick=t>.035f && lag<.34f?std::sin(lag*37)*std::exp(-lag*13):0;
        frame_.bodyKick=t>.015f && t<.30f?std::sin((t-.015f)*24)*std::exp(-(t-.015f)*15):0;
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
    if(index==0){rotate.x=attack.bodyKick*.008f-attack.brace*.025f;offset.z+=attack.bodyKick*.025f;}
    if(index==1 || index==2) {
        rotate.y=(index==1?1.0f:-1.0f)*(attack.opening*.12f+attack.brace*.10f);
        rotate.z=(index==1?1.0f:-1.0f)*(attack.wingKick*.030f+attack.brace*.08f);
    }
    if(index==3 || index==8)offset.z+=attack.recoil+attack.brace*.13f;
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
    auto local=Muzzle;local.z+=attack.recoil+attack.brace*.13f;return RearSubsystemWorldCenter(pose,local);
}
}
