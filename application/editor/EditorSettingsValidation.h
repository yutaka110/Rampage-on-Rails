#pragma once

#include <cmath>
#include <string>

namespace editor::settings {
template <typename Value>
inline bool InRange(Value value, Value minimum, Value maximum) {
    return std::isfinite(static_cast<double>(value)) && value >= minimum && value <= maximum;
}

inline bool Finite(float value) { return std::isfinite(value); }

inline bool Result(bool valid, std::string* errorMessage, const char* message) {
    if (errorMessage != nullptr) *errorMessage = valid ? "" : message;
    return valid;
}
} // namespace editor::settings
