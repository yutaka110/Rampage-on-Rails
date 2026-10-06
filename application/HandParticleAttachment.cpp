#include "HandParticleAttachment.h"

#include <cmath>

#include "EffectRuntime.h"

namespace {

Vector3 ExtractWorldPosition(const Matrix4x4& matrix) {
    return {matrix.m[3][0], matrix.m[3][1], matrix.m[3][2]};
}

bool IsFinite(const Vector3& value) {
    return std::isfinite(value.x) &&
        std::isfinite(value.y) &&
        std::isfinite(value.z);
}

} // namespace

void HandParticleAttachment::Update(
    const HandParticleAttachmentSettings& settings,
    const Skeleton* skeleton,
    const Matrix4x4& ownerWorldMatrix,
    EffectRuntime& effectRuntime) {
    telemetry_ = {};
    if (!settings.enabled) {
        StopInstance(effectRuntime);
        telemetry_.status = HandParticleAttachmentStatus::Disabled;
        return;
    }
    if (skeleton == nullptr) {
        StopInstance(effectRuntime);
        telemetry_.status = HandParticleAttachmentStatus::MissingSkeleton;
        return;
    }

    if (socketBinding_.jointName != settings.jointName) {
        SetBoneSocketJoint(socketBinding_, settings.jointName);
    }
    socketBinding_.enabled = true;
    socketBinding_.scaleMode = settings.scaleMode;
    socketBinding_.localOffset = settings.socketOffset;

    const BoneSocketPose socketPose = EvaluateBoneSocket(
        socketBinding_,
        *skeleton,
        ownerWorldMatrix);
    telemetry_.socketStatus = socketPose.status;
    telemetry_.sourceJointScale = socketPose.sourceJointScale;
    telemetry_.jointScaleRemoved = socketPose.jointScaleRemoved;
    if (!socketPose.IsValid()) {
        StopInstance(effectRuntime);
        telemetry_.status = HandParticleAttachmentStatus::SocketUnavailable;
        return;
    }

    telemetry_.worldPosition = ExtractWorldPosition(socketPose.worldMatrix);
    if (!IsFinite(telemetry_.worldPosition)) {
        StopInstance(effectRuntime);
        telemetry_.status = HandParticleAttachmentStatus::InvalidWorldTransform;
        return;
    }

    if (activeEffectName_ != settings.effectName) {
        StopInstance(effectRuntime);
    }

    const EffectInstance* instance = effectInstanceId_ != 0
        ? effectRuntime.FindInstance(effectInstanceId_)
        : nullptr;
    if (instance == nullptr) {
        effectInstanceId_ = effectRuntime.PlayEffectWithParams(
            settings.effectName,
            telemetry_.worldPosition,
            settings.color,
            settings.effectScale);
        if (effectInstanceId_ == 0) {
            activeEffectName_.clear();
            telemetry_.status = HandParticleAttachmentStatus::EffectUnavailable;
            return;
        }
        activeEffectName_ = settings.effectName;
        instance = effectRuntime.FindInstance(effectInstanceId_);
    }
    if (instance == nullptr || instance->asset == nullptr) {
        StopInstance(effectRuntime);
        telemetry_.status = HandParticleAttachmentStatus::EffectUnavailable;
        return;
    }

    Transform transform = instance->transform;
    Vector4 color;
    transform.translate = telemetry_.worldPosition;
    transform.scale = {
        instance->asset->size.x * settings.effectScale.x,
        instance->asset->size.y * settings.effectScale.y,
        instance->asset->size.z * settings.effectScale.z,
    };
    color = {
        instance->asset->color.x * settings.color.x,
        instance->asset->color.y * settings.color.y,
        instance->asset->color.z * settings.color.z,
        instance->asset->color.w * settings.color.w,
    };
    if (!effectRuntime.SetInstanceAppearance(effectInstanceId_, transform, color, true)) {
        StopInstance(effectRuntime); telemetry_.status = HandParticleAttachmentStatus::InvalidWorldTransform; return;
    }
    effectRuntime.SetEffectPreviewLoop(effectInstanceId_, true);

    telemetry_.effectInstanceId = effectInstanceId_;
    telemetry_.status = HandParticleAttachmentStatus::Active;
}

void HandParticleAttachment::Stop(EffectRuntime& effectRuntime) {
    StopInstance(effectRuntime);
    telemetry_ = {};
    telemetry_.status = HandParticleAttachmentStatus::Disabled;
}

void HandParticleAttachment::StopInstance(EffectRuntime& effectRuntime) {
    if (effectInstanceId_ != 0) {
        effectRuntime.StopEffect(effectInstanceId_);
    }
    effectInstanceId_ = 0;
    activeEffectName_.clear();
}

const char* HandParticleAttachmentStatusName(
    HandParticleAttachmentStatus status) noexcept {
    switch (status) {
    case HandParticleAttachmentStatus::Disabled:
        return "disabled";
    case HandParticleAttachmentStatus::MissingSkeleton:
        return "missing_skeleton";
    case HandParticleAttachmentStatus::SocketUnavailable:
        return "socket_unavailable";
    case HandParticleAttachmentStatus::InvalidWorldTransform:
        return "invalid_world_transform";
    case HandParticleAttachmentStatus::EffectUnavailable:
        return "effect_unavailable";
    case HandParticleAttachmentStatus::Active:
        return "active";
    }
    return "unknown";
}
