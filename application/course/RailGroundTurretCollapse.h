#pragma once

#include "RailTurretGeometry.h"
#include "utils/math/MathUtils.h"
#include <string>

namespace RailGroundTurretCollapse {
inline constexpr float HingeRelease=.09f, Slam=.48f, Lifetime=3.8f;

inline float Smooth(float time,float from,float to) {
    const float u=(std::clamp)((time-from)/(to-from),0.0f,1.0f);
    return u*u*(3-2*u);
}

struct Wreck {
    std::string meshId;
    Vector3 center{}, headRotation{}, baseRotation{}, contact{};
    float scale=1, side=1, rotor=0, recoil=0;
    bool Active() const { return !meshId.empty(); }
};

struct Pose {
    Matrix4x4 base{},head{},barrel{};
    Vector3 strike{};
    float opacity=0;
};

inline Vector3 Point(Vector3 p,const Matrix4x4& m) {
    return {p.x*m.m[0][0]+p.y*m.m[1][0]+p.z*m.m[2][0]+m.m[3][0],
        p.x*m.m[0][1]+p.y*m.m[1][1]+p.z*m.m[2][1]+m.m[3][1],
        p.x*m.m[0][2]+p.y*m.m[1][2]+p.z*m.m[2][2]+m.m[3][2]};
}

inline Pose Evaluate(const Wreck& wreck,float age) {
    const float t=(std::clamp)((age-HingeRelease)/(Slam-HingeRelease),0.0f,1.0f);
    const float fall=t*t;
    const float after=(std::max)(0.0f,age-Slam);
    const float bounce=.12f*std::sin(after*28)*std::exp(-after*12);
    const auto& rotation=wreck.headRotation;
    const float yaw=rotation.y;
    const float slide=.27f*fall*wreck.scale;
    const Vector3 headCenter{wreck.center.x-std::sin(yaw)*slide,
        wreck.center.y+wreck.scale*(-.38f*fall+bounce),wreck.center.z-std::cos(yaw)*slide};
    const Vector3 size{wreck.scale,wreck.scale,wreck.scale};
    const auto geometry=RailTurret::ForMesh(wreck.meshId);
    Pose pose;
    pose.base=MakeAffineMatrix(size,wreck.baseRotation,wreck.center);
    pose.head=MakeAffineMatrix(size,Vector3{std::lerp(rotation.x,-.98f,fall)+bounce,yaw,
        rotation.z+wreck.side*.24f*fall},headCenter);
    pose.barrel=Multiply(MakeAffineMatrix(Vector3{1,1,1},
        Vector3{-.11f*fall,0,wreck.rotor*(1-Smooth(age,HingeRelease,Slam))},
        Vector3{0,wreck.meshId=="siege_turret"?0.0f:geometry.muzzleHeight,
            std::lerp(wreck.recoil,-.18f,fall)}),pose.head);
    pose.strike=Point({wreck.side*.36f,-.60f,-.65f},pose.base);
    pose.opacity=1-Smooth(age,Lifetime-.65f,Lifetime);
    return pose;
}
}
