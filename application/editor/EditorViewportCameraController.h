#pragma once

#include <cstdint>
#include <string>

#include "EditorViewportCameraInput.h"
#include "utils/math/MathUtils.h"

namespace editor {

struct EditorViewportCameraSettings {
    float moveSpeed = 12.0f;
    float rotationSensitivity = 0.003f;
    float fastMoveMultiplier = 4.0f;
    float slowMoveMultiplier = 0.25f;
    float maximumDeltaTime = 0.1f;
    float maximumPitch = 1.55334306f; // 89 degrees.

    bool Validate(std::string* errorMessage = nullptr) const;
};

class EditorViewportCameraController {
public:
    void Initialize(
        const Transform& transform,
        float fovY,
        float aspectRatio,
        float nearZ,
        float farZ);
    void SetTransform(const Transform& transform);
    void SetLens(float fovY, float aspectRatio, float nearZ, float farZ);
    bool SetSettings(const EditorViewportCameraSettings& settings,
        std::string* errorMessage = nullptr);
    void Update(const EditorViewportCameraInput& input);

    bool Initialized() const { return initialized_; }
    const Transform& CameraTransform() const { return transform_; }
    const Vector3& WorldPosition() const { return transform_.translate; }
    const Matrix4x4& ViewMatrix() const { return viewMatrix_; }
    const Matrix4x4& ProjectionMatrix() const { return projectionMatrix_; }
    const Matrix4x4& ViewProjectionMatrix() const { return viewProjectionMatrix_; }
    const EditorViewportCameraSettings& Settings() const { return settings_; }
    uint32_t Revision() const { return revision_; }

    Vector3 Forward() const;
    Vector3 Right() const;

private:
    void RebuildMatrices();

    Transform transform_{{1.0f, 1.0f, 1.0f}, {}, {0.0f, 0.0f, -8.0f}};
    EditorViewportCameraSettings settings_{};
    float fovY_ = 0.785398163f;
    float aspectRatio_ = 16.0f / 9.0f;
    float nearZ_ = 0.1f;
    float farZ_ = 1000.0f;
    Matrix4x4 viewMatrix_{};
    Matrix4x4 projectionMatrix_{};
    Matrix4x4 viewProjectionMatrix_{};
    uint32_t revision_ = 0;
    bool initialized_ = false;
};

} // namespace editor
