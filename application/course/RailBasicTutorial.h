#pragma once
#include "RailLockOnTypes.h"
#include <algorithm>
#include <cmath>
#include <span>

// Presentation observes accepted gameplay actions. It never grants a lock,
// fires a shot, changes travel speed or teaches an enemy's solution.
struct RailBasicTutorialInput {
    float dt=0, distance=0, speed=0, normalSpeed=0;
    bool active=false, boss=false, targetVisible=false, lockTargetAvailable=false;
    bool primaryHeld=false, lockHeld=false, boostHeld=false, brakeHeld=false, rewardBoost=false;
    bool volleyAccepted=false;
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
    void ResetAttempt(){const auto learned=learned_;Reset();learned_=learned;}
    Lesson Current()const{return lesson_;}
    bool Visible()const{return visible_;}
    bool Success()const{return success_;}
    bool WaitingForRewardBoost()const{return waitingForRewardBoost_;}
    bool Learned(Lesson lesson)const{return lesson!=Lesson::None && (learned_ & Bit(lesson))!=0;}
    unsigned LearnedMask()const{return learned_;}
    unsigned Locks()const{return locks_;}
    float Age()const{return age_;}
    float Progress()const{return progress_;}
    LockStep LockPhase()const{return locks_>0?LockStep::Release:held_?LockStep::Sweep:LockStep::Hold;}
    void Update(const RailBasicTutorialInput& in) {
        if(!std::isfinite(in.dt) || in.dt<=0)return;
        visible_=false;
        const bool wasWaitingForRewardBoost=waitingForRewardBoost_;
        waitingForRewardBoost_=false;
        if(!in.active || in.boss)return;
        const float dt=(std::min)(in.dt,.1f);
        const auto enemyLocks=[](std::span<const RailLockToken> tokens) {
            return unsigned(std::count_if(tokens.begin(),tokens.end(),[](const auto& t){return t.target.kind==RailLockTargetKind::Enemy;}));
        };
        locks_=enemyLocks(in.locks);held_=in.lockHeld;
        if(in.targetVisible && in.primaryHeld) {primaryHold_+=dt;shots_+=in.primaryShots;}
        else if(!Learned(Lesson::Primary)){primaryHold_=0;shots_=0;}
        boostHold_=lesson_==Lesson::Boost && in.boostHeld && !in.brakeHeld && !in.rewardBoost &&
            in.normalSpeed>1 && in.speed>in.normalSpeed*1.03f?boostHold_+dt:0;
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
        lockTargetAbsent_=lesson_==Lesson::Lock && !in.lockTargetAvailable?lockTargetAbsent_+dt:0;
        const Lesson desired=StateFor(lesson_).Next(*this,in);
        if(desired!=lesson_ || waitingForRewardBoost_!=wasWaitingForRewardBoost){lesson_=desired;age_=0;}
        visible_=lesson_!=Lesson::None || waitingForRewardBoost_;
        if(visible_)age_+=dt;
        progress_=StateFor(lesson_).Progress(*this);
    }
private:
    class LessonState {
    public:
        virtual ~LessonState() = default;
        virtual float Progress(const RailBasicTutorial& tutorial) const = 0;
        virtual Lesson Next(RailBasicTutorial& tutorial, const RailBasicTutorialInput& input) const {
            // When an exercise yields the card, preserve the route priority.
            if(input.distance>=BoostGuideStart && !tutorial.Learned(Lesson::Brake))return Lesson::Brake;
            if(input.distance>=BoostGuideStart && !tutorial.Learned(Lesson::Boost)) {
                tutorial.waitingForRewardBoost_=input.rewardBoost;
                return input.rewardBoost?Lesson::None:Lesson::Boost;
            }
            if(input.distance<SpeedEnd && input.targetVisible && !tutorial.Learned(Lesson::Primary))return Lesson::Primary;
            if(input.lockTargetAvailable && !tutorial.Learned(Lesson::Lock))return Lesson::Lock;
            return Lesson::None;
        }
    };

    static const LessonState& StateFor(Lesson lesson) noexcept {
        class HiddenState final : public LessonState {
            float Progress(const RailBasicTutorial&) const override { return 0.0f; }
        };
        class SustainedInputState final : public LessonState {
        public:
            SustainedInputState(float RailBasicTutorial::* heldSeconds, float required)
                : heldSeconds_(heldSeconds), required_(required) {}
        private:
            float Progress(const RailBasicTutorial& tutorial) const override {
                return (std::min)(1.0f, (tutorial.*heldSeconds_)/required_);
            }
            float RailBasicTutorial::* heldSeconds_;
            float required_;
        };
        class LockState final : public LessonState {
            float Progress(const RailBasicTutorial& tutorial) const override {
                return tutorial.locks_>0?1.0f:0.0f;
            }
            Lesson Next(RailBasicTutorial& tutorial, const RailBasicTutorialInput& input) const override {
                // Let a useful lock exercise finish even after passing the
                // speed-guide marker or briefly losing sight around a curve.
                if(!tutorial.Learned(Lesson::Lock) && tutorial.lockTargetAbsent_<.45f)return Lesson::Lock;
                return LessonState::Next(tutorial,input);
            }
        };
        static const HiddenState hidden;
        static const SustainedInputState primary(&RailBasicTutorial::primaryHold_, .35f);
        static const LockState lock;
        static const SustainedInputState boost(&RailBasicTutorial::boostHold_, .6f);
        static const SustainedInputState brake(&RailBasicTutorial::brakeHold_, .6f);
        switch(lesson) {
        case Lesson::None: return hidden;
        case Lesson::Primary: return primary;
        case Lesson::Lock: return lock;
        case Lesson::Boost: return boost;
        case Lesson::Brake: return brake;
        }
        return hidden;
    }

    static unsigned Bit(Lesson lesson){return 1u<<unsigned(lesson);}
    Lesson lesson_=Lesson::None;
    unsigned learned_=0,locks_=0,shots_=0;
    bool visible_=false,success_=false,held_=false,waitingForRewardBoost_=false;
    float age_=0,progress_=0,primaryHold_=0,boostHold_=0,brakeHold_=0;
    float lockTargetAbsent_=0;
};
