#pragma once
#include "utils/math/Vector.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

// Observes real emissions/contacts; never owns combat clocks, aim or damage.
class RailEliteFirePresentation final {
public:
    enum class Kind { Fang, Veil };
    struct Pulse {
        Kind kind=Kind::Fang;
        uint32_t owner=0;
        uint64_t id=0;
        Vector3 origin{},direction{0,0,1},contact{};
        float age=10,proximity=0;
        bool shockDelivered=true;
        bool Visible()const{return age<(kind==Kind::Veil?.42f:.18f);}
    };
    void Reset(){*this={};}
    void Begin(float dt,bool active) {
        shake_=0;fangFired_=veilFired_=shockSound_=false;
        enabled_=active && std::isfinite(dt) && dt>0;
        if(!active){for(auto& p:pulses_)p=Pulse{};return;}
        if(!enabled_)return;
        dt=(std::min)(dt,.1f);sinceFang_+=dt;
        for(auto& p:pulses_) {
            p.age+=dt;
            if(p.kind==Kind::Veil && !p.shockDelivered && p.age>=.045f) {
                p.shockDelivered=true;shockSound_=p.proximity>.02f;
                shake_=(std::max)(shake_,.14f*p.proximity);
            }
        }
    }
    bool FangLaunch(uint64_t id,uint32_t owner,Vector3 origin,Vector3 direction) {
        if(!enabled_ || !id || Seen(fired_,id))return false;
        fired_[fireSerial_++%fired_.size()]=id;
        auto& p=Allocate();p={Kind::Fang,owner,id,origin,direction,{},0,0,true};
        fangFired_=true;
        // One tiny source kick per burst. Consecutive rounds never stack it.
        if(sinceFang_>1.1f)shake_=(std::max)(shake_,.018f);
        sinceFang_=0;return true;
    }
    bool VeilLaunch(uint64_t serial,uint32_t owner,Vector3 origin,Vector3 direction,Vector3 contact,float proximity) {
        if(!enabled_ || !serial || serial==veilSerial_)return false;
        veilSerial_=serial;auto& p=Allocate();
        p={Kind::Veil,owner,serial,origin,direction,contact,0,(std::clamp)(proximity,0.0f,1.0f),false};
        veilFired_=true;shake_=(std::max)(shake_,.035f);return true;
    }
    void FangContact(uint64_t id,float closeness,bool impact) {
        if(!enabled_ || !Seen(fired_,id) || Seen(contacts_,id))return;
        contacts_[contactSerial_++%contacts_.size()]=id;
        const float amount=(impact?.14f:.09f)*(std::clamp)(closeness,0.0f,1.0f);
        shake_=(std::max)(shake_,amount);
    }
    float FangRecoil(uint32_t owner)const {
        float kick=0;
        for(const auto& p:pulses_)if(p.kind==Kind::Fang && p.owner==owner && p.age<.18f) {
            const float t=p.age;
            const float value=t<.018f?.10f+.06f*t/.018f:.16f*std::pow(1-(std::clamp)((t-.018f)/.162f,0.0f,1.0f),2.0f);
            kick=(std::max)(kick,value);
        }
        return kick;
    }
    const std::array<Pulse,8>& Pulses()const{return pulses_;}
    bool Visible()const{return std::any_of(pulses_.begin(),pulses_.end(),[](const auto& p){return p.Visible();});}
    const Pulse* LensPulse()const {
        const Pulse* result=nullptr;
        for(const auto& p:pulses_)if(p.kind==Kind::Veil && p.age<.12f && (!result || p.age<result->age))result=&p;
        return result;
    }
    float Shake()const{return (std::min)(shake_,.16f);}
    bool FangFired()const{return fangFired_;}
    bool VeilFired()const{return veilFired_;}
    bool ShockSound()const{return shockSound_;}
private:
    template<size_t N> static bool Seen(const std::array<uint64_t,N>& ids,uint64_t id) {
        return std::find(ids.begin(),ids.end(),id)!=ids.end();
    }
    Pulse& Allocate() {
        return *std::max_element(pulses_.begin(),pulses_.end(),[](const auto& a,const auto& b){return a.age<b.age;});
    }
    std::array<Pulse,8> pulses_{};
    std::array<uint64_t,64> fired_{},contacts_{};
    uint64_t veilSerial_=0;
    unsigned fireSerial_=0,contactSerial_=0;
    float sinceFang_=10,shake_=0;
    bool enabled_=false,fangFired_=false,veilFired_=false,shockSound_=false;
};
