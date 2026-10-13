#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "EnemyCombatSystem.h"
#include "../terrain/RailPath.h"
#include "utils/math/Vector.h"

class CourseSpawnRuntime;
struct CourseEnemyActor;
namespace ge3::debug { class DebugDrawSystem; }

enum class EnemyCombatAnimationState : uint8_t {
    Spawn,
    Engage,
    Telegraph,
    Attack,
    Recover,
    HitReact,
    Death,
    Hidden,
};

enum class EnemyCombatPresentationAudioCueKind : uint8_t {
    Spawn,
    Engage,
    Attack,
    HitReact,
    Death,
};

struct EnemyCombatPresentationSettings final {
    bool enabled = true;
    bool emitAttackAudio = false; // Telegraph feedback owns the default fire cue.
    float masterAudioVolume = 0.72f;
    float audioReferenceDistance = 42.0f;
    float audioPanWidth = 18.0f;
    float idleBobAmplitude = 0.16f;
    float hitShakeAmplitude = 0.24f;
    float attackRecoilDistance = 0.46f;
    float deathDropDistance = 1.35f;
    float telegraphPulseStrength = 0.16f;
    float minimumSpawnAlpha = 0.16f;
    float dronePodSpread = 1.08f;
    size_t maximumAudioCuesPerFrame = 12;
    size_t maximumVfxCommandsPerFrame = 8;
};

// Actor-local presentation output. This never feeds collision, aim or damage.
struct EnemyCombatActorPresentation final {
    uint32_t actorId = 0;
    EnemyCombatAnimationState animation = EnemyCombatAnimationState::Hidden;
    float animationNormalizedTime = 0.0f;
    float forwardOffset = 0.0f;
    float lateralOffset = 0.0f;
    float verticalOffset = 0.0f;
    Vector3 rotationOffset{};
    Vector3 bodyScale{1.0f, 1.0f, 1.0f};
    float scaleMultiplier = 1.0f;
    Vector4 materialColor{1.0f, 1.0f, 1.0f, 1.0f};
    Vector4 coreColor{0.30f, 0.92f, 1.0f, 1.0f};
    float flashStrength = 0.0f;
    float emissiveStrength = 0.0f;
    float silhouetteSpread = 1.0f;
    float weaponCharge = 0.0f;
    // Presentation-only outline strength. This is raised during
    // decision-critical states so bright terrain cannot erase the silhouette.
    float contrastBackdropStrength = 0.0f;
    bool visible = false;
    bool commercialSilhouette = false;
    uint64_t sourceCombatRevision = 0;
    uint64_t sourceBehaviorRevision = 0;
    uint64_t sourceAttackRevision = 0;
    bool attackTokenReserved = false;
    bool attackCommittedThisFrame = false;
    bool turret = false;
    bool turretMuzzleActive = false;
    Vector3 turretWorldRotation{};
};

// Pure pose evaluation also used before attack execution to bind the weapon
// mounts to the same banking/hover/recoil pose submitted by the renderer.
EnemyCombatActorPresentation ResolveEnemyCombatActorPresentation(
    const CourseEnemyActor& actor, const EnemyCombatPresentationSettings& settings);

struct EnemyCombatPresentationAudioCue final {
    EnemyCombatPresentationAudioCueKind kind =
        EnemyCombatPresentationAudioCueKind::Spawn;
    uint32_t actorId = 0;
    Vector3 worldPosition{};
    float volume = 0.0f;
    float pitch = 1.0f;
    float pan = 0.0f;
};

struct EnemyCombatPresentationVfxCommand final {
    uint32_t actorId = 0;
    Vector3 worldPosition{};
    Vector4 color{1.0f, 1.0f, 1.0f, 1.0f};
    const char* cueId = "";
    const char* effectName = "hit_ring";
    float radius = 1.0f;
    float lifetime = 0.4f;
};

struct EnemyDestructionBurst final {
    uint32_t actorId = 0;
    Vector3 worldPosition{};
    float radius = 1.0f;
    float age = 0.0f;
};

struct EnemyCombatPresentationFrame final {
    std::vector<EnemyDestructionBurst> destructionBursts;
    std::vector<EnemyCombatActorPresentation> actors;
    std::vector<EnemyCombatPresentationAudioCue> audioCues;
    std::vector<EnemyCombatPresentationVfxCommand> vfxCommands;
    uint32_t droppedAudioCues = 0;
    uint32_t droppedVfxCommands = 0;
    float cameraShakeImpulse = 0.0f;
    float cameraFovImpulseRadians = 0.0f;
    float cameraRollImpulseRadians = 0.0f;
    uint64_t revision = 0;
};

struct EnemyCombatPresentationInput final {
    const CourseSpawnRuntime* runtime = nullptr;
    const RailPath* railPath = nullptr;
    std::span<const EnemyCombatEvent> events{};
    Vector3 listenerPosition{};
    Vector3 listenerRight{1.0f, 0.0f, 0.0f};
    float deltaTime = 0.0f;
    bool gameplayActive = true;
    EnemyCombatPresentationSettings settings{};
};

// Converts authoritative combat state/events into procedural model animation,
// per-actor material parameters and bounded one-shot audio/VFX commands.
class EnemyCombatPresentationBridge final {
public:
    void Reset();
    void Update(const EnemyCombatPresentationInput& input);
    void AppendDestructionPrimitives(ge3::debug::DebugDrawSystem& draw) const;

    const EnemyCombatPresentationFrame& Frame() const noexcept { return frame_; }
    const EnemyCombatActorPresentation* FindActor(uint32_t actorId) const noexcept;

private:
    EnemyCombatPresentationFrame frame_{};
    uint64_t revision_ = 0;
};

const char* ToString(EnemyCombatAnimationState state) noexcept;
const char* ToString(EnemyCombatPresentationAudioCueKind kind) noexcept;
