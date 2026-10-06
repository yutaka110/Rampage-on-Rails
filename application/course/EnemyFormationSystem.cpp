#include "EnemyFormationSystem.h"

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

void EnemyFormationSystem::Update(CourseSpawnRuntime& runtime, float deltaTime) {
    runtime.UpdateEnemyFormations(*this, deltaTime);
}

void EnemyFormationSystem::UpdateActors(std::span<CourseEnemyActor> actors,
        CourseSpawnRuntime& runtime,
        float deltaTime) {
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
            const float targetForward = clampCorrection(
                desiredForward - actor.desc.distanceOffset);
            const float targetLateral = clampCorrection(
                desiredLateral - actor.desc.lateralOffset);
            const float targetVertical = clampCorrection(
                desiredVertical - actor.desc.verticalOffset);
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
    frame_.revision = revision_;
}
