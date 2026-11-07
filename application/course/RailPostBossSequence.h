#pragma once
#include "CourseAsset.h"
#include "CourseEventDispatcher.h"
#include "CourseSpawnRuntime.h"
#include "PlayerHitboxSystem.h"
#include "RailRearPursuitRules.h"
#include "RailBridgeEscape.h"
#include "RailBridgeStructure.h"
#include "RailPursuerCoordination.h"
#include <array>
#include <algorithm>
#include <cmath>
#include <span>
#include <vector>

// Runtime-relative blockout: a late boss clear must get the same breathing
// room as an early clear. No teleport, fake defeat, or fixed-distance clear.
class RailPostBossSequence final {
public:
    static constexpr const char* Marker="post-boss-white-bridge";
    static constexpr float BridgeStart=360, BridgeLength=4400;
    static constexpr float MinimumBreather=12, NoticeDistance=490;
    static constexpr float NoticeSeconds=2.4f, TurnSeconds=1.8f;
    static constexpr float TimeBudgetSeconds=60;
    enum class Phase { Waiting, Aftermath, Vista, Bridge, Notice, Turning, Combat, Returning, Complete, Escape };
    enum class BeamPhase { Inactive, Tracking, Locked, Flash };
    static constexpr float VeilBeamLockSeconds = .70f;
    static constexpr float VeilBeamFlashSeconds = .20f;
    static constexpr float VeilBeamRadius = .42f;
    struct BeamFrame {
        BeamPhase phase = BeamPhase::Inactive;
        Vector2 aim{};
        float distance = 0.0f;
        float secondsToFire = 0.0f;
        float flashAge = 0.0f;
        bool earlyLock = false;
    };
    struct BeamEvents {
        bool locked = false;
        bool fired = false;
        bool hit = false;
    };
    void Reset(){*this={};}
    static bool Enabled(const CourseAsset& course) {
        return std::any_of(course.events.begin(),course.events.end(),[](const auto& e){return e.id==Marker;}) &&
            std::any_of(course.waveDefinitions.begin(),course.waveDefinitions.end(),[](const auto& w){
                return w.editorGuid=="short-mothership" && w.enabled;
            });
    }
    void Notify(std::span<const EnemyCombatEvent> events) {
        for(const auto& e:events)if(e.kind==EnemyCombatEventKind::Defeated)
            for(size_t i=0;i<ids_.size();++i)if(ids_[i]!=0 && ids_[i]==e.actorId)defeated_[i]=true;
    }
    void Update(float dt,float distance,bool bossDefeated,CourseSpawnRuntime& runtime,
            CourseEventDispatcher& dispatcher,const TerrainEditLayer* preview=nullptr,float realDt=-1,float entrySpeed=36,const RailPursuerPerception* perception=nullptr) {
        if(!std::isfinite(dt) || dt<=0 || !std::isfinite(distance))return;
        dt=(std::min)(dt,.25f);
        if(phase_==Phase::Waiting) {
            if(!bossDefeated)return;
            anchor_=distance;phase_=Phase::Aftermath;BuildScenery();BuildValley(preview);
        }
        age_+=dt;phaseAge_+=dt;
        const float d=distance-anchor_;relativeDistance_=d;
        if(phase_==Phase::Aftermath && age_>=3.5f)Enter(Phase::Vista);
        if(phase_==Phase::Vista && d>=BridgeStart)Enter(Phase::Bridge);
        if(phase_==Phase::Bridge && d>=NoticeDistance && age_>=MinimumBreather)Enter(Phase::Notice);
        if(phase_==Phase::Notice && phaseAge_>=NoticeSeconds)Enter(Phase::Turning);
        if(phase_==Phase::Turning && phaseAge_>=TurnSeconds)Enter(Phase::Combat);
        if(phase_==Phase::Turning || phase_==Phase::Combat) {
            // Spawn outside the moving view. The nearer pursuer enters on
            // the side the camera is turning toward, then its wingman enters
            // from the opposite side as the view settles.
            struct Entrance { float time, gap, lateral, height; };
            constexpr std::array<Entrance,2> entrances{{
                {.78f,46.0f,17.0f,8.0f},
                {1.24f,76.0f,-29.0f,15.0f},
            }};
            for(size_t i=0;i<ids_.size();++i) {
                const auto& entrance=entrances[i];
                if(ids_[i]!=0 || (phase_==Phase::Turning && phaseAge_<entrance.time))continue;
                const size_t before=runtime.Enemies().size();
                if(dispatcher.SpawnAuthoredEnemy(i==0?"rear_fang":"rear_veil","post-boss-rear-pair",
                        distance-entrance.gap,entrance.lateral,entrance.height,runtime) &&
                        runtime.Enemies().size()>before) {ids_[i]=runtime.Enemies().back().actorId;initialHull_[i]=runtime.Enemies().back().desc.hitPoints;}
            }
        }
        for(const auto& actor:runtime.Enemies())for(size_t i=0;i<ids_.size();++i)
            if(ids_[i] && actor.actorId==ids_[i] && actor.desc.hitPoints<=0)defeated_[i]=true;
        for(const auto& actor:runtime.Enemies())for(size_t i=0;i<ids_.size();++i)if(ids_[i] && actor.actorId==ids_[i]) {
            const float damage=initialHull_[i]-(std::max)(0.0f,actor.desc.hitPoints);
            damageTaken_[i]=(std::max)(damageTaken_[i],damage);
            if(i==1)veilSource_=actor.weaponMount.ready?actor.weaponMount.muzzleRail[0]:Vector3{actor.desc.lateralOffset,actor.desc.verticalOffset,actor.desc.spawnDistance+actor.desc.distanceOffset};
        }
        if(phase_==Phase::Combat) {
            runtime.UpdateRearArmor(dt);
            combatSeconds_+=dt;
            const bool earned=(combatSeconds_>=12 && DamageDealt()>=64) || combatSeconds_>=26 || (defeated_[0] && defeated_[1]);
            UpdatePursuers(dt,distance,entrySpeed,earned,runtime,perception);
            const bool safe=coordination_.SafeToExit();
            if(earned && safe) {
                Enter(Phase::Escape);escape_.Begin(distance,veilSource_,anchor_+BridgeStart-10,entrySpeed);veilBeam_={};runtime.ClearProjectiles();
            }
        }
        if(phase_==Phase::Escape) {
            escape_.Tick(realDt>=0?realDt:dt,distance,runtime);
            BuildScenery();
            if(escape_.Complete()) {
                runtime.RetireEnemies(ids_,false);runtime.ClearProjectiles();Enter(Phase::Complete);
            }
        }
        ApplyAttackGates(runtime);
    }
    void AdvancePanelBoost(float dt) {
        if(phase_!=Phase::Combat) panelBoostRemaining_=0;
        else panelBoostRemaining_=(std::max)(0.0f,panelBoostRemaining_-dt);
        if(panelBoostRemaining_==0) panelBoostStacks_=0;
    }
    bool OnPanelBreaks(std::span<const DamageResult> hits) {
        if(phase_!=Phase::Combat) return false;
        bool broke=false;
        for(const auto& hit:hits) {
            if(!hit.rearPanelBroken) continue;
            panelBoostStacks_=(std::min)(3u,panelBoostStacks_+1);
            panelBoostRemaining_=(std::min)(2.25f,(std::max)(1.25f,panelBoostRemaining_+.45f));
            broke=true;
        }
        return broke;
    }
    float PanelBoostMultiplier() const {
        return panelBoostStacks_==0?1.0f:1.85f+.075f*float(panelBoostStacks_-1);
    }
    float PanelBoostRemaining() const { return panelBoostRemaining_; }
    void ApplyAttackGates(CourseSpawnRuntime& runtime) const {
        if(ids_[0])runtime.SetEnemyEncounterPacing(ids_[0],true,phase_==Phase::Combat && coordination_.FangCanFire());
        // Veil's encounter beam replaces its ordinary projectile attack.
        if(ids_[1])runtime.SetEnemyEncounterPacing(ids_[1],true,false);
    }
    const RailPursuerCoordination& Coordination()const{return coordination_;}
    // Legacy capture counter: completed attack pairs, no alternating cycle.
    unsigned CompletedCycles() const { return coordination_.AttackCount()/2; }
    uint32_t FangFollowupTarget() const {
        return phase_==Phase::Combat && coordination_.FangFollowup() && !defeated_[0]?ids_[0]:0;
    }
    bool VeilSensorExposed() const {return phase_==Phase::Combat && !defeated_[1] && coordination_.VeilPreparing();}
    bool VeilInterrupted() const {return phase_==Phase::Combat && coordination_.VeilInterrupted();}
    bool VeilCanBeInterrupted() const {
        return VeilSensorExposed() && ids_[1] && veilBeam_.phase!=BeamPhase::Flash;
    }
    bool OnVeilLockContact(std::span<const DamageResult> hits) {
        if(!VeilCanBeInterrupted())return false;
        const bool interrupted=std::any_of(hits.begin(),hits.end(),[&](const auto& hit) {
            return hit.targetActorId==ids_[1] && hit.damageApplied;
        });
        if(interrupted){coordination_.CancelVeil();veilBeam_={};}
        return interrupted;
    }
    const BeamFrame& VeilBeam() const { return veilBeam_; }
    unsigned VeilBeamShots() const { return veilBeamShots_; }
    Vector2 VeilLastBeamAim() const { return veilLastBeamAim_; }
    BeamEvents AdvanceVeilBeam(float dt,const CourseSpawnRuntime& runtime,
        const PlayerHitboxRuntimeState& player,const RailPursuerPerception* currentSight=nullptr) {
        if(dt<=0)return {};
        if(phase_!=Phase::Combat || coordination_.Reservation()!=RailPursuerCoordination::Owner::Veil) {veilBeam_={};return {};}
        if(veilBeam_.phase==BeamPhase::Flash) {veilBeam_.flashAge+=dt;veilBeam_.distance=player.distance;return {};}
        if(currentSight && (!currentSight->lineClear[1] || !currentSight->readable[1])) {
            coordination_.CancelVeil();veilBeam_={};return {};
        }
        const auto actor=std::find_if(runtime.Enemies().begin(),runtime.Enemies().end(),
            [&](const auto& enemy){return enemy.actorId==ids_[1] && enemy.desc.hitPoints>0;});
        if(!coordination_.VeilCanFire() || actor==runtime.Enemies().end() || !actor->weaponMount.ready) {veilBeam_={};return {};}
        BeamEvents events{};
        if(veilBeam_.phase!=BeamPhase::Locked) {
            veilBeam_.aim={player.lateralOffset,player.verticalOffset};
            veilBeam_.earlyLock=actor->veilSensorHitPoints<=0;
            if(veilBeam_.earlyLock && actor->veilDescentAimCaptured)veilBeam_.aim=actor->veilDescentAim;
            events.locked=veilBeam_.earlyLock || coordination_.LeaseAge()>=VeilDescentSeconds;
            veilBeam_.phase=events.locked?BeamPhase::Locked:BeamPhase::Tracking;
        }
        veilBeam_.distance=player.distance;
        veilBeam_.secondsToFire=(std::max)(0.0f,VeilDescentSeconds+VeilBeamLockSeconds-coordination_.LeaseAge());
        if(coordination_.LeaseAge()>=VeilDescentSeconds+VeilBeamLockSeconds) {
            veilBeam_.phase=BeamPhase::Flash;veilBeam_.flashAge=0;
            ++veilBeamShots_;veilLastBeamAim_=veilBeam_.aim;events.fired=true;
            coordination_.NotifyVeilFired();
            events.hit=std::hypot(player.lateralOffset-veilBeam_.aim.x,player.verticalOffset-veilBeam_.aim.y)<=VeilBeamRadius+player.hurtRadius;
        }
        return events;
    }
    bool InPursuitHandoff() const {
        return phase_==Phase::Combat && coordination_.AircraftState(0).flight==RailPursuerCoordination::Flight::Recover &&
            coordination_.VeilPreparing() && !defeated_[0] && !defeated_[1];
    }
    RailBridgeEscape& Escape(){return escape_;}
    const RailBridgeEscape& Escape()const{return escape_;}
    bool EscapeActive()const{return phase_==Phase::Escape;}
    float DamageDealt()const{return damageTaken_[0]+damageTaken_[1];}
    float SurvivalSeconds()const{return combatSeconds_;}
    float CameraWeight() const {
        if(EscapeActive())return escape_.RearWeight();
        if(phase_==Phase::Turning)return Smooth(phaseAge_/TurnSeconds);
        if(phase_==Phase::Combat)return 1;
        if(phase_==Phase::Returning)return 1-Smooth((phaseAge_-.9f)/TurnSeconds);
        return 0;
    }
    float ViewWeight() const {
        if(EscapeActive())return 1;
        if(phase_==Phase::Vista)return Smooth((age_-3.5f)/1.2f)*(1-Smooth((relativeDistance_-270)/70));
        return CameraWeight();
    }
    CourseCameraViewDefinition View() const {
        if(EscapeActive())return escape_.View();
        if(phase_!=Phase::Vista)return RearView();
        auto view=CourseCameraViewDefinition::Preset(CourseCameraViewMode::Custom);
        view.positionOffset={-20,15,-28};view.yaw=.09f;view.pitch=-.065f;
        view.fovY=1.012291f;view.hideVehicle=false;
        return view;
    }
    const TerrainEditLayer& TerrainPreview(const TerrainEditLayer& fallback)const {
        return Started()?terrainPreview_:fallback;
    }
    static CourseCameraViewDefinition RearView() {
        auto view=CourseCameraViewDefinition::Preset(CourseCameraViewMode::Rear);
        view.positionOffset={0,7.8f,17};view.pitch=-.08f;
        view.fovY=1.012291f;view.hideVehicle=false;
        return view;
    }
    bool Started()const{return phase_!=Phase::Waiting;}
    bool Complete()const{return phase_==Phase::Complete;}
    bool CheckpointPending()const{return (phase_==Phase::Bridge && !checkpointCaptured_) || (EscapeActive() && escape_.CheckpointPending());}
    void MarkCheckpointCaptured(bool captured){if(EscapeActive())escape_.MarkCheckpoint(captured);else checkpointCaptured_=captured;}
    Phase CurrentPhase()const{return phase_;}
    float Anchor()const{return anchor_;}
    float Age()const{return age_;}
    float PhaseAge()const{return phaseAge_;}
    const std::array<uint32_t,2>& ActorIds()const{return ids_;}
    std::span<const CourseTerrainPlacement> Scenery()const{return scenery_;}
    const char* Label()const {
        switch(phase_) {
        case Phase::Aftermath:return "CARRIER WITHDRAWAL";
        case Phase::Vista:return "WHITE BRIDGE VISTA";
        case Phase::Bridge:return "WHITE BRIDGE";
        case Phase::Notice:return "PURSUERS APPROACHING";
        case Phase::Turning:case Phase::Combat:return "REAR INTERCEPT";
        case Phase::Escape:return "BRIDGE ESCAPE";
        case Phase::Returning:case Phase::Complete:return "PURSUERS EVADED";
        default:return "CARRIER ASSAULT";
        }
    }
private:
    static constexpr float VeilDescentSeconds=1.10f;
    void UpdatePursuers(float dt,float distance,float speed,bool finish,CourseSpawnRuntime& runtime,const RailPursuerPerception* perception) {
        RailPursuerCoordination::Input input;input.speed=speed;input.finishRequested=finish;
        input.acceleration=perception?perception->acceleration:(hasSpeed_?(speed-lastSpeed_)/dt:0);
        lastSpeed_=speed;hasSpeed_=true;
        for(const auto& actor:runtime.Enemies())for(size_t i=0;i<ids_.size();++i)if(ids_[i] && actor.actorId==ids_[i]) {
            auto& a=input.aircraft[i];a.alive=actor.desc.hitPoints>0;
            // Environment readiness includes the encounter permission itself;
            // using it here would make a closed reservation unable to open.
            a.ready=actor.weaponMount.ready && actor.combatState.canBeTargeted && !actor.desc.suppressFire &&
                !(actor.entranceExitState.initialized && actor.entranceExitState.attackSuppressed);
            a.gap=distance-actor.desc.spawnDistance-actor.desc.distanceOffset;
            a.lateral=actor.desc.lateralOffset;a.height=actor.desc.verticalOffset;a.shots=actor.fireSequence;
            if(perception){a.lineClear=perception->lineClear[i];a.readable=perception->readable[i];}
        }
        for(const auto& bullet:runtime.Bullets())if(bullet.ownerActorId==ids_[0] && !bullet.hitConsumed && bullet.age<bullet.lifetime &&
            (bullet.active || bullet.launchDelayRemaining>0))input.fangThreat=true;
        input.veilThreat=veilBeam_.phase==BeamPhase::Flash && veilBeam_.flashAge<VeilBeamFlashSeconds;
        const auto previousGrant=coordination_.GrantSerial();
        coordination_.Tick(dt,input);
        if(coordination_.GrantSerial()!=previousGrant && coordination_.FangCanFire())
            runtime.InvalidateEnemyAttackWarning(ids_[0]);
        for(size_t i=0;i<ids_.size();++i) {
            const auto& n=coordination_.AircraftState(i);
            runtime.SetRearPursuerFormation(ids_[i],n.formation.x,n.formation.y,n.formation.z,n.bank,
                i==0?FangFollowupTarget()!=0:VeilSensorExposed());
        }
    }
    RailPursuerCoordination coordination_{};
    float lastSpeed_=36.45f;bool hasSpeed_=false;
    float panelBoostRemaining_=0;unsigned panelBoostStacks_=0;
    BeamFrame veilBeam_{};unsigned veilBeamShots_=0;Vector2 veilLastBeamAim_{};
    static float Smooth(float x){x=(std::clamp)(x,0.0f,1.0f);return x*x*(3-2*x);}
    void Enter(Phase phase){phase_=phase;phaseAge_=0;}
    void BuildValley(const TerrainEditLayer* preview) {
        terrainPreview_=preview?*preview:TerrainEditLayer{};
        std::vector<TerrainBrushStamp> stamps;
        for(int i=0;i<=13;++i)for(int side=0;side<3;++side) {
            TerrainBrushStamp p;p.strokeGuid="postboss-prototype-valley";
            p.stampGuid=p.strokeGuid+"-"+std::to_string(i)+"-"+std::to_string(side);
            p.distance=anchor_+600+float(i)*160;p.angle=side==0?0:side==1?3.14159265f:4.71238898f;
            // Keep adjacent rings gentle: a deep, angularly overlapping carve
            // can expose reverse-facing terrain at the bridge's rear camera.
            p.radius=240;p.surfaceRadius=180;p.strength=side==2?12.0f:24.0f;p.hardness=.35f;
            stamps.push_back(std::move(p));
        }
        // A derived preview layer keeps source course edits untouched and is
        // copied atomically with the sequence at checkpoint/retry boundaries.
        terrainPreview_.ApplyStroke(stamps);
    }
    void BuildScenery() {
        scenery_.clear();
        for(int i=0;i<=int(BridgeLength/20);++i) {
            CourseTerrainPlacement p;p.id="postboss-white-bridge-"+std::to_string(i);
            p.meshId="course_blockout_bridge";p.distance=anchor_+BridgeStart+float(i)*20;
            if(escape_.GapOpen() && p.distance+10>escape_.Arc().start && p.distance-10<escape_.Arc().Landing())continue;
            p.cullAheadDistance=1050;p.cullBehindDistance=340;p.renderPriority=14;
            scenery_.push_back(std::move(p));
        }
        RailBridgeStructure::Append(escape_,scenery_);
        // Distant shoulders frame the bridge; the runway's existing wide
        // canyon cross-section provides physical clearance below the deck.
        for(int side:{-1,1})for(int i=0;i<3;++i) {
            CourseTerrainPlacement p;p.id="postboss-valley-"+std::to_string(side)+"-"+std::to_string(i);
            p.meshId="root_spire_column";p.distance=anchor_+180+float(i)*420;
            p.lateralOffset=float(side)*(160+float(i)*35);p.verticalOffset=-20;
            p.scale={45,90,55};p.cullAheadDistance=1400;p.cullBehindDistance=500;
            scenery_.push_back(std::move(p));
        }
    }
    RailBridgeEscape escape_{};
    std::array<float,2> damageTaken_{};
    std::array<float,2> initialHull_{};
    float combatSeconds_=0;
    Vector3 veilSource_{};
    Phase phase_=Phase::Waiting;
    float anchor_=0,age_=0,phaseAge_=0,relativeDistance_=0;
    std::array<uint32_t,2> ids_{};
    std::array<bool,2> defeated_{};
    bool checkpointCaptured_=false;
    std::vector<CourseTerrainPlacement> scenery_;
    TerrainEditLayer terrainPreview_;
};
