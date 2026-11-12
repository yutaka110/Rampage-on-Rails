#pragma once

#include <algorithm>

enum class RailBatteryThermalPhase : unsigned char {
    Inactive, Standby, Charging, Imminent, Firing, Cooling, Counter, Destroyed
};

// Read-only presentation of the admitted warning, actual emission and cooldown.
struct RailBatteryThermalFrame {
    RailBatteryThermalPhase phase=RailBatteryThermalPhase::Inactive;
    float charge=0,heat=0,ventOpen=0,coolingProgress=0,firingProgress=0;
    float remainingSeconds=0,durationSeconds=1,clock=0;
    float counterHitAge=-1;
    bool valid=false,heavy=false;
};

inline float HeavyMuzzleArmorOpen(const RailBatteryThermalFrame& frame,float shotAge) {
    if(!frame.valid || !frame.heavy || frame.phase==RailBatteryThermalPhase::Destroyed)return 0;
    const auto smooth=[](float value){
        const float t=(std::clamp)(value,0.0f,1.0f);
        return t*t*(3-2*t);
    };
    if(frame.phase==RailBatteryThermalPhase::Charging || frame.phase==RailBatteryThermalPhase::Imminent)
        return smooth((frame.charge-.06f)/.62f);
    if(frame.phase==RailBatteryThermalPhase::Firing)return 1;
    const float cycleOpening=frame.phase==RailBatteryThermalPhase::Counter ?
        .86f*smooth((frame.durationSeconds-frame.remainingSeconds)/.16f)*smooth(frame.remainingSeconds/.22f) :
        (shotAge>=0 ? 1-smooth((shotAge-.16f)/.44f) : 0);
    const float hitOpening=frame.counterHitAge>=0 ? .94f*(1-smooth((frame.counterHitAge-.30f)/.54f)) : 0;
    return (std::max)(cycleOpening,hitOpening);
}
