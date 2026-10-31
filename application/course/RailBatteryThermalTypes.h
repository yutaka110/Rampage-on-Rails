#pragma once

enum class RailBatteryThermalPhase : unsigned char {
    Inactive, Standby, Charging, Imminent, Firing, Cooling, Counter, Destroyed
};

// Read-only presentation of the admitted warning, actual emission and cooldown.
struct RailBatteryThermalFrame {
    RailBatteryThermalPhase phase=RailBatteryThermalPhase::Inactive;
    float charge=0,heat=0,ventOpen=0,coolingProgress=0,firingProgress=0;
    float remainingSeconds=0,durationSeconds=1,clock=0;
    bool valid=false,heavy=false;
};
