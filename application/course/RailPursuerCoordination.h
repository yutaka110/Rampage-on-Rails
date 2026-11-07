#pragma once
#include "utils/math/Vector.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

// Independent flight/weapon recovery, with one reservation covering a full
// warning AND all of its live rounds. No random order, timer-based turn handoff,
// projectile deletion, or movement teleport is used to manufacture openings.
class RailPursuerCoordination final {
public:
    enum class Owner { None, Fang, Veil };
    enum class Flight { Approach, Attack, Recover };
    struct Aircraft {bool alive=false,ready=false,lineClear=true,readable=true;float gap=60,lateral=0,height=10;uint64_t shots=0;};
    struct Input {
        std::array<Aircraft,2> aircraft{};
        float speed=36.45f,acceleration=0;
        bool fangThreat=false,veilThreat=false,finishRequested=false;
    };
    struct Node {
        Flight flight=Flight::Approach;float age=0,cooldown=0,wait=0;
        Vector3 formation{};float bank=0;unsigned attacks=0;
    };
    void Tick(float dt,const Input& input) {
        if(!std::isfinite(dt) || dt<=0)return;
        dt=(std::min)(dt,.1f);input_=input;elapsed_+=dt;leaseAge_+=dt;
        rest_=(std::max)(0.0f,rest_-dt);brakeRest_=(std::max)(0.0f,brakeRest_-dt);
        brakeIntent_=(std::max)(0.0f,brakeIntent_-dt);
        if(input.acceleration<-7 || (input.speed<28 && !wasSlow_))brakeIntent_=1.2f;
        wasSlow_=input.speed<28;
        if(!initialized_) {
            initialized_=true;
            for(size_t i=0;i<2;++i)nodes_[i].formation={input.aircraft[i].gap,input.aircraft[i].lateral,input.aircraft[i].height};
            nodes_[1].cooldown=1.1f;
        }
        for(size_t i=0;i<2;++i) {
            auto& n=nodes_[i];n.age+=dt;n.wait+=dt;n.cooldown=(std::max)(0.0f,n.cooldown-dt);
            if(n.flight==Flight::Recover && n.age>=(i==0?1.65f:2.1f)) {
                n.flight=Flight::Approach;n.age=0;if(i==1)interrupted_=false;
            }
        }
        // A braking feint draws the pursuing fighter sideways to regain room.
        // Once a warning is reserved it is allowed to complete normally.
        if(brakeIntent_>0 && input.aircraft[0].gap<39 && owner_!=Owner::Fang &&
            nodes_[0].flight==Flight::Approach && brakeRest_==0) {
            Recover(0,1.4f);brakeRest_=2.5f;brakeIntent_=0;
        }
        if(owner_==Owner::Fang) {
            if(!committed_ && input.aircraft[0].shots!=fangShotAtEntry_) {
                committed_=true;Commit(0);Recover(0,3.4f);
            }
            if(committed_) {
                if(!input.fangThreat && nodes_[0].age>=.35f)Release(.55f);
            } else if(!input.aircraft[0].alive || !input.aircraft[0].lineClear || !input.aircraft[0].readable || leaseAge_>=5.5f) {
                Recover(0,.85f);Release(.55f);
            }
        } else if(owner_==Owner::Veil) {
            if(committed_) {
                if(!input.veilThreat && nodes_[1].age>=.20f)Release(.65f);
            } else if(!input.aircraft[1].alive || !input.aircraft[1].lineClear || !input.aircraft[1].readable || leaseAge_>=3.5f)CancelVeil();
        }
        if(owner_==Owner::None && rest_==0 && elapsed_>=1.35f && !input.finishRequested && !input.fangThreat && !input.veilThreat) {
            const bool fang=Eligible(0),veil=Eligible(1);
            const float accelerating=(std::clamp)((input.speed-37)/12,0.0f,1.0f)+
                (std::clamp)(input.acceleration/18,0.0f,1.0f);
            const float fangScore=2.4f+(48-input.aircraft[0].gap)*.08f+nodes_[0].wait*.10f-accelerating*1.6f;
            const float veilScore=1.8f+(input.aircraft[1].gap-32)*.02f+nodes_[1].wait*.14f+accelerating*1.4f;
            const float repeatPenalty=streak_>=2?2.0f:.55f;
            const float f=fangScore-(last_==Owner::Fang?repeatPenalty:0),v=veilScore-(last_==Owner::Veil?repeatPenalty:0);
            if(fang || veil)Reserve(fang && (!veil || f>=v)?Owner::Fang:Owner::Veil);
        }
        Move(dt,0);Move(dt,1);
    }
    void NotifyVeilFired() {if(owner_==Owner::Veil && !committed_){committed_=true;Commit(1);Recover(1,5.2f);}}
    void CancelVeil() {interrupted_=true;Recover(1,4.2f);if(owner_==Owner::Veil)Release(.85f);else rest_=(std::max)(rest_,.65f);}
    Owner Reservation()const{return owner_;}
    float LeaseAge()const{return leaseAge_;}
    bool Committed()const{return committed_;}
    bool FangCanFire()const{return owner_==Owner::Fang && !committed_;}
    bool VeilCanFire()const{return owner_==Owner::Veil && !committed_;}
    bool SafeToExit()const{return owner_==Owner::None && !input_.fangThreat && !input_.veilThreat;}
    bool VeilInterrupted()const{return interrupted_;}
    bool VeilPreparing()const{return input_.aircraft[1].alive && !interrupted_ &&
        (nodes_[1].flight==Flight::Approach || VeilCanFire());}
    bool FangFollowup()const{return input_.aircraft[0].alive && nodes_[0].flight==Flight::Recover && nodes_[0].age>=.68f && nodes_[0].age<1.38f;}
    const Node& AircraftState(size_t i)const{return nodes_[i];}
    unsigned GrantSerial()const{return grants_;}
    unsigned AttackCount()const{return nodes_[0].attacks+nodes_[1].attacks;}
    const Input& Perception()const{return input_;}
private:
    bool Eligible(size_t i)const {
        const auto& a=input_.aircraft[i];const auto& n=nodes_[i];
        return a.alive && a.ready && a.lineClear && a.readable && n.flight==Flight::Approach &&
            n.cooldown==0 && n.age>=(i==0?.65f:.95f) && a.gap>=23 && a.gap<=(i==0?48.0f:82.0f) &&
            std::abs(a.lateral)<=(i==0?13.0f:21.0f) && a.height<=(i==0?11.0f:16.0f);
    }
    void Recover(size_t i,float cooldown) {nodes_[i].flight=Flight::Recover;nodes_[i].age=0;nodes_[i].cooldown=(std::max)(nodes_[i].cooldown,cooldown);}
    void Reserve(Owner owner) {
        owner_=owner;committed_=false;leaseAge_=0;++grants_;
        const size_t i=owner==Owner::Fang?0:1;
        nodes_[i].flight=Flight::Attack;nodes_[i].age=0;
        if(i==0)fangShotAtEntry_=input_.aircraft[0].shots;
        else interrupted_=false;
    }
    void Commit(size_t i) {
        ++nodes_[i].attacks;nodes_[i].wait=0;const Owner who=i==0?Owner::Fang:Owner::Veil;
        streak_=last_==who?streak_+1:1;last_=who;
    }
    void Release(float rest) {owner_=Owner::None;committed_=false;leaseAge_=0;rest_=rest;}
    void Move(float dt,size_t i) {
        if(!input_.aircraft[i].alive)return;
        auto& n=nodes_[i];const float t=elapsed_;Vector3 target{};
        if(i==0) {
            target=n.flight==Flight::Recover?Vector3{52+4*std::sin(t*.8f),18+4*std::sin(t*1.3f),9+2*std::sin(t)}:
                n.flight==Flight::Attack?Vector3{26,5.5f,5.5f}:Vector3{28+2*std::sin(t*.73f),6+3*std::sin(t*1.21f),6.5f+std::sin(t*.83f)};
        } else {
            target=n.flight==Flight::Recover?Vector3{58+3*std::sin(t*.6f),-23+3*std::sin(t*.9f),14+std::sin(t)}:
                n.flight==Flight::Attack?Vector3{32,-9,8}:Vector3{43+6*std::sin(t*.47f+.7f),-13-4*std::sin(t*.68f),10+1.8f*std::sin(t*.91f)};
        }
        const float blend=1-std::exp(-dt*2.6f),old=n.formation.y;
        n.formation.x+=(target.x-n.formation.x)*blend;
        n.formation.y+=(target.y-n.formation.y)*blend;
        n.formation.z+=(target.z-n.formation.z)*blend;
        const float bank=(std::clamp)((n.formation.y-old)/dt*.035f,-.52f,.52f);
        n.bank+=(bank-n.bank)*(1-std::exp(-dt*5));
    }
    Input input_{};std::array<Node,2> nodes_{};
    Owner owner_=Owner::None,last_=Owner::None;
    bool initialized_=false,committed_=false,interrupted_=false,wasSlow_=false;
    float elapsed_=0,leaseAge_=0,rest_=0,brakeRest_=0,brakeIntent_=0;
    uint64_t fangShotAtEntry_=0;unsigned grants_=0,streak_=0;
};

struct RailPursuerPerception {
    std::array<bool,2> lineClear{{true,true}},readable{{true,true}};
    float acceleration=0;
};
