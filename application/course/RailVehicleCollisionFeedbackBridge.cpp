#include "RailVehicleCollisionFeedbackBridge.h"
#include "RailCartImpactEnergy.h"
#include "RailGatlingAttack.h"
#include "RailHeavyAttack.h"
#include "RailRearCartImpact.h"
#include "../diagnostics/DebugDrawSystem.h"

#include <algorithm>
#include <cmath>

namespace {

constexpr float kTau = 6.28318530717958647692f;
constexpr float kHeavyShakeSeconds = 0.10f;
constexpr float kGatlingSettleSeconds = 0.15f;

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

float SmoothStep(float t) noexcept {
    t = (std::clamp)(t, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
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
    presentedRounds_ = {};
    nextPresentedRound_ = 0;
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
            const float responseDt=response.kind==RailVehicleImpactKind::Projectile &&
                Finite(input.ordinaryProjectileDeltaTime) && input.ordinaryProjectileDeltaTime>=0.0f
                ? (std::clamp)(input.ordinaryProjectileDeltaTime,0.0f,.25f) : dt;
            response.elapsed += responseDt;
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
            const bool gatling = projectile && RailGatling::IsProjectile(result.request.sourceId);
            const auto rear = RailRearCartImpact::FromSource(result.request.sourceId);
            const bool wing = projectile && rear==RailRearCartImpact::Type::Wing;
            const bool ordinaryProjectile=projectile && !heavy && !gatling && !wing;
            const bool lightning = result.request.kind==PlayerHitKind::ScriptedHazard &&
                rear==RailRearCartImpact::Type::Lightning;
            const bool beam = result.request.kind==PlayerHitKind::ScriptedHazard &&
                rear==RailRearCartImpact::Type::Beam;
            const bool accepted = result.accepted && result.appliedDamage > 0.0f;
            // The damage cooldown can reject a round that still reached the cart.
            // Reuse its swept contact for the local rattle and metal tick.
            const bool protectedContact = input.presentProtectedProjectileContacts &&
                (gatling || wing) && result.rejectReason == PlayerDamageRejectReason::Invulnerable &&
                result.request.hasWorldImpact && result.request.sourceProjectileId != 0;
            if ((!accepted && !protectedContact) || (!contact && !projectile && !lightning && !beam) ||
                result.sequence <= lastConsumedResultSequence_) {
                continue;
            }
            lastConsumedResultSequence_ = result.sequence;
            const auto kind = lightning ? RailVehicleImpactKind::VeilLightning : beam ? RailVehicleImpactKind::VeilBeam
                : wing ? RailVehicleImpactKind::Wing : heavy ? RailVehicleImpactKind::Heavy : gatling ? RailVehicleImpactKind::Gatling
                : projectile ? RailVehicleImpactKind::Projectile : RailVehicleImpactKind::Contact;
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
            if (accepted) {
                responseOffsetLocal_ = {Dot(impactOffset,vehicle->right),Dot(impactOffset,vehicle->up),Dot(impactOffset,vehicle->forward)};
            }
            if ((gatling || wing) && result.request.sourceProjectileId != 0) {
                const uint64_t id = result.request.sourceProjectileId;
                if (std::find(presentedRounds_.begin(), presentedRounds_.end(), id) != presentedRounds_.end()) continue;
                presentedRounds_[nextPresentedRound_] = id;
                nextPresentedRound_ = (nextPresentedRound_ + 1) % presentedRounds_.size();
            }
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
                if (std::abs(direction.x)<(ordinaryProjectile?.035f:.15f)) direction.x=0;
                if (std::abs(direction.z)<0.15f) direction.z=0;
                const float extent=(std::max)(1.0f,(std::max)(std::abs(direction.x),std::abs(direction.z)));
                direction=wing?Vector3{(std::clamp)(direction.x/.85f,-1.0f,1.0f),0,0}:Scale(direction,1.0f/extent);
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
                const float strength=projectile?(heavy ? 1.0f : gatling ?
                    1.0f + .22f * HashSigned(result.request.sourceProjectileId, 11) :
                    (std::clamp)(.55f+result.appliedDamage/85.0f,.55f,.78f)):
                    (std::clamp)(.38f+result.appliedDamage/60.0f,.38f,1.0f);
                const float duration = lightning ? RailRearCartImpact::LightningSettle : beam ? RailRearCartImpact::BeamSettle
                    : wing ? RailRearCartImpact::WingSettle : heavy ? RailCartImpactEnergy::HeavySettleSeconds : gatling ? kGatlingSettleSeconds
                    : input.settings.responseDurationSeconds;
                *slot = {direction, 0.0f, duration, strength, kind};
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
                if (heavy) {
                    cue.volume = (std::min)(1.0f, input.settings.impactVolume * 1.12f);
                    cue.pitch = .89f;
                } else if (gatling) {
                    cue.volume = (std::min)(1.0f, input.settings.impactVolume * (accepted ? .55f : .43f));
                    cue.pitch = 1.20f + HashSigned(result.sequence, 31) * .085f;
                } else if (lightning || beam || wing) {
                    cue.volume=(std::min)(1.0f,input.settings.impactVolume*
                        (lightning?.88f:beam?.59f:protectedContact?.30f:.47f));
                    cue.pitch=lightning?.83f:beam?.72f:1.24f+HashSigned(result.sequence,31)*.10f;
                }
                cue.pan = (std::clamp)(
                    frame_.impactDirectionScreen.x * 0.78f, -1.0f, 1.0f);
                cue.lethal = result.lethal;
                cue.kind = kind;
            }

            if (input.settings.vfxEnabled) {
                if ((gatling || lightning || beam || wing) && !result.lethal && result.request.hasWorldImpact &&
                    frame_.vfxCommandCount < frame_.vfxCommands.size()) {
                    auto& impact = frame_.vfxCommands[frame_.vfxCommandCount++];
                    impact.resultSequence = result.sequence;
                    impact.effectId = result.request.sourceId;
                    impact.worldPosition = responseWorldPosition_;
                    impact.impactNormalWorld = normalWorld;
                }
                if(lightning || beam || wing) continue;
                const uint32_t bursts=(std::clamp)(input.settings.sparkBurstCount,1u,8u);
                const uint32_t count = gatling ? (protectedContact ? 4u : 7u) :
                    projectile ? bursts*3u+(heavy?6u:2u) : bursts*6u;
                for(uint32_t index=0; index<count && particles_.size()<128; ++index) {
                    RailVehicleImpactParticle particle;
                    particle.metal=gatling ? index==count-1 : index>=count-4;
                    const Vector3 spread=Add(Add(Scale(vehicle->right,HashSigned(result.sequence,index*3)),
                        Scale(vehicle->up,0.25f+std::abs(HashSigned(result.sequence,index*3+1)))),
                        Scale(vehicle->forward,HashSigned(result.sequence,index*3+2)));
                    const Vector3 directionOut=NormalizeOr(Add(Scale(spread,input.settings.sparkSpread/0.75f),
                        projectile?Scale(normalWorld,1.05f):Scale(worldDirection,.65f)),vehicle->up);
                    particle.origin=particle.position=particle.previousPosition=responseWorldPosition_;
                    particle.velocity=Add(Scale(directionOut,(particle.metal ? 2.5f : projectile?8.0f:5.5f)*(heavy?1.28f:contact?1.25f:1.0f)),
                        Scale(vehicle->forward,vehicle->speed*0.65f));
                    particle.lifetime=(particle.metal ? 0.48f : 0.18f+0.12f*std::abs(HashSigned(result.sequence,index+70)))
                        *input.settings.sparkLifetimeSeconds/0.38f * (gatling ? .42f : 1.0f);
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
    float heavyHeave=0, heavyBank=0, heavyPitch=0;
    float gatlingHeave=0, gatlingBank=0, gatlingPitch=0;
    float rearHeave=0, rearBank=0, rearPitch=0;
    float projectileHeave=0, projectileBank=0, projectilePitch=0;
    Vector2 projectilePushLocal{};
    if(vehicle!=nullptr && vehicle->initialized) for(const auto& response:responses_) {
        if(response.intensity<=0) continue;
        frame_.activeResponseRemainingSeconds=(std::max)(frame_.activeResponseRemainingSeconds,
            response.duration-response.elapsed);
        const float age = response.elapsed;
        using RailRearCartImpact::Pulse;
        if (response.kind==RailVehicleImpactKind::VeilLightning) {
            rearHeave+=2.7f*Pulse(age,0,.033f,.193f)-.21f*Pulse(age,.151f,.197f,response.duration);
            frame_.heavyCameraOffset.y-=.10f*Pulse(age,0,.021f,.091f);
            continue;
        }
        if (response.kind==RailVehicleImpactKind::VeilBeam) {
            const float pitchPulse=Pulse(age,0,.057f,response.duration);
            rearHeave+=.46f*pitchPulse;
            rearPitch+=2.05f*pitchPulse;
            continue;
        }
        if (response.kind==RailVehicleImpactKind::Wing) {
            const float hop=Pulse(age,0,.047f,.107f)-.16f*Pulse(age,.065f,.103f,response.duration);
            rearHeave-=.34f*hop;
            rearBank-=.48f*response.directionLocal.x*hop;
            continue;
        }
        if (response.kind == RailVehicleImpactKind::Heavy) {
            using namespace RailCartImpactEnergy;
            const float compression = Smooth(age,0.0f,.035f) * (1.0f-Smooth(age,.035f,.176f));
            const float rebound = .20f * Smooth(age,HeavyCoverArrival,HeavyReboundPeak) *
                (1.0f-Smooth(age,HeavyReboundPeak,HeavySettleSeconds));
            const float spring = compression - rebound;
            heavyHeave += spring;
            heavyBank += response.directionLocal.x * spring;
            heavyPitch += response.directionLocal.z * spring;
            // One short, zero-ended camera displacement with a small return.
            if (age < kHeavyShakeSeconds) {
                const float jolt = SmoothStep(age/.018f) * (1.0f-SmoothStep((age-.018f)/.056f));
                const float returnJolt = .22f * SmoothStep((age-.055f)/.023f) *
                    (1.0f-SmoothStep((age-.078f)/(kHeavyShakeSeconds-.078f)));
                const float kick = jolt - returnJolt;
                frame_.heavyCameraOffset = {-.15f * response.directionLocal.x * kick, -.10f * kick};
            }
            // The axles stay on the rail while the chassis compresses.
            continue;
        }
        if (response.kind == RailVehicleImpactKind::Gatling) {
            const float tap = SmoothStep(age/.016f) * (1.0f-SmoothStep((age-.016f)/.022f));
            const float hop = RailCartImpactEnergy::GatlingFlash(age) * SmoothStep(age/.018f);
            const float rattle = std::sin(kTau * 13.0f * age) *
                SmoothStep(age/.021f) * (1.0f-SmoothStep((age-.021f)/.129f));
            const float spring = response.intensity * (.55f*tap - hop + .24f*rattle);
            gatlingHeave += spring;
            gatlingBank += response.directionLocal.x * spring;
            gatlingPitch += response.directionLocal.z * spring;
            continue;
        }
        if(response.kind==RailVehicleImpactKind::Projectile) {
            const float time=age/response.duration*.40f;
            const float compression=Pulse(time,0,.036f,.142f);
            const float rebound=.34f*Pulse(time,.103f,.190f,.302f);
            const float settle=.075f*Pulse(time,.255f,.305f,.40f);
            const float spring=response.intensity*(compression-rebound+settle);
            const float x=response.directionLocal.x,z=response.directionLocal.z;
            const float roll=std::abs(x)>.001f?std::copysign(.45f+.55f*std::abs(x),x):0.0f;
            projectileHeave+=spring;
            projectileBank+=roll*spring;
            projectilePitch+=z*spring;
            projectilePushLocal.x += x * spring;
            projectilePushLocal.y += z * spring;
            continue;
        }
        const float t=(std::clamp)(response.elapsed/response.duration,0.0f,1.0f);
        // Ease in with zero velocity, one compression/rebound, ease out to rest.
        const float cycles=(std::clamp)(input.settings.oscillationFrequencyHz*response.duration,0.8f,1.2f);
        const float onset=(std::min)(1.0f,t/0.12f);
        const float easedOnset=onset*onset*(3.0f-2.0f*onset);
        float pulse=2.2f*std::sin(kTau*cycles*t)*std::exp(-2.0f*t)*(1-t)*(1-t)*easedOnset;
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
    }
    if(vehicle!=nullptr) {
        const float bodyTravel = input.settings.bodyKickDistance * (
            (std::clamp)(heave,-.30f,1.0f) + 2.25f*(std::clamp)(heavyHeave,-.20f,1.0f) +
            .45f*(std::clamp)(gatlingHeave,-1.15f,.60f) + (std::clamp)(rearHeave,-.48f,2.7f) +
            2.2f*(std::clamp)(projectileHeave,-.40f,1.0f));
        // Keep overlapping rounds within the recoil envelope in every axis.
        const Vector3 projectilePush = Add(
            Scale(vehicle->right, -.12f * (std::clamp)(projectilePushLocal.x, -1.0f, 1.0f)),
            Scale(vehicle->forward, -.10f * (std::clamp)(projectilePushLocal.y, -1.0f, 1.0f)));
        frame_.bodyTranslationWorld = Add(Scale(vehicle->up,-bodyTravel),projectilePush);
        frame_.bodyBankDegrees = input.settings.maximumBankDegrees * (
            (std::clamp)(bank,-1.0f,1.0f) + 1.8f*(std::clamp)(heavyBank,-1.0f,1.0f) +
            .30f*(std::clamp)(gatlingBank,-1.0f,1.0f) + (std::clamp)(rearBank,-.57f,.57f) +
            2.2f*(std::clamp)(projectileBank,-1.0f,1.0f));
        frame_.bodyPitchDegrees = input.settings.maximumPitchDegrees * (
            (std::clamp)(pitch,-1.0f,1.0f) + 1.35f*(std::clamp)(heavyPitch,-1.0f,1.0f) +
            .32f*(std::clamp)(gatlingPitch,-1.0f,1.0f) + (std::clamp)(rearPitch,0.0f,2.05f) +
            1.8f*(std::clamp)(projectilePitch,-1.0f,1.0f));
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
