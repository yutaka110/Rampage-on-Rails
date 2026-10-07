#include "EnemyFormationSystem.h"
#include "../diagnostics/DronePerformanceProfile.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <unordered_map>
#include <utility>

#include "CourseSpawnRuntime.h"

namespace {
std::string FormationId(const CourseEnemyActor& actor) {
    if (!actor.desc.formationDefinition.definitionId.empty()) {
        return actor.desc.formationDefinition.definitionId;
    }
    if (!actor.desc.waveId.empty()) return actor.desc.waveId;
    return actor.desc.sourcePlacementGuid.empty()
        ? "actor:" + std::to_string(actor.actorId)
        : "placement:" + actor.desc.sourcePlacementGuid;
}

void AuthoredPosition(const CourseEnemyActor& actor,
                      float& forward, float& lateral, float& vertical) {
    if (actor.behaviorState.initialized) {
        forward = actor.behaviorState.authoredForwardOffset;
        lateral = actor.behaviorState.authoredLateralOffset;
        vertical = actor.behaviorState.authoredVerticalOffset;
    } else {
        forward = actor.desc.distanceOffset;
        lateral = actor.desc.lateralOffset;
        vertical = actor.desc.verticalOffset;
    }
}

void ProceduralSlot(EnemyFormationPattern pattern, uint32_t index,
                    uint32_t count, const EnemyFormationDefinition& definition,
                    float& forward, float& lateral, float& vertical) {
    const float center = (static_cast<float>(count) - 1.0f) * 0.5f;
    const float signedIndex = static_cast<float>(index) - center;
    forward = 0.0f; lateral = 0.0f; vertical = 0.0f;
    switch (pattern) {
    case EnemyFormationPattern::Authored: break;
    case EnemyFormationPattern::V:
        lateral = signedIndex * definition.slotSpacing;
        forward = std::abs(signedIndex) * definition.slotSpacing * 0.72f;
        vertical = -std::abs(signedIndex) * definition.verticalSpacing * 0.35f;
        break;
    case EnemyFormationPattern::LineAbreast:
        lateral = signedIndex * definition.slotSpacing;
        break;
    case EnemyFormationPattern::Column:
        forward = static_cast<float>(index) * definition.slotSpacing;
        vertical = signedIndex * definition.verticalSpacing * 0.25f;
        break;
    case EnemyFormationPattern::EchelonLeft:
    case EnemyFormationPattern::EchelonRight: {
        const float side = pattern == EnemyFormationPattern::EchelonLeft ? -1.0f : 1.0f;
        forward = static_cast<float>(index) * definition.slotSpacing * 0.65f;
        lateral = side * static_cast<float>(index) * definition.slotSpacing;
        vertical = static_cast<float>(index) * definition.verticalSpacing * 0.25f;
        break;
    }
    case EnemyFormationPattern::Ring: {
        const float angle = count > 0
            ? static_cast<float>(index) * 2.0f * std::numbers::pi_v<float> /
                static_cast<float>(count) : 0.0f;
        lateral = std::cos(angle) * definition.slotSpacing;
        vertical = std::sin(angle) * definition.slotSpacing * 0.62f;
        break;
    }
    }
}
}

void EnemyFormationSystem::Reset() {
    frame_ = {};
    revision_ = 0;
}

void EnemyFormationSystem::BeginFrame(CourseSpawnRuntime& runtime) {
    runtime.BeginEnemyFormationFrame(*this);
}

void EnemyFormationSystem::BeginFrameActors(std::span<CourseEnemyActor> actors,
        CourseSpawnRuntime& runtime) {
    for (CourseEnemyActor& actor : actors) {
        EnemyFormationMemberRuntimeState& member = actor.formationState;
        if (!member.initialized) continue;
        actor.desc.distanceOffset -= member.appliedForwardOffset;
        actor.desc.lateralOffset -= member.appliedLateralOffset;
        actor.desc.verticalOffset -= member.appliedVerticalOffset;
        member.appliedForwardOffset = 0.0f;
        member.appliedLateralOffset = 0.0f;
        member.appliedVerticalOffset = 0.0f;
    }
}

bool EnemyFormationSystem::SetDefinition(
    std::string formationId,
    EnemyFormationDefinition definition,
    std::string* errorMessage) {
    if (formationId.empty() || !definition.Validate(errorMessage)) return false;
    definition.definitionId = formationId;
    definitions_[formationId] = std::move(definition);
    ++revision_;
    return true;
}

void EnemyFormationSystem::ClearDefinition(const std::string& formationId) {
    if (definitions_.erase(formationId) > 0) ++revision_;
}

const EnemyFormationDefinition* EnemyFormationSystem::FindDefinition(
    const std::string& formationId) const noexcept {
    const auto found = definitions_.find(formationId);
    return found == definitions_.end() ? nullptr : &found->second;
}

EnemyFormationDefinition EnemyFormationSystem::ResolveDefinition(
    const std::string& formationId) const {
    const EnemyFormationDefinition* definition = FindDefinition(formationId);
    return definition != nullptr
        ? *definition : EnemyFormationDefinition::CommercialDefault(formationId);
}

void EnemyFormationSystem::Update(CourseSpawnRuntime& runtime, float deltaTime, const RailPath* railPath) {
    const drone_perf::Scope profile(drone_perf::Stage::Formation);
    runtime.UpdateEnemyFormations(*this, deltaTime, railPath);
}

void EnemyFormationSystem::UpdateActors(std::span<CourseEnemyActor> actors,
        CourseSpawnRuntime& runtime,
        float deltaTime, const RailPath* railPath) {
    frame_ = {};
    const float dt = std::isfinite(deltaTime)
        ? (std::clamp)(deltaTime, 0.0f, 0.25f) : 0.0f;
    std::unordered_map<std::string, std::vector<CourseEnemyActor*>> groups;
    for (CourseEnemyActor& actor : actors) {
        const std::string formationId = FormationId(actor);
        const bool explicitlyAuthored =
            !actor.desc.formationDefinition.definitionId.empty();
        if (!explicitlyAuthored && FindDefinition(formationId) == nullptr) {
            continue;
        }
        groups[formationId].push_back(&actor);
    }

    for (auto& [formationId, actors] : groups) {
        std::sort(actors.begin(), actors.end(), [](const auto* left, const auto* right) {
            return left->actorId < right->actorId;
        });
        EnemyFormationDefinition definition = ResolveDefinition(formationId);
        for (CourseEnemyActor* actor : actors) {
            if (!actor->desc.formationDefinition.definitionId.empty()) {
                definition = actor->desc.formationDefinition;
                break;
            }
        }
        if (!definition.Validate(nullptr) || !definition.commercialFormation) continue;
        ++frame_.formations;
        frame_.members += static_cast<uint32_t>(actors.size());
        if (actors.size() <= 1) ++frame_.singleActors;

        float authoredForwardCenter = 0.0f;
        float authoredLateralCenter = 0.0f;
        float authoredVerticalCenter = 0.0f;
        float currentForwardDelta = 0.0f;
        float currentLateralDelta = 0.0f;
        float currentVerticalDelta = 0.0f;
        for (CourseEnemyActor* actor : actors) {
            float authoredForward = 0.0f, authoredLateral = 0.0f, authoredVertical = 0.0f;
            AuthoredPosition(*actor, authoredForward, authoredLateral, authoredVertical);
            authoredForwardCenter += authoredForward;
            authoredLateralCenter += authoredLateral;
            authoredVerticalCenter += authoredVertical;
            currentForwardDelta += actor->desc.distanceOffset - authoredForward;
            currentLateralDelta += actor->desc.lateralOffset - authoredLateral;
            currentVerticalDelta += actor->desc.verticalOffset - authoredVertical;
        }
        const float inverseCount = 1.0f / static_cast<float>(actors.size());
        authoredForwardCenter *= inverseCount;
        authoredLateralCenter *= inverseCount;
        authoredVerticalCenter *= inverseCount;
        currentForwardDelta *= inverseCount;
        currentLateralDelta *= inverseCount;
        currentVerticalDelta *= inverseCount;
        const float response = 1.0f - std::exp(-definition.cohesionResponse * dt);

        for (uint32_t index = 0; index < actors.size(); ++index) {
            CourseEnemyActor& actor = *actors[index];
            EnemyFormationMemberRuntimeState& member = actor.formationState;
            const bool newlyInitialized = !member.initialized ||
                member.formationId != formationId ||
                member.slotIndex != index || member.slotCount != actors.size();
            member.initialized = true;
            member.formationId = formationId;
            member.slotIndex = index;
            member.slotCount = static_cast<uint32_t>(actors.size());
            member.leader = index == 0;

            float authoredForward = 0.0f, authoredLateral = 0.0f, authoredVertical = 0.0f;
            AuthoredPosition(actor, authoredForward, authoredLateral, authoredVertical);
            if (definition.preserveAuthoredSlots ||
                definition.pattern == EnemyFormationPattern::Authored) {
                member.slotForwardOffset = authoredForward - authoredForwardCenter;
                member.slotLateralOffset = authoredLateral - authoredLateralCenter;
                member.slotVerticalOffset = authoredVertical - authoredVerticalCenter;
            } else {
                ProceduralSlot(definition.pattern, index,
                    static_cast<uint32_t>(actors.size()), definition,
                    member.slotForwardOffset, member.slotLateralOffset,
                    member.slotVerticalOffset);
            }
            const float desiredForward = authoredForwardCenter +
                member.slotForwardOffset + currentForwardDelta;
            const float desiredLateral = authoredLateralCenter +
                member.slotLateralOffset + currentLateralDelta;
            const float desiredVertical = authoredVerticalCenter +
                member.slotVerticalOffset + currentVerticalDelta;
            const auto clampCorrection = [&](float value) {
                return (std::clamp)(value, -definition.maximumCorrection,
                    definition.maximumCorrection);
            };
            const bool individualHover=actor.desc.meshId=="twin_shield_hull" &&
                actor.behaviorDefinition.commercialBehavior &&
                actor.behaviorDefinition.maintainForwardEngagementBand &&
                !actor.behaviorDefinition.choreographedAttackPass;
            const float targetForward = clampCorrection(
                desiredForward - actor.desc.distanceOffset)*(individualHover?0.05f:1.0f);
            const float targetLateral = clampCorrection(
                desiredLateral - actor.desc.lateralOffset)*(individualHover?0.15f:1.0f);
            const float targetVertical = clampCorrection(
                desiredVertical - actor.desc.verticalOffset)*(individualHover?0.15f:1.0f);
            member.smoothedForwardCorrection +=
                (targetForward - member.smoothedForwardCorrection) * response;
            member.smoothedLateralCorrection +=
                (targetLateral - member.smoothedLateralCorrection) * response;
            member.smoothedVerticalCorrection +=
                (targetVertical - member.smoothedVerticalCorrection) * response;
            member.appliedForwardOffset = member.smoothedForwardCorrection;
            member.appliedLateralOffset = member.smoothedLateralCorrection;
            member.appliedVerticalOffset = member.smoothedVerticalCorrection;
            actor.desc.distanceOffset += member.appliedForwardOffset;
            actor.desc.lateralOffset += member.appliedLateralOffset;
            actor.desc.verticalOffset += member.appliedVerticalOffset;
            if (newlyInitialized && index > 0) {
                actor.behaviorState.attackCooldownRemaining +=
                    definition.attackStaggerSeconds * static_cast<float>(index);
                actor.fireTimer = (std::max)(actor.fireTimer,
                    actor.behaviorState.attackCooldownRemaining);
            }
            if (std::abs(member.appliedForwardOffset) > 0.001f ||
                std::abs(member.appliedLateralOffset) > 0.001f ||
                std::abs(member.appliedVerticalOffset) > 0.001f) {
                ++frame_.correctedMembers;
            }
            member.revision = ++revision_;
        }
    }
    ApplyHoverSpacing(actors,dt,railPath);
    frame_.revision = revision_;
}

void EnemyFormationSystem::ApplyHoverSpacing(std::span<CourseEnemyActor> actors, float dt, const RailPath* railPath) {
    struct HoverSlot {
        CourseEnemyActor* actor;
        float forward, lateral, vertical;
        float goalForward, goalLateral, goalVertical;
        bool active;
        bool enteringHome;
    };
    std::vector<HoverSlot> slots;
    const auto noise=[](uint32_t id,uint32_t salt) {
        uint32_t bits=id^salt;
        bits^=bits>>16; bits*=0x7feb352du; bits^=bits>>15;
        bits*=0x846ca68bu; bits^=bits>>16;
        return static_cast<float>(bits&0xffffu)/65535.0f*2.0f-1.0f;
    };
    for(auto& actor:actors) {
        const auto& behavior=actor.behaviorDefinition;
        auto& member=actor.formationState;
        const bool hovering=actor.desc.meshId=="twin_shield_hull" &&
            behavior.commercialBehavior && behavior.maintainForwardEngagementBand &&
            !behavior.choreographedAttackPass;
        if(!hovering && !member.hoverInitialized) continue;
        const bool active=hovering && actor.desc.hitPoints>0.0f &&
            actor.combatState.phase!=EnemyCombatPhase::Dying &&
            actor.combatState.phase!=EnemyCombatPhase::Retired &&
            !actor.behaviorState.engagementBandExitRequested &&
            !actor.entranceExitState.exitRequested;
        if(!member.initialized) {
            member.initialized=true; member.formationId=FormationId(actor);
        }
        const bool enteringHome=!member.hoverInitialized && actor.age<0.30f;
        member.hoverInitialized=true;
        const float authoredSide=actor.behaviorState.authoredLateralOffset;
        const float spreadBias=authoredSide < -1.0f ? -2.5f : authoredSide > 1.0f ? 2.5f : 0.0f;
        // Homes depend only on stable actor identity, never the live count or
        // slot index. New waves and defeated neighbors cannot reassign them.
        slots.push_back({&actor,actor.desc.spawnDistance+actor.desc.distanceOffset,
            actor.desc.lateralOffset,actor.desc.verticalOffset,
            active?0.0f:member.hoverForwardOffset,
            active?spreadBias+noise(actor.actorId,0x912f56cdu)*5.0f:member.hoverLateralOffset,
            active?8.0f+noise(actor.actorId,0x36b7a981u)*3.5f-actor.desc.verticalOffset:
                member.hoverVerticalOffset,active,enteringHome});
    }
    std::sort(slots.begin(),slots.end(),[](const auto& a,const auto& b){
        return a.actor->actorId<b.actor->actorId;
    });
    const auto bound=[railPath](HoverSlot& slot) {
        slot.goalForward=(std::clamp)(slot.goalForward,-3.0f,3.0f);
        slot.goalLateral=(std::clamp)(slot.goalLateral,-8.0f,8.0f);
        const float floor=(std::max)(3.0f,slot.actor->desc.radius*2.4f);
        slot.goalVertical=(std::clamp)(slot.goalVertical,(std::max)(-6.0f,floor-slot.vertical),
            (std::max)(9.0f,floor-slot.vertical));
        if(railPath && railPath->Length()>0.0f) {
            const auto sample=railPath->Evaluate(slot.forward+slot.goalForward);
            const auto& scale=slot.actor->desc.localScale;
            const float bodyScale=(std::max)({1.0f,std::abs(scale.x),std::abs(scale.y),std::abs(scale.z)});
            const float clearance=(std::max)(floor+0.5f,
                sample.corridorRadius*0.92f-slot.actor->desc.radius*2.15f*bodyScale-0.7f);
            const float height=(std::clamp)(slot.vertical+slot.goalVertical,floor,clearance-0.25f);
            const float sideLimit=std::sqrt((std::max)(0.0f,clearance*clearance-height*height));
            slot.goalVertical=height-slot.vertical;
            slot.goalLateral=(std::clamp)(slot.lateral+slot.goalLateral,-sideLimit,sideLimit)-slot.lateral;
        }
    };
    for(auto& slot:slots) if(slot.active) bound(slot);
    // Solve all nearby waves together using snapshot goals, then move toward
    // the result. A depth allowance still encourages screen-plane clearance.
    for(int iteration=0;iteration<6;++iteration) {
        for(size_t i=0;i<slots.size();++i) for(size_t j=i+1;j<slots.size();++j) {
            auto& a=slots[i];auto& b=slots[j];
            if(!a.active || !b.active || std::abs(a.forward-b.forward)>24.0f) continue;
            const float size=(std::max)(1.0f,(a.actor->desc.radius+b.actor->desc.radius)/2.1f);
            const float depth=60.0f*size,width=9.0f*size,height=6.0f*size;
            float x=(a.forward+a.goalForward-b.forward-b.goalForward)/depth;
            float y=(a.lateral+a.goalLateral-b.lateral-b.goalLateral)/width;
            float z=(a.vertical+a.goalVertical-b.vertical-b.goalVertical)/height;
            float distance=std::sqrt(x*x+y*y+z*z);
            if(distance>=1.0f) continue;
            float divisor=distance;
            if(distance<0.001f) {x=0.23f;y=-0.92f;z=0.32f;distance=0.0f;divisor=1.0f;}
            const float push=(1.0f-distance)*0.52f/divisor;
            a.goalForward+=x*push*depth;b.goalForward-=x*push*depth;
            a.goalLateral+=y*push*width;b.goalLateral-=y*push*width;
            a.goalVertical+=z*push*height;b.goalVertical-=z*push*height;
            bound(a);bound(b);
        }
    }
    for(auto& slot:slots) {
        auto& actor=*slot.actor;auto& member=actor.formationState;
        if(slot.active) {
            // Enter directly into the private region while entrance staging
            // still hides the actor. Visible survivors always move continuously.
            if(slot.enteringHome) {
                member.hoverForwardOffset=slot.goalForward;
                member.hoverLateralOffset=slot.goalLateral;
                member.hoverVerticalOffset=slot.goalVertical;
            }
            const bool warning=actor.behaviorState.state==EnemyBehaviorState::Aiming ||
                (actor.behaviorState.attackIntentActive && actor.attackState.tokenReserved);
            const float response=1.0f-std::exp(-1.8f*dt);
            float df=(slot.goalForward-member.hoverForwardOffset)*response;
            float dl=(slot.goalLateral-member.hoverLateralOffset)*response;
            float dv=(slot.goalVertical-member.hoverVerticalOffset)*response;
            const float length=std::sqrt(df*df+dl*dl+dv*dv);
            const float limit=(warning?0.65f:2.0f)*dt;
            const float scale=length>limit && length>0.0001f ? limit/length:1.0f;
            member.hoverForwardOffset+=df*scale;
            member.hoverLateralOffset+=dl*scale;
            member.hoverVerticalOffset+=dv*scale;
        }
        // Frozen offsets also accompany hit/death/exit poses: removing a home
        // at those transitions would make the rendered and collision body jump.
        member.appliedForwardOffset+=member.hoverForwardOffset;
        member.appliedLateralOffset+=member.hoverLateralOffset;
        member.appliedVerticalOffset+=member.hoverVerticalOffset;
        actor.desc.distanceOffset+=member.hoverForwardOffset;
        actor.desc.lateralOffset+=member.hoverLateralOffset;
        actor.desc.verticalOffset+=member.hoverVerticalOffset;
        member.revision=++revision_;
        if(slot.active) {
            auto& state=actor.behaviorState;
            const auto& definition=actor.behaviorDefinition;
            state.engagementBandForwardDistance+=member.appliedForwardOffset;
            state.engagementBandAttackAllowed=state.engagementBandForwardDistance>=
                definition.engagementBandMinimumForwardDistance && state.engagementBandForwardDistance<=
                definition.engagementBandMaximumForwardDistance && !state.engagementBandExitRequested;
        }
    }
}
