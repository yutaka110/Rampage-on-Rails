#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <string_view>
#include "../terrain/RailPath.h"

// Blockout dimensions are in the same world units as the cart and terrain.
// Rendering, launch sockets and the reveal camera share this one contract.
namespace RailMothership {
inline constexpr std::array<std::string_view,2> PartActorIds={"short_mothership_rapid","short_mothership_heavy"};
inline constexpr std::array<float,2> PartLateral={-20.0f,20.0f};
// Measured from the cart; leave room for the rear camera inside the 92m gun ray.
inline constexpr float PartForwardDistance=50.0f;
inline int PartIndex(std::string_view actor) {
    for(int i=0;i<2;++i) if(actor==PartActorIds[i]) return i;
    return -1;
}
inline bool IsCombatActor(std::string_view actor) {
    return actor=="short_mothership_core" || PartIndex(actor)>=0;
}
inline float Smooth(float value, float begin, float end) {
    const float t = (std::clamp)((value-begin)/(end-begin), 0.0f, 1.0f);
    return t*t*(3.0f-2.0f*t);
}
inline float RidgeVistaLift(float distance) {
    return 110.0f*Smooth(distance,2320,2440)*(1.0f-Smooth(distance,2680,2820));
}
inline Vector3 Add(Vector3 a, Vector3 b) { return {a.x+b.x,a.y+b.y,a.z+b.z}; }
inline Vector3 Scale(Vector3 a, float s) { return {a.x*s,a.y*s,a.z*s}; }
inline Vector3 Mix(Vector3 a, Vector3 b, float t) {
    return Add(Scale(a,1.0f-t),Scale(b,t));
}
inline constexpr float Height = 76.0f;
inline constexpr float BayHeight = -9.5f;
inline constexpr float BayLateral = 43.0f;
inline constexpr float FlightDuration = 3.1f;
inline constexpr float LaunchStagger = 0.22f;
inline constexpr float FlybyDuration = 3.2f;
inline constexpr float DeploymentBegin = 5.2f;
inline constexpr float IntroductionDuration = 8.0f;
inline constexpr float FlybyRearOffset = -130.0f;
inline constexpr float EscortForwardOffset = 155.0f;

struct Frame {
    bool visible = false;
    bool introductionActive = false;
    bool battleActive = false, battleDefeated = false;
    float battleHealth = 1.0f, battleCharge = 0.0f, battleDestruction = 0.0f;
    bool partsBattle = false, battleRepelled = false;
    std::array<float,2> partHealth{};
    std::array<float,2> partCharge{};
    unsigned remainingParts = 0;
    float railDistance = 0.0f;
    float cameraWeight = 0.0f;
    float heroWeight = 0.0f;
    float launchGlow = 0.0f;
    float railVertical = Height;
    float deploymentDelay = 0.0f;
    Vector3 center{}, right{}, up{}, forward{}, cameraSubject{};
    Vector3 World(Vector3 local) const {
        return Add(center,Add(Scale(right,local.x),
            Add(Scale(up,local.y),Scale(forward,local.z))));
    }
    Vector3 Bay(uint32_t slot) const {
        return World({(slot&1u) ? BayLateral : -BayLateral,BayHeight,0.0f});
    }
};

class Reveal final {
public:
    void Reset() { *this = {}; }
    const Frame& Update(float dt, float distance, float outdoorEnd,
            const RailPath& rail, bool enabled, bool preserveCourseView=false) {
        if (!enabled || rail.Length() <= 0.0f) { Reset(); return frame_; }
        if (distance+25.0f < previousDistance_) Reset();
        previousDistance_ = distance;
        dt = std::isfinite(dt) ? (std::clamp)(dt,0.0f,0.25f) : 0.0f;
        // The exit crossing owns this one-shot introduction, including when
        // an older group is still alive. The host protects combat during it.
        if (!begun_ && dt>0.0f && distance>=outdoorEnd) {
            begun_ = true;
            // A late checkpoint resumes the escort/boss, never the exit flyby
            // or its camera lock and temporary invulnerability.
            if(distance>outdoorEnd+240.0f) elapsed_=IntroductionDuration;
        }
        if (begun_) elapsed_ += dt;
        frame_ = {};
        frame_.visible = begun_;
        frame_.introductionActive=begun_ && elapsed_<IntroductionDuration;
        const float t=(std::clamp)(elapsed_/FlybyDuration,0.0f,1.0f);
        // Fast rear approach, horizontal overtake, then zero relative velocity
        // at the existing forward escort position (C1 at the handoff).
        const float progress=t*(1.25f+t*(0.5f-0.75f*t));
        const float forward=FlybyRearOffset+(EscortForwardOffset-FlybyRearOffset)*progress;
        frame_.railDistance = distance+forward;
        const auto sample = rail.EvaluateExtended(frame_.railDistance);
        frame_.right=sample.right; frame_.up=sample.up; frame_.forward=sample.tangent;
        const float flightAltitude=rail.EvaluateExtended(outdoorEnd).position.y+Height+RidgeVistaLift(distance);
        // Constant WORLD altitude: no downward slide or terrain-following bob
        // may turn this pass into a ship falling from the sky.
        frame_.railVertical=(flightAltitude-sample.position.y)/(std::max)(sample.up.y,0.1f);
        frame_.center=Add(sample.position,Scale(sample.up,frame_.railVertical));
        frame_.deploymentDelay=(std::max)(0.0f,DeploymentBegin-elapsed_);
        frame_.launchGlow=0.35f+0.65f*Smooth(elapsed_,FlybyDuration-0.5f,FlybyDuration+0.3f);
        // Take attention immediately at the exit, hold the ship's reveal,
        // then return while its first drones descend toward the combat lane.
        const float desired=begun_ ? Smooth(elapsed_,0.0f,0.65f)*
            (1.0f-Smooth(elapsed_,DeploymentBegin,IntroductionDuration-0.5f))*0.96f : 0.0f;
        weight_ += (desired-weight_)*(1.0f-std::exp(-dt*4.5f));
        frame_.cameraWeight=weight_;
        frame_.heroWeight=begun_ ? Smooth(elapsed_,0.8f,2.3f)*
            (1.0f-Smooth(elapsed_,DeploymentBegin,IntroductionDuration-0.5f)) : 0.0f;
        const auto ahead=rail.EvaluateExtended(distance+65.0f);
        const auto player=rail.EvaluateExtended(distance);
        // Do not swivel behind the cart or look straight up as the ship passes.
        const auto subject=Add(frame_.World({0,BayHeight*(1.0f-frame_.heroWeight),0}),
            Scale(player.tangent,(std::max)(0.0f,80.0f-forward)));
        frame_.cameraSubject=Mix(subject,Add(ahead.position,Scale(ahead.up,12.0f)),
            Smooth(elapsed_,DeploymentBegin,IntroductionDuration-0.5f));
        // The six-beat course owns the vista. Keep deployment timing and the
        // escort flight, without steering away from the rail and its landmarks.
        if(preserveCourseView) {
            frame_.cameraWeight=0;frame_.heroWeight=0;
            // Vacate the bridge skyline before returning for the next launch.
            // Bay staging shares this exact height; drones cannot launch from
            // the previous, now empty, escort position.
            const float climb=110.0f*Smooth(distance,1530,1640)*(1.0f-Smooth(distance,1830,1990));
            frame_.railVertical+=climb;
            frame_.center=Add(frame_.center,Scale(frame_.up,climb));
        }
        return frame_;
    }
    const Frame& Current() const { return frame_; }
private:
    Frame frame_{};
    float elapsed_=0.0f, weight_=0.0f, previousDistance_=0.0f;
    bool begun_=false;
};

// Saved as part of the actor checkpoint so a retry cannot repeat its launch.
struct Deployment {
    bool initialized=false, finished=false;
    float elapsed=0.0f, delay=0.0f;
    float launchDistance=0.0f, launchLateral=0.0f, launchVertical=0.0f;
    float forward=55.0f, lateral=0.0f, vertical=8.0f;
};
struct Flight {
    float distance=0.0f, lateral=0.0f, vertical=0.0f;
    Vector3 rotation{};
    bool released=false;
};
inline Flight Evaluate(const Deployment& launch,float playerDistance) {
    const float age=(std::max)(0.0f,launch.elapsed-launch.delay);
    const float t=(std::clamp)(age/FlightDuration,0.0f,1.0f);
    // Accelerate away from the bay, then brake into the authored combat slot.
    const float p=t*t*t*(10.0f+t*(-15.0f+6.0f*t));
    const float side=launch.launchLateral<0.0f ? -1.0f : 1.0f;
    Flight flight;
    flight.distance=launch.launchDistance*(1-p)+(playerDistance+launch.forward)*p;
    flight.lateral=launch.launchLateral*(1-p)+launch.lateral*p+side*5.0f*std::sin(t*3.14159265f);
    flight.vertical=launch.launchVertical*(1-p)+launch.vertical*p;
    flight.rotation={-0.20f*std::sin(t*3.14159265f),
        0.40f*side*std::sin(t*3.14159265f),-0.32f*side*std::sin(t*3.14159265f)};
    flight.released=t>=1.0f;
    return flight;
}
}
