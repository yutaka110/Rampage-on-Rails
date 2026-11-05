#pragma once
#include "CourseAsset.h"
#include "CourseEventDispatcher.h"
#include "CourseSpawnRuntime.h"
#include "PlayerHitboxSystem.h"
#include "RailRearPursuitRules.h"
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
    static constexpr float BridgeStart=360, BridgeLength=1800;
    static constexpr float MinimumBreather=12, NoticeDistance=490;
    static constexpr float NoticeSeconds=2.4f, TurnSeconds=1.8f;
    static constexpr float TimeBudgetSeconds=40;
    enum class Phase { Waiting, Aftermath, Vista, Bridge, Notice, Turning, Combat, Returning, Complete };
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
            CourseEventDispatcher& dispatcher,const TerrainEditLayer* preview=nullptr) {
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
                        runtime.Enemies().size()>before)ids_[i]=runtime.Enemies().back().actorId;
            }
        }
        for(const auto& actor:runtime.Enemies())for(size_t i=0;i<ids_.size();++i)
            if(ids_[i] && actor.actorId==ids_[i] && actor.desc.hitPoints<=0)defeated_[i]=true;
        if(phase_==Phase::Combat && defeated_[0] && defeated_[1])Enter(Phase::Returning);
        if(phase_==Phase::Combat) {
            runtime.UpdateRearArmor(dt);
            UpdatePass(dt,runtime);
        }
        if(phase_==Phase::Returning && phaseAge_>=3.2f)Enter(Phase::Complete);
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
        if(ids_[0])runtime.SetEnemyEncounterPacing(ids_[0],true,phase_==Phase::Combat && pass_==Pass::FangFire);
        // Veil's encounter beam replaces its ordinary projectile attack.
        if(ids_[1])runtime.SetEnemyEncounterPacing(ids_[1],true,false);
    }
    unsigned CompletedCycles() const { return completedCycles_; }
    uint32_t FangFollowupTarget() const {
        return phase_==Phase::Combat && pass_==Pass::FangExit &&
            passAge_>=.68f && passAge_<1.38f && !defeated_[0] ? ids_[0] : 0;
    }
    bool VeilSensorExposed() const {
        return phase_==Phase::Combat && !defeated_[1] &&
            ((pass_==Pass::FangExit && passAge_>=VeilDescentCue) || pass_==Pass::VeilApproach);
    }
    bool VeilInterrupted() const { return phase_==Phase::Combat && veilInterrupted_; }
    bool VeilCanBeInterrupted() const {
        if(phase_!=Phase::Combat || veilInterrupted_ || defeated_[1] || !ids_[1] ||
            veilBeam_.phase==BeamPhase::Flash)return false;
        return (pass_==Pass::FangExit && passAge_>=VeilDescentCue) ||
            pass_==Pass::VeilApproach || pass_==Pass::VeilFire;
    }
    bool OnVeilLockContact(std::span<const DamageResult> hits) {
        if(!VeilCanBeInterrupted())return false;
        veilInterrupted_=std::any_of(hits.begin(),hits.end(),[&](const auto& hit) {
            return hit.targetActorId==ids_[1] && hit.damageApplied;
        });
        if(veilInterrupted_)veilBeam_={};
        return veilInterrupted_;
    }
    const BeamFrame& VeilBeam() const { return veilBeam_; }
    unsigned VeilBeamShots() const { return veilBeamShots_; }
    Vector2 VeilLastBeamAim() const { return veilLastBeamAim_; }
    BeamEvents AdvanceVeilBeam(float dt, const CourseSpawnRuntime& runtime,
        const PlayerHitboxRuntimeState& player) {
        if(dt<=0)return {};
        if(phase_==Phase::Combat && pass_==Pass::VeilFire && !defeated_[1] &&
            veilBeam_.phase==BeamPhase::Flash) {
            veilBeam_.flashAge+=dt;
            veilBeam_.distance=player.distance;
            return {};
        }
        if(!VeilCanBeInterrupted()) {
            veilBeam_={};
            return {};
        }
        const auto actor=std::find_if(runtime.Enemies().begin(),runtime.Enemies().end(),
            [&](const auto& enemy) { return enemy.actorId==ids_[1] && enemy.desc.hitPoints>0; });
        if(actor==runtime.Enemies().end() || !actor->weaponMount.ready) {
            veilBeam_={};
            return {};
        }
        BeamEvents events{};
        if(veilBeam_.phase!=BeamPhase::Locked) {
            veilBeam_.aim={player.lateralOffset,player.verticalOffset};
            veilBeam_.earlyLock=actor->veilSensorHitPoints<=0;
            if(veilBeam_.earlyLock && actor->veilDescentAimCaptured)
                veilBeam_.aim=actor->veilDescentAim;
            events.locked=veilBeam_.earlyLock || pass_==Pass::VeilFire;
            veilBeam_.phase=events.locked?BeamPhase::Locked:BeamPhase::Tracking;
        }
        veilBeam_.distance=player.distance;
        const float descentAge=pass_==Pass::FangExit?passAge_-VeilDescentCue:passAge_;
        veilBeam_.secondsToFire=pass_==Pass::VeilFire?
            (std::max)(0.0f,VeilBeamLockSeconds-passAge_):
            VeilBeamLockSeconds+(std::max)(0.0f,VeilDescentSeconds-descentAge);
        if(pass_==Pass::VeilFire && passAge_>=VeilBeamLockSeconds) {
            veilBeam_.phase=BeamPhase::Flash;
            ++veilBeamShots_;
            veilLastBeamAim_=veilBeam_.aim;
            events.fired=true;
            events.hit=std::hypot(player.lateralOffset-veilBeam_.aim.x,
                player.verticalOffset-veilBeam_.aim.y)<=VeilBeamRadius+player.hurtRadius;
        }
        return events;
    }
    bool InPursuitHandoff() const {
        return phase_==Phase::Combat && pass_==Pass::FangExit &&
            passAge_>=.98f && !defeated_[0] && !defeated_[1];
    }
    float CameraWeight() const {
        if(phase_==Phase::Turning)return Smooth(phaseAge_/TurnSeconds);
        if(phase_==Phase::Combat)return 1;
        if(phase_==Phase::Returning)return 1-Smooth((phaseAge_-.9f)/TurnSeconds);
        return 0;
    }
    float ViewWeight() const {
        if(phase_==Phase::Vista)return Smooth((age_-3.5f)/1.2f)*(1-Smooth((relativeDistance_-270)/70));
        return CameraWeight();
    }
    CourseCameraViewDefinition View() const {
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
    bool CheckpointPending()const{return phase_==Phase::Bridge && !checkpointCaptured_;}
    void MarkCheckpointCaptured(bool captured){checkpointCaptured_=captured;}
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
        case Phase::Returning:case Phase::Complete:return "PURSUIT CLEARED";
        default:return "CARRIER ASSAULT";
        }
    }
private:
    enum class Pass { Read, FangApproach, FangFire, FangExit, VeilApproach, VeilFire, Reset };
    static constexpr float FangExitSeconds=1.55f;
    static constexpr float VeilDescentCue=.62f;
    static constexpr float VeilDescentSeconds=1.10f;
    void UpdatePass(float dt,CourseSpawnRuntime& runtime) {
        uint64_t fangShots=0;
        for(const auto& actor:runtime.Enemies())
            if(ids_[0] && ids_[0]==actor.actorId)fangShots=actor.fireSequence;

        passAge_+=dt;
        const auto enter=[&](Pass pass) {
            pass_=pass;
            passAge_=0;
            if(pass==Pass::FangFire)shotAtEntry_=fangShots;
        };
        switch(pass_) {
        case Pass::Read:
            if(passAge_>=1.0f)enter(defeated_[0]?Pass::VeilApproach:Pass::FangApproach);
            break;
        case Pass::FangApproach:
            if(defeated_[0] || passAge_>=1.15f)enter(defeated_[0]?Pass::VeilApproach:Pass::FangFire);
            break;
        case Pass::FangFire:
            if(fangShots!=shotAtEntry_ || defeated_[0] || passAge_>=6.0f)enter(Pass::FangExit);
            break;
        case Pass::FangExit:
            if(passAge_>=FangExitSeconds) {
                // Carry the descent clock across the handoff so Veil never
                // jumps back up when Fang finishes banking away.
                const float descentAge=passAge_-VeilDescentCue;
                enter(defeated_[1]?Pass::Reset:Pass::VeilApproach);
                if(pass_==Pass::VeilApproach)passAge_=descentAge;
            }
            break;
        case Pass::VeilApproach:
            if(defeated_[1] || passAge_>=VeilDescentSeconds)
                enter(defeated_[1] || veilInterrupted_?Pass::Reset:Pass::VeilFire);
            break;
        case Pass::VeilFire:
            if(defeated_[1] || veilInterrupted_ || passAge_>=6.0f ||
                (veilBeam_.phase==BeamPhase::Flash && veilBeam_.flashAge>=VeilBeamFlashSeconds))enter(Pass::Reset);
            break;
        case Pass::Reset:
            if(passAge_>=1.25f){++completedCycles_;veilInterrupted_=false;enter(Pass::Read);}
            break;
        }
        float fang=0,veil=0,fangBank=0,veilBank=0;
        if(pass_==Pass::FangApproach) {
            fang=Smooth(passAge_/1.15f);
            fangBank=.48f*std::sin(fang*3.14159265f);
        } else if(pass_==Pass::FangFire)fang=1;
        else if(pass_==Pass::FangExit) {
            const float exitAge=passAge_-.42f;
            fang=1-Smooth(exitAge/(FangExitSeconds-.42f));
            fangBank=-.52f*Smooth(exitAge/.26f)*(1-Smooth((exitAge-.75f)/.38f));
        }
        if(pass_==Pass::VeilApproach || (pass_==Pass::FangExit && !defeated_[1])) {
            const float descentAge=pass_==Pass::FangExit?passAge_-VeilDescentCue:passAge_;
            veil=Smooth(descentAge/VeilDescentSeconds);
            veilBank=-.30f*std::sin(veil*3.14159265f);
        } else if(pass_==Pass::VeilFire)veil=1;
        else if(pass_==Pass::Reset && !defeated_[1])veil=1-Smooth(passAge_/1.25f);
        runtime.SetRearPursuerFormation(ids_[0],std::lerp(54.0f,26.0f,fang),
            std::lerp(17.0f,5.0f,fang),std::lerp(8.0f,5.5f,fang),fangBank,FangFollowupTarget()!=0);
        runtime.SetRearPursuerFormation(ids_[1],std::lerp(66.0f,32.0f,veil),
            std::lerp(-29.0f,-9.0f,veil),std::lerp(15.0f,8.0f,veil),veilBank,VeilSensorExposed());
    }
    Pass pass_=Pass::Read;
    float passAge_=0;
    uint64_t shotAtEntry_=0;
    unsigned completedCycles_=0;
    float panelBoostRemaining_=0;
    unsigned panelBoostStacks_=0;
    bool veilInterrupted_=false;
    BeamFrame veilBeam_{};
    unsigned veilBeamShots_=0;
    Vector2 veilLastBeamAim_{};
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
            p.cullAheadDistance=1050;p.cullBehindDistance=340;p.renderPriority=14;
            scenery_.push_back(std::move(p));
        }
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
    Phase phase_=Phase::Waiting;
    float anchor_=0,age_=0,phaseAge_=0,relativeDistance_=0;
    std::array<uint32_t,2> ids_{};
    std::array<bool,2> defeated_{};
    bool checkpointCaptured_=false;
    std::vector<CourseTerrainPlacement> scenery_;
    TerrainEditLayer terrainPreview_;
};
