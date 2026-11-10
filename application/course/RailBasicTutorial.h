#pragma once
#include "RailLockOnTypes.h"
#include <algorithm>
#include <cmath>
#include <span>

// Presentation observes accepted gameplay actions. It never grants a lock,
// fires a shot, changes travel speed or teaches an enemy's solution.
struct RailBasicTutorialInput {
    float dt=0, distance=0, speed=0, normalSpeed=0;
    bool active=false, boss=false, targetVisible=false, lockFormation=false;
    bool primaryHeld=false, lockHeld=false, boostHeld=false, brakeHeld=false, rewardBoost=false;
    bool threat=false, volleyAccepted=false;
    unsigned primaryShots=0;
    std::span<const RailLockToken> locks{}, release{};
};

class RailBasicTutorial final {
public:
    enum class Lesson { None, Primary, Lock, Boost, Brake };
    enum class LockStep { Hold, Sweep, Release };
    static constexpr float BoostGuideStart=630, SpeedStart=680, SpeedEnd=1200;
    void Reset(){*this={};}
    // Checkpoint retries preserve knowledge, but discard transient input and
    // success feedback. Returning to the title / starting a new run resets it.
    void ResetAttempt(){const auto learned=learned_;const bool offered=brakeOffered_;Reset();learned_=learned;brakeOffered_=offered;}
    Lesson Current()const{return lesson_;}
    bool Visible()const{return visible_;}
    bool Success()const{return success_;}
    bool Learned(Lesson lesson)const{return lesson!=Lesson::None && (learned_ & Bit(lesson))!=0;}
    unsigned LearnedMask()const{return learned_;}
    unsigned Locks()const{return locks_;}
    float Age()const{return age_;}
    float Progress()const{return progress_;}
    LockStep LockPhase()const{return locks_>0?LockStep::Release:held_?LockStep::Sweep:LockStep::Hold;}
    void Update(const RailBasicTutorialInput& in) {
        if(!std::isfinite(in.dt) || in.dt<=0)return;
        visible_=false;
        if(!in.active || in.boss)return;
        // Travel can leave the practice stretch while the reward is still active.
        const bool speedPending=(brakeOffered_ && !Learned(Lesson::Brake)) || lesson_==Lesson::Brake ||
            (Learned(Lesson::Brake) && !Learned(Lesson::Boost)) || (lesson_==Lesson::Boost && success_);
        if(in.distance>=SpeedEnd && !speedPending)return;
        const float dt=(std::min)(in.dt,.1f);
        const auto enemyLocks=[](std::span<const RailLockToken> tokens) {
            return unsigned(std::count_if(tokens.begin(),tokens.end(),[](const auto& t){return t.target.kind==RailLockTargetKind::Enemy;}));
        };
        locks_=enemyLocks(in.locks);held_=in.lockHeld;
        if(in.targetVisible && in.primaryHeld) {primaryHold_+=dt;shots_+=in.primaryShots;}
        else if(!Learned(Lesson::Primary)){primaryHold_=0;shots_=0;}
        boostHold_=lesson_==Lesson::Boost && in.boostHeld && !in.brakeHeld && !in.rewardBoost &&
            !in.threat && in.normalSpeed>1 && in.speed>in.normalSpeed*1.03f?boostHold_+dt:0;
        brakeHold_=lesson_==Lesson::Brake && in.brakeHeld && in.normalSpeed>1 &&
            in.speed<in.normalSpeed*.92f?brakeHold_+dt:0;
        const auto learn=[&](Lesson lesson,bool accepted) {
            if(!accepted || Learned(lesson))return;
            learned_|=Bit(lesson);
            if(lesson_==lesson){success_=true;age_=0;}
        };
        learn(Lesson::Primary,shots_>=3 && primaryHold_>=.35f);
        learn(Lesson::Lock,in.volleyAccepted && enemyLocks(in.release)>0);
        learn(Lesson::Boost,boostHold_>=.6f);
        learn(Lesson::Brake,brakeHold_>=.6f);
        if(success_) {
            visible_=true;age_+=dt;progress_=1;
            const bool speedLesson=lesson_==Lesson::Brake || lesson_==Lesson::Boost;
            if(age_<(speedLesson?.50f:1.25f))return;
            success_=false;lesson_=Lesson::None;age_=0;
        }
        Lesson desired=Lesson::None;
        if(Learned(Lesson::Brake) && !Learned(Lesson::Boost) && in.distance>=BoostGuideStart) {
            if(!in.rewardBoost && !in.threat)desired=Lesson::Boost;
        }
        else if(brakeOffered_ && !Learned(Lesson::Brake) && in.distance>=BoostGuideStart)desired=Lesson::Brake;
        else if(in.distance<SpeedEnd) {
            if(in.targetVisible && !Learned(Lesson::Primary))desired=Lesson::Primary;
            else if(in.targetVisible && !Learned(Lesson::Lock) && in.lockFormation)desired=Lesson::Lock;
            else if((!in.threat || in.rewardBoost) && in.distance>=BoostGuideStart && !Learned(Lesson::Brake))
                desired=Lesson::Brake;
        }
        if(desired!=lesson_){lesson_=desired;age_=0;}
        brakeOffered_|=lesson_==Lesson::Brake;
        visible_=lesson_!=Lesson::None;
        if(visible_)age_+=dt;
        progress_=lesson_==Lesson::Primary?(std::min)(1.0f,primaryHold_/.35f):
            lesson_==Lesson::Lock?(locks_>0?1.0f:0.0f):
            lesson_==Lesson::Boost?(std::min)(1.0f,boostHold_/.6f):
            lesson_==Lesson::Brake?(std::min)(1.0f,brakeHold_/.6f):0;
    }
private:
    static unsigned Bit(Lesson lesson){return 1u<<unsigned(lesson);}
    Lesson lesson_=Lesson::None;
    unsigned learned_=0,locks_=0,shots_=0;
    bool visible_=false,success_=false,held_=false,brakeOffered_=false;
    float age_=0,progress_=0,primaryHold_=0,boostHold_=0,brakeHold_=0;
};
