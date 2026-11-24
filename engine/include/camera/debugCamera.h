#pragma once

#include "camera/VfxCamera.h"
#include "utils/math/MathUtils.h"

class DebugCamera {
public:
    void Initialize();
    void Update();

    Matrix4x4 GetViewMatrix() const { return camera_.GetViewMatrix(); }
    Matrix4x4 GetProjectionMatrix() const { return camera_.GetProjectionMatrix(); }
    Matrix4x4 GetViewProjectionMatrix() const { return camera_.GetViewProjectionMatrix(); }
    const Vector3& GetWorldPosition() const { return camera_.GetPosition(); }
    const Transform& GetTransform() const { return camera_.GetTransform(); }
    void SetTransform(const Transform& transform);
    void SetLens(float fovY, float aspectRatio, float nearZ, float farZ);
    void SetInputEnabled(bool enabled) { inputEnabled_ = enabled; }
    void SetMoveSpeed(float speed) { moveSpeed_ = speed; }
    void SetRotateSpeed(float speed) { rotateSpeed_ = speed; }
    void SetSpeedMultipliers(float slowMultiplier, float fastMultiplier);

private:
    VfxCamera camera_{};
    bool inputEnabled_ = true;
    float moveSpeed_ = 0.65f;
    float rotateSpeed_ = 0.02f;
    float slowMoveMultiplier_ = 0.25f;
    float fastMoveMultiplier_ = 6.0f;
};
