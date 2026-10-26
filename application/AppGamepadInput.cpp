#include "AppGamepadInput.h"

#include <Windows.h>
#include <Xinput.h>

#include <algorithm>
#include <cmath>

#pragma comment(lib, "Xinput.lib")

namespace {
constexpr uint32_t kMaxXInputControllers = XUSER_MAX_COUNT;

bool IsPressed(uint16_t buttons, uint16_t previousButtons, uint16_t mask) noexcept {
    return (buttons & mask) != 0 && (previousButtons & mask) == 0;
}
} // namespace

AppGamepadStick AppGamepadInput::ApplyRadialDeadZone(
    int16_t x,
    int16_t y,
    int16_t deadZone) noexcept {
    const float rawX = x < 0
        ? static_cast<float>(x) / 32768.0f
        : static_cast<float>(x) / 32767.0f;
    const float rawY = y < 0
        ? static_cast<float>(y) / 32768.0f
        : static_cast<float>(y) / 32767.0f;
    const float rawMagnitude = std::sqrt(rawX * rawX + rawY * rawY);
    const float normalizedDeadZone =
        (std::clamp)(static_cast<float>(deadZone) / 32767.0f, 0.0f, 0.95f);
    if (rawMagnitude <= normalizedDeadZone || rawMagnitude <= 0.00001f) {
        return {};
    }

    const float directionX = rawX / rawMagnitude;
    const float directionY = rawY / rawMagnitude;
    const float magnitude = (std::clamp)(
        (rawMagnitude - normalizedDeadZone) / (1.0f - normalizedDeadZone),
        0.0f,
        1.0f);
    return {
        directionX * magnitude,
        directionY * magnitude,
        magnitude,
    };
}

AppGamepadStick AppGamepadInput::ClampUnitCircle(
    float x,
    float y) noexcept {
    const float rawMagnitude = std::sqrt(x * x + y * y);
    if (!std::isfinite(rawMagnitude) || rawMagnitude <= 0.00001f) {
        return {};
    }
    if (rawMagnitude <= 1.0f) {
        return {x, y, rawMagnitude};
    }
    return {x / rawMagnitude, y / rawMagnitude, 1.0f};
}

bool AppGamepadInput::SetVibration(
    uint32_t controllerIndex,
    float lowFrequencyMotor,
    float highFrequencyMotor) noexcept {
    if (controllerIndex >= kMaxXInputControllers) {
        return false;
    }
    XINPUT_VIBRATION vibration{};
    vibration.wLeftMotorSpeed = static_cast<WORD>(std::lround(
        (std::clamp)(lowFrequencyMotor, 0.0f, 1.0f) * 65535.0f));
    vibration.wRightMotorSpeed = static_cast<WORD>(std::lround(
        (std::clamp)(highFrequencyMotor, 0.0f, 1.0f) * 65535.0f));
    return XInputSetState(controllerIndex, &vibration) == ERROR_SUCCESS;
}

AppGamepadFrame AppGamepadInput::Poll() noexcept {
    AppGamepadFrame frame{};
    XINPUT_STATE state{};
    uint32_t resolvedController = UINT32_MAX;

    if (activeControllerIndex_ < kMaxXInputControllers &&
        XInputGetState(activeControllerIndex_, &state) == ERROR_SUCCESS) {
        resolvedController = activeControllerIndex_;
    } else {
        for (uint32_t controllerIndex = 0;
             controllerIndex < kMaxXInputControllers;
             ++controllerIndex) {
            state = {};
            if (XInputGetState(controllerIndex, &state) == ERROR_SUCCESS) {
                resolvedController = controllerIndex;
                break;
            }
        }
    }

    if (resolvedController == UINT32_MAX) {
        frame.justDisconnected = wasConnected_;
        activeControllerIndex_ = UINT32_MAX;
        previousButtons_ = 0;
        wasConnected_ = false;
        return frame;
    }

    const bool newlyConnected =
        !wasConnected_ || activeControllerIndex_ != resolvedController;
    activeControllerIndex_ = resolvedController;
    frame.connected = true;
    frame.justConnected = newlyConnected;
    frame.controllerIndex = resolvedController;
    frame.leftStick = ApplyRadialDeadZone(
        state.Gamepad.sThumbLX,
        state.Gamepad.sThumbLY,
        XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE);
    frame.rightStick = ApplyRadialDeadZone(
        state.Gamepad.sThumbRX,
        state.Gamepad.sThumbRY,
        XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE);

    const uint16_t buttons = state.Gamepad.wButtons;
    frame.boostHeld = (buttons & XINPUT_GAMEPAD_LEFT_THUMB) != 0;
    const uint16_t edgeBaseline = newlyConnected ? buttons : previousButtons_;
    frame.toggleSkeletonPressed =
        IsPressed(buttons, edgeBaseline, XINPUT_GAMEPAD_A);
    frame.nextAnimationPressed =
        IsPressed(buttons, edgeBaseline, XINPUT_GAMEPAD_X);
    frame.resetPressed =
        IsPressed(buttons, edgeBaseline, XINPUT_GAMEPAD_B);
    frame.toggleHelpPressed =
        IsPressed(buttons, edgeBaseline, XINPUT_GAMEPAD_Y);
    frame.leftShoulderPressed =
        IsPressed(buttons, edgeBaseline, XINPUT_GAMEPAD_LEFT_SHOULDER);
    frame.rightShoulderPressed =
        IsPressed(buttons, edgeBaseline, XINPUT_GAMEPAD_RIGHT_SHOULDER);
    previousButtons_ = buttons;
    wasConnected_ = true;
    return frame;
}
