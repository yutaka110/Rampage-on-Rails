#include "camera/DebugCamera.h"

#include <algorithm>
#include <Windows.h>
#include <numbers>

void DebugCamera::Initialize() {
    Transform transform{};
    transform.scale = {1.0f, 1.0f, 1.0f};
    transform.rotate = {0.0f, 0.0f, 0.0f};
    transform.translate = {0.0f, 0.0f, -8.0f};
    camera_.Initialize(
        transform,
        0.25f * std::numbers::pi_v<float>,
        16.0f / 9.0f,
        0.1f,
        1000.0f);
}

void DebugCamera::SetTransform(const Transform& transform) {
    camera_.SetTransform(transform);
}

void DebugCamera::SetLens(float fovY, float aspectRatio, float nearZ, float farZ) {
    camera_.SetLens(fovY, aspectRatio, nearZ, farZ);
}

void DebugCamera::SetSpeedMultipliers(float slowMultiplier, float fastMultiplier) {
    slowMoveMultiplier_ = (std::max)(0.01f, slowMultiplier);
    fastMoveMultiplier_ = (std::max)(1.0f, fastMultiplier);
}

void DebugCamera::Update() {
    Transform transform = camera_.GetTransform();
    if (inputEnabled_) {
        float moveSpeed = (std::max)(0.0f, moveSpeed_);
        if (GetAsyncKeyState(VK_SHIFT) & 0x8000) {
            moveSpeed *= fastMoveMultiplier_;
        }
        if (GetAsyncKeyState(VK_CONTROL) & 0x8000) {
            moveSpeed *= slowMoveMultiplier_;
        }
        const float rotateSpeed = (std::max)(0.0f, rotateSpeed_);

        if (GetAsyncKeyState('W') & 0x8000) {
            transform.translate.z += moveSpeed;
        }
        if (GetAsyncKeyState('S') & 0x8000) {
            transform.translate.z -= moveSpeed;
        }
        if (GetAsyncKeyState('D') & 0x8000) {
            transform.translate.x += moveSpeed;
        }
        if (GetAsyncKeyState('A') & 0x8000) {
            transform.translate.x -= moveSpeed;
        }
        if ((GetAsyncKeyState('E') & 0x8000) || (GetAsyncKeyState(VK_SPACE) & 0x8000)) {
            transform.translate.y += moveSpeed;
        }
        if ((GetAsyncKeyState('Q') & 0x8000) || (GetAsyncKeyState('C') & 0x8000)) {
            transform.translate.y -= moveSpeed;
        }
        if (GetAsyncKeyState(VK_UP) & 0x8000) {
            transform.rotate.x += rotateSpeed;
        }
        if (GetAsyncKeyState(VK_DOWN) & 0x8000) {
            transform.rotate.x -= rotateSpeed;
        }
        if (GetAsyncKeyState(VK_RIGHT) & 0x8000) {
            transform.rotate.y += rotateSpeed;
        }
        if (GetAsyncKeyState(VK_LEFT) & 0x8000) {
            transform.rotate.y -= rotateSpeed;
        }
    }

    transform.scale = {1.0f, 1.0f, 1.0f};
    SetTransform(transform);
}
