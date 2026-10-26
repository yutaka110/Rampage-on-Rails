#pragma once

#include "../../course/CourseAsset.h"
#include "../EditorViewportCameraController.h"

#include <algorithm>
#include <cmath>
#include <DirectXMath.h>

namespace editor {

// A temporary camera: navigation never mutates the course. Capture converts
// the displayed world pose into the same rail basis used by RailCameraDirector.
class CourseCameraCompositionController final {
public:
    void Begin(bool hideVehicle) {
        active_ = true;
        ready_ = false;
        hideVehicle_ = hideVehicle;
    }
    void Stop() { active_ = ready_ = false; }
    bool Active() const { return active_; }
    bool Ready() const { return active_ && ready_; }
    bool Changed() const {
        if (!Ready()) return false;
        const auto& pose = camera_.CameraTransform();
        const auto different = [](float a, float b) { return std::abs(a-b) > 0.0001f; };
        return different(pose.translate.x, initialPose_.translate.x) ||
            different(pose.translate.y, initialPose_.translate.y) ||
            different(pose.translate.z, initialPose_.translate.z) ||
            different(pose.rotate.x, initialPose_.rotate.x) ||
            different(pose.rotate.y, initialPose_.rotate.y) ||
            different(fovY_, initialFov_) || hideVehicle_ != initialHideVehicle_;
    }
    bool HideVehicle() const { return hideVehicle_; }
    void SetHideVehicle(bool hide) { hideVehicle_ = hide; }
    float FovY() const { return fovY_; }
    void SetFovY(float fov) {
        if (std::isfinite(fov)) fovY_ = (std::clamp)(fov, 0.261799388f, 1.91986218f);
    }
    const EditorViewportCameraController& Camera() const { return camera_; }
    const Matrix4x4& ViewMatrix() const { return view_; }
    const Matrix4x4& ViewProjectionMatrix() const { return viewProjection_; }
    float Distance() const { return sample_.distance; }
    bool HasPose() const { return hasSource_ || Ready(); }
    Transform DisplayPose() const { return Ready() ? camera_.CameraTransform() : source_; }
    Vector3 Eye() const { return DisplayPose().translate; }
    Vector3 Forward() const {
        if (Ready()) return camera_.Forward();
        const auto rotation = source_.rotate;
        return {std::cos(rotation.x)*std::sin(rotation.y), -std::sin(rotation.x),
            std::cos(rotation.x)*std::cos(rotation.y)};
    }
    Vector3 Target() const {
        const auto eye = Eye(), f = Forward();
        return {eye.x + f.x*focusDistance_, eye.y + f.y*focusDistance_, eye.z + f.z*focusDistance_};
    }
    Vector3 RailUp() const { return sample_.up; }
    float AspectRatio() const { return aspect_; }
    float DisplayFovY() const { return Ready() ? fovY_ : sourceFov_; }

    // Map handles own this temporary pose only. Moving the eye retains the
    // target; moving the target changes the viewing direction immediately.
    bool SetWorldPose(Vector3 eye, Vector3 target, std::string* error = nullptr) {
        const Vector3 delta{target.x-eye.x, target.y-eye.y, target.z-eye.z};
        const float length = std::sqrt(Dot(delta, delta));
        if (!Ready() || !std::isfinite(length) || length < 0.1f || length > 2000.0f ||
            !std::isfinite(eye.x) || !std::isfinite(eye.y) || !std::isfinite(eye.z)) {
            if (error) *error = "Camera and target need finite, distinct positions within editing range.";
            return false;
        }
        const float pitch = -std::atan2(delta.y, std::hypot(delta.x, delta.z));
        if (std::abs(pitch) > camera_.Settings().maximumPitch) {
            if (error) *error = "Camera direction is too close to vertical.";
            return false;
        }
        const auto original = camera_.CameraTransform();
        auto pose = original;
        pose.translate = eye;
        pose.rotate = {pitch, std::atan2(delta.x, delta.z), 0};
        camera_.SetTransform(pose);
        CourseCameraViewDefinition candidate;
        if (!Capture(candidate, error)) { camera_.SetTransform(original); return false; }
        focusDistance_ = length;
        if (error) error->clear();
        return true;
    }

    void Update(const EditorViewportCameraInput& input, const Transform& source,
        float sourceFov, float aspect, float nearZ, float farZ,
        const RailPathSample& sample, bool pausedCameraPreview) {
        source_ = source;
        sourceFov_ = sourceFov;
        hasSource_ = true;
        aspect_ = aspect;
        sample_ = sample;
        if (!pausedCameraPreview) { Stop(); return; }
        if (!active_) return;
        if (!ready_) {
            SetFovY(sourceFov);
            camera_.Initialize(source, fovY_, aspect, nearZ, farZ);
            initialPose_ = camera_.CameraTransform();
            initialFov_ = fovY_;
            initialHideVehicle_ = hideVehicle_;
            ready_ = true;
        }
        camera_.SetLens(fovY_, aspect, nearZ, farZ);
        camera_.Update(input);
        if (input.captureActive && std::isfinite(input.wheelDelta))
            Nudge(0, 0, input.wheelDelta * 1.5f);
        if (input.focusSelectionPressed) LookAtVehicle();
        const auto eye = camera_.WorldPosition();
        const auto forward = camera_.Forward();
        const Vector3 target{eye.x + forward.x, eye.y + forward.y, eye.z + forward.z};
        // Keep the horizon relative to the rail, including pitched/banked rails.
        // This is also the up reference of the saved Custom view.
        DirectX::XMFLOAT4X4 view;
        DirectX::XMStoreFloat4x4(&view, DirectX::XMMatrixLookAtLH(
            DirectX::XMVectorSet(eye.x, eye.y, eye.z, 1),
            DirectX::XMVectorSet(target.x, target.y, target.z, 1),
            DirectX::XMVectorSet(sample_.up.x, sample_.up.y, sample_.up.z, 0)));
        for (int r = 0; r < 4; ++r) for (int c = 0; c < 4; ++c) view_.m[r][c] = view.m[r][c];
        viewProjection_ = Multiply(view_, camera_.ProjectionMatrix());
    }

    void Nudge(float right, float up, float forward) {
        if (!Ready()) return;
        auto pose = camera_.CameraTransform();
        const auto r = camera_.Right(), f = camera_.Forward();
        pose.translate.x += r.x * right + f.x * forward;
        pose.translate.y += r.y * right + up + f.y * forward;
        pose.translate.z += r.z * right + f.z * forward;
        camera_.SetTransform(pose);
    }
    void LookAtVehicle() {
        if (!Ready()) return;
        auto pose = camera_.CameraTransform();
        const Vector3 delta{sample_.position.x + sample_.up.x * 4.5f - pose.translate.x,
            sample_.position.y + sample_.up.y * 4.5f - pose.translate.y,
            sample_.position.z + sample_.up.z * 4.5f - pose.translate.z};
        if (Dot(delta, delta) < 0.000001f) return;
        pose.rotate.y = std::atan2(delta.x, delta.z);
        pose.rotate.x = -std::atan2(delta.y, std::hypot(delta.x, delta.z));
        camera_.SetTransform(pose);
    }

    bool Capture(CourseCameraViewDefinition& view, std::string* error = nullptr) const {
        if (!Ready()) {
            if (error) *error = "Open Compose in Viewport before registering a composition.";
            return false;
        }
        auto captured = view;
        captured.mode = CourseCameraViewMode::Custom;
        const auto eye = camera_.WorldPosition();
        const Vector3 delta{eye.x - sample_.position.x, eye.y - sample_.position.y,
            eye.z - sample_.position.z};
        const auto forward = camera_.Forward();
        captured.positionOffset = {Dot(delta, sample_.right), Dot(delta, sample_.up),
            Dot(delta, sample_.tangent)};
        captured.yaw = std::atan2(Dot(forward, sample_.right), Dot(forward, sample_.tangent));
        captured.pitch = std::asin((std::clamp)(Dot(forward, sample_.up), -1.0f, 1.0f));
        captured.fovY = fovY_;
        captured.hideVehicle = hideVehicle_;
        if (!captured.Validate(error)) return false;
        view = captured;
        return true;
    }

private:
    static float Dot(Vector3 a, Vector3 b) { return a.x*b.x + a.y*b.y + a.z*b.z; }
    EditorViewportCameraController camera_{};
    RailPathSample sample_{};
    Matrix4x4 view_{}, viewProjection_{};
    float fovY_ = 1.0471975512f;
    float sourceFov_ = 1.0471975512f;
    float focusDistance_ = 18.0f;
    float aspect_ = 16.0f/9.0f;
    Transform source_{};
    Transform initialPose_{};
    float initialFov_ = 1.0471975512f;
    bool initialHideVehicle_ = false;
    bool hasSource_ = false;
    bool active_ = false, ready_ = false, hideVehicle_ = false;
};

} // namespace editor
