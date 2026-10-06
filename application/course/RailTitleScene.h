#pragma once

#include "CourseRailTrackMeshBakePipeline.h"
#include "RailVehicleRenderer.h"
#include "RailVehicleWheelContactPresentationBridge.h"
#include "TwinShieldDronePose.h"

// A presentation-only world: it never advances the playable course or combat.
struct RailTitleColors final {
    Vector4 light{1.15f,0.89f,0.72f,1.0f};
    Vector3 background{0.55f,0.51f,0.46f};
    float transition = 0.0f;
};

struct RailTitleShot final {
    uint32_t id = 0;
    Vector3 origin{}, position{}, target{};
    float age = 0.0f;
    float duration = 0.78f;
};
enum class RailTitleAttackCueKind { Muzzle, GroundImpact };
struct RailTitleAttackCue final {
    RailTitleAttackCueKind kind = RailTitleAttackCueKind::Muzzle;
    Vector3 position{};
};
struct RailTitleDustCloud final {
    static constexpr float Lifetime = 2.15f;
    Vector3 origin{};
    float age = 0.0f;
    uint32_t seed = 0;
};
struct RailTitleSandGrain final {
    Vector3 position{};
    Vector3 velocity{};
    float radius = 0.0f;
    float opacity = 0.0f;
};

class RailTitleScene final {
public:
    bool Initialize();
    void Update(float deltaTime);
    void BeginStart();
    bool Starting() const { return starting_; }
    bool ReadyForGameplay() const { return starting_ && startTime_ >= StartDuration; }
    float TunnelCameraDepth() const;
    float TunnelShade() const;
    float CurrentSpeed() const { return Speed; }
    float CameraFov() const { return 0.70f; }
    float StartProgress() const;
    float MenuOpacity() const;
    float Blackout() const;
    float AmbienceGain() const;
    RailTitleColors Colors(Vector4 gameplaySunColor) const;
    float Age() const { return age_; }
    const RailPath& Path() const { return path_; }
    const RailPath& SceneryPath() const { return starting_ ? sceneryPath_ : path_; }
    float SceneryDistance() const { return starting_ ? 400.0f+static_cast<float>(travel_-startTravel_) : state_.distance; }
    const CourseAsset& Scenery() const { return scenery_; }
    const CourseRailTrackMeshBakeResult& Track() const { return track_.Result(); }
    const RailVehicleRenderFrame& Vehicle() const { return vehicleFrame_; }
    const TwinShieldDronePose& Pursuer() const { return pursuer_; }
    Vector3 PursuerAimTarget() const { return pursuerAimTarget_; }
    const std::vector<RailTitleShot>& Shots() const { return shots_; }
    const std::vector<RailTitleAttackCue>& AttackCues() const { return attackCues_; }
    uint32_t ShotSequence() const { return shotSequence_; }
    const std::vector<RailTitleDustCloud>& DustClouds() const { return dustClouds_; }
    // Analytic trajectories stay identical at different frame rates and when paused.
    static RailTitleSandGrain EvaluateSandGrain(const RailTitleDustCloud& cloud, uint32_t index);
    const RailVehicleWheelContactPresentationFrame& Wheels() const { return wheels_.Frame(); }
    Vector3 CameraPosition() const { return cameraPosition_; }
    Vector3 CameraTarget() const { return cameraTarget_; }
    float Distance() const { return state_.distance; }
    float LapLength() const { return lapLength_; }
    static constexpr float Speed = 12.0f;
    static constexpr float OrbitDuration = 1.0f;
    static constexpr float StartDuration = 2.55f;
    static constexpr float TunnelLead = 18.0f;
    // Shared 1600 x 900 layout coordinates, including letterboxing.
    static int HitTest(float x, float y, float width, float height);
private:
    void UpdateAttack(float deltaTime);
    float ChaseDistance() const { return 8.0f; }
    RailPath path_;
    RailPath sceneryPath_;
    CourseAsset scenery_;
    CourseRailTrackMeshBakePipeline track_;
    RailVehicleDefinition definition_ = RailVehicleDefinition::MineCartDefaults();
    RailVehicleRuntimeState state_;
    RailVehicleTrackContactPoseSolver contacts_;
    RailVehicleActor actor_;
    RailVehicleRenderer renderer_;
    RailVehicleRenderFrame vehicleFrame_;
    RailVehicleWheelContactPresentationBridge wheels_;
    double travel_ = 0.0;
    double startTravel_ = 0.0;
    float startDistance_ = 0.0f;
    bool starting_ = false;
    float startTime_ = 0.0f;
    float age_ = 0.0f;
    float lapStart_ = 0.0f;
    float lapLength_ = 0.0f;
    Vector3 cameraPosition_{};
    Vector3 cameraTarget_{};
    TwinShieldDronePose pursuer_{};
    Vector3 pursuerAimTarget_{};
    std::vector<RailTitleShot> shots_;
    std::vector<RailTitleAttackCue> attackCues_;
    std::vector<RailTitleDustCloud> dustClouds_;
    double attackTime_ = 0.0;
    double nextShotTime_ = 1.2;
    uint32_t shotSequence_ = 0;
    int burstShot_ = 0;
    float muzzleFlashTime_ = 0.0f;
};
