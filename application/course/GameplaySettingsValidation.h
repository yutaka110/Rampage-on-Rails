#pragma once

#include <cmath>
#include <initializer_list>
#include <string>

namespace gameplay::settings {
inline bool InRange(float value, float minimum, float maximum) {
    return std::isfinite(value) && value >= minimum && value <= maximum;
}

// Prevent nonfinite or extreme authoring values from entering the simulation.
inline bool NonNegative(std::initializer_list<float> values) {
    for (float value : values) {
        if (!InRange(value, 0.0f, 100000.0f)) return false;
    }
    return true;
}

inline bool UnitInterval(std::initializer_list<float> values) {
    for (float value : values) {
        if (!InRange(value, 0.0f, 1.0f)) return false;
    }
    return true;
}

inline bool Result(bool valid, std::string* errorMessage, const char* message) {
    if (errorMessage != nullptr) *errorMessage = valid ? "" : message;
    return valid;
}
} // namespace gameplay::settings
