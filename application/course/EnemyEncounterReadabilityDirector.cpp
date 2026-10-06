#include "EnemyEncounterReadabilityDirector.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <unordered_map>
#include <utility>

#include "CourseSpawnRuntime.h"
#include "EnemyAttackDefensePresentationBridge.h"
#include "EnemyAttackTelegraphSystem.h"
#include "EnemyProjectilePresentationBridge.h"
#include "../terrain/RailPath.h"

namespace {
constexpr float kEpsilon = 0.00001f;

Vector3 Add(const Vector3& left, const Vector3& right) noexcept {
    return {left.x + right.x, left.y + right.y, left.z + right.z};
}
Vector3 Scale(const Vector3& value, float scale) noexcept {
    return {value.x * scale, value.y * scale, value.z * scale};
}

struct Projection final {
    Vector2 screen{};
    float clipW = 0.0f;
    bool onScreen = false;
    bool behind = false;
};

Projection Project(
    const Vector3& world,
    const Matrix4x4& matrix,
    uint32_t width,
    uint32_t height,
    float margin) noexcept {
    const float clipX = world.x * matrix.m[0][0] +
        world.y * matrix.m[1][0] + world.z * matrix.m[2][0] + matrix.m[3][0];
    const float clipY = world.x * matrix.m[0][1] +
        world.y * matrix.m[1][1] + world.z * matrix.m[2][1] + matrix.m[3][1];
    const float clipZ = world.x * matrix.m[0][2] +
        world.y * matrix.m[1][2] + world.z * matrix.m[2][2] + matrix.m[3][2];
    const float clipW = world.x * matrix.m[0][3] +
        world.y * matrix.m[1][3] + world.z * matrix.m[2][3] + matrix.m[3][3];
    Projection result{};
    result.clipW = clipW;
    result.behind = clipW <= kEpsilon;
    const float divisor = std::abs(clipW) > kEpsilon ? std::abs(clipW) : 1.0f;
    float ndcX = clipX / divisor;
    float ndcY = clipY / divisor;
    if (result.behind) { ndcX = -ndcX; ndcY = -ndcY; }
    const float depth = clipW > kEpsilon ? clipZ / clipW : -1.0f;
    result.screen = {
        (ndcX * 0.5f + 0.5f) * static_cast<float>(width),
        (1.0f - (ndcY * 0.5f + 0.5f)) * static_cast<float>(height)};
    const float safeMargin = (std::clamp)(margin, 0.0f,
        (std::min)(static_cast<float>(width), static_cast<float>(height)) * 0.45f);
    result.onScreen = !result.behind && depth >= 0.0f && depth <= 1.0f &&
        result.screen.x >= safeMargin &&
        result.screen.x <= static_cast<float>(width) - safeMargin &&
        result.screen.y >= safeMargin &&
        result.screen.y <= static_cast<float>(height) - safeMargin;
    return result;
}

bool RoleIsBoss(const std::string& role) {
    return role.find("boss") != std::string::npos ||
        role.find("gatekeeper") != std::string::npos;
}
}

void EnemyEncounterReadabilityDirector::Reset() {
    truthGate_.Reset();
    tracked_.clear();
    frame_ = {};
    revision_ = 0;
}

void EnemyEncounterReadabilityDirector::Update(
    const EnemyEncounterReadabilityInput& input) {
    EnemyEncounterReadabilityFrame next{};
    next.revision = ++revision_;

    CombatTruthGateInput truthInput{};
    truthInput.runtime = input.runtime;
    truthInput.telegraph = input.telegraph;
    truthInput.projectiles = input.projectiles;
    truthInput.defense = input.defense;
    truthInput.damageResults = input.damageResults;
    truthInput.activeWaves = input.activeWaves;
    truthInput.deltaTime = input.deltaTime;
    truthInput.gameplayActive = input.gameplayActive;
    truthGate_.Update(truthInput, input.settings.truth);
    next.truth = truthGate_.Frame();

    const bool projectionValid = input.runtime != nullptr &&
        input.railPath != nullptr && input.viewProjection != nullptr &&
        input.railPath->Length() > 0.0f && input.viewportWidth > 0 &&
        input.viewportHeight > 0;
    if (!projectionValid) {
        if (input.runtime != nullptr) {
            input.runtime->ResetEnemyScreenPresence();
        }
        frame_ = std::move(next);
        return;
    }

    const float dt = std::isfinite(input.deltaTime)
        ? (std::clamp)(input.deltaTime, 0.0f, 0.25f) : 0.0f;
    std::unordered_map<uint32_t, const EnemyAttackTelegraphCue*> warnings;
    if (input.telegraph != nullptr) {
        for (const EnemyAttackTelegraphCue& cue : input.telegraph->cues) {
            const auto found = warnings.find(cue.actorId);
            if (found == warnings.end() ||
                found->second->priority < cue.priority) {
                warnings[cue.actorId] = &cue;
            }
        }
    }

    for (const CourseEnemyActor& actor : input.runtime->Enemies()) {
        const RailPathSample sample = input.railPath->Evaluate(
            actor.desc.spawnDistance + actor.desc.distanceOffset);
        const Vector3 world = Add(
            Add(sample.position,
                Scale(sample.right, actor.desc.lateralOffset)),
            Scale(sample.up, actor.desc.verticalOffset));
        const Projection center = Project(
            world, *input.viewProjection, input.viewportWidth,
            input.viewportHeight, input.settings.safeAreaPixels);
        const Projection edge = Project(
            Add(world, Scale(sample.up, (std::max)(0.05f, actor.desc.radius))),
            *input.viewProjection, input.viewportWidth,
            input.viewportHeight, input.settings.safeAreaPixels);
        const float dx = edge.screen.x - center.screen.x;
        const float dy = edge.screen.y - center.screen.y;
        const float diameter = center.behind || edge.behind
            ? 0.0f : 2.0f * std::sqrt(dx * dx + dy * dy);

        const auto warningIt = warnings.find(actor.actorId);
        const EnemyAttackTelegraphCue* warning =
            warningIt == warnings.end() ? nullptr : warningIt->second;
        const bool attackEngaged =
            actor.behaviorState.attackIntentActive ||
            actor.attackState.tokenReserved || warning != nullptr;
        const bool targetable =
            (!actor.combatState.initialized ||
             actor.combatState.canBeTargeted) &&
            (!actor.entranceExitState.initialized ||
             actor.entranceExitState.targetable);

        EnemyScreenPresenceInput presenceInput{};
        presenceInput.projectedDiameterPixels = diameter;
        presenceInput.authoredAlpha = actor.combatState.initialized
            ? actor.combatState.presentationAlpha : 1.0f;
        presenceInput.onScreen = center.onScreen;
        presenceInput.behindCamera = center.behind;
        presenceInput.targetable = targetable;
        presenceInput.attackEngaged = attackEngaged;
        presenceInput.boss = RoleIsBoss(actor.desc.role);
        presenceInput.readableOffscreenWarning = warning != nullptr &&
            !warning->occluded && warning->phase != EnemyAttackTelegraphPhase::None;
        presenceInput.settings = input.settings.presence;
        if (actor.desc.actorAssetId == "drone_scout" &&
            input.playerDistance >= 0.0f &&
            input.playerDistance < input.settings.openingScoutEndDistance) {
            // The first targets teach the combat silhouette. Enlarge only the
            // presentation proxy; aiming and collision retain the authored size.
            presenceInput.settings.minimumIdleDiameterPixels = (std::max)(
                presenceInput.settings.minimumIdleDiameterPixels,
                input.settings.openingScoutIdleDiameterPixels);
            presenceInput.settings.minimumEngagedDiameterPixels = (std::max)(
                presenceInput.settings.minimumEngagedDiameterPixels,
                input.settings.openingScoutEngagedDiameterPixels);
        }
        const EnemyScreenPresenceResult presence =
            presencePolicy_.Evaluate(presenceInput);

        TrackedActor& tracked = tracked_[actor.actorId];
        tracked.touchedRevision = next.revision;
        if (presence.attackPresentationCandidate) {
            tracked.readableExposureSeconds += dt;
        } else {
            tracked.readableExposureSeconds = (std::max)(
                0.0f,
                tracked.readableExposureSeconds -
                    dt * (std::max)(0.0f,
                        input.settings.exposureDecayPerSecond));
        }
        const bool attackReady = center.onScreen && presence.screenReadable &&
            tracked.readableExposureSeconds >=
            (std::max)(0.0f,
                input.settings.minimumAttackExposureSeconds);
        input.runtime->SetEnemyScreenPresence(actor.actorId, input.gameplayActive,
            !input.gameplayActive || attackReady);

        EnemyEncounterActorReadability output{};
        output.actorId = actor.actorId;
        output.worldPosition = world;
        output.screenPosition = center.screen;
        output.projectedDiameterPixels = diameter;
        output.presentationScale = presence.presentationScale;
        output.presentationAlpha = presence.presentationAlpha;
        output.colorBoost = presence.colorBoost;
        output.priority = presence.priority;
        output.readableExposureSeconds = tracked.readableExposureSeconds;
        output.onScreen = center.onScreen;
        output.behindCamera = center.behind;
        output.targetable = targetable;
        output.attackEngaged = attackEngaged;
        output.fixedSizeProxyApplied = presence.fixedSizeProxyApplied;
        output.screenReadable = presence.screenReadable;
        output.warningReadable = presence.warningReadable;
        output.attackPresentationReady = attackReady;
        output.offscreenIndicatorRecommended =
            presence.offscreenIndicatorRecommended;
        next.actors.push_back(output);
        if (output.screenReadable) ++next.readableHostiles;
        if (!output.onScreen) ++next.offscreenHostiles;
        if (output.fixedSizeProxyApplied) ++next.fixedSizeProxies;
        if (!output.attackPresentationReady) ++next.attackGatedActors;
    }

    for (auto it = tracked_.begin(); it != tracked_.end();) {
        if (it->second.touchedRevision != next.revision) {
            it = tracked_.erase(it);
        } else {
            ++it;
        }
    }
    std::sort(next.actors.begin(), next.actors.end(),
        [](const auto& left, const auto& right) {
            if (left.priority != right.priority) {
                return left.priority > right.priority;
            }
            return left.actorId < right.actorId;
        });
    const size_t maximum = (std::max<size_t>)(
        1, input.settings.maximumActorProxies);
    if (next.actors.size() > maximum) {
        next.droppedActorProxies = static_cast<uint32_t>(
            next.actors.size() - maximum);
        next.actors.resize(maximum);
    }
    frame_ = std::move(next);
}

const EnemyEncounterActorReadability*
EnemyEncounterReadabilityDirector::FindActor(uint32_t actorId) const noexcept {
    const auto found = std::find_if(
        frame_.actors.begin(), frame_.actors.end(),
        [actorId](const auto& actor) { return actor.actorId == actorId; });
    return found == frame_.actors.end() ? nullptr : &*found;
}
