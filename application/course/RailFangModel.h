#pragma once
#include "RailRearPursuitRules.h"
#include <array>

namespace RailFangModel {
inline constexpr const char* Directory="Resources/enemies/ElitePursuers";
enum class Node : size_t { Body, LeftWing, RightWing, LeftBarrel, RightBarrel, LeftEngine, RightEngine, Sensor, Count };
struct Part {const char* id;const char* file;Vector3 pivot;};
inline constexpr std::array<Part,8> Parts{{
    {"fang_body","FangBody.obj",{0,0,0}},
    {"fang_wing_left","FangWingLeft.obj",{-.58f,-.04f,.185f}},
    {"fang_wing_right","FangWingRight.obj",{.58f,-.04f,.185f}},
    {"fang_barrel_left","FangBarrelLeft.obj",{-.73f,-.18f,-.30f}},
    {"fang_barrel_right","FangBarrelRight.obj",{.73f,-.18f,-.30f}},
    {"fang_engine_left","FangEngineLeft.obj",{-.54f,-.36f,1.19f}},
    {"fang_engine_right","FangEngineRight.obj",{.54f,-.36f,1.19f}},
    {"fang_sensor","FangSensor.obj",{0,.015f,-2.18f}},
}};
struct Motion {Vector3 rotation{},translation{};};
struct Pose {
    std::array<Motion,Parts.size()> nodes{};
    float bodyKick=0;
};
inline Pose FiringPose(float recoil) {
    Pose p;p.bodyKick=recoil;
    p.nodes[size_t(Node::LeftBarrel)].translation.z=recoil;
    p.nodes[size_t(Node::RightBarrel)].translation.z=recoil;return p;
}
struct PartPose {Matrix4x4 local=MakeIdentity4x4();bool visible=true;};
inline PartPose EvaluatePart(size_t index,const Pose& pose={},bool cannonDamaged=false) {
    PartPose p;if(index>=Parts.size()){p.visible=false;return p;}
    p.visible=!(cannonDamaged && index==size_t(Node::RightBarrel));
    const auto pivot=Parts[index].pivot,offset=pose.nodes[index].translation;
    p.local=MakeAffineMatrix({1,1,1},pose.nodes[index].rotation,{pivot.x+offset.x,pivot.y+offset.y,pivot.z+offset.z});
    if(index!=size_t(Node::LeftBarrel) && index!=size_t(Node::RightBarrel))
        p.local=Multiply(p.local,MakeAffineMatrix(Vector3{1,1,1},Vector3{pose.bodyKick*.06f,0,0},Vector3{0,0,pose.bodyKick*.10f}));
    return p;
}
inline const char* Mesh(size_t index,bool exposed,bool damaged) {
    if(index==0)return damaged?"fang_body_damaged":exposed?"fang_body_exposed":"fang_body";
    return index<Parts.size()?Parts[index].id:"";
}
}
