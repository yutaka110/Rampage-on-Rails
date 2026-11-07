#pragma once

#include "RailBridgeStructure.h"
#include "RailVeilModel.h"

namespace RailBridgeCannon {

inline constexpr float DischargeTailSeconds = .80f;
inline constexpr float ImpactFlashSeconds = .10f;

inline Vector3 LerpPoint(Vector3 a, Vector3 b, float t) {
    return {std::lerp(a.x,b.x,t),std::lerp(a.y,b.y,t),std::lerp(a.z,b.z,t)};
}

struct Feed {
    Vector3 tip{}, bend{}, throat{}, mouth{};
    float strength = 0.0f;
    float heatFront = 0.0f;

    float HeatAt(float t) const {
        return TwinShieldDestructionSmooth(heatFront,t+.01f,t+.14f);
    }

    Vector3 Point(float t) const {
        const auto a=LerpPoint(tip,bend,t);
        const auto b=LerpPoint(bend,throat,t);
        const auto c=LerpPoint(throat,mouth,t);
        return LerpPoint(LerpPoint(a,b,t),LerpPoint(b,c,t),t);
    }
};

struct Frame {
    std::array<Feed,4> feeds{};
    Vector3 mouth{}, head{};
    float charge = 0.0f;
    float compression = 0.0f;
    float mouthHeat = 0.0f;
    float power = 0.0f;
    float width = 0.0f;
    float travel = 0.0f;
    float impactPower = 0.0f;
};

inline Frame Evaluate(const RailVeilAttackFrame& gun, Vector3 impact, float age) {
    Frame frame;
    const float sinceFire=age-RailBridgeEscape::FireSeconds;
    const float sinceImpact=age-RailBridgeEscape::ImpactSeconds;
    if(sinceImpact>=0.0f)
        frame.impactPower=1.0f-TwinShieldDestructionSmooth(sinceImpact,0.0f,ImpactFlashSeconds);
    if(!gun.bridgePose.visible)return frame;

    frame.mouth=RailVeilModel::MuzzleWorld(gun.bridgePose,gun);
    frame.head=frame.mouth;
    if(sinceFire>=0.0f && sinceImpact<DischargeTailSeconds) {
        constexpr float flightTime=RailBridgeEscape::ImpactSeconds-RailBridgeEscape::FireSeconds;
        frame.travel=(std::clamp)(sinceFire/flightTime,.018f,1.0f);
        frame.head=LerpPoint(frame.mouth,impact,frame.travel);
        const float taper=TwinShieldDestructionSmooth(sinceImpact,RailBridgeStructure::TearSeconds-.12f,DischargeTailSeconds);
        const float settle=TwinShieldDestructionSmooth(sinceImpact,0.0f,.13f);
        frame.power=std::lerp(1.0f,.70f,settle)*(1.0f-taper);
        frame.width=std::lerp(1.0f,.68f,settle)*std::lerp(1.0f,.08f,taper);
        return frame;
    }
    if(sinceFire>=0.0f)return frame;

    frame.charge=gun.charge;
    frame.compression=TwinShieldDestructionSmooth(sinceFire,-.20f,-.035f);
    // The upper and lower blades share a hinge on each side. Socket
    // coordinates are pivot-local, on the front face of the authored blades.
    for(size_t i=0;i<frame.feeds.size();++i) {
        const bool upper=i<2;
        const float side=(i%2)==0?-1.0f:1.0f;
        const float vertical=upper?1.0f:-1.0f;
        const auto wing=RailVeilModel::EvaluatePart(1+i%2,gun,false,false).local;
        const auto worldPoint=[&](Vector3 p) {
            const Vector3 local{
                p.x*wing.m[0][0]+p.y*wing.m[1][0]+p.z*wing.m[2][0]+wing.m[3][0],
                p.x*wing.m[0][1]+p.y*wing.m[1][1]+p.z*wing.m[2][1]+wing.m[3][1],
                p.x*wing.m[0][2]+p.y*wing.m[1][2]+p.z*wing.m[2][2]+wing.m[3][2]};
            return RearSubsystemWorldCenter(gun.bridgePose,local);
        };
        auto& feed=frame.feeds[i];
        feed.tip=worldPoint({side*.26f,upper?2.13f:-1.40f,upper?.005f:-.25f});
        feed.bend=worldPoint({side*.49f,vertical*.91f,-.56f});
        feed.throat=worldPoint({side*.13f,vertical*.25f,-1.52f});
        feed.mouth=frame.mouth;
        const float start=.36f+.045f*float(i);
        const float opened=TwinShieldDestructionSmooth(age,start,start+.18f);
        feed.strength=opened*(.46f+.54f*frame.charge)*(1.0f-frame.compression);
        const float ignition=.88f+.036f*float(i);
        feed.heatFront=(std::clamp)((age-ignition)/.37f,0.0f,1.16f);
        frame.mouthHeat+=feed.HeatAt(1.0f)/float(frame.feeds.size());
    }
    return frame;
}

} // namespace RailBridgeCannon
