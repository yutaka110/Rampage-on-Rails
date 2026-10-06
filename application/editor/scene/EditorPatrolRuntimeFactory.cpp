#include "EditorPatrolRuntimeFactory.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <locale>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace editor {
namespace {

constexpr float kEpsilon = 0.00001f;

struct AffineTransform {
    double basis[3][3]{
        {1.0, 0.0, 0.0},
        {0.0, 1.0, 0.0},
        {0.0, 0.0, 1.0},
    };
    double translation[3]{0.0, 0.0, 0.0};
};

const EditorSceneProperty* FindProperty(
    const EditorSceneComponent& component,
    std::string_view name) {
    const auto found = std::find_if(
        component.properties.begin(), component.properties.end(),
        [&](const EditorSceneProperty& property) {
            return property.name == name;
        });
    return found == component.properties.end() ? nullptr : &*found;
}

bool ParseVector3(std::string_view text, Vector3& value) {
    std::istringstream input{std::string(text)};
    input.imbue(std::locale::classic());
    if (!(input >> value.x >> value.y >> value.z) ||
        !std::isfinite(value.x) ||
        !std::isfinite(value.y) ||
        !std::isfinite(value.z)) {
        return false;
    }
    input >> std::ws;
    return input.eof();
}

AffineTransform Multiply(
    const AffineTransform& parent,
    const AffineTransform& local) {
    AffineTransform result{};
    for (std::size_t row = 0; row < 3; ++row) {
        for (std::size_t column = 0; column < 3; ++column) {
            result.basis[row][column] = 0.0;
            for (std::size_t index = 0; index < 3; ++index) {
                result.basis[row][column] +=
                    parent.basis[row][index] *
                    local.basis[index][column];
            }
        }
        result.translation[row] = parent.translation[row];
        for (std::size_t index = 0; index < 3; ++index) {
            result.translation[row] +=
                parent.basis[row][index] *
                local.translation[index];
        }
    }
    return result;
}

AffineTransform MakeLocalTransform(
    const Vector3& translation,
    const Vector3& rotation,
    const Vector3& scale) {
    const double cx = std::cos(rotation.x);
    const double sx = std::sin(rotation.x);
    const double cy = std::cos(rotation.y);
    const double sy = std::sin(rotation.y);
    const double cz = std::cos(rotation.z);
    const double sz = std::sin(rotation.z);
    const double rotationMatrix[3][3]{
        {cz * cy, cz * sy * sx - sz * cx,
            cz * sy * cx + sz * sx},
        {sz * cy, sz * sy * sx + cz * cx,
            sz * sy * cx - cz * sx},
        {-sy, cy * sx, cy * cx},
    };
    const double scales[3]{scale.x, scale.y, scale.z};
    AffineTransform result{};
    for (std::size_t row = 0; row < 3; ++row) {
        for (std::size_t column = 0; column < 3; ++column) {
            result.basis[row][column] =
                rotationMatrix[row][column] * scales[column];
        }
    }
    result.translation[0] = translation.x;
    result.translation[1] = translation.y;
    result.translation[2] = translation.z;
    return result;
}

bool LocalTransform(
    const EditorScene& scene,
    const EditorSceneEntity& entity,
    AffineTransform& output,
    std::string& error) {
    const EditorSceneComponent* transform =
        scene.FindComponent(entity, kEditorTransformComponentType);
    if (transform == nullptr || !transform->enabled) {
        output = {};
        return true;
    }
    Vector3 translation{};
    Vector3 rotation{};
    Vector3 scale{1.0f, 1.0f, 1.0f};
    const std::array<std::pair<std::string_view, Vector3*>, 3> fields{{
        {"translation", &translation},
        {"rotation", &rotation},
        {"scale", &scale},
    }};
    for (const auto& [name, target] : fields) {
        const EditorSceneProperty* property =
            FindProperty(*transform, name);
        if (property == nullptr ||
            !ParseVector3(property->value, *target)) {
            error =
                "Entity \"" + entity.name +
                "\" has a malformed Transform " +
                std::string(name) + ".";
            return false;
        }
    }
    output = MakeLocalTransform(translation, rotation, scale);
    return true;
}

bool WorldTransform(
    const EditorScene& scene,
    const EditorSceneEntity& entity,
    std::unordered_map<std::string, AffineTransform>& cache,
    std::unordered_set<std::string>& resolving,
    AffineTransform& output,
    std::string& error) {
    const auto cached = cache.find(entity.guid);
    if (cached != cache.end()) {
        output = cached->second;
        return true;
    }
    if (!resolving.insert(entity.guid).second) {
        error = "Scene hierarchy contains a Transform cycle.";
        return false;
    }
    AffineTransform local{};
    if (!LocalTransform(scene, entity, local, error)) {
        resolving.erase(entity.guid);
        return false;
    }
    output = local;
    if (!entity.parentGuid.empty()) {
        const EditorSceneEntity* parent =
            scene.FindEntity(entity.parentGuid);
        if (parent == nullptr) {
            error = "Spline Route Entity has a missing parent.";
            resolving.erase(entity.guid);
            return false;
        }
        AffineTransform parentWorld{};
        if (!WorldTransform(
                scene, *parent, cache, resolving,
                parentWorld, error)) {
            resolving.erase(entity.guid);
            return false;
        }
        output = Multiply(parentWorld, local);
    }
    resolving.erase(entity.guid);
    cache.emplace(entity.guid, output);
    return true;
}

Vector3 TransformPoint(
    const AffineTransform& transform,
    const Vector3& point) {
    return {
        static_cast<float>(
            transform.translation[0] +
            transform.basis[0][0] * point.x +
            transform.basis[0][1] * point.y +
            transform.basis[0][2] * point.z),
        static_cast<float>(
            transform.translation[1] +
            transform.basis[1][0] * point.x +
            transform.basis[1][1] * point.y +
            transform.basis[1][2] * point.z),
        static_cast<float>(
            transform.translation[2] +
            transform.basis[2][0] * point.x +
            transform.basis[2][1] * point.y +
            transform.basis[2][2] * point.z),
    };
}

Vector3 TransformDirection(
    const AffineTransform& transform,
    const Vector3& direction) {
    return {
        static_cast<float>(
            transform.basis[0][0] * direction.x +
            transform.basis[0][1] * direction.y +
            transform.basis[0][2] * direction.z),
        static_cast<float>(
            transform.basis[1][0] * direction.x +
            transform.basis[1][1] * direction.y +
            transform.basis[1][2] * direction.z),
        static_cast<float>(
            transform.basis[2][0] * direction.x +
            transform.basis[2][1] * direction.y +
            transform.basis[2][2] * direction.z),
    };
}

float Dot(const Vector3& left, const Vector3& right) noexcept {
    return left.x * right.x + left.y * right.y +
        left.z * right.z;
}

float DistanceSquared(
    const Vector3& left,
    const Vector3& right) noexcept {
    const Vector3 delta{
        left.x - right.x,
        left.y - right.y,
        left.z - right.z,
    };
    return Dot(delta, delta);
}

struct RailProjection {
    float distance = 0.0f;
    float lateral = 0.0f;
    float vertical = 0.0f;
};

RailProjection ProjectToRail(
    const Vector3& world,
    const RailPath& railPath) {
    const float length = railPath.Length();
    constexpr uint32_t kCoarseSamples = 96;
    float best = 0.0f;
    float bestSquared =
        (std::numeric_limits<float>::max)();
    for (uint32_t index = 0; index <= kCoarseSamples; ++index) {
        const float distance =
            length * static_cast<float>(index) /
            static_cast<float>(kCoarseSamples);
        const float candidate =
            DistanceSquared(world, railPath.Evaluate(distance).position);
        if (candidate < bestSquared) {
            bestSquared = candidate;
            best = distance;
        }
    }
    float window = length /
        static_cast<float>(kCoarseSamples);
    for (uint32_t iteration = 0; iteration < 8; ++iteration) {
        const float left =
            (std::clamp)(best - window, 0.0f, length);
        const float right =
            (std::clamp)(best + window, 0.0f, length);
        for (const float distance : std::array<float, 5>{
                 left, (left + best) * 0.5f, best,
                 (best + right) * 0.5f, right}) {
            const float candidate =
                DistanceSquared(
                    world, railPath.Evaluate(distance).position);
            if (candidate < bestSquared) {
                bestSquared = candidate;
                best = distance;
            }
        }
        window *= 0.5f;
    }
    const RailPathSample sample = railPath.Evaluate(best);
    const Vector3 delta{
        world.x - sample.position.x,
        world.y - sample.position.y,
        world.z - sample.position.z,
    };
    return {best, Dot(delta, sample.right), Dot(delta, sample.up)};
}

const CourseEnemyActor* FindEnemy(
    CourseSpawnRuntime& runtime,
    std::string_view waveId) {
    const auto found = std::find_if(
        runtime.Enemies().begin(),
        runtime.Enemies().end(),
        [&](const CourseEnemyActor& enemy) {
            return enemy.desc.waveId == waveId;
        });
    return found == runtime.Enemies().end()
        ? nullptr : &*found;
}

float ResolveDistance(
    EditorPatrolRuntimeInstance& patrol,
    float length,
    float delta) {
    if (length <= kEpsilon) return 0.0f;
    float candidate =
        patrol.distance +
        delta * static_cast<float>(patrol.direction);
    switch (patrol.traversalMode) {
    case EditorPatrolTraversalMode::Loop:
        candidate = std::fmod(candidate, length);
        if (candidate < 0.0f) candidate += length;
        break;
    case EditorPatrolTraversalMode::Once:
        if (candidate <= 0.0f || candidate >= length) {
            patrol.completed = true;
        }
        candidate = (std::clamp)(candidate, 0.0f, length);
        break;
    case EditorPatrolTraversalMode::PingPong: {
        const float period = length * 2.0f;
        float phase = std::fmod(candidate, period);
        if (phase < 0.0f) phase += period;
        if (phase > length) {
            candidate = period - phase;
            patrol.direction = -1;
        } else {
            candidate = phase;
            patrol.direction = 1;
        }
        break;
    }
    }
    return candidate;
}

} // namespace

bool EditorPatrolRuntimeWorld::ReplaceRoutes(
    std::vector<EditorSplineRouteRuntimeInstance> routes,
    std::string* errorMessage) {
    std::unordered_set<std::string> ids;
    std::unordered_set<std::string> entities;
    for (const auto& route : routes) {
        if (route.stableId.empty() || route.entityGuid.empty() ||
            !route.evaluator.Valid() ||
            !ids.insert(route.stableId).second ||
            !entities.insert(route.entityGuid).second) {
            if (errorMessage != nullptr) {
                *errorMessage =
                    "Runtime Spline Routes require unique stable IDs, "
                    "Entity GUIDs, and valid evaluators.";
            }
            return false;
        }
    }
    std::sort(
        routes.begin(), routes.end(),
        [](const auto& left, const auto& right) {
            return left.entityGuid < right.entityGuid;
        });
    routes_ = std::move(routes);
    ++revision_;
    if (errorMessage != nullptr) errorMessage->clear();
    return true;
}

bool EditorPatrolRuntimeWorld::ReplacePatrols(
    std::vector<EditorPatrolRuntimeInstance> patrols,
    CourseSpawnRuntime* spawnRuntime,
    const RailPath* railPath,
    std::string* errorMessage) {
    if ((!patrols.empty() &&
         (spawnRuntime == nullptr || railPath == nullptr ||
          railPath->Length() <= kEpsilon))) {
        if (errorMessage != nullptr) {
            *errorMessage =
                "Runtime Patrol requires Spawn Runtime and a valid Rail Path.";
        }
        return false;
    }
    std::unordered_set<std::string> ids;
    std::unordered_set<std::string> entities;
    for (const auto& patrol : patrols) {
        if (patrol.stableId.empty() || patrol.entityGuid.empty() ||
            FindRoute(patrol.routeEntityGuid) == nullptr ||
            FindEnemy(*spawnRuntime, patrol.enemyWaveId) == nullptr ||
            !ids.insert(patrol.stableId).second ||
            !entities.insert(patrol.entityGuid).second) {
            if (errorMessage != nullptr) {
                *errorMessage =
                    "Runtime Patrol requires a unique spawned Enemy and "
                    "a resolved Spline Route.";
            }
            return false;
        }
    }
    std::sort(
        patrols.begin(), patrols.end(),
        [](const auto& left, const auto& right) {
            return left.stableId < right.stableId;
        });
    patrols_ = std::move(patrols);
    spawnRuntime_ = spawnRuntime;
    railPath_ = railPath;
    ++revision_;
    if (errorMessage != nullptr) errorMessage->clear();
    return true;
}

void EditorPatrolRuntimeWorld::ClearRoutes() noexcept {
    if (routes_.empty()) return;
    routes_.clear();
    ++revision_;
}

void EditorPatrolRuntimeWorld::ClearPatrols() noexcept {
    if (patrols_.empty() &&
        spawnRuntime_ == nullptr && railPath_ == nullptr) {
        return;
    }
    patrols_.clear();
    spawnRuntime_ = nullptr;
    railPath_ = nullptr;
    ++revision_;
}

void EditorPatrolRuntimeWorld::Clear() noexcept {
    const bool changed =
        !routes_.empty() || !patrols_.empty() ||
        spawnRuntime_ != nullptr || railPath_ != nullptr;
    routes_.clear();
    patrols_.clear();
    spawnRuntime_ = nullptr;
    railPath_ = nullptr;
    if (changed) ++revision_;
}

void EditorPatrolRuntimeWorld::Update(float deltaTime) {
    if (spawnRuntime_ == nullptr || railPath_ == nullptr ||
        !std::isfinite(deltaTime) || deltaTime <= 0.0f) {
        return;
    }
    for (EditorPatrolRuntimeInstance& patrol : patrols_) {
        const CourseEnemyActor* enemy =
            FindEnemy(*spawnRuntime_, patrol.enemyWaveId);
        const EditorSplineRouteRuntimeInstance* route =
            FindRoute(patrol.routeEntityGuid);
        if (enemy == nullptr || route == nullptr) continue;
        if (!patrol.completed) {
            patrol.distance = ResolveDistance(
                patrol,
                route->evaluator.TotalLength(),
                patrol.speed * deltaTime);
        }
        const EditorSplineRouteSample sample =
            route->evaluator.EvaluateDistance(
                patrol.distance,
                patrol.traversalMode ==
                        EditorPatrolTraversalMode::Loop
                    ? EditorSplineRouteDistanceMode::Wrap
                    : EditorSplineRouteDistanceMode::Clamp);
        if (!sample.valid) continue;
        const RailProjection projection =
            ProjectToRail(sample.position, *railPath_);
        spawnRuntime_->SetEnemyRailPose(enemy->actorId,
            projection.distance - enemy->desc.spawnDistance,
            projection.lateral, projection.vertical, 0.0f);
    }
}

const EditorSplineRouteRuntimeInstance*
EditorPatrolRuntimeWorld::FindRoute(
    std::string_view entityGuid) const {
    const auto found = std::lower_bound(
        routes_.begin(), routes_.end(), entityGuid,
        [](const EditorSplineRouteRuntimeInstance& route,
           std::string_view value) {
            return route.entityGuid < value;
        });
    return found != routes_.end() &&
        found->entityGuid == entityGuid
        ? &*found : nullptr;
}

const EditorPatrolRuntimeInstance*
EditorPatrolRuntimeWorld::FindPatrol(
    std::string_view stableId) const {
    const auto found = std::lower_bound(
        patrols_.begin(), patrols_.end(), stableId,
        [](const EditorPatrolRuntimeInstance& patrol,
           std::string_view value) {
            return patrol.stableId < value;
        });
    return found != patrols_.end() &&
        found->stableId == stableId
        ? &*found : nullptr;
}

EditorSceneRuntimeFactoryResult
EditorSplineRouteRuntimeFactory::Instantiate(
    const EditorScene& scene,
    const std::vector<EditorSceneRuntimeComponentRecord>& components,
    const EditorSceneRuntimeServiceRegistry& services) {
    EditorSceneRuntimeFactoryResult result{};
    EditorPatrolRuntimeTarget* target =
        services.Find<EditorPatrolRuntimeTarget>(
            kEditorPatrolRuntimeTargetServiceId);
    if (target == nullptr || target->world == nullptr) {
        result.message =
            "Spline Route Runtime Factory requires Patrol Runtime World.";
        return result;
    }

    std::unordered_map<std::string, AffineTransform> cache;
    std::unordered_set<std::string> resolving;
    std::vector<EditorSplineRouteRuntimeInstance> routes;
    routes.reserve(components.size());
    for (const EditorSceneRuntimeComponentRecord& record : components) {
        if (record.entity == nullptr || record.component == nullptr) {
            result.message =
                "Spline Route Runtime Factory received an invalid record.";
            return result;
        }
        EditorSplineRouteComponent routeComponent{};
        std::string error;
        if (!EditorSplineRouteComponent::FromSceneComponent(
                *record.component, routeComponent, &error)) {
            result.message = error;
            return result;
        }
        AffineTransform world{};
        if (!WorldTransform(
                scene, *record.entity, cache, resolving,
                world, error)) {
            result.message = error;
            return result;
        }
        for (auto& point : routeComponent.controlPoints) {
            point.position = TransformPoint(world, point.position);
        }
        routeComponent.upVector =
            TransformDirection(world, routeComponent.upVector);
        EditorSplineRouteEvaluationService evaluator;
        if (!evaluator.Build(routeComponent, &error)) {
            result.message =
                "Runtime Spline Route \"" +
                record.entity->name + "\" failed: " + error;
            return result;
        }
        routes.push_back(
            {record.stableId, record.entity->guid,
             record.sourceHash, std::move(evaluator)});
    }
    std::string error;
    if (!target->world->ReplaceRoutes(
            std::move(routes), &error)) {
        result.message = error;
        return result;
    }
    activeWorld_ = target->world;
    result.succeeded = true;
    result.applied = true;
    result.message =
        "Runtime Spline Routes instantiated: " +
        std::to_string(activeWorld_->Routes().size()) + ".";
    return result;
}

void EditorSplineRouteRuntimeFactory::Destroy() noexcept {
    if (activeWorld_ != nullptr) activeWorld_->ClearRoutes();
    activeWorld_ = nullptr;
}

EditorSceneRuntimeFactoryResult
EditorPatrolRuntimeFactory::Instantiate(
    const EditorScene&,
    const std::vector<EditorSceneRuntimeComponentRecord>& components,
    const EditorSceneRuntimeServiceRegistry& services) {
    EditorSceneRuntimeFactoryResult result{};
    EditorPatrolRuntimeTarget* target =
        services.Find<EditorPatrolRuntimeTarget>(
            kEditorPatrolRuntimeTargetServiceId);
    if (target == nullptr || target->world == nullptr ||
        target->spawnRuntime == nullptr ||
        target->railPath == nullptr) {
        result.message =
            "Patrol Runtime Factory requires World, Spawn Runtime, "
            "and Rail Path services.";
        return result;
    }
    std::vector<EditorPatrolRuntimeInstance> patrols;
    patrols.reserve(components.size());
    for (const EditorSceneRuntimeComponentRecord& record : components) {
        if (record.entity == nullptr || record.component == nullptr) {
            result.message =
                "Patrol Runtime Factory received an invalid record.";
            return result;
        }
        EditorPatrolComponent component{};
        std::string error;
        if (!EditorPatrolComponent::FromSceneComponent(
                *record.component, component, &error)) {
            result.message = error;
            return result;
        }
        const std::string routeGuid =
            component.routeEntityGuid.empty()
            ? record.entity->guid
            : component.routeEntityGuid;
        EditorPatrolRuntimeInstance patrol{};
        patrol.stableId = record.stableId;
        patrol.entityGuid = record.entity->guid;
        patrol.routeEntityGuid = routeGuid;
        patrol.enemyWaveId =
            "editor.scene.spawn:" + record.entity->guid;
        patrol.sourceHash = record.sourceHash;
        patrol.speed = component.speed;
        patrol.distance = component.startDistance;
        patrol.traversalMode = component.traversalMode;
        patrol.direction = component.reverse ? -1 : 1;
        patrols.push_back(std::move(patrol));
    }
    std::string error;
    if (!target->world->ReplacePatrols(
            std::move(patrols),
            target->spawnRuntime,
            target->railPath,
            &error)) {
        result.message = error;
        return result;
    }
    activeWorld_ = target->world;
    result.succeeded = true;
    result.applied = true;
    result.message =
        "Runtime Patrols instantiated: " +
        std::to_string(activeWorld_->Patrols().size()) + ".";
    return result;
}

void EditorPatrolRuntimeFactory::Destroy() noexcept {
    if (activeWorld_ != nullptr) activeWorld_->ClearPatrols();
    activeWorld_ = nullptr;
}

} // namespace editor
