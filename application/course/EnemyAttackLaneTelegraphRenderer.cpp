#include "EnemyAttackLaneTelegraphRenderer.h"

#include "../EffectRuntime.h"
#include "../diagnostics/DebugDrawSystem.h"
#include "../terrain/RailPath.h"

#include <algorithm>
#include <cmath>

namespace {
Vector3 Add(Vector3 a, Vector3 b) noexcept {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}
Vector3 Subtract(Vector3 a, Vector3 b) noexcept {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}
Vector3 Scale(Vector3 value, float scale) noexcept {
    return {value.x * scale, value.y * scale, value.z * scale};
}
Vector3 Lerp(Vector3 a, Vector3 b, float t) noexcept {
    return Add(a, Scale(Subtract(b, a), t));
}
float LengthSquared(Vector3 value) noexcept {
    return value.x * value.x + value.y * value.y + value.z * value.z;
}
Vector3 NormalizeOr(Vector3 value, Vector3 fallback) noexcept {
    const float lengthSquared = LengthSquared(value);
    if (!std::isfinite(lengthSquared) || lengthSquared <= 0.000001f) {
        return fallback;
    }
    return Scale(value, 1.0f / std::sqrt(lengthSquared));
}
float PhaseOpacity(EnemyAttackTelegraphPhase phase) noexcept {
    switch (phase) {
    case EnemyAttackTelegraphPhase::Warming: return 0.12f;
    case EnemyAttackTelegraphPhase::Tracking: return 0.20f;
    case EnemyAttackTelegraphPhase::Imminent: return 0.42f;
    case EnemyAttackTelegraphPhase::Fired: return 0.0f;
    case EnemyAttackTelegraphPhase::None: return 0.0f;
    }
    return 0.0f;
}
EnemyAttackLaneShape ResolveShape(const EnemyAttackTelegraphCue& cue) noexcept {
    if (cue.projectileTrajectory == EnemyProjectileTrajectory::Homing) {
        return EnemyAttackLaneShape::Homing;
    }
    if (cue.projectileTrajectory == EnemyProjectileTrajectory::Arc) {
        return EnemyAttackLaneShape::Arc;
    }
    if (cue.projectileCount >= 3 ||
        cue.attackPattern == CourseEnemyFirePattern::Spread ||
        cue.attackPattern == CourseEnemyFirePattern::BossArc) {
        return EnemyAttackLaneShape::Fan;
    }
    return EnemyAttackLaneShape::Line;
}
void UpdateMarker(
    EffectRuntime& runtime,
    uint32_t id,
    const Vector3& position,
    const Vector4& color,
    float radius) {
    const EffectInstance* instance = runtime.FindInstance(id);
    if (instance == nullptr) return;
    Transform transform = instance->transform;
    transform.translate = position;
    transform.scale = {radius, radius, radius};
    if (runtime.SetInstanceAppearance(id, transform, color, true)) runtime.SetEffectPreviewLoop(id, true);
}
} // namespace

size_t EnemyAttackLaneTelegraphRenderer::LaneKeyHash::operator()(
    const LaneKey& key) const noexcept {
    const size_t first = std::hash<uint32_t>{}(key.actorId);
    const size_t second = std::hash<uint64_t>{}(key.attackIntentSequence);
    return first ^ (second + 0x9e3779b9u + (first << 6u) + (first >> 2u));
}

void EnemyAttackLaneTelegraphRenderer::Reset(EffectRuntime* effectRuntime) {
    if (effectRuntime != nullptr) {
        for (auto& [key, markers] : managedMarkers_) {
            (void)key;
            StopMarkers(markers, effectRuntime);
        }
    }
    managedMarkers_.clear();
    frame_ = {};
    revision_ = 0;
}

void EnemyAttackLaneTelegraphRenderer::Update(
    const EnemyAttackLaneTelegraphRenderInput& input) {
    frame_ = {};
    const uint64_t revision = ++revision_;
    if (!input.settings.enabled || !input.gameplayActive ||
        input.telegraph == nullptr || input.railPath == nullptr ||
        input.railPath->Length() <= 0.0f) {
        StopUntouched(input.effectRuntime, revision);
        frame_.revision = revision;
        return;
    }

    const size_t budget = (std::max)(
        static_cast<size_t>(1), input.settings.maximumVisibleLanes);
    frame_.lanes.reserve((std::min)(budget, input.telegraph->cues.size()));
    for (const EnemyAttackTelegraphCue& cue : input.telegraph->cues) {
        if (frame_.lanes.size() >= budget) {
            ++frame_.droppedByBudget;
            continue;
        }
        if (cue.phase == EnemyAttackTelegraphPhase::None ||
            cue.phase == EnemyAttackTelegraphPhase::Fired ||
            !cue.onScreen || cue.occluded || !cue.hasLockedTarget) continue;

        const RailPathSample targetSample = input.railPath->Evaluate(
            cue.targetRailDistance);
        const Vector3 target = Add(
            Add(
                targetSample.position,
                Scale(targetSample.right, cue.targetLateralOffset)),
            Scale(targetSample.up, cue.targetVerticalOffset));
        const float opacity = PhaseOpacity(cue.phase);
        const EnemyAttackTelegraphReadabilityStyle style =
            ResolveEnemyAttackTelegraphReadabilityStyle(cue.phase, cue.pulse);
        const float urgency = (std::clamp)(cue.urgency, 0.0f, 1.0f);
        const float urgencyScale = style.markerScale *
            (cue.phase == EnemyAttackTelegraphPhase::Imminent
                ? input.settings.imminentScale
                : 1.0f);
        const float pulse = 1.0f + 0.08f * std::sin(
            input.elapsedTime * 11.0f + static_cast<float>(cue.actorId % 13u));

        EnemyAttackLaneTelegraphProxy proxy{};
        proxy.actorId = cue.actorId;
        proxy.attackIntentSequence = cue.attackIntentSequence;
        proxy.attackTokenId = cue.attackTokenId;
        proxy.shape = ResolveShape(cue);
        proxy.phase = cue.phase;
        proxy.trajectory = cue.projectileTrajectory;
        proxy.startWorld = cue.worldPosition;
        proxy.targetWorld = target;
        proxy.railRight = targetSample.right;
        proxy.railUp = targetSample.up;
        proxy.opacity = opacity;
        proxy.color = style.primaryColor;
        proxy.color.w *= opacity;
        proxy.laneWidth = input.settings.baseLaneWidth * urgencyScale;
        proxy.sourceRadius = input.settings.sourceMarkerRadius *
            urgencyScale * pulse;
        proxy.targetRadius = input.settings.targetMarkerRadius *
            urgencyScale * pulse;
        proxy.urgency = urgency;
        proxy.pulse = cue.pulse;
        proxy.convergenceRadius = proxy.targetRadius *
            (std::max)(0.18f, 1.0f - urgency * 0.82f);
        proxy.flowPhase = std::fmod(
            input.elapsedTime * (0.62f + urgency * 1.10f), 1.0f);
        proxy.readabilityTier = style.tier;
        proxy.directionMarkerCount = 1;
        proxy.projectileCount = (std::max)(1, cue.projectileCount);
        proxy.openingCue = input.openingPresentation &&
            proxy.shape == EnemyAttackLaneShape::Line;

        const LaneKey key{cue.actorId, cue.attackIntentSequence};
        ManagedMarkers& markers = managedMarkers_[key];
        markers.touchedRevision = revision;
        if (input.settings.effectRuntimeEnabled && input.effectRuntime != nullptr) {
            if (markers.targetInstanceId == 0) {
                markers.targetInstanceId = input.effectRuntime->PlayEffectWithParams(
                    input.settings.markerEffectId,
                    proxy.targetWorld,
                    proxy.color,
                    {proxy.targetRadius, proxy.targetRadius, proxy.targetRadius});
                if (markers.targetInstanceId != 0) {
                    input.effectRuntime->SetEffectPreviewLoop(
                        markers.targetInstanceId, true);
                }
            }
            UpdateMarker(
                *input.effectRuntime,
                markers.sourceInstanceId,
                proxy.startWorld,
                proxy.color,
                proxy.sourceRadius);
            UpdateMarker(
                *input.effectRuntime,
                markers.targetInstanceId,
                proxy.targetWorld,
                proxy.color,
                proxy.targetRadius);
        }
        proxy.sourceEffectInstanceId = markers.sourceInstanceId;
        proxy.targetEffectInstanceId = markers.targetInstanceId;
        frame_.effectBackedMarkers +=
            (markers.sourceInstanceId != 0 ? 1u : 0u) +
            (markers.targetInstanceId != 0 ? 1u : 0u);
        frame_.lanes.push_back(std::move(proxy));
        ++frame_.productionSubmittedLanes;
    }

    StopUntouched(input.effectRuntime, revision);
    frame_.sourceTelegraphRevision = input.telegraph->revision;
    frame_.revision = revision;
}

void EnemyAttackLaneTelegraphRenderer::AppendWorldPrimitives(
    ge3::debug::DebugDrawSystem& productionDraw) const {
    for (const EnemyAttackLaneTelegraphProxy& lane : frame_.lanes) {
        Vector4 faint = lane.color;
        faint.w *= 0.32f;
        Vector4 core = lane.color;
        core.w = (std::min)(1.0f, core.w * 1.18f);
        productionDraw.AddLine(lane.startWorld, lane.targetWorld, faint, lane.color);
        const float halfWidth = lane.laneWidth * 0.5f;
        const Vector3 horizontal = Scale(lane.railRight, halfWidth);
        const Vector3 vertical = Scale(lane.railUp, halfWidth * 0.68f);
        productionDraw.AddLine(
            Add(lane.startWorld, horizontal),
            Add(lane.targetWorld, horizontal), faint, faint);
        productionDraw.AddLine(
            Subtract(lane.startWorld, horizontal),
            Subtract(lane.targetWorld, horizontal), faint, faint);
        productionDraw.AddLine(
            Add(lane.startWorld, vertical),
            Add(lane.targetWorld, vertical), faint, faint);
        productionDraw.AddLine(
            Subtract(lane.startWorld, vertical),
            Subtract(lane.targetWorld, vertical), faint, faint);
        productionDraw.AddCircle(
            lane.startWorld,
            lane.railRight,
            lane.railUp,
            lane.sourceRadius,
            core,
            20);
        productionDraw.AddCircle(
            lane.startWorld,
            lane.railRight,
            lane.railUp,
            lane.sourceRadius * 1.45f,
            faint,
            20);
        productionDraw.AddCircle(
            lane.targetWorld,
            lane.railRight,
            lane.railUp,
            lane.targetRadius,
            core,
            24);
        productionDraw.AddCircle(
            lane.targetWorld,
            lane.railRight,
            lane.railUp,
            lane.convergenceRadius,
            core,
            24);
        productionDraw.AddLine(
            Subtract(lane.targetWorld, Scale(lane.railRight, lane.targetRadius * 1.25f)),
            Add(lane.targetWorld, Scale(lane.railRight, lane.targetRadius * 1.25f)),
            faint,
            core);
        productionDraw.AddLine(
            Subtract(lane.targetWorld, Scale(lane.railUp, lane.targetRadius * 1.25f)),
            Add(lane.targetWorld, Scale(lane.railUp, lane.targetRadius * 1.25f)),
            faint,
            core);

        // Moving chevrons make the attack direction legible even without
        // colour perception and accelerate as the commit moment approaches.
        const Vector3 direction = NormalizeOr(
            Subtract(lane.targetWorld, lane.startWorld),
            {0.0f, 0.0f, 1.0f});
        for (uint32_t index = 0; index < lane.directionMarkerCount; ++index) {
            const float base = static_cast<float>(index + 1u) /
                static_cast<float>(lane.directionMarkerCount + 1u);
            const float t = 0.10f + std::fmod(
                base + lane.flowPhase * 0.14f, 0.80f);
            const Vector3 center = Lerp(lane.startWorld, lane.targetWorld, t);
            const float markerLength = lane.targetRadius * 0.42f;
            const Vector3 tip = Add(center, Scale(direction, markerLength));
            const Vector3 back = Subtract(center, Scale(direction, markerLength * 0.65f));
            productionDraw.AddLine(
                Add(back, Scale(lane.railRight, markerLength * 0.58f)),
                tip,
                faint,
                core);
            productionDraw.AddLine(
                Subtract(back, Scale(lane.railRight, markerLength * 0.58f)),
                tip,
                faint,
                core);
        }

        if (lane.shape == EnemyAttackLaneShape::Fan) {
            const int count = (std::clamp)(lane.projectileCount, 3, 5);
            for (int index = 0; index < count; ++index) {
                const float centered = static_cast<float>(index) -
                    static_cast<float>(count - 1) * 0.5f;
                const Vector3 endpoint = Add(
                    lane.targetWorld,
                    Scale(lane.railRight, centered * lane.targetRadius * 2.4f));
                productionDraw.AddLine(lane.startWorld, endpoint, faint, lane.color);
            }
        } else if (lane.shape == EnemyAttackLaneShape::Homing) {
            productionDraw.AddCircle(
                lane.targetWorld,
                lane.railRight,
                lane.railUp,
                lane.targetRadius * 1.55f,
                faint,
                24);
        } else if (lane.shape == EnemyAttackLaneShape::Arc) {
            Vector3 previous = lane.startWorld;
            for (int segment = 1; segment <= 10; ++segment) {
                const float t = static_cast<float>(segment) / 10.0f;
                Vector3 point = Lerp(lane.startWorld, lane.targetWorld, t);
                point = Add(
                    point,
                    Scale(lane.railUp, std::sin(t * 3.14159265f) *
                        lane.targetRadius * 3.0f));
                productionDraw.AddLine(previous, point, faint, lane.color);
                previous = point;
            }
        }
    }
}

void EnemyAttackLaneTelegraphRenderer::AppendProductionWorldPrimitives(
    ge3::debug::DebugDrawSystem& draw) const {
    for (const auto& lane : frame_.lanes) {
        // The opening shot teaches where a threat originates. Later encounters
        // retain the compact arrival cue so multiple lanes do not fill the view.
        const Vector3 tail = Lerp(lane.startWorld, lane.targetWorld, 0.78f);
        if (lane.openingCue) {
            Vector4 sourceColor = lane.color;
            sourceColor.w *= 0.48f;
            draw.AddLine(lane.startWorld, tail, sourceColor, lane.color);
            draw.AddCircle(lane.startWorld, lane.railRight, lane.railUp,
                lane.sourceRadius * 1.5f, lane.color, 20);
        }
        draw.AddLine(tail, lane.targetWorld, lane.color);
        if (lane.targetEffectInstanceId == 0) {
            draw.AddCircle(lane.targetWorld, lane.railRight, lane.railUp,
                lane.targetRadius, lane.color, 16);
        }
        const Vector3 direction = NormalizeOr(Subtract(lane.targetWorld, tail), {0, 0, -1});
        const Vector3 base = Subtract(lane.targetWorld, Scale(direction, lane.targetRadius * 0.70f));
        const Vector3 wing = Scale(lane.railRight, lane.targetRadius * 0.35f);
        draw.AddLine(Add(base, wing), lane.targetWorld, lane.color);
        draw.AddLine(Subtract(base, wing), lane.targetWorld, lane.color);
        if (lane.shape == EnemyAttackLaneShape::Fan) {
            // Two boundaries communicate spread without drawing every bullet
            // ray on top of the enemy, actual shots or one another.
            const Vector3 spread = Scale(lane.railRight, lane.targetRadius * 2.4f);
            draw.AddLine(tail, Add(lane.targetWorld, spread), lane.color);
            draw.AddLine(tail, Subtract(lane.targetWorld, spread), lane.color);
        }
    }
}

bool EnemyAttackLaneTelegraphRenderer::WasSubmitted(
    uint32_t actorId,
    uint64_t attackIntentSequence) const noexcept {
    return std::any_of(
        frame_.lanes.begin(),
        frame_.lanes.end(),
        [actorId, attackIntentSequence](
            const EnemyAttackLaneTelegraphProxy& lane) {
            return lane.actorId == actorId &&
                lane.attackIntentSequence == attackIntentSequence;
        });
}

void EnemyAttackLaneTelegraphRenderer::CancelActor(uint32_t actorId, EffectRuntime* runtime) {
    std::erase_if(frame_.lanes,[actorId](const auto& lane) { return lane.actorId == actorId; });
    for (auto it=managedMarkers_.begin();it!=managedMarkers_.end();) {
        if (it->first.actorId == actorId) {
            StopMarkers(it->second,runtime);
            it=managedMarkers_.erase(it);
        } else ++it;
    }
}

void EnemyAttackLaneTelegraphRenderer::StopMarkers(
    ManagedMarkers& markers,
    EffectRuntime* runtime) {
    if (runtime != nullptr) {
        if (markers.sourceInstanceId != 0) runtime->StopEffect(markers.sourceInstanceId);
        if (markers.targetInstanceId != 0) runtime->StopEffect(markers.targetInstanceId);
    }
    markers.sourceInstanceId = 0;
    markers.targetInstanceId = 0;
}

void EnemyAttackLaneTelegraphRenderer::StopUntouched(
    EffectRuntime* runtime,
    uint64_t revision) {
    for (auto it = managedMarkers_.begin(); it != managedMarkers_.end();) {
        if (it->second.touchedRevision == revision) {
            ++it;
            continue;
        }
        StopMarkers(it->second, runtime);
        it = managedMarkers_.erase(it);
    }
}

const char* ToString(EnemyAttackLaneShape shape) noexcept {
    switch (shape) {
    case EnemyAttackLaneShape::Line: return "Line";
    case EnemyAttackLaneShape::Fan: return "Fan";
    case EnemyAttackLaneShape::Homing: return "Homing";
    case EnemyAttackLaneShape::Arc: return "Arc";
    }
    return "Unknown";
}
