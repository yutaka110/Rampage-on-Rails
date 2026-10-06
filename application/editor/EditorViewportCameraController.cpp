#include "EditorViewportCameraController.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace editor {
namespace {

bool Finite(float value) {
    return std::isfinite(value);
}

float LengthSquared(const Vector3& value) {
    return value.x * value.x + value.y * value.y + value.z * value.z;
}

Vector3 Add(const Vector3& lhs, const Vector3& rhs) {
    return {lhs.x + rhs.x, lhs.y + rhs.y, lhs.z + rhs.z};
}

Vector3 Scale(const Vector3& value, float scale) {
    return {value.x * scale, value.y * scale, value.z * scale};
}

Vector3 NormalizeOrZero(const Vector3& value) {
    const float lengthSquared = LengthSquared(value);
    if (!Finite(lengthSquared) || lengthSquared <= 1.0e-12f) return {};
    return Scale(value, 1.0f / std::sqrt(lengthSquared));
}

} // namespace

void EditorViewportCameraController::Initialize(
    const Transform& transform,
    float fovY,
    float aspectRatio,
    float nearZ,
    float farZ) {
    initialized_ = true;
    SetTransform(transform);
    SetLens(fovY, aspectRatio, nearZ, farZ);
}

void EditorViewportCameraController::SetTransform(const Transform& transform) {
    transform_ = transform;
    transform_.scale = {1.0f, 1.0f, 1.0f};
    if (!Finite(transform_.rotate.x)) transform_.rotate.x = 0.0f;
    if (!Finite(transform_.rotate.y)) transform_.rotate.y = 0.0f;
    transform_.rotate.x = (std::clamp)(
        transform_.rotate.x, -settings_.maximumPitch, settings_.maximumPitch);
    transform_.rotate.y = std::remainder(
        transform_.rotate.y, 2.0f * std::numbers::pi_v<float>);
    transform_.rotate.z = 0.0f;
    if (!Finite(transform_.translate.x)) transform_.translate.x = 0.0f;
    if (!Finite(transform_.translate.y)) transform_.translate.y = 0.0f;
    if (!Finite(transform_.translate.z)) transform_.translate.z = 0.0f;
    RebuildMatrices();
}

void EditorViewportCameraController::SetLens(
    float fovY, float aspectRatio, float nearZ, float farZ) {
    fovY_ = (std::clamp)(Finite(fovY) ? fovY : 0.785398163f, 0.01f, 3.13f);
    aspectRatio_ = (std::max)(Finite(aspectRatio) ? aspectRatio : 1.0f, 0.01f);
    nearZ_ = (std::max)(Finite(nearZ) ? nearZ : 0.1f, 0.001f);
    farZ_ = (std::max)(Finite(farZ) ? farZ : 1000.0f, nearZ_ + 0.001f);
    RebuildMatrices();
}

bool EditorViewportCameraSettings::Validate(std::string* errorMessage) const {
    const bool valid = Finite(moveSpeed) && moveSpeed >= 0.0f && moveSpeed <= 100000.0f &&
        Finite(rotationSensitivity) && rotationSensitivity >= 0.0f && rotationSensitivity <= 1.0f &&
        Finite(fastMoveMultiplier) && fastMoveMultiplier >= 1.0f && fastMoveMultiplier <= 1000.0f &&
        Finite(slowMoveMultiplier) && slowMoveMultiplier >= 0.01f && slowMoveMultiplier <= 1.0f &&
        Finite(maximumDeltaTime) && maximumDeltaTime >= 0.001f && maximumDeltaTime <= 0.25f &&
        Finite(maximumPitch) && maximumPitch >= 0.1f && maximumPitch <= 1.57069633f;
    if (errorMessage != nullptr) {
        *errorMessage = valid ? "" : "Viewport camera settings require finite, bounded speed, sensitivity, time and pitch.";
    }
    return valid;
}

bool EditorViewportCameraController::SetSettings(
    const EditorViewportCameraSettings& settings, std::string* errorMessage) {
    if (!settings.Validate(errorMessage)) return false;
    settings_ = settings;
    SetTransform(transform_);
    return true;
}

void EditorViewportCameraController::Update(const EditorViewportCameraInput& input) {
    if (!initialized_ || !input.captureActive) return;

    bool changed = false;
    if (Finite(input.mouseDeltaX) && Finite(input.mouseDeltaY)) {
        const float yawDelta = input.mouseDeltaX * settings_.rotationSensitivity;
        const float pitchDelta = input.mouseDeltaY * settings_.rotationSensitivity;
        if (yawDelta != 0.0f || pitchDelta != 0.0f) {
            transform_.rotate.y = std::remainder(
                transform_.rotate.y + yawDelta,
                2.0f * std::numbers::pi_v<float>);
            transform_.rotate.x = (std::clamp)(
                transform_.rotate.x + pitchDelta,
                -settings_.maximumPitch,
                settings_.maximumPitch);
            changed = true;
        }
    }

    const float deltaTime = (std::clamp)(
        Finite(input.deltaTime) ? input.deltaTime : 0.0f,
        0.0f,
        settings_.maximumDeltaTime);
    Vector3 movement{};
    if (input.moveForward) movement = Add(movement, Forward());
    if (input.moveBackward) movement = Add(movement, Scale(Forward(), -1.0f));
    if (input.moveRight) movement = Add(movement, Right());
    if (input.moveLeft) movement = Add(movement, Scale(Right(), -1.0f));
    if (input.moveUp) movement.y += 1.0f;
    if (input.moveDown) movement.y -= 1.0f;
    movement = NormalizeOrZero(movement);
    if (deltaTime > 0.0f && LengthSquared(movement) > 0.0f) {
        float speed = settings_.moveSpeed;
        if (input.slowModifier) speed *= settings_.slowMoveMultiplier;
        else if (input.fastModifier) speed *= settings_.fastMoveMultiplier;
        transform_.translate = Add(transform_.translate, Scale(movement, speed * deltaTime));
        changed = true;
    }

    if (changed) {
        ++revision_;
        RebuildMatrices();
    }
}

Vector3 EditorViewportCameraController::Forward() const {
    const float pitchCos = std::cos(transform_.rotate.x);
    return NormalizeOrZero({
        pitchCos * std::sin(transform_.rotate.y),
        -std::sin(transform_.rotate.x),
        pitchCos * std::cos(transform_.rotate.y)});
}

Vector3 EditorViewportCameraController::Right() const {
    return NormalizeOrZero({
        std::cos(transform_.rotate.y),
        0.0f,
        -std::sin(transform_.rotate.y)});
}

void EditorViewportCameraController::RebuildMatrices() {
    Matrix4x4 world = MakeAffineMatrix(
        transform_.scale, transform_.rotate, transform_.translate);
    viewMatrix_ = Inverse(world);
    projectionMatrix_ = MakePerspectiveFovMatrix(fovY_, aspectRatio_, nearZ_, farZ_);
    viewProjectionMatrix_ = Multiply(viewMatrix_, projectionMatrix_);
}

} // namespace editor
