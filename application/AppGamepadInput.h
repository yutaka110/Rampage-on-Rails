#pragma once

#include <cstdint>

struct AppGamepadStick {
    float x = 0.0f;
    float y = 0.0f;
    float magnitude = 0.0f;
};

struct AppGamepadFrame {
    bool connected = false;
    bool justConnected = false;
    bool justDisconnected = false;
    uint32_t controllerIndex = UINT32_MAX;
    AppGamepadStick leftStick{};
    AppGamepadStick rightStick{};
    bool toggleSkeletonPressed = false;
    bool nextAnimationPressed = false;
    bool resetPressed = false;
    bool toggleHelpPressed = false;
    bool leftShoulderPressed = false;
    bool rightShoulderPressed = false;
    bool boostHeld = false;
};

class AppGamepadInput final {
public:
    [[nodiscard]] AppGamepadFrame Poll() noexcept;

    [[nodiscard]] static AppGamepadStick ApplyRadialDeadZone(
        int16_t x,
        int16_t y,
        int16_t deadZone) noexcept;

    [[nodiscard]] static AppGamepadStick ClampUnitCircle(
        float x,
        float y) noexcept;

    [[nodiscard]] static bool SetVibration(
        uint32_t controllerIndex,
        float lowFrequencyMotor,
        float highFrequencyMotor) noexcept;

private:
    uint32_t activeControllerIndex_ = UINT32_MAX;
    uint16_t previousButtons_ = 0;
    bool wasConnected_ = false;
};
