#include "CourseRailTrackMeshBakePipeline.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <utility>

namespace {
Vector3 Add(Vector3 a, Vector3 b) { return {a.x+b.x, a.y+b.y, a.z+b.z}; }
Vector3 Sub(Vector3 a, Vector3 b) { return {a.x-b.x, a.y-b.y, a.z-b.z}; }
Vector3 Scale(Vector3 value, float scale) {
    return {value.x*scale, value.y*scale, value.z*scale};
}
float Dot(Vector3 a, Vector3 b) { return a.x*b.x + a.y*b.y + a.z*b.z; }
Vector3 Cross(Vector3 a, Vector3 b) {
    return {a.y*b.z-a.z*b.y, a.z*b.x-a.x*b.z, a.x*b.y-a.y*b.x};
}
float Length(Vector3 value) { return std::sqrt(Dot(value, value)); }
Vector3 NormalizeOr(Vector3 value, Vector3 fallback) {
    const float length = Length(value);
    return length > 0.00001f ? Scale(value, 1.0f/length) : fallback;
}

Matrix4x4 MakeBasisWorld(
    Vector3 right,
    Vector3 up,
    Vector3 forward,
    Vector3 scale,
    Vector3 position) {
    Matrix4x4 world = MakeIdentity4x4();
    world.m[0][0] = right.x * scale.x;
    world.m[0][1] = right.y * scale.x;
    world.m[0][2] = right.z * scale.x;
    world.m[1][0] = up.x * scale.y;
    world.m[1][1] = up.y * scale.y;
    world.m[1][2] = up.z * scale.y;
    world.m[2][0] = forward.x * scale.z;
    world.m[2][1] = forward.y * scale.z;
    world.m[2][2] = forward.z * scale.z;
    world.m[3][0] = position.x;
    world.m[3][1] = position.y;
    world.m[3][2] = position.z;
    return world;
}

RailPathSample EvaluateOpen(const RailPath& railPath, float distance) {
    const float length = railPath.Length();
    return railPath.Evaluate(distance >= length
        ? std::nextafter(length, 0.0f)
        : (std::max)(0.0f, distance));
}

void HashBytes(uint64_t& hash, const void* bytes, size_t size) {
    const auto* data = static_cast<const uint8_t*>(bytes);
    for (size_t index = 0; index < size; ++index) {
        hash ^= data[index];
        hash *= 1099511628211ull;
    }
}
void HashFloat(uint64_t& hash, float value) {
    const uint32_t bits = std::bit_cast<uint32_t>(value);
    HashBytes(hash, &bits, sizeof(bits));
}
} // namespace

void CourseRailTrackMeshBakePipeline::Reset() {
    result_ = {};
    revision_ = 0;
}

bool CourseRailTrackMeshBakePipeline::Bake(
    const CourseRailTrackDefinitionAsset& definition,
    const RailPath& railPath,
    std::string* errorMessage,
    float rearPresentationLength) {
    CourseRailTrackMeshBakeResult next{};
    next.definition = definition;
    next.assetId = definition.assetId;
    next.railLength = railPath.Length();
    next.revision = ++revision_;
    if (!definition.Validate(errorMessage)) {
        next.status = errorMessage != nullptr ? *errorMessage : "Track definition is invalid.";
        result_ = std::move(next);
        return false;
    }
    if (!definition.enabled) {
        next.valid = true;
        next.status = "Track presentation is disabled by its definition asset.";
        result_ = std::move(next);
        if (errorMessage != nullptr) errorMessage->clear();
        return true;
    }
    if (!std::isfinite(next.railLength) || next.railLength <= 0.01f) {
        if (errorMessage != nullptr) *errorMessage = "Cannot bake track without a valid RailPath.";
        next.status = "Cannot bake track without a valid RailPath.";
        result_ = std::move(next);
        return false;
    }

    const uint32_t railIntervals = (std::max)(1u, static_cast<uint32_t>(
        std::ceil(next.railLength / definition.bakeSegmentLength)));
    const uint32_t sleeperIntervals = static_cast<uint32_t>(
        std::floor(next.railLength / definition.sleeperSpacing)) + 1u;
    const uint32_t supportIntervals = definition.supportsEnabled
        ? static_cast<uint32_t>(std::floor(next.railLength / definition.supportSpacing)) + 1u
        : 0u;
    next.instances.reserve(
        static_cast<size_t>(railIntervals) * 2u + sleeperIntervals +
        static_cast<size_t>(supportIntervals) * 2u);
    const float halfGauge = definition.trackGauge * 0.5f;

    for (uint32_t index = 0; index < railIntervals; ++index) {
        const float startDistance = (std::min)(
            next.railLength, index * definition.bakeSegmentLength);
        const float endDistance = (std::min)(
            next.railLength, (index + 1u) * definition.bakeSegmentLength);
        const RailPathSample start = EvaluateOpen(railPath, startDistance);
        const RailPathSample end = EvaluateOpen(railPath, endDistance);
        for (uint32_t sideIndex = 0; sideIndex < 2; ++sideIndex) {
            const float side = sideIndex == 0 ? -1.0f : 1.0f;
            const Vector3 startHead = Add(
                Add(start.position, Scale(start.right, halfGauge * side)),
                Scale(start.up, definition.railHeadVerticalOffset));
            const Vector3 endHead = Add(
                Add(end.position, Scale(end.right, halfGauge * side)),
                Scale(end.up, definition.railHeadVerticalOffset));
            const Vector3 chord = Sub(endHead, startHead);
            const float chordLength = Length(chord);
            if (chordLength <= 0.0001f) continue;
            const Vector3 forward = Scale(chord, 1.0f/chordLength);
            Vector3 right = NormalizeOr(Add(start.right, end.right), start.right);
            right = NormalizeOr(Sub(right, Scale(forward, Dot(right, forward))), start.right);
            const Vector3 up = NormalizeOr(Cross(forward, right), start.up);
            CourseRailTrackBakedInstance instance{};
            instance.part = sideIndex == 0
                ? CourseRailTrackMeshPart::LeftRail
                : CourseRailTrackMeshPart::RightRail;
            instance.meshId = definition.trackUnitMeshId;
            instance.worldMatrix = MakeBasisWorld(
                right, up, forward,
                {definition.railHeadWidth, definition.railHeadHeight, chordLength},
                Scale(Add(startHead, endHead), 0.5f));
            instance.color = definition.railColor;
            instance.startDistance = startDistance;
            instance.endDistance = endDistance;
            instance.detailOrdinal = index;
            next.instances.push_back(std::move(instance));
            ++next.railSegmentCount;
        }
    }

    for (uint32_t index = 0; index < sleeperIntervals; ++index) {
        const float distance = (std::min)(
            next.railLength, index * definition.sleeperSpacing);
        const RailPathSample sample = EvaluateOpen(railPath, distance);
        CourseRailTrackBakedInstance instance{};
        instance.part = CourseRailTrackMeshPart::Sleeper;
        instance.meshId = definition.trackUnitMeshId;
        instance.worldMatrix = MakeBasisWorld(
            sample.right, sample.up, sample.tangent,
            {definition.sleeperLength, definition.sleeperHeight, definition.sleeperWidth},
            Add(sample.position, Scale(sample.up,
                definition.railHeadVerticalOffset + definition.sleeperVerticalOffset)));
        instance.color = definition.sleeperColor;
        instance.startDistance = distance;
        instance.endDistance = distance;
        instance.detailOrdinal = index;
        next.instances.push_back(std::move(instance));
        ++next.sleeperCount;
    }

    for (uint32_t index = 0; index < supportIntervals; ++index) {
        const float distance = (std::min)(
            next.railLength, index * definition.supportSpacing);
        const RailPathSample sample = EvaluateOpen(railPath, distance);
        for (uint32_t sideIndex = 0; sideIndex < 2; ++sideIndex) {
            const float side = sideIndex == 0 ? -1.0f : 1.0f;
            CourseRailTrackBakedInstance instance{};
            instance.part = sideIndex == 0
                ? CourseRailTrackMeshPart::LeftSupport
                : CourseRailTrackMeshPart::RightSupport;
            instance.meshId = definition.trackUnitMeshId;
            const Vector3 railHead = Add(
                Add(sample.position, Scale(sample.right, halfGauge*side)),
                Scale(sample.up, definition.railHeadVerticalOffset));
            instance.worldMatrix = MakeBasisWorld(
                sample.right, sample.up, sample.tangent,
                {definition.supportWidth, definition.supportHeight, definition.supportDepth},
                Add(railHead, Scale(sample.up,
                    -definition.railHeadHeight*0.5f - definition.supportHeight*0.5f)));
            instance.color = definition.supportColor;
            instance.startDistance = distance;
            instance.endDistance = distance;
            instance.detailOrdinal = index;
            next.instances.push_back(std::move(instance));
            ++next.supportCount;
        }
    }

    // Optional immutable scenery beyond the playable start. Neither RailPath,
    // course markers nor the vehicle's distance are shifted by this extension.
    const float rearLength=std::isfinite(rearPresentationLength)
        ? (std::clamp)(rearPresentationLength,0.0f,240.0f) : 0.0f;
    const auto origin=railPath.Evaluate(0.0f);
    const auto appendRear=[&](CourseRailTrackMeshPart part,float begin,float end,
            float lateral,float vertical,Vector3 scale,Vector4 color,uint32_t ordinal) {
        CourseRailTrackBakedInstance instance;
        instance.part=part; instance.meshId=definition.trackUnitMeshId;
        instance.startDistance=begin;instance.endDistance=end;instance.detailOrdinal=ordinal;
        instance.color=color;
        const auto center=Add(origin.position,Add(Scale(origin.tangent,(begin+end)*0.5f),
            Add(Scale(origin.right,lateral),Scale(origin.up,vertical))));
        instance.worldMatrix=MakeBasisWorld(origin.right,origin.up,origin.tangent,scale,center);
        next.instances.push_back(std::move(instance));
    };
    for(uint32_t i=0;i<static_cast<uint32_t>(std::ceil(rearLength/definition.bakeSegmentLength));++i) {
        const float end=-static_cast<float>(i)*definition.bakeSegmentLength;
        const float begin=-(std::min)(rearLength,(i+1)*definition.bakeSegmentLength);
        for(int side:{-1,1}) {
            appendRear(side<0?CourseRailTrackMeshPart::LeftRail:CourseRailTrackMeshPart::RightRail,
                begin,end,side*halfGauge,definition.railHeadVerticalOffset,
                {definition.railHeadWidth,definition.railHeadHeight,end-begin},definition.railColor,i);
            ++next.railSegmentCount;
        }
    }
    for(uint32_t i=1;i<=static_cast<uint32_t>(std::floor(rearLength/definition.sleeperSpacing));++i) {
        const float d=-static_cast<float>(i)*definition.sleeperSpacing;
        appendRear(CourseRailTrackMeshPart::Sleeper,d,d,0,
            definition.railHeadVerticalOffset+definition.sleeperVerticalOffset,
            {definition.sleeperLength,definition.sleeperHeight,definition.sleeperWidth},definition.sleeperColor,i);
        ++next.sleeperCount;
    }
    std::stable_sort(next.instances.begin(), next.instances.end(),
        [](const CourseRailTrackBakedInstance& a,
           const CourseRailTrackBakedInstance& b) {
            return a.startDistance < b.startDistance;
        });
    uint64_t fingerprint = 1469598103934665603ull;
    HashBytes(fingerprint, definition.assetId.data(), definition.assetId.size());
    HashFloat(fingerprint, definition.trackGauge);
    HashFloat(fingerprint, definition.bakeSegmentLength);
    HashFloat(fingerprint, next.railLength);
    if(rearLength>0.0f) HashFloat(fingerprint,rearLength);
    const RailPathSample first = EvaluateOpen(railPath, 0.0f);
    const RailPathSample last = EvaluateOpen(railPath, next.railLength);
    HashBytes(fingerprint, &first.position, sizeof(first.position));
    HashBytes(fingerprint, &last.position, sizeof(last.position));
    next.sourceFingerprint = fingerprint;
    next.valid = true;
    next.status = "Baked " + std::to_string(next.instances.size()) +
        " immutable track instances.";
    result_ = std::move(next);
    if (errorMessage != nullptr) errorMessage->clear();
    return true;
}
