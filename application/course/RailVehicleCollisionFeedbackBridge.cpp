#include "RailVehicleCollisionFeedbackBridge.h"
#include "RailHeavyAttack.h"
#include "../diagnostics/DebugDrawSystem.h"

#include <algorithm>
#include <cmath>

namespace {

constexpr float kTau = 6.28318530717958647692f;

void SetError(std::string* errorMessage, const char* message) {
    if (errorMessage != nullptr) *errorMessage = message;
}

bool Finite(float value) noexcept { return std::isfinite(value); }

float Dot(Vector3 a, Vector3 b) noexcept {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

Vector3 Add(Vector3 a, Vector3 b) noexcept {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}

Vector3 Scale(Vector3 value, float scale) noexcept {
    return {value.x * scale, value.y * scale, value.z * scale};
}

Vector3 NormalizeOr(Vector3 value, Vector3 fallback) noexcept {
    const float lengthSquared = Dot(value, value);
    if (!Finite(lengthSquared) || lengthSquared <= 0.0000001f) return fallback;
    return Scale(value, 1.0f / std::sqrt(lengthSquared));
}

bool IsVehicleContact(const PlayerDamageResult& result) noexcept {
    return result.accepted && result.appliedDamage > 0.0f &&
        (result.request.kind == PlayerHitKind::ObstacleContact ||
         result.request.kind == PlayerHitKind::TerrainContact);
}

float HashSigned(uint64_t sequence, uint32_t index) noexcept {
    uint64_t value = sequence ^
        (static_cast<uint64_t>(index + 1u) * 0x9E3779B97F4A7C15ull);
    value ^= value >> 30u;
    value *= 0xBF58476D1CE4E5B9ull;
    value ^= value >> 27u;
    value *= 0x94D049BB133111EBull;
    value ^= value >> 31u;
    return static_cast<float>(value & 0xFFFFu) / 32767.5f - 1.0f;
}

} // namespace

bool RailVehicleCollisionFeedbackSettings::Validate(
    std::string* errorMessage) const {
    if (!Finite(bodyKickDistance) || bodyKickDistance < 0.0f ||
        bodyKickDistance > 5.0f || !Finite(maximumBankDegrees) ||
        maximumBankDegrees < 0.0f || maximumBankDegrees > 45.0f ||
        !Finite(maximumPitchDegrees) || maximumPitchDegrees < 0.0f ||
        maximumPitchDegrees > 45.0f || !Finite(maximumYawDegrees) ||
        maximumYawDegrees < 0.0f || maximumYawDegrees > 45.0f ||
        !Finite(responseDurationSeconds) || responseDurationSeconds <= 0.01f ||
        responseDurationSeconds > 3.0f || !Finite(oscillationFrequencyHz) ||
        oscillationFrequencyHz < 0.0f || oscillationFrequencyHz > 60.0f ||
        !Finite(cameraShake) || cameraShake < 0.0f || cameraShake > 5.0f ||
        !Finite(sparkRadius) || sparkRadius <= 0.0f || sparkRadius > 20.0f ||
        !Finite(sparkLifetimeSeconds) || sparkLifetimeSeconds <= 0.01f ||
        sparkLifetimeSeconds > 5.0f || !Finite(sparkSpread) ||
        sparkSpread < 0.0f || sparkSpread > 20.0f || sparkBurstCount == 0 ||
        sparkBurstCount > RailVehicleCollisionFeedbackFrame::kMaximumVfxCommands ||
        !Finite(impactVolume) || impactVolume < 0.0f || impactVolume > 2.0f ||
        !Finite(slowdownSpeedMultiplier) || slowdownSpeedMultiplier <= 0.0f ||
        slowdownSpeedMultiplier > 1.0f || !Finite(slowdownDurationSeconds) ||
        slowdownDurationSeconds <= 0.01f || slowdownDurationSeconds > 5.0f) {
        SetError(errorMessage, "Rail vehicle collision feedback setting is invalid.");
        return false;
    }
    if (errorMessage != nullptr) errorMessage->clear();
    return true;
}

void RailVehicleCollisionFeedbackBridge::Reset() {
    frame_ = {};
    responseNormalWorld_ = {0.0f, 1.0f, 0.0f};
    responseNormalRailLocal_ = {0.0f, 1.0f, 0.0f};
    responseWorldPosition_ = {};
    responseOffsetLocal_ = {};
    responseIntensity_ = 0.0f;
    lastConsumedResultSequence_ = 0;
    responses_ = {};
    particles_.clear();
    revision_ = 0;
}

void RailVehicleCollisionFeedbackBridge::Update(
    const RailVehicleCollisionFeedbackInput& input) {
    frame_ = {};
    const float dt = Finite(input.deltaTime)
        ? (std::clamp)(input.deltaTime, 0.0f, 0.25f)
        : 0.0f;
    if (!input.settings.enabled) { responses_ = {}; particles_.clear(); }
    if (!input.settings.vfxEnabled) particles_.clear();
    if (input.gameplayActive) {
        for (auto& particle : particles_) {
            particle.age += dt;
            particle.previousPosition = particle.position;
            particle.position = Add(particle.origin,Scale(particle.velocity,particle.age));
            particle.position.y -= 4.9f*particle.age*particle.age;
        }
        std::erase_if(particles_,[](const auto& p){return p.age>=p.lifetime;});
    }
    if (input.gameplayActive) {
        for (auto& response : responses_) {
            response.elapsed += dt;
            if (response.elapsed >= response.duration) response.intensity = 0.0f;
        }
    }

    const RailVehicleRuntimeState* vehicle = input.vehicleState;
    if (input.settings.enabled && input.gameplayActive && vehicle != nullptr &&
        vehicle->initialized) {
        for (const PlayerDamageResult& result : input.damageResults) {
            const bool contact = IsVehicleContact(result);
            const bool projectile = result.request.kind == PlayerHitKind::EnemyProjectile;
            const bool heavy = projectile && RailHeavy::IsProjectile(result.request.sourceId);
            if (!result.accepted || result.appliedDamage <= 0.0f ||
                (!contact && !projectile) || result.sequence <= lastConsumedResultSequence_) {
                continue;
            }
            lastConsumedResultSequence_ = result.sequence;
            const Vector3 normalWorld = NormalizeOr(
                result.request.impactNormalWorld,
                Scale(vehicle->forward, -1.0f));
            responseNormalWorld_ = normalWorld;
            responseNormalRailLocal_ = {
                Dot(normalWorld, vehicle->right),
                Dot(normalWorld, vehicle->up),
                Dot(normalWorld, vehicle->forward)};
            responseWorldPosition_ = result.request.hasWorldImpact
                ? result.request.impactWorldPosition
                : vehicle->damageVfxMountPosition;
            const Vector3 impactOffset{responseWorldPosition_.x-vehicle->position.x,
                responseWorldPosition_.y-vehicle->position.y,responseWorldPosition_.z-vehicle->position.z};
            responseOffsetLocal_={Dot(impactOffset,vehicle->right),Dot(impactOffset,vehicle->up),Dot(impactOffset,vehicle->forward)};
            responseIntensity_ = (std::clamp)(
                0.55f + result.appliedDamage / 100.0f,
                0.55f, result.lethal ? 1.35f : 1.0f);

            Vector3 direction{};
            if (result.request.hasWorldImpact &&
                Finite(result.request.impactWorldPosition.x) && Finite(result.request.impactWorldPosition.y) &&
                Finite(result.request.impactWorldPosition.z)) {
                const Vector3 offset{result.request.impactWorldPosition.x-vehicle->position.x,
                    result.request.impactWorldPosition.y-vehicle->position.y,
                    result.request.impactWorldPosition.z-vehicle->position.z};
                direction = {Dot(offset,vehicle->right),0,Dot(offset,vehicle->forward)};
                if (std::abs(direction.x)<0.15f) direction.x=0;
                if (std::abs(direction.z)<0.15f) direction.z=0;
                const float extent=(std::max)(1.0f,(std::max)(std::abs(direction.x),std::abs(direction.z)));
                direction=Scale(direction,1.0f/extent);
            }
            frame_.impactDirectionLocal=direction;
            frame_.hasImpactDirection=std::abs(direction.x)+std::abs(direction.z)>0.01f;
            const Vector3 worldDirection=NormalizeOr(Add(Scale(vehicle->right,direction.x),
                Scale(vehicle->forward,direction.z)),{0,0,0});
            frame_.impactDirectionScreen={Dot(worldDirection,input.listenerRight),Dot(worldDirection,input.listenerForward)};
            // Add a smooth impulse instead of restarting an active reaction.
            const auto slot=std::find_if(responses_.begin(),responses_.end(),
                [](const auto& r){return r.intensity<=0.0f;});
            if(slot!=responses_.end()) {
                const float strength=projectile?(heavy?.98f:(std::clamp)(.55f+result.appliedDamage/85.0f,.55f,.78f)):
                    (std::clamp)(.38f+result.appliedDamage/60.0f,.38f,1.0f);
                *slot={direction,0.0f,input.settings.responseDurationSeconds*(projectile?(heavy?.89f:.65f):1.0f),strength,projectile};
            }

            // One dedicated vehicle impact owns sound and particles for both
            // bullets and contacts. Generic damage keeps only screen/haptic feedback.
            if (input.settings.audioEnabled &&
                frame_.audioCueCount < frame_.audioCues.size()) {
                RailVehicleCollisionAudioCue& cue =
                    frame_.audioCues[frame_.audioCueCount++];
                cue.resultSequence = result.sequence;
                cue.volume = (std::clamp)(
                    input.settings.impactVolume * responseIntensity_ * (projectile ? 0.72f : 1.0f), 0.0f, 1.0f);
                cue.pitch = result.lethal ? (projectile?0.85f:0.72f) : (projectile ? 1.0f : 0.86f) +
                    HashSigned(result.sequence,31)*0.035f;
                cue.pan = (std::clamp)(
                    frame_.impactDirectionScreen.x * 0.78f, -1.0f, 1.0f);
                cue.lethal = result.lethal;
            }

            if (input.settings.vfxEnabled) {
                const uint32_t bursts=(std::clamp)(input.settings.sparkBurstCount,1u,8u);
                const uint32_t count=projectile ? bursts*3u+(heavy?6u:2u) : bursts*6u;
                for(uint32_t index=0; index<count && particles_.size()<128; ++index) {
                    RailVehicleImpactParticle particle;
                    particle.metal=index>=count-4;
                    const Vector3 spread=Add(Add(Scale(vehicle->right,HashSigned(result.sequence,index*3)),
                        Scale(vehicle->up,0.25f+std::abs(HashSigned(result.sequence,index*3+1)))),
                        Scale(vehicle->forward,HashSigned(result.sequence,index*3+2)));
                    const Vector3 directionOut=NormalizeOr(Add(Scale(spread,input.settings.sparkSpread/0.75f),
                        projectile?Scale(normalWorld,1.05f):Scale(worldDirection,.65f)),vehicle->up);
                    particle.origin=particle.position=particle.previousPosition=responseWorldPosition_;
                    particle.velocity=Add(Scale(directionOut,(particle.metal ? 2.5f : projectile?8.0f:5.5f)*(heavy?1.28f:contact?1.25f:1.0f)),
                        Scale(vehicle->forward,vehicle->speed*0.65f));
                    particle.lifetime=(particle.metal ? 0.48f : 0.18f+0.12f*std::abs(HashSigned(result.sequence,index+70)))
                        *input.settings.sparkLifetimeSeconds/0.38f;
                    particle.size=(particle.metal ? 0.055f : 0.022f)*input.settings.sparkRadius/0.55f;
                    particles_.push_back(particle);
                }
            }

            if (contact) {
                frame_.slowdown = {
                    result.sequence,
                    input.settings.slowdownSpeedMultiplier,
                    input.settings.slowdownDurationSeconds,
                    true};
                frame_.cameraShake = input.settings.cameraShake * responseIntensity_;
                frame_.cameraPitchImpulse =
                    -responseNormalRailLocal_.y * 0.005f * responseIntensity_;
                frame_.cameraYawImpulse =
                    responseNormalRailLocal_.x * 0.006f * responseIntensity_;
            }
        }
    }

    float heave=0, bank=0, pitch=0;
    if(vehicle!=nullptr && vehicle->initialized) for(const auto& response:responses_) {
        if(response.intensity<=0) continue;
        const float t=(std::clamp)(response.elapsed/response.duration,0.0f,1.0f);
        // Ease in with zero velocity, one compression/rebound, ease out to rest.
        const float cycles=(std::clamp)(input.settings.oscillationFrequencyHz*response.duration,0.8f,1.2f);
        const float onset=(std::min)(1.0f,t/0.12f);
        const float easedOnset=onset*onset*(3.0f-2.0f*onset);
        float pulse=2.2f*std::sin(kTau*cycles*t)*std::exp(-2.0f*t)*(1-t)*(1-t)*easedOnset;
        if(response.projectile) {
            // Fast suspension compression, one smaller rebound, and a clean settle.
            const auto ease=[](float u){return u*u*(3-2*u);};
            if(t<.16f)pulse=ease(t/.16f);
            else if(t<.59f)pulse=std::lerp(1.0f,-.23f,ease((t-.16f)/.43f));
            else pulse=std::lerp(-.23f,0.0f,ease((t-.59f)/.41f));
        }
        pulse*=response.intensity;
        heave+=pulse;
        bank+=response.directionLocal.x*pulse;
        pitch+=response.directionLocal.z*pulse;
        for(size_t index=0;index<4;++index) {
            const float side=(index&1)?1.0f:-1.0f;
            const float fore=index>=2?1.0f:-1.0f;
            const float weight=(0.5f+0.5f*side*response.directionLocal.x)*
                (0.5f+0.5f*fore*response.directionLocal.z);
            frame_.wheelLiftOffsets[index]+=0.12f*weight*(std::max)(0.0f,pulse);
        }
        frame_.activeResponseRemainingSeconds=(std::max)(frame_.activeResponseRemainingSeconds,
            response.duration-response.elapsed);
    }
    if(vehicle!=nullptr) {
        frame_.bodyTranslationWorld=Scale(vehicle->up,-input.settings.bodyKickDistance*(std::clamp)(heave,-0.3f,1.0f));
        frame_.bodyBankDegrees=input.settings.maximumBankDegrees*(std::clamp)(bank,-1.0f,1.0f);
        frame_.bodyPitchDegrees=input.settings.maximumPitchDegrees*(std::clamp)(pitch,-1.0f,1.0f);
        // No yaw kick: preserve the cart's heading while its suspension reacts.
        for(float& lift:frame_.wheelLiftOffsets) lift=(std::clamp)(lift,0.0f,0.12f);
    }

    frame_.particles = particles_;
    frame_.impactWorldPosition = responseWorldPosition_;
    frame_.impactOffsetLocal = responseOffsetLocal_;
    frame_.impactNormalWorld = responseNormalWorld_;
    frame_.lastConsumedResultSequence = lastConsumedResultSequence_;
    frame_.revision = ++revision_;
}

void RailVehicleCollisionFeedbackBridge::AppendImpactPrimitives(ge3::debug::DebugDrawSystem& draw) const {
    for(const auto& particle:frame_.particles) {
        const float t=particle.age/particle.lifetime;
        const float alpha=(1-t)*(1-t);
        if(particle.metal) {
            const Vector4 metal{0.66f,0.75f,0.84f,alpha};
            const float angle=particle.age*19;
            const Vector3 edge{std::cos(angle)*particle.size,std::sin(angle)*particle.size,particle.size*0.35f};
            draw.AddLine(Add(particle.position,Scale(edge,-1)),Add(particle.position,edge),metal);
            draw.AddPoint(particle.position,particle.size*0.5f,{0.92f,0.95f,1,alpha});
        } else {
            const Vector3 tail=Add(particle.position,Scale(NormalizeOr(particle.velocity,{0,1,0}),-0.24f*(1-t)));
            draw.AddLine(tail,particle.position,{1,0.22f,0.035f,alpha*0.35f},{1,0.85f-0.4f*t,0.25f,alpha});
            draw.AddPoint(particle.position,particle.size*(1-t),{1,0.96f,0.75f,alpha});
        }
    }
}
