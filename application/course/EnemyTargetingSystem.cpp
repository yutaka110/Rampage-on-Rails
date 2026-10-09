#include "EnemyTargetingSystem.h"

#include "CourseSpawnRuntime.h"
#include "EnemyCombatPresentationBridge.h"
#include "EnemyEncounterReadabilityDirector.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace {
Vector3 Unit(Vector3 v) {
    const float length = std::sqrt(v.x*v.x+v.y*v.y+v.z*v.z);
    return length > 0.0001f ? Vector3{v.x/length,v.y/length,v.z/length} : Vector3{0,0,-1};
}

Vector3 TransformAnchor(Vector3 p, const TwinShieldDronePose& pose) {
    const auto matrix=MakeAffineMatrix(pose.scale,pose.rotation,pose.position);
    return {p.x*matrix.m[0][0]+p.y*matrix.m[1][0]+p.z*matrix.m[2][0]+matrix.m[3][0],
        p.x*matrix.m[0][1]+p.y*matrix.m[1][1]+p.z*matrix.m[2][1]+matrix.m[3][1],
        p.x*matrix.m[0][2]+p.y*matrix.m[1][2]+p.z*matrix.m[2][2]+matrix.m[3][2]};
}

Vector3 ProjectMuzzleToRail(const RailPath& rail, Vector3 world, float hint) {
    // Solve the local rail cross-section, rather than projecting onto only the
    // actor's center tangent. That approximation detaches barrels on bends.
    const auto residual=[&](float d) {
        const auto sample=rail.Evaluate(d);
        return (world.x-sample.position.x)*sample.tangent.x+
            (world.y-sample.position.y)*sample.tangent.y+
            (world.z-sample.position.z)*sample.tangent.z;
    };
    float distance=(std::clamp)(hint,0.0f,rail.Length());
    for(int i=0;i<12;++i) {
        const float f=residual(distance);
        if(std::abs(f)<0.00001f) break;
        const float lo=(std::max)(0.0f,distance-0.05f);
        const float hi=(std::min)(rail.Length(),distance+0.05f);
        const float derivative=(residual(hi)-residual(lo))/(std::max)(0.00001f,hi-lo);
        if(std::abs(derivative)<0.0001f) break;
        distance=(std::clamp)(distance-(std::clamp)(f/derivative,-8.0f,8.0f),0.0f,rail.Length());
    }
    const auto sample=rail.Evaluate(distance);
    const Vector3 delta{world.x-sample.position.x,world.y-sample.position.y,world.z-sample.position.z};
    return {delta.x*sample.right.x+delta.y*sample.right.y+delta.z*sample.right.z,
        delta.x*sample.up.x+delta.y*sample.up.y+delta.z*sample.up.z,distance};
}

void PrepareWeaponMount(CourseEnemyActor& actor,const EnemyTargetingFrameInput& input) {
    auto& mount=actor.weaponMount;
    mount={};
    if(actor.desc.meshId!="twin_shield_hull" || !input.railPath ||
        !input.hasCameraPosition || input.railPath->Length()<=0 ||
        actor.combatState.phase==EnemyCombatPhase::Dying || actor.combatState.phase==EnemyCombatPhase::Retired) return;
    const auto animation=ResolveEnemyCombatActorPresentation(actor,input.presentationSettings?
        *input.presentationSettings:EnemyCombatPresentationSettings{});
    const float distance=actor.desc.spawnDistance+actor.desc.distanceOffset;
    const auto sample=input.railPath->Evaluate(distance);
    auto& pose=mount.dronePose;
    pose.visible=true;
    pose.position=ResolveEnemyProjectileWorldPosition(*input.railPath,
        {actor.desc.lateralOffset,actor.desc.verticalOffset,distance});
    pose.position.x+=sample.tangent.x*animation.forwardOffset+sample.right.x*animation.lateralOffset+sample.up.x*animation.verticalOffset;
    pose.position.y+=sample.tangent.y*animation.forwardOffset+sample.right.y*animation.lateralOffset+sample.up.y*animation.verticalOffset;
    pose.position.z+=sample.tangent.z*animation.forwardOffset+sample.right.z*animation.lateralOffset+sample.up.z*animation.verticalOffset;
    const auto forward=Unit({input.cameraPosition.x-pose.position.x,
        input.cameraPosition.y-pose.position.y,input.cameraPosition.z-pose.position.z});
    pose.rotation={std::asin((std::clamp)(forward.y,-1.0f,1.0f))+actor.desc.localRotation.x+animation.rotationOffset.x,
        std::atan2(-forward.x,-forward.z)+actor.desc.localRotation.y+animation.rotationOffset.y,
        actor.desc.localRotation.z+animation.rotationOffset.z};
    float readableScale=1.0f;
    if(input.readability) if(const auto* readable=input.readability->FindActor(actor.actorId))
        readableScale=(std::max)(1.0f,readable->presentationScale);
    const float size=(std::max)(0.01f,actor.desc.radius*kTwinShieldGameplayModelScale*
        (actor.combatState.initialized?actor.combatState.presentationScale:1.0f)*animation.scaleMultiplier*readableScale);
    pose.scale={size*(std::max)(0.01f,actor.desc.localScale.x),size*(std::max)(0.01f,actor.desc.localScale.y),
        size*(std::max)(0.01f,actor.desc.localScale.z)};
    // Authored gun lips in TwinShieldHull.obj after Assimp's LH conversion.
    for(int barrel=0;barrel<2;++barrel) mount.muzzleRail[barrel]=ProjectMuzzleToRail(*input.railPath,
        TransformAnchor({barrel==0?-0.30f:0.30f,-0.61f,-0.94f},pose),distance);
    mount.ready=true;
}

uint32_t PrepareScheduledWeaponMount(CourseEnemyActor& actor,
        const EnemyTargetingFrameInput& input, float delay) {
    auto& mount=actor.weaponMount;
    mount.scheduledPredictionRequested=false;
    mount.scheduledPredictionReady=false;
    const auto* travel=actor.targetingState.forwardTravelPrediction.get();
    if (!mount.ready || !travel || travel->samples.size()<2 ||
        !actor.behaviorState.initialized || !actor.HoldsCombatPositionUntilResolved() ||
        delay<=0.000001f) return 0;
    mount.scheduledPredictionRequested=true;
    mount.scheduledDelaySeconds=delay;
    if (!std::isfinite(delay) || delay>travel->HorizonSeconds()) return 0;

    auto future=actor;
    future.attackState.committedThisFrame=false;
    const auto& behavior=actor.behaviorState;
    const auto& home=actor.formationState;
    const bool spacing=home.hoverInitialized && home.hoverGoalReady;
    // Preserve the already resolved home/entrance contribution. Only the
    // smoothed hover correction is advanced; crowd/terrain queries stay live.
    const Vector3 additive{
        actor.desc.lateralOffset-behavior.authoredLateralOffset-behavior.behaviorLateralOffset-
            behavior.safetyLateralOffset-(spacing?home.hoverLateralOffset:0.0f),
        actor.desc.verticalOffset-behavior.authoredVerticalOffset-behavior.behaviorVerticalOffset-
            (spacing?home.hoverVerticalOffset:0.0f),
        actor.desc.distanceOffset-behavior.authoredForwardOffset-behavior.integratedForwardOffset-
            behavior.behaviorForwardOffset-(spacing?home.hoverForwardOffset:0.0f)};
    // Follow the moving camera in rail coordinates as well, so camera-facing
    // hulls do not retain today's heading while rounding a future bend.
    const Vector3 cameraRail=ProjectMuzzleToRail(*input.railPath,input.cameraPosition,input.playerDistance);
    const float startDistance=travel->samples.front().distance;
    const float baseStep=travel->samples[1].seconds;
    // Bound work even for unusually long authored charge times. No model or
    // terrain work is done inside this loop; anchors are evaluated only once.
    const float step=(std::max)(delay/240.0f,baseStep);
    const uint32_t count=(std::min)(240u,static_cast<uint32_t>(std::ceil(delay/step)));
    float previousTime=0.0f,previousDistance=input.playerDistance;
    for(uint32_t index=1;index<=count;++index) {
        const float time=index==count?delay:(std::min)(delay,index*step);
        const float dt=time-previousTime;
        const float distance=input.playerDistance+travel->DistanceAt(time)-startDistance;
        const float speed=(std::clamp)((distance-previousDistance)/dt,0.0f,72.0f);
        EnemyCombatSystem::AdvancePoseForPrediction(future,dt);
        future.behaviorState.stateElapsedSeconds+=dt;
        future.behaviorState.attackTimeRemaining=(std::max)(0.0f,
            actor.behaviorState.attackTimeRemaining-time);
        AdvanceEnemyBehaviorMovement(future,dt,distance,speed);
        if(spacing) AdvanceEnemyHoverSpacing(future,dt);
        future.desc.lateralOffset+=additive.x+(spacing?future.formationState.hoverLateralOffset:0.0f);
        future.desc.verticalOffset+=additive.y+(spacing?future.formationState.hoverVerticalOffset:0.0f);
        future.desc.distanceOffset+=additive.z+(spacing?future.formationState.hoverForwardOffset:0.0f);
        // Production increments age after movement, before preparing mounts.
        future.age+=dt;
        previousTime=time;
        previousDistance=distance;
    }
    auto futureInput=input;
    const float playerTravel=travel->DistanceAt(delay)-startDistance;
    futureInput.cameraPosition=ResolveEnemyProjectileWorldPosition(*input.railPath,
        {cameraRail.x,cameraRail.y,cameraRail.z+playerTravel});
    PrepareWeaponMount(future,futureInput);
    if(future.weaponMount.ready) {
        for(int barrel=0;barrel<2;++barrel) mount.scheduledMuzzleRail[barrel]=future.weaponMount.muzzleRail[barrel];
        mount.scheduledPredictionReady=true;
    }
    return count;
}

} // namespace

namespace {
double EarliestQuadraticRoot(double a, double b, double c, double minimum, double maximum) {
    double time = std::numeric_limits<double>::infinity();
    const auto accept = [&](double root) {
        if (std::isfinite(root) && root >= minimum && root <= maximum)
            time = (std::min)(time, root);
    };
    if (std::abs(a) < 1.0e-10) {
        if (b != 0.0) accept(-c / b);
        else if (std::abs(c) < 1.0e-8) accept(minimum);
    } else {
        double discriminant = b*b - 4.0*a*c;
        const double rounding = 8.0*std::numeric_limits<double>::epsilon()*
            (std::max)(1.0, b*b + std::abs(4.0*a*c));
        if (discriminant >= -rounding) {
            const double q = -0.5*(b + std::copysign(std::sqrt((std::max)(0.0, discriminant)), b));
            accept(q / a);
            if (q != 0.0) accept(c / q);
        }
    }
    return time;
}
} // namespace

EnemyForwardInterceptSolution SolveEnemyPredictedForwardIntercept(
        const RailTravelPrediction& prediction, float launchDelaySeconds,
        Vector3 relativeAtLaunch, float speed, float lifetime) {
    if (prediction.samples.size() < 2 || !std::isfinite(launchDelaySeconds) || launchDelaySeconds < 0.0f ||
        !std::isfinite(relativeAtLaunch.x) || !std::isfinite(relativeAtLaunch.y) || !std::isfinite(relativeAtLaunch.z) ||
        !std::isfinite(speed) || speed <= 0.0f || !std::isfinite(lifetime) || lifetime <= 0.0f)
        return {};
    const double baseDistance = prediction.DistanceAt(launchDelaySeconds);
    const double lateralSquared = static_cast<double>(relativeAtLaunch.x)*relativeAtLaunch.x +
        static_cast<double>(relativeAtLaunch.y)*relativeAtLaunch.y;
    const double speedSquared = static_cast<double>(speed)*speed;
    // Each forecast interval is linear in rail distance. Solve its quadratic
    // exactly, including two crossings or a tangent hit between sample points.
    for (size_t index = 1; index < prediction.samples.size(); ++index) {
        const auto& from = prediction.samples[index - 1];
        const auto& to = prediction.samples[index];
        const double start = (std::max)(0.0, static_cast<double>(from.seconds) - launchDelaySeconds);
        const double end = (std::min)(static_cast<double>(lifetime), static_cast<double>(to.seconds) - launchDelaySeconds);
        if (end < start) continue;
        const double velocity = (to.distance - static_cast<double>(from.distance)) / (to.seconds - static_cast<double>(from.seconds));
        const double z = relativeAtLaunch.z + from.distance - baseDistance +
            velocity*(launchDelaySeconds + start - from.seconds);
        const double root = EarliestQuadraticRoot(velocity*velocity - speedSquared,
            2.0*(z*velocity - speedSquared*start),
            lateralSquared + z*z - speedSquared*start*start, 0.0, end - start);
        if (!std::isfinite(root)) continue;
        const float flight = static_cast<float>(start + root);
        if (start + root >= lifetime || flight >= lifetime)
            return {0.0f, EnemyForwardInterceptStatus::BeyondLifetime};
        return {flight, EnemyForwardInterceptStatus::Reachable};
    }
    // No extrapolation at the forecast boundary: a long-lived authored shot
    // cannot claim reachability using an invented future speed.
    return {0.0f, EnemyForwardInterceptStatus::NoFutureIntersection};
}

EnemyForwardInterceptSolution SolveEnemyForwardIntercept(
    Vector3 relative, float forwardVelocity, float speed, float lifetime) {
    if (!std::isfinite(relative.x) || !std::isfinite(relative.y) || !std::isfinite(relative.z) ||
        !std::isfinite(forwardVelocity) || !std::isfinite(speed) || !std::isfinite(lifetime) ||
        speed <= 0.0f || lifetime <= 0.0f) return {};
    const double v = forwardVelocity, s = speed;
    const double a = v*v-s*s;
    const double b = 2.0*relative.z*v;
    const double c = static_cast<double>(relative.x)*relative.x+
        static_cast<double>(relative.y)*relative.y+static_cast<double>(relative.z)*relative.z;
    if (c < 0.00000001) return {0.0f, EnemyForwardInterceptStatus::Reachable};
    double time = std::numeric_limits<double>::infinity();
    const auto accept = [&](double root) {
        if (std::isfinite(root) && root >= 0.0) time = (std::min)(time, root);
    };
    if (std::abs(a) <= 1.0e-10*(v*v+s*s)) {
        if (b != 0.0) accept(-c/b);
    } else {
        double discriminant = b*b-4.0*a*c;
        const double rounding = 8.0*std::numeric_limits<double>::epsilon()*
            (std::max)(1.0, b*b+std::abs(4.0*a*c));
        if (discriminant >= -rounding) {
            discriminant = (std::max)(0.0, discriminant);
            const double q = -0.5*(b+std::copysign(std::sqrt(discriminant), b));
            accept(q/a);
            if (q != 0.0) accept(c/q);
        }
    }
    if (!std::isfinite(time)) return {0.0f, EnemyForwardInterceptStatus::NoFutureIntersection};
    // Runtime expires a projectile at age >= lifetime. A root at that exact
    // boundary is not a usable hit, and must not be clamped to a fake solution.
    const float seconds = static_cast<float>(time);
    if (time >= lifetime || seconds >= lifetime)
        return {0.0f, EnemyForwardInterceptStatus::BeyondLifetime};
    return {seconds, EnemyForwardInterceptStatus::Reachable};
}

Vector3 ResolveEnemyProjectileWorldPosition(const RailPath& rail,Vector3 p) {
    return ResolveEnemyProjectileWorldPosition(rail.Evaluate(p.z),p);
}

Vector3 ResolveEnemyProjectileWorldPosition(const RailPathSample& sample,Vector3 p) {
    return {sample.position.x+sample.right.x*p.x+sample.up.x*p.y,
        sample.position.y+sample.right.y*p.x+sample.up.y*p.y,
        sample.position.z+sample.right.z*p.x+sample.up.z*p.y};
}

Vector3 ResolveEnemyProjectileMuzzleRailPosition(const CourseEnemyActor& actor,int index) {
    if(actor.desc.meshId=="twin_shield_hull" && actor.weaponMount.ready)
        return actor.weaponMount.muzzleRail[index & 1];
    if(actor.desc.meshId=="combat_turret" && actor.targetingState.solutionLocked &&
        actor.targetingState.attackIntentSequence==actor.attackState.intentSequence &&
        actor.targetingState.attackTokenId==actor.attackState.tokenId)
        return ResolveTurretMuzzleRailPosition(actor,(index & 1)?1.0f:-1.0f);
    const float lane=static_cast<float>(index)-static_cast<float>((std::max)(1,actor.desc.bulletCount)-1)*0.5f;
    return {actor.desc.lateralOffset+lane*actor.desc.radius*0.7f,actor.desc.verticalOffset,
        actor.desc.spawnDistance+actor.desc.distanceOffset-actor.desc.radius*1.5f};
}

EnemyProjectileAimSolution ResolveEnemyProjectileAimSolution(
    const CourseEnemyActor& actor, const Vector3& muzzle, float launchDelaySeconds) {
    const auto& state=actor.targetingState;
    EnemyProjectileAimSolution result{{state.targetLateralOffset,state.targetVerticalOffset,state.targetDistance},
        state.predictedFlightSeconds,true,muzzle};
    if (!state.compensatesForwardTravel) return result;
    const float wait=(std::max)(0.0f,launchDelaySeconds);
    const float enemyVelocity=actor.behaviorState.initialized?
        actor.behaviorState.engagementBandVelocity:actor.desc.forwardSpeed;
    const auto* forecast = state.forwardTravelPrediction.get();
    const float playerTravel = forecast ? forecast->DistanceAt(wait) - forecast->samples.front().distance :
        state.playerForwardVelocity*wait;
    const float playerAtLaunch = state.forwardTravelReferenceDistance + playerTravel;
    // Hover enemies follow automatic rail motion too. Preserve only their
    // current relative band correction when estimating a delayed muzzle.
    const float muzzleAtLaunch = muzzle.z + (forecast ? playerTravel +
        (enemyVelocity - state.playerForwardVelocity)*wait : enemyVelocity*wait);
    result.muzzleRail.z = muzzleAtLaunch;
    const auto& mount=actor.weaponMount;
    if(wait>0.000001f && mount.scheduledPredictionRequested) {
        if(!mount.scheduledPredictionReady || std::abs(wait-mount.scheduledDelaySeconds)>0.00001f) {
            result.reachable=false;
            return result;
        }
        const auto squared=[&](Vector3 p) {
            const float x=p.x-muzzle.x,y=p.y-muzzle.y,z=p.z-muzzle.z;
            return x*x+y*y+z*z;
        };
        const int barrel=squared(mount.muzzleRail[0])<=squared(mount.muzzleRail[1])?0:1;
        result.muzzleRail=mount.scheduledMuzzleRail[barrel];
    }
    const Vector3 relative{result.targetRail.x-result.muzzleRail.x,result.targetRail.y-result.muzzleRail.y,
        playerAtLaunch-result.muzzleRail.z};
    const auto flight = forecast ? SolveEnemyPredictedForwardIntercept(*forecast,wait,relative,
        actor.desc.projectileDefinition.initialSpeed,actor.desc.projectileDefinition.lifetime) :
        SolveEnemyForwardIntercept(relative,state.playerForwardVelocity,
            actor.desc.projectileDefinition.initialSpeed,actor.desc.projectileDefinition.lifetime);
    result.reachable=flight.Reachable();
    result.flightSeconds=flight.flightSeconds;
    result.targetRail.z = forecast ? state.forwardTravelReferenceDistance +
        forecast->DistanceAt(wait + flight.flightSeconds) - forecast->samples.front().distance :
        playerAtLaunch + state.playerForwardVelocity*flight.flightSeconds;
    return result;
}

Vector3 ResolveEnemyProjectileTargetRailPosition(const CourseEnemyActor& actor, const Vector3& muzzle) {
    return ResolveEnemyProjectileAimSolution(actor,muzzle).targetRail;
}

bool CanEnemyProjectileVolleyReachTarget(const CourseEnemyActor& actor,float launchDelaySeconds) {
    if (!actor.targetingState.compensatesForwardTravel) return true;
    if (!actor.targetingState.solutionLocked) return false;
    for (int i=0;i<(std::max)(1,actor.desc.bulletCount);++i)
        if (!ResolveEnemyProjectileAimSolution(actor,ResolveEnemyProjectileMuzzleRailPosition(actor,i),
                launchDelaySeconds).reachable) return false;
    return true;
}

Vector3 ResolveTurretAimDirection(const CourseEnemyActor& actor) {
    const auto& target = actor.targetingState;
    return Unit({target.targetLateralOffset-actor.desc.lateralOffset,
        target.targetVerticalOffset-actor.desc.verticalOffset-0.1598f*actor.desc.radius,
        target.targetDistance-actor.desc.spawnDistance-actor.desc.distanceOffset});
}

Vector3 ResolveTurretMuzzleRailPosition(const CourseEnemyActor& actor, float barrelSide) {
    const Vector3 forward = ResolveTurretAimDirection(actor);
    const Vector3 right = Unit({-forward.z,0,forward.x});
    const Vector3 up{right.y*forward.z-right.z*forward.y,
        right.z*forward.x-right.x*forward.z,right.x*forward.y-right.y*forward.x};
    const float r = actor.desc.radius;
    return {actor.desc.lateralOffset+r*(forward.x*0.8272f+right.x*barrelSide*0.2538f+up.x*0.1598f),
        actor.desc.verticalOffset+r*(forward.y*0.8272f+right.y*barrelSide*0.2538f+up.y*0.1598f),
        actor.desc.spawnDistance+actor.desc.distanceOffset+r*(forward.z*0.8272f+right.z*barrelSide*0.2538f+up.z*0.1598f)};
}

void EnemyTargetingSystem::Reset() {
    frame_ = {};
    previousPlayerDistance_ = 0.0f;
    previousPlayerLateralOffset_ = 0.0f;
    previousPlayerVerticalOffset_ = 4.0f;
    hasPreviousPlayerSample_ = false;
    revision_ = 0;
}

void EnemyTargetingSystem::Update(CourseSpawnRuntime& runtime,
    const EnemyTargetingFrameInput& input) {
    runtime.UpdateEnemyTargeting(*this, input);
}

void EnemyTargetingSystem::UpdateActors(std::span<CourseEnemyActor> actors,
        CourseSpawnRuntime& runtime,
    const EnemyTargetingFrameInput& sourceInput) {
    EnemyTargetingFrameInput input = sourceInput;
    input.minimumVisibleBeforeFire=runtime.FireSafetySettings().minVisibleBeforeFire;
    bool builtForecast = false;
    if (input.travelPredictionInput && input.deltaTime > 0.0f && !input.travelPrediction) {
        float horizon = 0.0f;
        for (const auto& actor : actors) {
            if (actor.behaviorDefinition.commercialBehavior && actor.HoldsCombatPositionUntilResolved() &&
                actor.desc.projectileDefinition.trajectory == EnemyProjectileTrajectory::Direct) {
                horizon = (std::max)(horizon, actor.desc.projectileDefinition.lifetime +
                    (std::max)(actor.behaviorDefinition.attackLeadSeconds, actor.behaviorState.attackTimeRemaining) + 0.1f);
            }
        }
        if (horizon > 0.0f) {
            input.travelPrediction = BuildRailTravelPrediction(*input.travelPredictionInput, horizon);
            builtForecast = input.travelPrediction != nullptr;
        }
    }
    const float dt = (std::max)(0.0f, input.deltaTime);
    const bool hasMeasuredVelocity=hasPreviousPlayerSample_ && dt>0.000001f;
    float forwardVelocity = 0.0f;
    float lateralVelocity = 0.0f;
    float verticalVelocity = 0.0f;
    if (hasPreviousPlayerSample_ && dt > 0.000001f) {
        forwardVelocity =
            (input.playerDistance - previousPlayerDistance_) / dt;
        lateralVelocity =
            (input.playerLateralOffset - previousPlayerLateralOffset_) / dt;
        verticalVelocity =
            (input.playerVerticalOffset - previousPlayerVerticalOffset_) / dt;
    }
    forwardVelocity = (std::clamp)(forwardVelocity, -120.0f, 120.0f);
    lateralVelocity = (std::clamp)(lateralVelocity, -40.0f, 40.0f);
    verticalVelocity = (std::clamp)(verticalVelocity, -40.0f, 40.0f);
    previousPlayerDistance_ = input.playerDistance;
    previousPlayerLateralOffset_ = input.playerLateralOffset;
    previousPlayerVerticalOffset_ = input.playerVerticalOffset;
    hasPreviousPlayerSample_ = true;

    frame_ = {};
    frame_.travelForecastsBuilt = builtForecast ? 1 : 0;
    frame_.travelForecastSamples = input.travelPrediction ? static_cast<uint32_t>(input.travelPrediction->samples.size()) : 0;
    for (CourseEnemyActor& actor : actors) {
        PrepareWeaponMount(actor,input);
        const bool turret = actor.desc.meshId == "combat_turret";
        const Vector3 previousAim = actor.targetingState.turretAimDirection;
        if (turret && actor.combatState.canBeTargeted) {
            if (!actor.attackState.tokenReserved || !actor.targetingState.solutionLocked) {
                actor.targetingState.targetDistance = input.playerDistance;
                actor.targetingState.targetLateralOffset = input.playerLateralOffset;
                actor.targetingState.targetVerticalOffset = input.playerVerticalOffset;
            }
            const Vector3 desired = ResolveTurretAimDirection(actor);
            const float blend = 1.0f-std::exp(-8.0f*dt);
            actor.targetingState.turretAimDirection = Unit({
                previousAim.x+(desired.x-previousAim.x)*blend,
                previousAim.y+(desired.y-previousAim.y)*blend,
                previousAim.z+(desired.z-previousAim.z)*blend});
        }
        EnemyTargetingRuntimeState& state = actor.targetingState;
        const Vector3 previousShotDirection = Unit({state.targetLateralOffset - state.originLateralOffset,
            state.targetVerticalOffset - state.originVerticalOffset, state.targetDistance - state.originDistance});
        const bool hadPresentedWarning = state.solutionLocked && state.initialized &&
            actor.attackState.telegraphPresented && actor.behaviorState.telegraphPresented &&
            state.attackIntentSequence == actor.attackState.intentSequence &&
            state.attackTokenId == actor.attackState.tokenId && !state.waitingForReachableLaunch;
        if (dt > 0.0f || input.travelPrediction) state.forwardTravelPrediction = input.travelPrediction;
        const float actorForwardVelocity=hasMeasuredVelocity?forwardVelocity:state.playerForwardVelocity;
        const bool deferredAnchor = state.waitingForReachableLaunch && state.solutionLocked &&
            state.attackIntentSequence == actor.attackState.intentSequence &&
            state.attackIntentSequence == actor.behaviorState.attackIntentSequence &&
            actor.behaviorState.attackIntentActive;
        if (!actor.behaviorDefinition.commercialBehavior ||
            (!actor.attackState.tokenReserved && !deferredAnchor) ||
            actor.attackState.intentSequence == 0) continue;
        if(actor.attackState.tokenReserved) ++frame_.activeReservations;
        if(deferredAnchor) {
            if(actor.attackState.tokenReserved && state.attackTokenId!=actor.attackState.tokenId) {
                state.attackTokenId=actor.attackState.tokenId;
                state.revision=++revision_;
            }
            if(dt>0.000001f) RefreshForwardIntercept(actor,input,actorForwardVelocity);
        } else if (!state.solutionLocked ||
            state.attackIntentSequence != actor.attackState.intentSequence ||
            state.attackTokenId != actor.attackState.tokenId) {
            LockSolution(
                actor,
                input,
                forwardVelocity,
                lateralVelocity,
                verticalVelocity);
            ++frame_.solutionsLockedThisFrame;
        } else if(state.compensatesForwardTravel && dt>0.000001f) {
            RefreshForwardIntercept(actor,input,actorForwardVelocity);
        }
        if(state.compensatesForwardTravel) {
            if (dt > 0.0f && state.forwardTravelPrediction && state.forwardInterceptReachable && hadPresentedWarning) {
                const Vector3 direction = Unit({state.targetLateralOffset - state.originLateralOffset,
                    state.targetVerticalOffset - state.originVerticalOffset, state.targetDistance - state.originDistance});
                const float agreement = direction.x*previousShotDirection.x + direction.y*previousShotDirection.y +
                    direction.z*previousShotDirection.z;
                // A large correction (12 degrees) needs a fresh readable warning,
                // rather than turning a nearly completed warning into a new shot.
                if (agreement < 0.9781476f) {
                    runtime.EnemyAttacks().RestartTelegraphForAimChange(actor);
                    RefreshForwardIntercept(actor,input,actorForwardVelocity);
                }
            }
            ++frame_.forwardCompensatedSolutions;
            if(!state.forwardInterceptReachable) {
                ++frame_.unreachableSolutions;
                runtime.EnemyAttacks().DeferUnreachableAttack(actor);
            } else if(actor.attackState.tokenReserved) {
                state.waitingForReachableLaunch=false;
            }
        }
        if (actor.desc.projectileDefinition.trajectory ==
            EnemyProjectileTrajectory::Predictive) {
            ++frame_.predictiveSolutions;
        } else if (actor.desc.projectileDefinition.trajectory ==
                   EnemyProjectileTrajectory::Homing) {
            ++frame_.homingSolutions;
        }
    }
    frame_.revision = revision_;
}

void EnemyTargetingSystem::LockSolution(
    CourseEnemyActor& actor,
    const EnemyTargetingFrameInput& input,
    float playerForwardVelocity,
    float playerLateralVelocity,
    float playerVerticalVelocity) {
    const EnemyProjectileDefinitionAsset& definition =
        actor.desc.projectileDefinition;
    EnemyTargetingRuntimeState& state = actor.targetingState;
    const Vector3 previousAim = state.turretAimDirection;
    state = {};
    state.turretAimDirection = previousAim;
    state.attackIntentSequence = actor.attackState.intentSequence;
    state.attackTokenId = actor.attackState.tokenId;
    state.originDistance = actor.desc.spawnDistance + actor.desc.distanceOffset -
        actor.desc.radius * 1.5f;
    state.originLateralOffset = actor.desc.lateralOffset;
    state.originVerticalOffset = actor.desc.verticalOffset;
    state.playerForwardVelocity = playerForwardVelocity;
    state.playerLateralVelocity = playerLateralVelocity;
    state.playerVerticalVelocity = playerVerticalVelocity;
    const float distanceToPlayer = (std::max)(
        0.0f, state.originDistance - input.playerDistance);
    const float speed = (std::max)(1.0f, definition.initialSpeed);
    state.predictedFlightSeconds = (std::clamp)(
        distanceToPlayer / speed,
        0.0f,
        (std::max)(0.0f, definition.maximumPredictionSeconds));
    const bool predictive =
        definition.trajectory == EnemyProjectileTrajectory::Predictive ||
        definition.trajectory == EnemyProjectileTrajectory::Homing;
    const float predictionSeconds = predictive
        ? state.predictedFlightSeconds * definition.predictionScale
        : 0.0f;
    state.targetDistance = input.playerDistance +
        playerForwardVelocity * predictionSeconds;
    state.targetLateralOffset = input.playerLateralOffset +
        playerLateralVelocity * predictionSeconds;
    state.targetVerticalOffset = input.playerVerticalOffset +
        playerVerticalVelocity * predictionSeconds;
    state.compensatesForwardTravel=actor.HoldsCombatPositionUntilResolved() &&
        definition.trajectory==EnemyProjectileTrajectory::Direct;
    state.forwardTravelPrediction = input.travelPrediction;
    state.solutionLocked = true;
    if(state.compensatesForwardTravel) RefreshForwardIntercept(actor,input,playerForwardVelocity);
    if (actor.desc.meshId == "combat_turret") {
        const Vector3 muzzle = ResolveTurretMuzzleRailPosition(actor,0.0f);
        state.originDistance = muzzle.z;
        state.originLateralOffset = muzzle.x;
        state.originVerticalOffset = muzzle.y;
    }
    state.initialized = true;
    state.solutionLocked = true;
    state.revision = ++revision_;
}

void EnemyTargetingSystem::RefreshForwardIntercept(CourseEnemyActor& actor,
        const EnemyTargetingFrameInput& input,float playerForwardVelocity) {
    auto& state=actor.targetingState;
    state.playerForwardVelocity=state.forwardTravelPrediction ? state.forwardTravelPrediction->sourceSpeed : playerForwardVelocity;
    state.forwardTravelReferenceDistance=input.playerDistance;
    state.predictedLaunchDelaySeconds=(std::max)(0.0f,actor.behaviorState.attackTimeRemaining);
    if(actor.fireSafetyReason=="visible time warming")
        state.predictedLaunchDelaySeconds=(std::max)(state.predictedLaunchDelaySeconds,
            input.minimumVisibleBeforeFire-actor.fireVisibleTime);
    const uint32_t steps=PrepareScheduledWeaponMount(actor,input,state.predictedLaunchDelaySeconds);
    if(actor.weaponMount.scheduledPredictionRequested) ++frame_.muzzleForecastsBuilt;
    frame_.muzzleForecastSteps+=steps;
    const Vector3 muzzle=ResolveEnemyProjectileMuzzleRailPosition(actor,0);
    const auto aim=ResolveEnemyProjectileAimSolution(actor,muzzle,state.predictedLaunchDelaySeconds);
    // Warning-direction corrections must use the same future origin as the
    // scheduled launch template, rather than adding the warning lead twice.
    state.originDistance=aim.muzzleRail.z;
    state.originLateralOffset=aim.muzzleRail.x;
    state.originVerticalOffset=aim.muzzleRail.y;
    state.predictedFlightSeconds=aim.flightSeconds;
    state.targetDistance=aim.targetRail.z;
    state.forwardInterceptReachable=aim.reachable &&
        CanEnemyProjectileVolleyReachTarget(actor,state.predictedLaunchDelaySeconds) &&
        CanEnemyProjectileVolleyReachTarget(actor);
    // The token and lateral/up anchor never change here. A stalled warning or
    // moving muzzle changes only the automatic forward-travel compensation.
}
