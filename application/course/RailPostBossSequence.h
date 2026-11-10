#pragma once
#include "CourseAsset.h"
#include "CourseEventDispatcher.h"
#include "CourseSpawnRuntime.h"
#include "PlayerHitboxSystem.h"
#include "RailRearPursuitRules.h"
#include "RailRearDamageSmoke.h"
#include "RailBridgeEscape.h"
#include "RailBridgeStructure.h"
#include "RailPursuerCoordination.h"
#include "RailVeilRearBeam.h"
#include "RailVeilModel.h"
#include "RailVeilLightning.h"
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
    static constexpr float BridgeStart=112, BridgeLength=4400;
    static constexpr float AftermathSeconds=2.35f, MinimumVistaSeconds=.55f, MinimumBreather=4.35f;
    static constexpr float NoticeSeconds=1.8f, TurnSeconds=1.8f;
    static constexpr float SurvivalGoalSeconds=65, MinimumDamageExitSeconds=28;
    static constexpr float DamageGoal=2852;
    static constexpr float TimeBudgetSeconds=99; // Preserve the original margin past the survival goal.
    enum class Phase { Waiting, Aftermath, Vista, Bridge, Notice, Turning, Combat, Returning, Complete, Escape };
    using BeamPhase = RailVeilRearBeam::Phase;
    using BeamFrame = RailVeilRearBeam::Frame;
    using BeamEvents = RailVeilRearBeam::Events;
    void Reset(){*this={};}
    static bool Enabled(const CourseAsset& course) {
        return std::any_of(course.events.begin(),course.events.end(),[](const auto& e){return e.id==Marker;}) &&
            std::any_of(course.waveDefinitions.begin(),course.waveDefinitions.end(),[](const auto& w){
                return w.editorGuid==MinibossWaveId && w.enabled;
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
        const float d=distance-anchor_;
        if(phase_==Phase::Aftermath && age_>=AftermathSeconds)Enter(Phase::Vista);
        if(phase_==Phase::Vista && d>=BridgeStart && phaseAge_>=MinimumVistaSeconds)Enter(Phase::Bridge);
        else if(phase_==Phase::Bridge && age_>=MinimumBreather)Enter(Phase::Notice);
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
            const float remaining=(std::max)(0.0f,actor.desc.hitPoints);
            damageTaken_[i]=(std::max)(damageTaken_[i],initialHull_[i]-remaining);
            if(remaining>0 && initialHull_[i]>0)
                hullSmoke_[i].Observe(actor.age,remaining/initialHull_[i]);
            if(i==1)veilSource_=actor.weaponMount.ready?actor.weaponMount.muzzleRail[0]:Vector3{actor.desc.lateralOffset,actor.desc.verticalOffset,actor.desc.spawnDistance+actor.desc.distanceOffset};
        }
        if(phase_==Phase::Combat) {
            runtime.UpdateRearArmor(dt);
            combatSeconds_+=dt;
            const bool earned=BreakthroughEarned();
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
    const RailPursuerCoordination::Crossfire& FangCrossfire()const{return coordination_.FangCrossfire();}
    bool InterruptFangCrossfire(uint32_t actorId,int panel) {
        return phase_==Phase::Combat && actorId==ids_[0] && coordination_.InterruptCrossfire(panel);
    }
    void AdvanceFangCrossfire(CourseSpawnRuntime& runtime,float playerDistance,float playerSpeed,bool canLaunch) {
        if(phase_!=Phase::Combat || !coordination_.CrossfireRoundDue())return;
        const auto& actors=runtime.Enemies();
        const auto fang=std::find_if(actors.begin(),actors.end(),[&](const auto& actor) {
            return actor.actorId==ids_[0] && actor.desc.hitPoints>0 && actor.weaponMount.ready;
        });
        if(!canLaunch || fang==actors.end()) {coordination_.StopCrossfire();return;}
        const auto& attack=FangCrossfire();
        const auto& muzzle=fang->weaponMount.muzzleRail[attack.rounds%2==1 && fang->fangCannonHitPoints>0 ? 1 : 0];
        const auto& style=fang->desc.projectileDefinition;
        const float flight=(std::max)(.10f,(playerDistance-muzzle.z)/RailPursuerCoordination::CrossfireRelativeSpeed);
        const float alignTime=flight*.48f;
        CourseBulletActor round;
        round.initialized=true;round.ownerActorId=ids_[0];
        round.definitionId="enemy_fang_chase";round.sourceRole=kFangCrossfireRole;
        round.spawnDistance=muzzle.z;round.lateralOffset=round.previousLateralOffset=muzzle.x;
        round.verticalOffset=round.previousVerticalOffset=muzzle.y;
        round.forwardSpeed=playerSpeed+RailPursuerCoordination::CrossfireRelativeSpeed;
        const float lane=attack.lane;
        round.lateralSpeed=(lane-muzzle.x)/alignTime;
        round.verticalSpeed=(attack.height-muzzle.y)/alignTime;
        round.maximumSpeed=std::hypot(round.forwardSpeed,round.lateralSpeed,round.verticalSpeed);
        round.radius=.29f;round.lifetime=flight+.21f;round.damage=style.damage;
        round.color=style.color;round.trailEffectId=style.trailEffectId;round.impactEffectId=style.impactEffectId;
        round.defenseResponses=style.defenseResponses;
        round.shootDownHitPoints=round.shootDownMaximumHitPoints=style.shootDownHitPoints;
        round.shootDownRadiusScale=style.shootDownRadiusScale;round.shootDownEnabled=style.shootDownHitPoints>0;
        round.lockedTargetDistance=muzzle.z+round.forwardSpeed*alignTime;
        round.lockedTargetLateralOffset=lane;round.lockedTargetVerticalOffset=attack.height;
        if(runtime.SpawnProjectile(std::move(round)))coordination_.CrossfireRoundEmitted(fang->fangCannonHitPoints>0?3u:2u);
        else coordination_.StopCrossfire();
    }
    // Legacy capture counter: completed attack pairs, no alternating cycle.
    unsigned CompletedCycles() const { return coordination_.AttackCount()/2; }
    uint32_t FangFollowupTarget() const {
        return phase_==Phase::Combat && coordination_.FangFollowup() && !defeated_[0]?ids_[0]:0;
    }
    bool VeilSensorExposed() const {return phase_==Phase::Combat && !defeated_[1] &&
        !veilLightning_.BeforeFirstStrike() && coordination_.VeilPreparing();}
    bool VeilInterrupted() const {return phase_==Phase::Combat && coordination_.VeilInterrupted();}
    bool VeilCanBeInterrupted() const {
        return VeilSensorExposed() && ids_[1] && veilBeam_.phase!=BeamPhase::Flash;
    }
    bool OnVeilLockContact(std::span<const DamageResult> hits) {
        if(!VeilCanBeInterrupted())return false;
        const bool interrupted=std::any_of(hits.begin(),hits.end(),[&](const auto& hit) {
            return hit.targetActorId==ids_[1] && hit.damageApplied && hit.weakPointHit;
        });
        if(interrupted) {
            coordination_.CancelVeil();veilBeam_={};
        }
        return interrupted;
    }
    const RailVeilLightning::Chain& VeilLightning()const{return veilLightning_;}
    uint32_t VeilLightningFollowupTarget()const{
        return phase_==Phase::Combat && !defeated_[1] && veilLightning_.CounterOpen()?ids_[1]:0;
    }
    const BeamFrame& VeilBeam() const { return veilBeam_; }
    unsigned VeilBeamShots() const { return veilBeamShots_; }
    BeamEvents AdvanceVeilBeam(float dt,const CourseSpawnRuntime& runtime,const RailPath& rail,
        const PlayerHitboxRuntimeState& player,const RailVeilAttackFrame& attack,bool lineClear=true) {
        if(dt<=0 || phase_!=Phase::Combat || veilBeam_.phase==BeamPhase::Inactive)return {};
        BeamEvents events{};
        if(veilBeam_.phase==BeamPhase::Flash) {
            events.hit=veilBeam_.Advance(dt,player);
            if(veilBeam_.Fade()==0)veilBeam_={};
            return events;
        }
        if(!coordination_.VeilCanFire()) {veilBeam_={};return {};}
        if(veilBeam_.firstChargeFrame) {
            veilBeam_.firstChargeFrame=false;
            return {};
        }
        const float remaining=veilBeam_.secondsToFire;
        veilBeam_.secondsToFire=(std::max)(0.0f,remaining-dt);
        if(veilBeam_.phase==BeamPhase::Tracking && veilBeam_.secondsToFire<=RailVeilRearBeam::LockSeconds+.0001f) {
            veilBeam_.phase=BeamPhase::Locked;events.locked=true;
        }
        if(veilBeam_.secondsToFire>.0001f)return events;
        const auto actor=std::find_if(runtime.Enemies().begin(),runtime.Enemies().end(),
            [&](const auto& enemy){return enemy.actorId==ids_[1] && enemy.desc.hitPoints>0 && enemy.weaponMount.ready;});
        if(actor==runtime.Enemies().end() || !lineClear) {coordination_.CancelVeil();veilBeam_={};return {};}
        const auto source=VeilMuzzleRail(*actor,rail,attack);
        if(source.z>=veilBeam_.end) {coordination_.CancelVeil();veilBeam_={};return {};}
        veilBeam_.Launch(source);
        ++veilBeamShots_;events.fired=true;
        coordination_.NotifyVeilFired();
        const float chargeStep=(std::min)(remaining,dt);
        events.hit=veilBeam_.Advance(dt-chargeStep,player,chargeStep/dt);
        return events;
    }
    static Vector3 VeilMuzzleRail(const CourseEnemyActor& actor,const RailPath& rail,
        const RailVeilAttackFrame& attack) {
        const float base=actor.desc.spawnDistance+actor.desc.distanceOffset;
        const auto sample=rail.Evaluate(base);
        const auto muzzle=RailVeilModel::MuzzleWorld(actor.weaponMount.dronePose,attack);
        const Vector3 offset{muzzle.x-sample.position.x,muzzle.y-sample.position.y,muzzle.z-sample.position.z};
        const auto project=[&](Vector3 axis){return offset.x*axis.x+offset.y*axis.y+offset.z*axis.z;};
        return {project(sample.right),project(sample.up),base+project(sample.tangent)};
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
    bool BreakthroughEarned()const {
        return combatSeconds_>=SurvivalGoalSeconds ||
            (combatSeconds_>=MinimumDamageExitSeconds && DamageDealt()>=DamageGoal) ||
            (defeated_[0] && defeated_[1]);
    }
    float HullFraction(size_t i)const {
        return initialHull_[i]>0 ? 1-damageTaken_[i]/initialHull_[i] : 1.0f;
    }
    const RailRearDamageSmoke::State& HullSmoke(size_t i)const{return hullSmoke_[i];}
    float CameraWeight() const {
        if(EscapeActive())return escape_.RearWeight();
        if(phase_==Phase::Turning)return Smooth(phaseAge_/TurnSeconds);
        if(phase_==Phase::Combat)return 1;
        if(phase_==Phase::Returning)return 1-Smooth((phaseAge_-.9f)/TurnSeconds);
        return 0;
    }
    float ViewWeight() const {
        if(EscapeActive())return 1;
        if(phase_==Phase::Vista)return Smooth(phaseAge_/.24f)*(1-Smooth((phaseAge_-.25f)/.28f));
        return CameraWeight();
    }
    CourseCameraViewDefinition View() const {
        if(EscapeActive())return escape_.View();
        if(phase_!=Phase::Vista) {
            auto view=RearView();
            if(phase_==Phase::Combat && veilLightning_.Visible()) {
                // Keep the rear view and move the boom ahead of the near
                // strikes so their bases can enter the foreground.
                const float boom=Smooth(veilLightning_.age/.52f)*
                    (1-Smooth((veilLightning_.age-RailVeilLightning::Clearance)/.78f));
                view.positionOffset.z+=16.0f*boom;
                view.positionOffset.y+=2.0f*boom;
                view.pitch-=.06f*boom;
            }
            return view;
        }
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
    const char* PendingCheckpointId()const {
        if(phase_==Phase::Bridge && !checkpointCaptured_)return "WHITE BRIDGE";
        if(phase_==Phase::Notice && !pursuitCheckpointCaptured_)return "REAR PURSUIT";
        if(EscapeActive() && escape_.CheckpointPending())return "BRIDGE ESCAPE";
        return nullptr;
    }
    bool CheckpointPending()const{return PendingCheckpointId()!=nullptr;}
    void MarkCheckpointCaptured(bool captured){
        if(EscapeActive())escape_.MarkCheckpoint(captured);
        else if(phase_==Phase::Notice)pursuitCheckpointCaptured_=captured;
        else if(phase_==Phase::Bridge)checkpointCaptured_=captured;
    }
    static constexpr const char* MinibossCheckpointId="CARRIER APPROACH";
    static constexpr const char* MinibossWaveId="short-mothership";
    static constexpr float MinibossApproachDistance=65;

    Phase CurrentPhase()const{return phase_;}
    float Anchor()const{return anchor_;}
    float Age()const{return age_;}
    float PhaseAge()const{return phaseAge_;}
    const std::array<uint32_t,2>& ActorIds()const{return ids_;}
    bool VeilDefeated() const {return defeated_[1];}
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
    void UpdatePursuers(float dt,float distance,float speed,bool finish,CourseSpawnRuntime& runtime,const RailPursuerPerception* perception) {
        veilLightning_.Advance(dt,distance);
        RailPursuerCoordination::Input input;input.speed=speed;input.finishRequested=finish;
        input.acceleration=perception?perception->acceleration:(hasSpeed_?(speed-lastSpeed_)/dt:0);
        if(perception) {
            input.playerLane=perception->playerLane;input.playerHeight=perception->playerHeight;
            input.fangNearPanel=perception->fangNearPanel;
        }
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
        input.veilThreat=veilBeam_.phase==BeamPhase::Flash || veilLightning_.Threat();
        const auto previousFang=coordination_.AircraftState(0).grants;
        const auto previousVeil=coordination_.AircraftState(1).grants;
        coordination_.Tick(dt,input);
        if(!coordination_.AircraftState(1).reserved && veilLightning_.BeforeFirstStrike())
            veilLightning_={};
        if(veilLightning_.flashNow&1) {
            if(coordination_.VeilCanFire())coordination_.NotifyVeilFired();
            else veilLightning_={};
        }
        if(coordination_.AircraftState(1).grants!=previousVeil) {
            if(coordination_.AircraftState(1).attacks%2==1) {
                veilLightning_.Begin(distance,speed);
            } else {
                const float height=perception?perception->playerHeight:1.4f;
                veilBeam_.Begin(distance,height);
            }
        }
        if(coordination_.AircraftState(0).grants!=previousFang && coordination_.FangCanFire())
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
    BeamFrame veilBeam_{};unsigned veilBeamShots_=0;
    RailVeilLightning::Chain veilLightning_{};
    static float Smooth(float x){x=(std::clamp)(x,0.0f,1.0f);return x*x*(3-2*x);}
    void Enter(Phase phase){phase_=phase;phaseAge_=0;}
    void BuildValley(const TerrainEditLayer* preview) {
        terrainPreview_=preview?*preview:TerrainEditLayer{};
        std::vector<TerrainBrushStamp> stamps;
        for(int i=0;i<=15;++i)for(int side=0;side<3;++side) {
            TerrainBrushStamp p;p.strokeGuid="postboss-prototype-valley";
            p.stampGuid=p.strokeGuid+"-"+std::to_string(i)+"-"+std::to_string(side);
            p.distance=anchor_+280+float(i)*160;p.angle=side==0?0:side==1?3.14159265f:4.71238898f;
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
    std::array<RailRearDamageSmoke::State,2> hullSmoke_{};
    float combatSeconds_=0;
    Vector3 veilSource_{};
    Phase phase_=Phase::Waiting;
    float anchor_=0,age_=0,phaseAge_=0;
    std::array<uint32_t,2> ids_{};
    std::array<bool,2> defeated_{};
    bool checkpointCaptured_=false,pursuitCheckpointCaptured_=false;
    std::vector<CourseTerrainPlacement> scenery_;
    TerrainEditLayer terrainPreview_;
};
