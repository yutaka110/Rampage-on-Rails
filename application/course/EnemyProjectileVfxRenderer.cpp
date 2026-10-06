#include "EnemyProjectileVfxRenderer.h"

#include "../EffectRuntime.h"
#include "../diagnostics/DebugDrawSystem.h"

#include <algorithm>
#include <cmath>
#include <system_error>
#include <unordered_set>

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
float Length(Vector3 value) noexcept {
    return std::sqrt(value.x * value.x + value.y * value.y + value.z * value.z);
}
Vector3 NormalizeOr(Vector3 value, Vector3 fallback) noexcept {
    const float length = Length(value);
    return length > 0.00001f ? Scale(value, 1.0f / length) : fallback;
}
Vector4 WithAlphaScale(Vector4 color, float scale) noexcept {
    color.w *= (std::clamp)(scale, 0.0f, 1.0f);
    return color;
}

void UpdateEffectInstance(
    EffectRuntime& runtime,
    uint32_t instanceId,
    const Vector3& position,
    const Vector4& color,
    float radius) {
    const EffectInstance* instance = runtime.FindInstance(instanceId);
    if (instance == nullptr) return;
    Transform transform = instance->transform;
    transform.translate = position;
    transform.scale = {radius, radius, radius};
    if (runtime.SetInstanceAppearance(instanceId, transform, color, true)) runtime.SetEffectPreviewLoop(instanceId, true);
}

bool IsRenderableEffectInstance(
    const EffectRuntime* runtime,
    uint32_t instanceId) {
    if (runtime == nullptr || instanceId == 0) return false;
    const EffectInstance* instance = runtime->FindInstance(instanceId);
    if (instance == nullptr || instance->asset == nullptr) return false;

    for (const EffectComponentInstance& componentInstance : instance->components) {
        if (!componentInstance.active) continue;
        bool renderable = false;
        instance->asset->Components().ForEachComponentCommon(
            [&](const EffectComponentCommon& common) {
                if (renderable || common.id != componentInstance.componentId ||
                    common.duration <= 0.0f) {
                    return;
                }
                if (common.type != EffectComponentType::Particle) {
                    renderable = true;
                    return;
                }
                const ParticleComponentAssetView particle =
                    FindParticleComponent(
                        instance->asset->Components().ParticleStorageView(),
                        common.id);
                renderable = particle && particle.settings->spawnCount > 0.0f;
            });
        if (renderable) return true;
    }
    return false;
}

void AppendProjectilePrimitive(
    const EnemyProjectileVfxProxy& proxy,
    ge3::debug::DebugDrawSystem& draw) {
    const float coreRadius = proxy.coreRadius;
    const float haloRadius = proxy.haloRadius;
    const Vector3 head = Add(
        proxy.worldPosition,
        Scale(proxy.motionDirection, coreRadius * 1.15f));
    const Vector3 side = Scale(
        proxy.cameraRight,
        (std::max)(proxy.trailWidth * 0.42f, coreRadius * 0.12f));
    const Vector4 tailFade = WithAlphaScale(proxy.trailColor, 0.08f);
    draw.AddLine(
        proxy.trailStart,
        head,
        tailFade,
        proxy.coreColor);
    draw.AddLine(
        Add(proxy.trailStart, side),
        Add(proxy.worldPosition, side),
        tailFade,
        WithAlphaScale(proxy.haloColor, 0.72f));
    draw.AddLine(
        Subtract(proxy.trailStart, side),
        Subtract(proxy.worldPosition, side),
        tailFade,
        WithAlphaScale(proxy.haloColor, 0.72f));
    // Navy edge plus magenta diamond remains legible against pale terrain.
    draw.AddCircle(proxy.worldPosition, proxy.cameraRight, proxy.cameraUp,
        coreRadius * 1.18f, {0.018f, 0.008f, 0.035f, 0.95f}, 24);
    const Vector3 diamondRight = Scale(proxy.cameraRight, coreRadius * 1.35f);
    const Vector3 diamondUp = Scale(proxy.cameraUp, coreRadius * 1.35f);
    draw.AddLine(Add(proxy.worldPosition, diamondRight), Add(proxy.worldPosition, diamondUp), proxy.haloColor);
    draw.AddLine(Add(proxy.worldPosition, diamondUp), Subtract(proxy.worldPosition, diamondRight), proxy.haloColor);
    draw.AddLine(Subtract(proxy.worldPosition, diamondRight), Subtract(proxy.worldPosition, diamondUp), proxy.haloColor);
    draw.AddLine(Subtract(proxy.worldPosition, diamondUp), Add(proxy.worldPosition, diamondRight), proxy.haloColor);
    draw.AddPoint(
        proxy.worldPosition,
        coreRadius,
        proxy.coreColor);
    draw.AddPoint(
        proxy.worldPosition,
        coreRadius * 0.42f,
        {1.0f, 1.0f, 1.0f, 1.0f});
    draw.AddCircle(
        proxy.worldPosition,
        proxy.cameraRight,
        proxy.cameraUp,
        haloRadius,
        proxy.haloColor,
        32);
    draw.AddCircle(
        proxy.worldPosition,
        proxy.cameraRight,
        proxy.cameraUp,
        coreRadius,
        proxy.coreColor,
        24);

    // Repeated chevrons make travel direction readable even in a still frame.
    const Vector3 tailVector = Subtract(proxy.trailStart, proxy.worldPosition);
    for (int marker = 1; marker <= 2; ++marker) {
        const float t = static_cast<float>(marker) * 0.32f;
        const Vector3 center = Add(proxy.worldPosition, Scale(tailVector, t));
        const float spread = coreRadius * (0.62f + 0.14f * marker);
        const Vector3 tip = Add(
            center,
            Scale(proxy.motionDirection, coreRadius * 0.48f));
        const Vector4 markerColor = WithAlphaScale(
            proxy.trailColor,
            0.80f - 0.16f * static_cast<float>(marker));
        draw.AddLine(Add(center, Scale(proxy.cameraRight, spread)), tip,
                     markerColor, proxy.haloColor);
        draw.AddLine(Subtract(center, Scale(proxy.cameraRight, spread)), tip,
                     markerColor, proxy.haloColor);
    }

    if (proxy.threat) {
        const float warningRadius = haloRadius *
            (1.18f + 0.22f * proxy.approachNormalized);
        draw.AddCircle(
            proxy.worldPosition,
            proxy.cameraRight,
            proxy.cameraUp,
            warningRadius,
            WithAlphaScale(proxy.haloColor,
                0.34f + 0.34f * proxy.approachNormalized),
            28);
    }

    // Cyan brackets are reserved for projectiles the player can shoot down.
    if (proxy.shootDownEligible) {
        const Vector4 bracket{0.20f, 0.96f, 1.0f,
            0.58f + 0.28f * proxy.approachNormalized};
        const float outer = haloRadius * 1.34f;
        const float tick = haloRadius * 0.30f;
        const Vector3 rightOuter = Scale(proxy.cameraRight, outer);
        const Vector3 upOuter = Scale(proxy.cameraUp, outer);
        const Vector3 rightTick = Scale(proxy.cameraRight, tick);
        const Vector3 upTick = Scale(proxy.cameraUp, tick);
        const Vector3 right = Add(proxy.worldPosition, rightOuter);
        const Vector3 left = Subtract(proxy.worldPosition, rightOuter);
        const Vector3 top = Add(proxy.worldPosition, upOuter);
        const Vector3 bottom = Subtract(proxy.worldPosition, upOuter);
        draw.AddLine(Add(right, upTick), Subtract(right, upTick), bracket);
        draw.AddLine(Add(left, upTick), Subtract(left, upTick), bracket);
        draw.AddLine(Add(top, rightTick), Subtract(top, rightTick), bracket);
        draw.AddLine(Add(bottom, rightTick), Subtract(bottom, rightTick), bracket);
    }

    if (proxy.style == EnemyProjectileVisualStyle::Missile) {
        const Vector3 finBase = Add(
            proxy.worldPosition,
            Scale(proxy.motionDirection, -coreRadius * 1.35f));
        draw.AddLine(
            Add(finBase, Scale(proxy.cameraRight, coreRadius * 0.95f)),
            proxy.worldPosition,
            proxy.trailColor,
            proxy.coreColor);
        draw.AddLine(
            Subtract(finBase, Scale(proxy.cameraRight, coreRadius * 0.95f)),
            proxy.worldPosition,
            proxy.trailColor,
            proxy.coreColor);
    } else if (proxy.style == EnemyProjectileVisualStyle::Orb) {
        const Vector3 right = Scale(proxy.cameraRight, haloRadius * 0.78f);
        const Vector3 up = Scale(proxy.cameraUp, haloRadius * 0.78f);
        draw.AddLine(Add(proxy.worldPosition, right),
                     Add(proxy.worldPosition, up), proxy.haloColor);
        draw.AddLine(Add(proxy.worldPosition, up),
                     Subtract(proxy.worldPosition, right), proxy.haloColor);
        draw.AddLine(Subtract(proxy.worldPosition, right),
                     Subtract(proxy.worldPosition, up), proxy.haloColor);
        draw.AddLine(Subtract(proxy.worldPosition, up),
                     Add(proxy.worldPosition, right), proxy.haloColor);
    } else if (proxy.style == EnemyProjectileVisualStyle::Arc) {
        draw.AddCircle(
            proxy.worldPosition,
            proxy.cameraRight,
            proxy.motionDirection,
            haloRadius * 0.82f,
            WithAlphaScale(proxy.haloColor, 0.72f),
            20);
    }
}

void AppendLifecyclePrimitive(
    const EnemyProjectileLifecycleVfxProxy& burst,
    ge3::debug::DebugDrawSystem& draw) {
    constexpr float kTau = 6.28318530718f;
    const float age = (std::clamp)(burst.normalizedAge, 0.0f, 1.0f);
    const float fade = 1.0f - age;
    const float radius = burst.radius;
    const Vector4 primary = WithAlphaScale(burst.primaryColor, fade);
    const Vector4 secondary = WithAlphaScale(burst.secondaryColor, fade * fade);

    if (burst.kind == EnemyProjectileLifecycleVisualKind::Launch) {
        draw.AddCircle(
            burst.worldPosition,
            burst.cameraRight,
            burst.cameraUp,
            radius * (0.35f + age * 1.45f),
            secondary,
            28);
        draw.AddPoint(
            burst.worldPosition,
            radius * (0.82f - age * 0.42f),
            primary);
        const Vector3 exhaustCenter = Add(
            burst.worldPosition,
            Scale(burst.motionDirection, -radius * (0.4f + age * 1.8f)));
        draw.AddLine(exhaustCenter, burst.worldPosition, secondary, primary);
        return;
    }

    const int rayCount = burst.kind ==
        EnemyProjectileLifecycleVisualKind::Intercepted ? 12 : 10;
    const float expansion = burst.kind ==
        EnemyProjectileLifecycleVisualKind::Intercepted
        ? 0.55f + age * 2.65f
        : 0.45f + age * 3.20f;
    draw.AddPoint(
        burst.worldPosition,
        radius * (0.72f + fade * 0.70f),
        primary);
    draw.AddCircle(
        burst.worldPosition,
        burst.cameraRight,
        burst.cameraUp,
        radius * expansion,
        secondary,
        36);
    draw.AddCircle(
        burst.worldPosition,
        burst.cameraRight,
        burst.cameraUp,
        radius * expansion * 0.58f,
        primary,
        28);
    for (int ray = 0; ray < rayCount; ++ray) {
        const float angle = kTau * static_cast<float>(ray) /
            static_cast<float>(rayCount);
        const Vector3 direction = Add(
            Scale(burst.cameraRight, std::cos(angle)),
            Scale(burst.cameraUp, std::sin(angle)));
        const float alternating = (ray & 1) == 0 ? 1.0f : 0.68f;
        draw.AddLine(
            Add(burst.worldPosition,
                Scale(direction, radius * expansion * 0.24f)),
            Add(burst.worldPosition,
                Scale(direction, radius * expansion * alternating)),
            primary,
            secondary);
    }
}
} // namespace

EnemyProjectileVfxRenderer::EnemyProjectileVfxRenderer()
    : fallbackDirect_(EnemyProjectileVisualDefinitionAsset::CommercialDefault(
          EnemyProjectileTrajectory::Direct)),
      fallbackPredictive_(EnemyProjectileVisualDefinitionAsset::CommercialDefault(
          EnemyProjectileTrajectory::Predictive)),
      fallbackHoming_(EnemyProjectileVisualDefinitionAsset::CommercialDefault(
          EnemyProjectileTrajectory::Homing)),
      fallbackArc_(EnemyProjectileVisualDefinitionAsset::CommercialDefault(
          EnemyProjectileTrajectory::Arc)) {}

bool EnemyProjectileVfxRenderer::LoadDirectory(
    const std::filesystem::path& directory,
    std::string* errorMessage) {
    std::error_code ec;
    if (!std::filesystem::exists(directory, ec) || ec ||
        !std::filesystem::is_directory(directory, ec) || ec) {
        if (errorMessage != nullptr) {
            *errorMessage = "Enemy projectile visual directory is unavailable: " +
                directory.generic_string();
        }
        return false;
    }

    std::vector<std::filesystem::path> files;
    for (std::filesystem::directory_iterator it(directory, ec), end;
         !ec && it != end;
         it.increment(ec)) {
        if (it->is_regular_file(ec) &&
            it->path().extension() == ".projectilevisual") {
            files.push_back(it->path());
        }
    }
    if (ec) {
        if (errorMessage != nullptr) {
            *errorMessage = "Could not enumerate projectile visual directory: " +
                ec.message();
        }
        return false;
    }
    std::sort(files.begin(), files.end());

    std::vector<EnemyProjectileVisualDefinitionAsset> staged;
    std::unordered_map<std::string, size_t> stagedLookup;
    for (const std::filesystem::path& file : files) {
        EnemyProjectileVisualDefinitionAsset asset{};
        std::string assetError;
        if (!asset.LoadFromFile(file.string(), &assetError)) {
            if (errorMessage != nullptr) *errorMessage = assetError;
            return false;
        }
        if (stagedLookup.contains(asset.projectileDefinitionId)) {
            if (errorMessage != nullptr) {
                *errorMessage = "Duplicate projectile visual binding: " +
                    asset.projectileDefinitionId;
            }
            return false;
        }
        stagedLookup.emplace(asset.projectileDefinitionId, staged.size());
        staged.push_back(std::move(asset));
    }
    if (staged.empty()) {
        if (errorMessage != nullptr) {
            *errorMessage = "Projectile visual directory contains no assets.";
        }
        return false;
    }

    directory_ = directory;
    assets_ = std::move(staged);
    byProjectileDefinition_ = std::move(stagedLookup);
    ++assetRevision_;
    return true;
}

void EnemyProjectileVfxRenderer::Reset(EffectRuntime* effectRuntime) {
    if (effectRuntime != nullptr) {
        for (auto& [projectileId, managed] : managedEffects_) {
            (void)projectileId;
            StopManaged(managed, effectRuntime);
        }
    }
    managedEffects_.clear();
    lifecycleBursts_.clear();
    frame_ = {};
    revision_ = 0;
}

void EnemyProjectileVfxRenderer::Update(
    const EnemyProjectileVfxRenderInput& input) {
    frame_ = {};
    frame_.assetRevision = assetRevision_;
    const uint64_t touchedRevision = ++revision_;
    if (!input.settings.enabled || !input.gameplayActive ||
        input.presentation == nullptr) {
        lifecycleBursts_.clear();
        StopUntouched(input.effectRuntime, touchedRevision);
        frame_.revision = touchedRevision;
        return;
    }

    const float deltaTime = std::isfinite(input.deltaTime)
        ? (std::clamp)(input.deltaTime, 0.0f, 0.25f)
        : 0.0f;
    for (LifecycleBurst& burst : lifecycleBursts_) {
        burst.ageSeconds += deltaTime;
    }
    std::erase_if(lifecycleBursts_, [](const LifecycleBurst& burst) {
        return burst.ageSeconds >= burst.durationSeconds;
    });

    const Vector3 cameraRight = NormalizeOr(
        input.cameraRight, {1.0f, 0.0f, 0.0f});
    const Vector3 cameraUp = NormalizeOr(
        input.cameraUp, {0.0f, 1.0f, 0.0f});
    const float maximumDrawDistance = (std::max)(
        1.0f, input.settings.maximumDrawDistance);
    const size_t budget = (std::max)(
        static_cast<size_t>(1), input.settings.maximumVisibleProjectiles);
    frame_.proxies.reserve((std::min)(
        budget, input.presentation->projectiles.size()));

    for (const EnemyProjectilePresentation& projectile :
         input.presentation->projectiles) {
        const float cameraDistance = Length(Subtract(
            projectile.worldPosition, input.cameraWorldPosition));
        if (!std::isfinite(cameraDistance) ||
            cameraDistance > maximumDrawDistance) {
            ++frame_.culledByDistance;
            continue;
        }
        if (frame_.proxies.size() >= budget) {
            ++frame_.droppedByBudget;
            continue;
        }

        const EnemyProjectileVisualDefinitionAsset& visual =
            ResolveVisual(projectile);
        if (!visual.enabled) continue;
        const float pulse = 1.0f + visual.pulseAmplitude * std::sin(
            input.elapsedTime * visual.pulseFrequencyHz * 6.28318530718f +
            static_cast<float>(projectile.projectileId % 29u) * 0.21f);
        const float threatScale = projectile.threat
            ? visual.threatRadiusScale
            : 1.0f;
        const float physicalRadius = projectile.collisionRadius *
            visual.coreRadiusScale;
        const float angularRadius = cameraDistance *
            visual.minimumAngularRadius;
        const float authoredCoreRadius = (std::clamp)(
            (std::max)(physicalRadius, angularRadius) * pulse * threatScale,
            0.04f,
            visual.maximumWorldRadius);
        const float authoredHaloRadius = (std::min)(
            authoredCoreRadius * visual.haloRadiusScale,
            visual.maximumWorldRadius * visual.haloRadiusScale);
        EnemyProjectileScreenSpaceReadabilityInput readabilityInput{};
        readabilityInput.cameraDistance = cameraDistance;
        readabilityInput.verticalFovRadians = input.verticalFovRadians;
        readabilityInput.viewportHeightPixels = input.viewportHeightPixels;
        readabilityInput.authoredCoreRadius = authoredCoreRadius;
        readabilityInput.authoredHaloRadius = authoredHaloRadius;
        readabilityInput.authoredTrailWidth =
            authoredCoreRadius * visual.trailWidthScale;
        readabilityInput.threat = projectile.threat;
        readabilityInput.settings = input.readabilitySettings;
        const EnemyProjectileScreenSpaceReadabilityResult readability =
            EnemyProjectileScreenSpaceReadabilityPolicy{}.Evaluate(
                readabilityInput);
        const float coreRadius = readability.coreRadius;
        const float haloRadius = readability.haloRadius;

        EnemyProjectileVfxProxy proxy{};
        proxy.projectileId = projectile.projectileId;
        proxy.visualDefinitionId = visual.id;
        proxy.style = visual.style;
        proxy.worldPosition = projectile.worldPosition;
        proxy.cameraRight = cameraRight;
        proxy.cameraUp = cameraUp;
        proxy.coreColor = visual.coreColor;
        proxy.haloColor = visual.haloColor;
        proxy.trailColor = visual.trailColor;
        proxy.coreRadius = coreRadius;
        proxy.haloRadius = haloRadius;
        proxy.trailWidth = readability.trailWidth;
        proxy.distanceFromCamera = cameraDistance;
        proxy.coreDiameterPixels = readability.coreDiameterPixels;
        proxy.haloDiameterPixels = readability.haloDiameterPixels;
        proxy.threat = projectile.threat;
        proxy.shootDownEligible = HasDefenseResponse(
            projectile.defenseResponses,
            EnemyAttackDefenseResponse::ShootDown);
        const float approachDistance = (std::max)(
            1.0f, input.settings.threatApproachDistance);
        proxy.approachNormalized = projectile.threat
            ? (std::clamp)(
                1.0f - (std::max)(0.0f, projectile.forwardDistanceToPlayer) /
                    approachDistance,
                0.0f,
                1.0f)
            : 0.0f;
        proxy.readabilityBoosted = readability.boosted;
        proxy.readabilityLimitReached = readability.worldLimitReached;
        if (readability.boosted) ++frame_.readabilityBoostedProjectiles;
        if (readability.worldLimitReached) {
            ++frame_.readabilityLimitedProjectiles;
        }
        const Vector3 motionDirection = NormalizeOr(
            projectile.motionDirection, {0.0f, 0.0f, -1.0f});
        proxy.motionDirection = motionDirection;
        proxy.trailStart = Add(
            projectile.worldPosition,
            Scale(motionDirection, -coreRadius * (std::clamp)(visual.trailLengthInRadii, 2.0f, 4.0f)));

        ManagedEffect& managed = managedEffects_[projectile.projectileId];
        if (managed.visualDefinitionId != visual.id) {
            StopManaged(managed, input.effectRuntime);
            managed.visualDefinitionId = visual.id;
        }
        managed.touchedRevision = touchedRevision;
        if (input.settings.effectRuntimeEnabled && input.effectRuntime != nullptr) {
            if (managed.coreInstanceId == 0) {
                managed.coreInstanceId = input.effectRuntime->PlayEffectWithParams(
                    visual.coreEffectId,
                    projectile.worldPosition,
                    visual.coreColor,
                    {coreRadius, coreRadius, coreRadius});
                if (managed.coreInstanceId != 0) {
                    input.effectRuntime->SetEffectPreviewLoop(
                        managed.coreInstanceId, true);
                }
            }
            if (managed.haloInstanceId == 0) {
                managed.haloInstanceId = input.effectRuntime->PlayEffectWithParams(
                    visual.haloEffectId,
                    projectile.worldPosition,
                    visual.haloColor,
                    {haloRadius, haloRadius, haloRadius});
                if (managed.haloInstanceId != 0) {
                    input.effectRuntime->SetEffectPreviewLoop(
                        managed.haloInstanceId, true);
                }
            }
            UpdateEffectInstance(
                *input.effectRuntime,
                managed.coreInstanceId,
                projectile.worldPosition,
                visual.coreColor,
                coreRadius);
            UpdateEffectInstance(
                *input.effectRuntime,
                managed.haloInstanceId,
                projectile.worldPosition,
                visual.haloColor,
                haloRadius);
        }
        proxy.coreEffectInstanceId = managed.coreInstanceId;
        proxy.haloEffectInstanceId = managed.haloInstanceId;
        proxy.effectBacked =
            IsRenderableEffectInstance(input.effectRuntime, managed.coreInstanceId) &&
            IsRenderableEffectInstance(input.effectRuntime, managed.haloInstanceId);
        if (proxy.effectBacked) {
            proxy.visualState = EnemyProjectileVisualState::ProductionEffectReady;
            ++frame_.effectBackedProjectiles;
        } else if (input.settings.productionPrimitivesEnabled ||
                   input.settings.fallbackPrimitivesEnabled) {
            proxy.visualState = EnemyProjectileVisualState::ProductionFallbackReady;
            ++frame_.fallbackProjectiles;
        } else {
            proxy.visualState = EnemyProjectileVisualState::Unavailable;
            ++frame_.unavailableProjectiles;
        }
        if (input.settings.productionPrimitivesEnabled ||
            (!proxy.effectBacked && input.settings.fallbackPrimitivesEnabled)) {
            ++frame_.productionSubmittedProjectiles;
        }
        frame_.proxies.push_back(std::move(proxy));
    }

    const size_t lifecycleBudget = (std::max)(
        static_cast<size_t>(1), input.settings.maximumLifecycleBursts);
    for (const EnemyProjectilePresentationEvent& event :
         input.presentation->events) {
        if (event.kind == EnemyProjectilePresentationEventKind::Expired) {
            continue;
        }
        if (lifecycleBursts_.size() >= lifecycleBudget) {
            lifecycleBursts_.erase(lifecycleBursts_.begin());
        }
        LifecycleBurst burst{};
        burst.projectileId = event.projectileId;
        burst.worldPosition = event.worldPosition;
        burst.motionDirection = NormalizeOr(
            event.motionDirection, {0.0f, 0.0f, -1.0f});
        burst.color = event.color;
        switch (event.kind) {
        case EnemyProjectilePresentationEventKind::Spawned:
            burst.kind = EnemyProjectileLifecycleVisualKind::Launch;
            burst.durationSeconds = (std::max)(
                0.05f, input.settings.launchBurstDurationSeconds);
            break;
        case EnemyProjectilePresentationEventKind::Impacted:
            burst.kind = EnemyProjectileLifecycleVisualKind::PlayerImpact;
            burst.durationSeconds = (std::max)(
                0.05f, input.settings.impactBurstDurationSeconds);
            break;
        case EnemyProjectilePresentationEventKind::Intercepted:
            burst.kind = EnemyProjectileLifecycleVisualKind::Intercepted;
            burst.durationSeconds = (std::max)(
                0.05f, input.settings.interceptBurstDurationSeconds);
            break;
        case EnemyProjectilePresentationEventKind::Expired:
            continue;
        }
        lifecycleBursts_.push_back(std::move(burst));
    }

    frame_.lifecycleBursts.reserve(lifecycleBursts_.size());
    for (const LifecycleBurst& burst : lifecycleBursts_) {
        const float cameraDistance = Length(Subtract(
            burst.worldPosition, input.cameraWorldPosition));
        if (!std::isfinite(cameraDistance) ||
            cameraDistance > maximumDrawDistance) {
            continue;
        }
        EnemyProjectileLifecycleVfxProxy proxy{};
        proxy.projectileId = burst.projectileId;
        proxy.kind = burst.kind;
        proxy.worldPosition = burst.worldPosition;
        proxy.motionDirection = burst.motionDirection;
        proxy.cameraRight = cameraRight;
        proxy.cameraUp = cameraUp;
        proxy.normalizedAge = (std::clamp)(
            burst.ageSeconds / (std::max)(0.05f, burst.durationSeconds),
            0.0f,
            1.0f);
        proxy.radius = (std::clamp)(
            (std::max)(0.70f, cameraDistance * 0.010f),
            0.70f,
            4.50f);
        switch (burst.kind) {
        case EnemyProjectileLifecycleVisualKind::Launch:
            proxy.primaryColor = {1.0f, 1.0f, 1.0f, 1.0f};
            proxy.secondaryColor = WithAlphaScale(burst.color, 0.88f);
            ++frame_.launchBursts;
            break;
        case EnemyProjectileLifecycleVisualKind::PlayerImpact:
            proxy.primaryColor = {1.0f, 0.96f, 0.72f, 1.0f};
            proxy.secondaryColor = {1.0f, 0.12f, 0.02f, 0.92f};
            ++frame_.impactBursts;
            break;
        case EnemyProjectileLifecycleVisualKind::Intercepted:
            proxy.primaryColor = {0.88f, 1.0f, 1.0f, 1.0f};
            proxy.secondaryColor = {0.08f, 0.92f, 1.0f, 0.94f};
            ++frame_.interceptBursts;
            break;
        }
        frame_.lifecycleBursts.push_back(std::move(proxy));
    }

    StopUntouched(input.effectRuntime, touchedRevision);
    frame_.sourcePresentationRevision = input.presentation->revision;
    frame_.revision = touchedRevision;
}

void EnemyProjectileVfxRenderer::AppendProductionWorldPrimitives(
    ge3::debug::DebugDrawSystem& productionDraw) const {
    for (const EnemyProjectileVfxProxy& proxy : frame_.proxies) {
        if (proxy.visualState == EnemyProjectileVisualState::Unavailable) continue;
        AppendProjectilePrimitive(proxy, productionDraw);
    }
    for (const EnemyProjectileLifecycleVfxProxy& burst : frame_.lifecycleBursts) {
        AppendLifecyclePrimitive(burst, productionDraw);
    }
}

void EnemyProjectileVfxRenderer::AppendFallbackWorldPrimitives(
    ge3::debug::DebugDrawSystem& debugDraw) const {
    for (const EnemyProjectileVfxProxy& proxy : frame_.proxies) {
        if (proxy.effectBacked) continue;
        AppendProjectilePrimitive(proxy, debugDraw);
    }
    for (const EnemyProjectileLifecycleVfxProxy& burst : frame_.lifecycleBursts) {
        AppendLifecyclePrimitive(burst, debugDraw);
    }
}

const char* ToString(EnemyProjectileVisualState state) noexcept {
    switch (state) {
    case EnemyProjectileVisualState::ProductionEffectReady:
        return "ProductionEffectReady";
    case EnemyProjectileVisualState::ProductionFallbackReady:
        return "ProductionFallbackReady";
    case EnemyProjectileVisualState::Unavailable:
        return "Unavailable";
    }
    return "Unknown";
}

const EnemyProjectileVisualDefinitionAsset*
EnemyProjectileVfxRenderer::FindVisual(
    const std::string& projectileDefinitionId) const noexcept {
    const auto found = byProjectileDefinition_.find(projectileDefinitionId);
    return found != byProjectileDefinition_.end()
        ? &assets_[found->second]
        : nullptr;
}

std::filesystem::path EnemyProjectileVfxRenderer::DefaultDirectory() {
    return std::filesystem::path{"Resources/courses/projectile_visuals"};
}

const EnemyProjectileVisualDefinitionAsset&
EnemyProjectileVfxRenderer::ResolveVisual(
    const EnemyProjectilePresentation& projectile) const noexcept {
    if (const EnemyProjectileVisualDefinitionAsset* resolved =
            FindVisual(projectile.definitionId)) {
        return *resolved;
    }
    switch (projectile.trajectory) {
    case EnemyProjectileTrajectory::Direct: return fallbackDirect_;
    case EnemyProjectileTrajectory::Predictive: return fallbackPredictive_;
    case EnemyProjectileTrajectory::Homing: return fallbackHoming_;
    case EnemyProjectileTrajectory::Arc: return fallbackArc_;
    }
    return fallbackDirect_;
}

void EnemyProjectileVfxRenderer::StopManaged(
    ManagedEffect& managed,
    EffectRuntime* runtime) {
    if (runtime != nullptr) {
        if (managed.coreInstanceId != 0) runtime->StopEffect(managed.coreInstanceId);
        if (managed.haloInstanceId != 0) runtime->StopEffect(managed.haloInstanceId);
    }
    managed.coreInstanceId = 0;
    managed.haloInstanceId = 0;
}

void EnemyProjectileVfxRenderer::StopUntouched(
    EffectRuntime* runtime,
    uint64_t touchedRevision) {
    for (auto it = managedEffects_.begin(); it != managedEffects_.end();) {
        if (it->second.touchedRevision == touchedRevision) {
            ++it;
            continue;
        }
        StopManaged(it->second, runtime);
        it = managedEffects_.erase(it);
    }
}
