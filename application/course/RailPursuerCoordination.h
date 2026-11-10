#pragma once
#include "utils/math/Vector.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

// Each aircraft owns its warning, fire and recovery. Only warning starts
// share a short spacing window, so committed attacks can overlap.
class RailPursuerCoordination final {
public:
    enum class Flight { Approach, Attack, Recover };
    enum class CrossfirePhase { Idle, Warning, Volley, Stagger };
    static constexpr float WarningSpacing = .58f;
    static constexpr float CrossfireWarningSeconds=.80f;
    static constexpr float CrossfireRoundInterval=.037f;
    static constexpr float CrossfireRelativeSpeed=165.0f;
    struct Crossfire {
        CrossfirePhase phase=CrossfirePhase::Idle;
        float age=0,side=1,startLane=0,roll=0;
        float lane=0,height=0;
        int panel=-1;
        unsigned rounds=0;
    };
    struct Aircraft {bool alive=false,ready=false,lineClear=true,readable=true;float gap=60,lateral=0,height=10;uint64_t shots=0;};
    struct Input {
        std::array<Aircraft,2> aircraft{};
        float speed=36.45f,acceleration=0;
        bool fangThreat=false,veilThreat=false,finishRequested=false;
        int fangNearPanel=-1;
        float playerLane=0,playerHeight=0;
    };
    struct Node {
        Flight flight=Flight::Approach;float age=0,cooldown=0,leaseAge=0;
        bool reserved=false,committed=false;unsigned grants=0;
        Vector3 formation{};float bank=0;unsigned attacks=0;
    };
    void Tick(float dt,const Input& input) {
        if(!std::isfinite(dt) || dt<=0)return;
        dt=(std::min)(dt,.1f);input_=input;elapsed_+=dt;
        warningSpacing_=(std::max)(0.0f,warningSpacing_-dt);
        if(crossfire_.phase!=CrossfirePhase::Idle)crossfire_.age+=dt;
        if((crossfire_.phase==CrossfirePhase::Stagger ||
            (crossfire_.phase==CrossfirePhase::Volley && !nodes_[0].reserved)) && crossfire_.age>=1.65f)crossfire_={};
        brakeRest_=(std::max)(0.0f,brakeRest_-dt);
        brakeIntent_=(std::max)(0.0f,brakeIntent_-dt);
        if(input.acceleration<-7 || (input.speed<28 && !wasSlow_))brakeIntent_=1.2f;
        wasSlow_=input.speed<28;
        if(!initialized_) {
            initialized_=true;
            for(size_t i=0;i<2;++i)nodes_[i].formation={input.aircraft[i].gap,input.aircraft[i].lateral,input.aircraft[i].height};
            nodes_[1].cooldown=2.10f;
        }
        for(size_t i=0;i<2;++i) {
            auto& n=nodes_[i];n.age+=dt;if(n.reserved)n.leaseAge+=dt;n.cooldown=(std::max)(0.0f,n.cooldown-dt);
            if(n.flight==Flight::Recover && n.age>=(i==0?1.65f:2.1f)) {
                n.flight=Flight::Approach;n.age=0;if(i==1)interrupted_=false;
            }
        }
        // A braking feint draws the pursuing fighter sideways to regain room.
        // Once a warning is reserved it is allowed to complete normally.
        if(brakeIntent_>0 && input.aircraft[0].gap<39 && !nodes_[0].reserved &&
            nodes_[0].flight==Flight::Approach && brakeRest_==0) {
            Recover(0,1.4f);brakeRest_=2.5f;brakeIntent_=0;
        }
        auto& fang=nodes_[0];
        auto& veil=nodes_[1];
        if(fang.reserved) {
            if(crossfire_.phase==CrossfirePhase::Warning &&
                (!input.aircraft[0].alive || !input.aircraft[0].lineClear || !input.aircraft[0].readable)) {
                crossfire_={};Recover(0,1.40f);Release(0);
            } else if(crossfire_.phase==CrossfirePhase::Volley) {
                if((crossfire_.rounds>=crossfireRoundLimit_ || !input.aircraft[0].alive) &&
                    !input.fangThreat && crossfire_.age>=.42f) {
                    Recover(0,3.4f);fang.age=crossfire_.age;Release(0);
                }
            } else if(crossfire_.phase!=CrossfirePhase::Warning) {
                if(!fang.committed && input.aircraft[0].shots!=fangShotAtEntry_) {
                    Commit(0);Recover(0,3.4f);
                }
                if(fang.committed) {
                    if(!input.fangThreat && fang.age>=.35f)Release(0);
                } else if(!input.aircraft[0].alive || !input.aircraft[0].lineClear ||
                    !input.aircraft[0].readable || fang.leaseAge>=5.5f) {
                    Recover(0,1.40f);Release(0);
                }
            }
        }
        if(veil.reserved) {
            if(veil.committed) {
                if(!input.veilThreat && veil.age>=.20f)Release(1);
            } else if(!input.aircraft[1].alive || veil.leaseAge>=3.5f)CancelVeil();
        }
        if(warningSpacing_==0 && elapsed_>=1.35f && !input.finishRequested) {
            const bool fangReady=Eligible(0) && !input.fangThreat;
            const bool veilReady=Eligible(1) && !input.veilThreat;
            if(veilReady && (!fangReady || input.speed>42 || input.acceleration<-7))Reserve(1);
            else if(fangReady)Reserve(0);
        }
        Move(dt,0);Move(dt,1);
    }
    void NotifyVeilFired() {if(VeilCanFire()){Commit(1);Recover(1,5.2f);}}
    void CancelVeil() {interrupted_=true;Recover(1,4.2f);Release(1);}
    bool FangCanFire()const{return nodes_[0].reserved && !nodes_[0].committed && crossfire_.phase==CrossfirePhase::Idle;}
    const Crossfire& FangCrossfire() const {return crossfire_;}
    bool CrossfireRoundDue() const {
        return nodes_[0].reserved &&
            ((crossfire_.phase==CrossfirePhase::Warning && crossfire_.age>=CrossfireWarningSeconds) ||
             (crossfire_.phase==CrossfirePhase::Volley && crossfire_.rounds<crossfireRoundLimit_ &&
              crossfire_.age>=CrossfireRoundInterval*float(crossfire_.rounds)));
    }
    void CrossfireRoundEmitted(unsigned limit) {
        if(crossfire_.phase==CrossfirePhase::Warning) {
            crossfire_.phase=CrossfirePhase::Volley;crossfire_.age=0;crossfireRoundLimit_=limit;
            Commit(0);
        }
        ++crossfire_.rounds;
    }
    void StopCrossfire() {
        if(crossfire_.phase==CrossfirePhase::Volley)crossfireRoundLimit_=crossfire_.rounds;
        else if(crossfire_.phase==CrossfirePhase::Warning) {
            crossfire_={};Recover(0,1.40f);Release(0);
        }
    }
    bool InterruptCrossfire(int panel) {
        if(crossfire_.phase!=CrossfirePhase::Warning || crossfire_.panel!=panel)return false;
        crossfire_.phase=CrossfirePhase::Stagger;crossfire_.age=0;crossfire_.roll=nodes_[0].bank;
        Recover(0,3.4f);Release(0);
        return true;
    }
    bool VeilCanFire()const{return nodes_[1].reserved && !nodes_[1].committed;}
    bool SafeToExit()const{return !nodes_[0].reserved && !nodes_[1].reserved && !input_.fangThreat && !input_.veilThreat;}
    bool VeilInterrupted()const{return interrupted_;}
    bool VeilPreparing()const{return input_.aircraft[1].alive && !interrupted_ &&
        (nodes_[1].flight==Flight::Approach || VeilCanFire());}
    bool FangFollowup()const{
        if(!input_.aircraft[0].alive)return false;
        if(crossfire_.phase==CrossfirePhase::Volley || crossfire_.phase==CrossfirePhase::Stagger)
            return crossfire_.age>=.43f && crossfire_.age<1.60f;
        return nodes_[0].flight==Flight::Recover && nodes_[0].age>=.68f && nodes_[0].age<1.38f;
    }
    const Node& AircraftState(size_t i)const{return nodes_[i];}
    unsigned AttackCount()const{return nodes_[0].attacks+nodes_[1].attacks;}
    const Input& Perception()const{return input_;}
private:
    bool Eligible(size_t i)const {
        const auto& a=input_.aircraft[i];const auto& n=nodes_[i];
        return !n.reserved && a.alive && a.ready && a.lineClear && a.readable && n.flight==Flight::Approach &&
            n.cooldown==0 && n.age>=(i==0?.65f:.95f) && a.gap>=23 && a.gap<=(i==0?48.0f:82.0f) &&
            std::abs(a.lateral)<=(i==0?13.0f:21.0f) && a.height<=(i==0?11.0f:16.0f);
    }
    void Recover(size_t i,float cooldown) {nodes_[i].flight=Flight::Recover;nodes_[i].age=0;nodes_[i].cooldown=(std::max)(nodes_[i].cooldown,cooldown);}
    void Reserve(size_t i) {
        auto& n=nodes_[i];
        n.reserved=true;n.committed=false;n.leaseAge=0;++n.grants;
        n.flight=Flight::Attack;n.age=0;
        warningSpacing_=WarningSpacing;
        if(i==0) {
            fangShotAtEntry_=input_.aircraft[0].shots;
            crossfire_={};
            if(nodes_[0].attacks%2==1 && input_.fangNearPanel>=0) {
                crossfire_.phase=CrossfirePhase::Warning;crossfire_.panel=input_.fangNearPanel;
                crossfire_.startLane=nodes_[0].formation.y;
                crossfire_.side=crossfire_.startLane>=0?1.0f:-1.0f;
                crossfire_.lane=input_.playerLane;crossfire_.height=input_.playerHeight;
            }
        }
        else interrupted_=false;
    }
    void Commit(size_t i) {++nodes_[i].attacks;nodes_[i].committed=true;}
    void Release(size_t i) {
        auto& n=nodes_[i];n.reserved=false;n.committed=false;n.leaseAge=0;
    }
    void Move(float dt,size_t i) {
        if(!input_.aircraft[i].alive)return;
        auto& n=nodes_[i];const float t=elapsed_;Vector3 target{};
        if(i==0) {
            target=n.flight==Flight::Recover?Vector3{52+4*std::sin(t*.8f),18+4*std::sin(t*1.3f),9+2*std::sin(t)}:
                n.flight==Flight::Attack?Vector3{26,5.5f,5.5f}:Vector3{28+2*std::sin(t*.73f),6+3*std::sin(t*1.21f),6.5f+std::sin(t*.83f)};
            if(crossfire_.phase==CrossfirePhase::Warning) {
                const float u=(std::clamp)(crossfire_.age/CrossfireWarningSeconds,0.0f,1.0f);
                target={28,std::lerp(crossfire_.startLane,-3.8f*crossfire_.side,u*u*(3-2*u)),5.9f};
            } else if(crossfire_.phase==CrossfirePhase::Volley && crossfire_.age<1.65f) {
                target={32,-5.3f*crossfire_.side,6.7f};
            } else if(crossfire_.phase==CrossfirePhase::Stagger) {
                const float brokenSide=crossfire_.panel==0?-1.0f:1.0f;
                target={31,n.formation.y+brokenSide*4.6f*std::exp(-crossfire_.age*5.0f),
                    std::lerp(4.5f,6.4f,(std::min)(1.0f,crossfire_.age/.90f))};
            }
        } else {
            target=n.flight==Flight::Recover?Vector3{58+3*std::sin(t*.6f),-23+3*std::sin(t*.9f),14+std::sin(t)}:
                n.flight==Flight::Attack?(n.attacks%2==0?Vector3{32,0,3.8f}:Vector3{32,-9,8}):Vector3{43+6*std::sin(t*.47f+.7f),-13-4*std::sin(t*.68f),10+1.8f*std::sin(t*.91f)};
        }
        const bool crossing=i==0 && crossfire_.phase==CrossfirePhase::Warning;
        const float blend=1-std::exp(-dt*(crossing?6.5f:2.6f)),old=n.formation.y;
        n.formation.x+=(target.x-n.formation.x)*blend;
        n.formation.y+=(target.y-n.formation.y)*blend;
        n.formation.z+=(target.z-n.formation.z)*blend;
        float bank=(std::clamp)((n.formation.y-old)/dt*.035f,-.52f,.52f);
        if(crossing)bank=-crossfire_.side*1.02f;
        const bool stagger=i==0 && crossfire_.phase==CrossfirePhase::Stagger;
        if(stagger) {
            const float dropBank=crossfire_.panel==0?1.30f:-1.30f;
            const float u=(std::min)(1.0f,crossfire_.age/.20f);
            bank=std::lerp(crossfire_.roll,dropBank,u)*
                std::exp(-(std::max)(0.0f,crossfire_.age-.20f)*4.7f);
        }
        n.bank+=(bank-n.bank)*(1-std::exp(-dt*(stagger?14.0f:crossing?9.0f:5.0f)));
    }
    Input input_{};std::array<Node,2> nodes_{};
    bool initialized_=false,interrupted_=false,wasSlow_=false;
    float elapsed_=0,warningSpacing_=0,brakeRest_=0,brakeIntent_=0;
    uint64_t fangShotAtEntry_=0;
    Crossfire crossfire_{};
    unsigned crossfireRoundLimit_=0;
};

struct RailPursuerPerception {
    std::array<bool,2> lineClear{{true,true}},readable{{true,true}};
    float acceleration=0;
    float playerLane=0,playerHeight=0;
    int fangNearPanel=-1;
};
