#pragma once

#include "RailVeilModel.h"
#include <cstddef>

namespace RailVeilWreck {

struct Spark {
    Vector3 tail{}, tip{};
    float opacity=0;
};

struct Smoke {
    Vector3 position{};
    float radius=0, opacity=0, rotation=0;
    uint32_t variant=0;
};

struct Frame {
    std::array<Smoke,12> smoke{};
    std::array<Spark,18> sparks{};
    size_t smokeCount=0, sparkCount=0;
};

inline Frame Evaluate(const RailVeilAttackFrame& gun,Vector3 muzzle,Vector3 impact,float age,float fireSeconds) {
    Frame frame;
    if(!gun.bridgeWreck || !gun.bridgePose.visible)return frame;

    // Sample the pose at emission. Existing particles stay in world space as
    // the wreck drops a wing and then braces for the shot.
    const auto poseAt=[&](float born) {
        RailVeilModelPresentation sample;
        TwinShieldDronePose pose;
        pose.visible=true;
        pose.scale=gun.bridgePose.scale;
        sample.UpdateBridge(pose,impact,(std::max)(0.0f,born),fireSeconds,&muzzle);
        return sample.Frame().bridgePose;
    };

    constexpr float lead=.32f, interval=.081f, lifetime=.88f;
    const int newest=int((age+lead)/interval);
    for(int i=(std::max)(0,newest-int(frame.smoke.size())+1);i<=newest;++i) {
        const float born=float(i)*interval-lead;
        const float t=age-born;
        if(t<0 || t>=lifetime)continue;
        const auto pose=poseAt(born);
        const auto vent=RearSubsystemWorldCenter(pose,{-1.21f,.76f,.26f});
        const auto wake=RearSubsystemWorldCenter(pose,{-2.04f,.76f,1.26f});
        const float seed=float(i)*2.39996f;
        const float drift=t*(.67f+.44f*t);
        auto& puff=frame.smoke[frame.smokeCount++];
        puff.position={vent.x+(wake.x-vent.x)*drift+.58f*t*std::sin(seed),
            vent.y+(wake.y-vent.y)*drift+t*(3.9f+2.45f*t),
            vent.z+(wake.z-vent.z)*drift+.45f*t*std::cos(seed)};
        puff.radius=(.55f+1.83f*t)*(.84f+.08f*float(i%4));
        puff.opacity=TwinShieldDestructionSmooth(t,.0f,.09f)*(1-TwinShieldDestructionSmooth(t,.44f,lifetime))*.73f;
        puff.rotation=seed+t*.55f;
        puff.variant=(uint32_t(i)*7u)%47u;
    }

    const std::array<float,8> bursts{.03f,.28f,.61f,.91f,1.20f,1.38f,1.54f,fireSeconds+.018f};
    for(size_t i=0;i<bursts.size();++i) {
        const float elapsed=age-bursts[i];
        if(elapsed<0 || elapsed>=.30f)continue;
        const auto pose=poseAt(bursts[i]);
        const auto origin=RearSubsystemWorldCenter(pose,i%2?Vector3{-1.22f,.53f,-.66f}:Vector3{-1.23f,.48f,.40f});
        const auto world=BuildTwinShieldDroneWorldMatrix(pose);
        const auto out=Normalize(Vector3{-world.m[0][0],-world.m[0][1],-world.m[0][2]});
        const auto aft=Normalize(Vector3{world.m[2][0],world.m[2][1],world.m[2][2]});
        for(int j=0;j<6 && frame.sparkCount<frame.sparks.size();++j) {
            const float t=elapsed-.008f*float(j);
            const float life=.16f+.024f*float(j);
            if(t<0 || t>=life)continue;
            const float seed=float(i)*3.27f+float(j)*2.39996f;
            const float kick=5.6f+2.7f*std::sin(seed);
            const float slip=4.3f+3.0f*std::cos(seed);
            const float lift=1.4f+4.6f*std::sin(seed+.8f);
            const auto point=[&](float u) {return Vector3{origin.x+(out.x*kick+aft.x*slip)*u,
                origin.y+(out.y*kick+aft.y*slip+lift)*u-7.0f*u*u,origin.z+(out.z*kick+aft.z*slip)*u};};
            frame.sparks[frame.sparkCount++]={point((std::max)(0.0f,t-.052f)),point(t),1-TwinShieldDestructionSmooth(t,.045f,life)};
        }
    }
    return frame;
}

} // namespace RailVeilWreck
