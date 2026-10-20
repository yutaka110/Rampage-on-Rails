#pragma once

#include "CourseRailTrackMeshBakePipeline.h"
#include "RailVehicleRenderer.h"
#include "RailVehicleWheelContactPresentationBridge.h"
#include "TwinShieldDronePose.h"
#include <array>
#include <functional>
#include <span>

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
    bool railStrike = false;
    bool nearMissNotified = false;
};
enum class RailTitleAttackCueKind { Muzzle, GroundImpact, NearMiss, RailImpact, WallImpact };
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
struct RailTitleRailImpact final {
    Vector3 position{};
    float age = 0.0f;
    uint32_t seed = 0;
    bool active = false;
    Vector3 normal{0,1,0};
    bool wall = false;
};

// A bounded, presentation-only collapse. Positions are fixed in world space;
// the cinematic clock owns gravity, breakup and the delayed smoke together.
struct RailTitleSandCollapse final {
    Vector3 origin{}, right{}, forward{};
    float age = 0.0f;
    uint32_t seed = 0;
    bool active = false;
};
struct RailTitleSandClod final {
    Vector3 position{}, velocity{}, rotation{}, scale{}, impactPosition{};
    float emissionAge = -1.0f;
    float impactAge = -1.0f;
    bool visible = false;
};
struct RailTitleCollapsePuff final {
    Vector3 position{};
    float radius = 0.0f, aspect = 1.0f, rotation = 0.0f, opacity = 0.0f;
};

struct RailReadyDroneFlight final {
    float forward = -66.0f, lateral = 1.2f, vertical = 7.0f;
    Vector3 rotation{0.0f,3.14159265f,0.0f};
};
inline constexpr uint32_t kRailReadyDroneCount=2;
RailReadyDroneFlight EvaluateRailReadyDroneFlight(float elapsed,uint32_t droneIndex=0);
RailReadyDroneFlight EvaluateRailReadyDroneFlight(float elapsed,const RailPath& rail,
    float playerDistance,float playerSpeed,uint32_t droneIndex=0);
TwinShieldDronePose BuildRailReadyDronePose(const RailPath& rail,float railDistance,
    float lateral,float vertical,Vector3 rotation,float radius);

struct RailWorldRaycastInput;

// Presentation only: no actor IDs, damage, locks or gameplay projectiles.
class RailReadyDroneSalvo final {
public:
    using WallTrace=std::function<bool(Vector3,float,uint32_t,Vector3&,Vector3&)>;
    static constexpr std::array<float,2> ShotTimes{1.46f,1.69f};
    static constexpr float ShotSpeed=125.0f;
    RailReadyDroneFlight Flight(float elapsed,float playerDistance,const RailPath& rail,float radius) const;
    static Vector3 Muzzle(const TwinShieldDronePose& pose,uint32_t barrelIndex);
    void Reset();
    void Update(float elapsed,float playerDistance,const RailPath& rail,float radius,const WallTrace& trace);
    bool Visible() const { return !shots_.empty() || impacts_[0].active || impacts_[1].active; }
    std::span<const RailTitleShot> Shots() const { return shots_; }
    std::span<const RailTitleRailImpact> Impacts() const { return impacts_; }
    std::span<const RailTitleAttackCue> Cues() const { return cues_; }
    float MuzzleFlash() const { return muzzleFlash_; }
    Vector3 MuzzlePosition() const { return muzzlePosition_; }
    static bool TraceVehicleNearMiss(const RailWorldRaycastInput& terrain,Vector3 origin,
        float launchDistance,uint32_t shotIndex,Vector3& contact,Vector3& normal);
    static bool ClearsVehicle(const RailTitleShot& shot,const RailPath& rail,float playerDistanceAtLaunch);
private:
    std::array<bool,2> planAttempted_{}, planned_{};
    std::array<Vector3,2> targets_{};
    std::array<float,2> durations_{};
    std::array<bool,2> attempted_{};
    std::array<bool,2> launched_{};
    std::array<Vector3,2> normals_{};
    std::array<RailTitleRailImpact,2> impacts_{};
    std::vector<RailTitleShot> shots_{};
    std::vector<RailTitleAttackCue> cues_{};
    float muzzleFlash_=0.0f;
    Vector3 muzzlePosition_{};
};

struct RailReadyCameraShot final {
    // Rail-local right, up, forward. Rear pursuit settles into a close forward
    // shot; its weight releases after the turn, across GO rather than mid-pass.
    Vector3 eyeOffset{0.0f,5.2f,-8.0f};
    Vector3 droneOffset{1.2f,7.0f,-66.0f};
    Vector3 wingmanOffset{-1.8f,8.4f,-80.0f};
    float weight = 1.0f;
    float focusBlend = 0.0f;
    float fovY = 0.70f;
};
RailReadyCameraShot EvaluateRailReadyCameraShot(float elapsed);

// Presentation clock shared by the reveal, HUD startup, sound crossfade and
// combat/input gate. Rail travel can continue on this clock without ticking combat.
struct RailTitleGameplayEntryFrame final {
    float blackout = 0.0f;
    float cameraHandoffWeight = 0.0f;
    float reticleProgress = 1.0f;
    float gaugeProgress = 1.0f;
    float informationOpacity = 1.0f;
    float rollingHandoff = 1.0f;
    float travelDeltaTime = 0.0f;
    float readyOpacity = 0.0f, readyProgress = 0.0f, goOpacity = 0.0f;
    float droneTurnProgress = 0.0f;
    bool active = false;
    bool enemyFireAllowed = true;
    bool blockingGameplay = false;
    bool reticleStarted = false;
    bool gaugesStarted = false;
    bool readyStarted = false, goStarted = false;
};

struct CourseEnemyActorDesc;
CourseEnemyActorDesc BuildRailReadyDrone(float startDistance,uint32_t droneIndex=0);

class RailTitleGameplayEntry final {
public:
    static constexpr float CameraSettleEnd = 3.0f;
    static constexpr float DroneTurnBegin = 3.20f;
    static constexpr float DroneTurnEnd = 4.35f;
    static constexpr float DroneBrakeEnd = 4.70f;
    static constexpr float Duration = 4.85f; // Complete the moving turn and braking before GO.
    static constexpr float CameraReleaseBegin = Duration - 0.05f;
    static constexpr float CameraReleaseEnd = Duration + 0.60f;
    static constexpr float ReadyBegin = 0.64f;
    static constexpr float WingmanReadyBegin = ReadyBegin + 0.28f;
    static constexpr float EnemyFireBegin = Duration + 0.70f;
    static constexpr float GoEnd = Duration + 0.85f;
    static constexpr float RevealBegin = 0.01f;
    static constexpr float RevealEnd = 0.54f;
    static constexpr float CameraBegin = 0.08f;
    static constexpr float CameraEnd = 1.62f;
    void Reset();
    void Begin();
    void Update(float deltaTime);
    // Retain markers crossed by cinematic travel until gameplay can dispatch them.
    std::vector<CourseEventMarker> DispatchCourseEvents(
        const std::vector<CourseEventMarker>& crossed, bool gameplayEnabled);
    const RailTitleGameplayEntryFrame& Frame() const { return frame_; }
    float Elapsed() const { return static_cast<float>(elapsed_); }
private:
    double elapsed_ = 0.0;
    bool begun_ = false;
    RailTitleGameplayEntryFrame frame_{};
    std::vector<CourseEventMarker> pendingEvents_;
};

// Submission replay loop. The same sand clock crosses the game/title swap;
// only a fully opaque cover permits resetting the playable world.
class RailTransitionDemo final {
public:
    static constexpr float CoverDuration = 2.01f;
    static constexpr float RevealDuration = 1.29f;
    void BeginGameplay();
    void Update(float deltaTime, bool startupComplete);
    bool RequestReturn();
    bool NeedsTitleSwap() const;
    void CommitTitleSwap();
    bool Returning() const;
    bool RevealingTitle() const;
    bool ButtonVisible() const;
    float ButtonOpacity() const;
    float Coverage() const;
    float SandAge() const;
    float DustStrength() const;
    float TravelDeltaTime() const { return travelDeltaTime_; }
    static bool HitTest(float x, float y, float width, float height);
private:
    enum class Phase { Title, Playing, Covering, Revealing };
    Phase phase_ = Phase::Title;
    bool startupComplete_ = false;
    double playTime_ = 0.0, transitionTime_ = 0.0;
    float travelDeltaTime_ = 0.0f;
};

struct RailSandTransitionAudioFrame final {
    bool start=false, pauseChanged=false, paused=false;
};
class RailSandTransitionAudioClock final {
public:
    RailSandTransitionAudioFrame Update(bool active,float age,bool focused);
    void Reset() { *this=RailSandTransitionAudioClock{}; }
private:
    bool wasActive_=false, started_=false, paused_=false;
    float previousAge_=0.0f;
};

class RailTitleScene final {
public:
    bool Initialize();
    void Update(float deltaTime);
    void BeginStart();
    bool Starting() const { return starting_; }
    bool ReadyForGameplay() const { return starting_ && startTime_ >= StartDuration &&
        StartProgress() >= 1.0f && Blackout() >= 0.999f; }
    float TunnelCameraDepth() const;
    float TunnelShade() const;
    float CurrentSpeed() const { return Speed; }
    float CameraFov() const { return 0.70f; }
    float StartProgress() const;
    float MenuOpacity() const;
    float Blackout() const;
    static float SandCoverageAtAge(float age);
    float AmbienceGain() const;
    float RollingGain() const;
    float RailReflectionOpacity() const;
    float RailReflectionDistance() const;
    Vector2 LightSweep() const;
    float LightSweepIllumination(Vector3 worldPosition) const;
    static CourseCameraKey DepartureCameraRig();
    RailTitleColors Colors(Vector4 gameplaySunColor) const;
    float Age() const { return age_; }
    const RailPath& Path() const { return path_; }
    const RailPath& SceneryPath() const { return starting_ ? sceneryPath_ : path_; }
    float SceneryDistance() const { return starting_ ? 400.0f+static_cast<float>(travel_-startTravel_) : state_.distance; }
    const CourseAsset& Scenery() const { return scenery_; }
    const CourseAsset& RenderScenery() const { return starting_ ? renderScenery_ : scenery_; }
    const CourseRailTrackMeshBakeResult& Track() const { return track_.Result(); }
    const RailVehicleRenderFrame& Vehicle() const { return vehicleFrame_; }
    const TwinShieldDronePose& Pursuer() const { return pursuer_; }
    Vector3 PursuerAimTarget() const { return pursuerAimTarget_; }
    const std::vector<RailTitleShot>& Shots() const { return shots_; }
    const std::vector<RailTitleAttackCue>& AttackCues() const { return attackCues_; }
    uint32_t ShotSequence() const { return shotSequence_; }
    const std::vector<RailTitleDustCloud>& DustClouds() const { return dustClouds_; }
    const RailTitleRailImpact& RailImpact() const { return railImpact_; }
    float RailImpactLight() const;
    float LensLeakAge() const;
    float LensLeakStrength() const;
    // Analytic trajectories stay identical at different frame rates and when paused.
    static RailTitleSandGrain EvaluateSandGrain(const RailTitleDustCloud& cloud, uint32_t index);
    static RailTitleSandGrain EvaluateRailSpark(const RailTitleRailImpact& impact, uint32_t index);
    const RailTitleSandCollapse& SandCollapse() const { return sandCollapse_; }
    static RailTitleSandClod EvaluateSandClod(const RailTitleSandCollapse& collapse, uint32_t index);
    static RailTitleSandGrain EvaluateCollapseGrain(const RailTitleSandCollapse& collapse, uint32_t index);
    static RailTitleCollapsePuff EvaluateCollapsePuff(const RailTitleSandCollapse& collapse, uint32_t index);
    float CollapseDustStrength() const;
    const RailVehicleWheelContactPresentationFrame& Wheels() const { return wheels_.Frame(); }
    Vector3 CameraPosition() const { return cameraPosition_; }
    Vector3 CameraTarget() const { return cameraTarget_; }
    float Distance() const { return state_.distance; }
    float LapLength() const { return lapLength_; }
    static constexpr float Speed = 12.0f;
    static constexpr float OrbitDuration = 1.0f;
    static constexpr float StartDuration = 3.69f;
    static constexpr float FilmBurnDelay = 0.55f;
    static constexpr float FilmBurnDuration = 1.20f;
    static constexpr float FilmBurnTailBegin = 0.72f;
    static constexpr float FilmBurnTailDuration = 0.08f;
    static constexpr float DepartureShotTime = 1.02f;
    static constexpr float DepartureNearMissTime = 1.44f;
    static constexpr float DepartureImpactTime = 1.62f;
    static constexpr float DepartureImpactAhead = 18.0f;
    static constexpr float DepartureShotRadius = 0.65f;
    static constexpr uint32_t RailImpactSparkCount = 30;
    static constexpr float ReflectionDuration = 0.44f;
    static constexpr float ReflectionDelay = 0.035f;
    static constexpr float LightSweepDuration = 0.36f;
    static constexpr float TunnelLead = 18.0f;
    static constexpr float CollapseBegin = DepartureImpactTime + 0.06f;
    static constexpr uint32_t CollapseClodCount = 40;
    static constexpr uint32_t CollapseGrainsPerClod = 12;
    static constexpr uint32_t CollapsePuffsPerClod = 4;
    static constexpr uint32_t CollapseGrainCount = CollapseClodCount * CollapseGrainsPerClod;
    static constexpr uint32_t CollapsePuffCount = CollapseClodCount * CollapsePuffsPerClod;
    // Shared 1600 x 900 layout coordinates, including letterboxing.
    static int HitTest(float x, float y, float width, float height);
private:
    void UpdateAttack(float deltaTime);
    void UpdateSandCollapse();
    Vector3 DepartureDronePosition(float elapsed) const;
    Vector3 DepartureImpactTarget() const;
    void PrepareDepartureWallShot();
    float ChaseDistance() const { return 8.0f; }
    RailPath path_;
    RailPath sceneryPath_;
    CourseAsset scenery_;
    CourseAsset renderScenery_;
    RailTitleSandCollapse sandCollapse_{};
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
    float departureAge_ = 0.0f;
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
    RailTitleRailImpact railImpact_{};
    bool departureShotFired_ = false;
    Vector3 departureShotOrigin_{}, departureWallTarget_{}, departureWallNormal_{0,1,0};
    bool departureWallHit_ = false;
    double attackTime_ = 0.0;
    double nextShotTime_ = 1.2;
    uint32_t shotSequence_ = 0;
    int burstShot_ = 0;
    float muzzleFlashTime_ = 0.0f;
};
