#pragma once
#include "CourseSpawnRuntime.h"
#include "EnemyProjectileShootDownSystem.h"
#include "RailMothershipPresentation.h"
#include "WeaponFeedbackSystem.h"
#include "WeaponFireSystem.h"
#include "RailLockOnTypes.h"
#include "RailGatlingAttack.h"
#include <array>
#include <algorithm>
#include <bit>
#include <cmath>

// Refines the encounter's attack gate. Runtime, targeting, warning admission
// and execution retain ownership of actual fire; no synthetic shots or hits.
class RailMinibossCombatDirector final {
public:
    static constexpr float RapidCounterSeconds=1.15f,HeavyCounterSeconds=1.80f;
    static constexpr float CounterDamageMultiplier=2.0f;
    void Reset(uint32_t seed=0x6d2b79f5u){*this={};random_=seed?seed:0x6d2b79f5u;}
    bool Active()const{return active_;}
    int Turn()const{return turn_;}
    unsigned IntroducedMask()const{return introduced_;}
    float CounterRemaining()const{return counterRemaining_;}
    float CounterDuration()const{return counterDuration_;}
    uint32_t CounterOwner()const{return counterOwner_;}
    uint64_t CounterSerial()const{return counterSerial_;}
    int CounterPart()const{return counterPart_;}
    float FeedbackDamage()const{return feedbackDamage_;}
    float FeedbackRemaining()const{return feedbackRemaining_;}
    uint32_t FeedbackOwner()const{return feedbackOwner_;}
    float HitPulse()const{return (std::clamp)((feedbackRemaining_-.78f)/.12f,0.0f,1.0f);}
    float DamageMultiplier(uint32_t target,bool lockOn)const {
        return counterRemaining_>0 && target!=0 && target==counterOwner_ &&
            ((counterPart_==0 && !lockOn) || (counterPart_==1 && lockOn)) ? CounterDamageMultiplier : 1.0f;
    }
    // Capture eligibility at firing, so a released lock volley retains its
    // reward through travel. Only a matching accepted contact can consume it.
    bool CreditShot(uint64_t shot,uint32_t target,bool lockOn) {
        if(!shot || DamageMultiplier(target,lockOn)<=1)return false;
        for(const auto& credit:credits_)if(credit.shot==shot)return false;
        credits_[nextCredit_++%credits_.size()]={shot,target,counterSerial_};
        return true;
    }
    void PrepareLockShots(std::span<const RailLockToken> tokens,std::span<WeaponShot> shots) {
        for(size_t i=0;i<tokens.size() && i<shots.size();++i) {
            const auto& target=tokens[i].target;
            if(target.kind!=RailLockTargetKind::Enemy || !std::isfinite(shots[i].damage) || shots[i].damage<=0)continue;
            const float bonus=DamageMultiplier(target.actorId,true);
            if(bonus>1 && CreditShot(shots[i].shotId,target.actorId,true))shots[i].damage*=bonus;
        }
    }
    bool NotifyCounterDamage(const WeaponFeedbackEvent& hit) {
        if(hit.hitKind!=RailAimHitKind::Enemy || hit.appliedDamage<=0 || !std::isfinite(hit.appliedDamage))return false;
        for(auto& credit:credits_)if(credit.shot==hit.shotId && credit.owner==hit.targetActorId) {
            const bool first=feedbackCycle_!=credit.cycle;
            if(first)feedbackDamage_=0;
            feedbackCycle_=credit.cycle;feedbackOwner_=credit.owner;
            feedbackDamage_+=hit.appliedDamage;feedbackRemaining_=.9f;credit.owner=0;
            return first;
        }
        return false;
    }
    bool InFlight()const{return count_>0;}
    bool GatlingInFlight()const{return InFlight() && volleyPart_==0;}
    uint32_t VolleyOwner()const{return volleyOwner_;}
    bool GatlingGuardPoint(float playerDistance,Vector3& railPoint)const {
        if(!GatlingInFlight() || !guardValid_)return false;
        railPoint={guardOffset_.x,guardOffset_.y,playerDistance+guardOffset_.z};return true;
    }
    unsigned InterceptedCount()const{return std::popcount(intercepted_);}
    float CooldownRemaining(const CourseSpawnRuntime& runtime,int part)const {
        if(part<0 || part>1)return 0;
        const auto* actor=FindLiving(runtime,ids_[part]);
        return actor?(std::max)(cooldown_[part],actor->behaviorState.attackCooldownRemaining):0;
    }
    float CooldownDuration(const CourseSpawnRuntime& runtime,int part)const {
        if(part<0 || part>1)return 1;
        const auto* actor=FindLiving(runtime,ids_[part]);
        return (std::max)(part==0?2.4f:2.6f,actor?actor->behaviorDefinition.attackCooldownSeconds:0.0f);
    }

    void Update(CourseSpawnRuntime& runtime,float deltaTime,bool enabled,bool gameplayActive,bool pacingAllowsAttack,
            float playerDistance=-1.0f) {
        if(!enabled){Reset();return;}
        Bind(runtime);
        active_=Living(runtime,0) || Living(runtime,1);
        if(!active_){counterRemaining_=0;ClearVolley();return;}
        if(!gameplayActive){ApplyGate(runtime,false);return;}
        const float dt=gameplayActive && std::isfinite(deltaTime)?(std::clamp)(deltaTime,0.0f,.25f):0;
        elapsed_+=dt;
        for(auto& cooldown:cooldown_)cooldown=(std::max)(0.0f,cooldown-dt);
        counterRemaining_=(std::max)(0.0f,counterRemaining_-dt);
        gapRemaining_=(std::max)(0.0f,gapRemaining_-dt);
        feedbackRemaining_=(std::max)(0.0f,feedbackRemaining_-dt);
        if(counterRemaining_>0 && !FindLiving(runtime,counterOwner_))counterRemaining_=0;
        if(InFlight()) {
            bool live=false;
            if(!FindLiving(runtime,volleyOwner_))runtime.CancelEnemyVolley(volleyOwner_,volleyIntent_);
            for(const auto& bullet:runtime.Bullets())if((bullet.active || bullet.launchDelayRemaining>0) && !bullet.hitConsumed &&
                (bullet.launchDelayRemaining>0 || playerDistance<0 || bullet.spawnDistance+bullet.distanceOffset>playerDistance-3.0f) &&
                std::find(projectiles_.begin(),projectiles_.begin()+count_,bullet.projectileId)!=projectiles_.begin()+count_)live=true;
            if(!live)FinishTurn(.35f);
        }
        if(!Living(runtime,turn_))selectionPending_=true;
        if(pacingAllowsAttack && !InFlight() && counterRemaining_<=0 && gapRemaining_<=0 && selectionPending_)
            SelectTurn(runtime);
        ApplyGate(runtime,gameplayActive && pacingAllowsAttack);
    }

    // Called immediately after Runtime.Update and before the player's shots.
    void ObserveFire(CourseSpawnRuntime& runtime,float playerDistance=-1,float playerSpeed=0) {
        if(!active_)return;
        for(int part=0;part<2;++part) {
            const auto* actor=FindLiving(runtime,ids_[part]);
            if(!actor || actor->fireSequence<=observed_[part])continue;
            observed_[part]=actor->fireSequence;introduced_|=1u<<part;
            if(InFlight())continue;
            cooldown_[part]=CooldownDuration(runtime,part);
            streak_=lastPart_==part?streak_+1:1;lastPart_=part;
            // Defense timing affects later choices; checkpoint copies preserve
            // the stream, and pause never advances it.
            random_^=static_cast<uint32_t>(elapsed_*60)+actor->actorId+static_cast<uint32_t>(actor->fireSequence);
            volleyPart_=part;volleyOwner_=actor->actorId;intercepted_=0;count_=0;
            guardValid_=false;
            Vector3 guardSum{};unsigned guardSamples=0;
            volleyIntent_=actor->behaviorState.committedAttackSequence;
            for(const auto& bullet:runtime.Bullets())if((bullet.active || bullet.launchDelayRemaining>0) && !bullet.hitConsumed &&
                bullet.ownerActorId==actor->actorId && bullet.attackIntentSequence==actor->behaviorState.committedAttackSequence) {
                if(count_<projectiles_.size())projectiles_[count_++]=bullet.projectileId;
                if(part==0 && playerDistance>=0 && bullet.initialized) {
                    const float sample=RailGatling::GuardSampleSeconds-bullet.age;
                    const Vector3 offset{bullet.lateralOffset+bullet.lateralSpeed*sample,
                        bullet.verticalOffset+bullet.verticalSpeed*sample,
                        bullet.spawnDistance+bullet.distanceOffset+bullet.forwardSpeed*sample-
                            playerDistance-playerSpeed*(sample+bullet.launchDelayRemaining)};
                    if(std::isfinite(offset.x) && std::isfinite(offset.y) && std::isfinite(offset.z) && offset.z>4) {
                        guardSum.x+=offset.x;guardSum.y+=offset.y;guardSum.z+=offset.z;++guardSamples;
                    }
                }
            }
            if(guardSamples) {
                // Rotating barrels scatter around one stable stream center.
                // Freeze that center once; never chase individual live rounds.
                const float inv=1.0f/float(guardSamples);
                guardOffset_={guardSum.x*inv,guardSum.y*inv,guardSum.z*inv};guardValid_=true;
            }
            if(!count_)FinishTurn(.35f);
        }
        // A fired volley owns its complete flight, even if another actor was
        // already queued. Existing reservation and visibility checks remain.
        if(InFlight())ApplyGate(runtime,false);
    }

    bool NotifyIntercept(const EnemyProjectileShootDownResult& result,CourseSpawnRuntime& runtime) {
        if(!active_ || !result.accepted || !result.destroyed || result.ownerActorId!=volleyOwner_ ||
            !FindLiving(runtime,volleyOwner_))return false;
        const auto it=std::find(projectiles_.begin(),projectiles_.begin()+count_,result.projectileId);
        if(it==projectiles_.begin()+count_)return false;
        const unsigned bit=1u<<unsigned(it-projectiles_.begin());
        if(intercepted_&bit)return false;
        intercepted_|=bit;
        // Each launched round must actually be intercepted. A short opening
        // volley cannot erase the remainder of the gun's firing schedule.
        if(size_t(std::popcount(intercepted_))<count_)return false;
        counterOwner_=volleyOwner_;counterPart_=volleyPart_;
        counterDuration_=volleyPart_==0?RapidCounterSeconds:HeavyCounterSeconds;
        counterRemaining_=counterDuration_;++counterSerial_;
        FinishTurn(0);ApplyGate(runtime,false);
        return true;
    }
private:
    static const CourseEnemyActor* FindLiving(const CourseSpawnRuntime& runtime,uint32_t id) {
        for(const auto& actor:runtime.Enemies())if(actor.actorId==id && actor.desc.hitPoints>0 &&
            (!actor.combatState.initialized || (actor.combatState.phase!=EnemyCombatPhase::Dying &&
                actor.combatState.phase!=EnemyCombatPhase::Retired)))return &actor;
        return nullptr;
    }
    bool Living(const CourseSpawnRuntime& runtime,int part)const{return FindLiving(runtime,ids_[part])!=nullptr;}
    void Bind(const CourseSpawnRuntime& runtime) {
        for(const auto& actor:runtime.Enemies()) {
            const int part=RailMothership::PartIndex(actor.desc.actorAssetId);
            if(part<0 || actor.desc.hitPoints<=0)continue;
            if(ids_[part] && ids_[part]!=actor.actorId){Reset();break;}
        }
        for(const auto& actor:runtime.Enemies()) {
            const int part=RailMothership::PartIndex(actor.desc.actorAssetId);
            if(part>=0 && !ids_[part] && actor.desc.hitPoints>0) {
                ids_[part]=actor.actorId;observed_[part]=actor.fireSequence;
                if(actor.fireSequence)introduced_|=1u<<part;
            }
        }
    }
    void SelectTurn(const CourseSpawnRuntime& runtime) {
        const bool alive[2]={Living(runtime,0),Living(runtime,1)};
        // Teach both existing controls before introducing variable order.
        for(int part=0;part<2;++part)if(alive[part] && !(introduced_&(1u<<part))) {
            turn_=part;selectionPending_=CooldownRemaining(runtime,part)>.0001f;return;
        }
        bool ready[2]={alive[0] && CooldownRemaining(runtime,0)<=.0001f,
            alive[1] && CooldownRemaining(runtime,1)<=.0001f};
        if(alive[0] && alive[1] && streak_>=2 && lastPart_>=0)ready[lastPart_]=false;
        if(!ready[0] && !ready[1])return;
        if(ready[0] && ready[1]) {
            random_^=random_<<13;random_^=random_>>17;random_^=random_<<5;
            turn_=int(random_&1u);
        } else turn_=ready[0]?0:1;
        // Hold the choice through aiming, warning admission and execution.
        selectionPending_=false;
    }
    void ApplyGate(CourseSpawnRuntime& runtime,bool pacingAllowsAttack) {
        const bool available=pacingAllowsAttack && !selectionPending_ && !InFlight() && counterRemaining_<=0 && gapRemaining_<=0 &&
            CooldownRemaining(runtime,turn_)<=.0001f;
        for(const auto& actor:runtime.Enemies())if(RailMothership::PartIndex(actor.desc.actorAssetId)>=0)
            runtime.SetEnemyEncounterPacing(actor.actorId,true,available && actor.actorId==ids_[turn_]);
    }
    void ClearVolley(){projectiles_={};count_=0;intercepted_=0;volleyOwner_=0;volleyIntent_=0;volleyPart_=-1;}
    void FinishTurn(float gap){if(volleyPart_>=0)turn_=1-volleyPart_;ClearVolley();gapRemaining_=gap;selectionPending_=true;}
    std::array<uint32_t,2> ids_{};
    std::array<uint64_t,2> observed_{};
    std::array<uint64_t,RailGatling::Rounds> projectiles_{};
    size_t count_=0;
    unsigned introduced_=0,intercepted_=0;
    int turn_=0,volleyPart_=-1;
    uint32_t volleyOwner_=0,counterOwner_=0;
    uint64_t volleyIntent_=0;
    float counterRemaining_=0,counterDuration_=0,gapRemaining_=0;
    uint64_t counterSerial_=0;
    bool active_=false;
    std::array<float,2> cooldown_{};
    bool selectionPending_=true;
    int lastPart_=-1,streak_=0;
    uint32_t random_=0x6d2b79f5u;
    float elapsed_=0;
    Vector3 guardOffset_{};
    bool guardValid_=false;
    struct Credit {uint64_t shot=0;uint32_t owner=0;uint64_t cycle=0;};
    std::array<Credit,64> credits_{};
    size_t nextCredit_=0;
    int counterPart_=-1;
    uint64_t feedbackCycle_=0;
    uint32_t feedbackOwner_=0;
    float feedbackDamage_=0,feedbackRemaining_=0;
};
