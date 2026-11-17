#pragma once

#include "RailCartImpactEnergy.h"
#include "RailRearCartImpact.h"
#include "utils/math/MathUtils.h"
#include <algorithm>
#include <cmath>
#include <span>
#include <string>
#include <vector>

// Geometry keeps the heat mark in the struck object's space. Spall and smoke
// retain the emission pose in world space once the shot has broken the surface.
namespace RailCombatImpact {
inline constexpr size_t DrawCapacity=6;
inline constexpr uint32_t CartOwner=0;
inline constexpr float ArcLifetime=.18f;
inline constexpr float BlueArcDelay=.018f;
enum class Arc { None, Blue, Green };
enum class Kind { Hull, Plate, Cart, Heavy, Kill, GatlingCart, VeilLightningCart, VeilBeamCart, WingCart };

inline RailRearCartImpact::Type RearCartType(Kind kind) {
    if(kind==Kind::VeilLightningCart)return RailRearCartImpact::Type::Lightning;
    if(kind==Kind::VeilBeamCart)return RailRearCartImpact::Type::Beam;
    return RailRearCartImpact::Type::None;
}

inline bool TransientCart(Kind kind) {
    return kind==Kind::GatlingCart || kind==Kind::WingCart ||
        RearCartType(kind)!=RailRearCartImpact::Type::None;
}

inline Vector3 Transform(Vector3 p,const Matrix4x4& m,float w) {
    return {p.x*m.m[0][0]+p.y*m.m[1][0]+p.z*m.m[2][0]+w*m.m[3][0],
        p.x*m.m[0][1]+p.y*m.m[1][1]+p.z*m.m[2][1]+w*m.m[3][1],
        p.x*m.m[0][2]+p.y*m.m[1][2]+p.z*m.m[2][2]+w*m.m[3][2]};
}

struct Mark {
    uint32_t owner=0;
    uint64_t seed=0;
    Vector3 localPoint{},localNormal{},origin{},normal{},tint{};
    Kind kind=Kind::Hull;
    float age=0,radius=1;
    bool smokeEmitted=false;
    bool needsSurfaceBinding=false;
    Vector3 incoming{};
    std::string receiverMesh;
    Arc arc=Arc::None;
    bool surfaceProjected=false;
};

inline float CartDischargeDuration(const Mark& mark) {
    if (mark.kind==Kind::Heavy) return RailCartImpactEnergy::HeavyDuration;
    if (mark.kind==Kind::GatlingCart) return RailCartImpactEnergy::GatlingDuration;
    if (mark.kind==Kind::VeilLightningCart) return RailRearCartImpact::LightningDuration;
    if (mark.kind==Kind::VeilBeamCart) return RailRearCartImpact::BeamDuration;
    if (mark.kind==Kind::WingCart) return RailRearCartImpact::WingDuration;
    return RailCartImpactEnergy::Duration;
}

inline const Mark* LatestCartDischarge(std::span<const Mark> marks,bool gatling) {
    const Mark* latest=nullptr;
    for(auto it=marks.rbegin();it!=marks.rend();++it) {
        if(it->owner!=CartOwner || it->kind==Kind::WingCart || RearCartType(it->kind)!=RailRearCartImpact::Type::None ||
            (it->kind==Kind::GatlingCart)!=gatling ||
            it->age>=CartDischargeDuration(*it))continue;
        if(gatling || it->kind==Kind::Heavy)return &*it;
        if(!latest)latest=&*it;
    }
    return latest;
}

class Marks final {
public:
    void Reset() {marks_.clear();cartSequence_=0;}
    void Advance(float dt,float cartPresentationDt=-1.0f) {
        if(!std::isfinite(dt) || dt<=0)return;
        for(auto& mark:marks_) {
            const bool bossCart=mark.owner==CartOwner && (mark.kind==Kind::Heavy || TransientCart(mark.kind));
            mark.age+=bossCart && cartPresentationDt>=0.0f?cartPresentationDt:dt;
        }
        std::erase_if(marks_,[](const auto& mark){
            return mark.age>=(TransientCart(mark.kind)?CartDischargeDuration(mark):3.4f);
        });
    }
    bool AcceptCart(uint64_t sequence) {
        if(!sequence || sequence==cartSequence_)return false;
        cartSequence_=sequence;
        return true;
    }
    void Add(Mark mark) {
        if(marks_.size()>=18)marks_.erase(marks_.begin());
        marks_.push_back(mark);
    }
    std::span<Mark> Items() {return marks_;}
    std::span<const Mark> Items() const {return marks_;}
private:
    std::vector<Mark> marks_;
    uint64_t cartSequence_=0;
};
}
