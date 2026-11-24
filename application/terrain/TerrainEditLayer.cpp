#include "TerrainEditLayer.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <unordered_set>

namespace {
constexpr float kPi = 3.14159265358979323846f;
constexpr float kTau = 6.28318530717958647692f;

float WrappedAngleDelta(float lhs, float rhs) noexcept {
    float delta = std::fmod(lhs - rhs, kTau);
    if (delta > kPi) delta -= kTau;
    if (delta < -kPi) delta += kTau;
    return delta;
}

bool Finite(float value) noexcept {
    return std::isfinite(value);
}

float StampWeight(const TerrainBrushStamp& stamp, float distance, float angle) noexcept {
    const float along = distance - stamp.distance;
    const float around = WrappedAngleDelta(angle, stamp.angle) * stamp.surfaceRadius;
    const float radius = (std::max)(stamp.radius, 0.001f);
    const float normalized = std::sqrt(along * along + around * around) / radius;
    if (normalized >= 1.0f) return 0.0f;
    const float hardness = (std::clamp)(stamp.hardness, 0.0f, 0.95f);
    if (normalized <= hardness) return 1.0f;
    const float t = (normalized - hardness) / (1.0f - hardness);
    const float smooth = t * t * (3.0f - 2.0f * t);
    return 1.0f - smooth;
}

uint64_t Mix(uint64_t hash, uint64_t value) noexcept {
    hash ^= value + 0x9e3779b97f4a7c15ull + (hash << 6u) + (hash >> 2u);
    return hash;
}

uint64_t FloatBits(float value) noexcept {
    uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

uint64_t HashText(std::string_view value) noexcept {
    uint64_t hash = 1469598103934665603ull;
    for (const char character : value) {
        hash ^= static_cast<unsigned char>(character);
        hash *= 1099511628211ull;
    }
    return hash;
}

bool ValidateStamp(const TerrainBrushStamp& stamp, std::string* errorMessage) {
    const auto fail = [&](const char* message) {
        if (errorMessage != nullptr) *errorMessage = message;
        return false;
    };
    if (stamp.strokeGuid.empty() || stamp.stampGuid.empty()) {
        return fail("Terrain stamp requires stable stroke and stamp GUIDs.");
    }
    if (!Finite(stamp.distance) || stamp.distance < 0.0f ||
        !Finite(stamp.angle) || !Finite(stamp.radius) || stamp.radius < 0.05f ||
        stamp.radius > 256.0f || !Finite(stamp.surfaceRadius) ||
        stamp.surfaceRadius < 1.0f || stamp.surfaceRadius > 4096.0f ||
        !Finite(stamp.strength) || std::abs(stamp.strength) > 64.0f ||
        !Finite(stamp.hardness) || stamp.hardness < 0.0f || stamp.hardness > 0.95f) {
        return fail("Terrain stamp contains an invalid bounded numeric value.");
    }
    if (stamp.operation == TerrainEditOperation::Paint && stamp.materialLayer >= 4u) {
        return fail("Terrain paint material layer must be in [0, 3].");
    }
    return true;
}
} // namespace

float TerrainEditEvaluation::MaterialVariation() const noexcept {
    float total = 0.0f;
    float weighted = 0.0f;
    for (uint32_t index = 0; index < 4u; ++index) {
        const float value = (std::max)(0.0f, paintWeights[index]);
        total += value;
        weighted += value * static_cast<float>(index);
    }
    if (total <= 0.00001f) return -1.0f;
    return (std::clamp)(weighted / (total * 3.0f), 0.0f, 1.0f);
}

bool TerrainEditLayer::ApplyStroke(
    const std::vector<TerrainBrushStamp>& stamps,
    std::string* errorMessage) {
    if (stamps.empty() || stamps.size() > kMaxStrokeStamps) {
        if (errorMessage != nullptr) {
            *errorMessage = "Terrain stroke must contain between 1 and 256 stamps.";
        }
        return false;
    }
    if (stamps_.size() + stamps.size() > kMaxStamps) {
        if (errorMessage != nullptr) *errorMessage = "Terrain edit layer exceeds 8192 stamps.";
        return false;
    }
    const std::string& strokeGuid = stamps.front().strokeGuid;
    if (std::any_of(stamps_.begin(), stamps_.end(), [&](const TerrainBrushStamp& value) {
            return value.strokeGuid == strokeGuid;
        })) {
        if (errorMessage != nullptr) *errorMessage = "Terrain stroke GUID already exists.";
        return false;
    }
    std::unordered_set<std::string> stampGuids;
    for (const TerrainBrushStamp& stamp : stamps) {
        if (stamp.strokeGuid != strokeGuid || !ValidateStamp(stamp, errorMessage) ||
            !stampGuids.insert(stamp.stampGuid).second) {
            if (errorMessage != nullptr && errorMessage->empty()) {
                *errorMessage = "Terrain stroke contains mixed or duplicate stable identities.";
            }
            return false;
        }
    }
    stamps_.insert(stamps_.end(), stamps.begin(), stamps.end());
    ++revision_;
    return true;
}

bool TerrainEditLayer::RemoveStroke(
    std::string_view strokeGuid,
    std::string* errorMessage) {
    const std::size_t before = stamps_.size();
    std::erase_if(stamps_, [&](const TerrainBrushStamp& stamp) {
        return stamp.strokeGuid == strokeGuid;
    });
    if (stamps_.size() == before) {
        if (errorMessage != nullptr) *errorMessage = "Terrain stroke GUID was not found.";
        return false;
    }
    ++revision_;
    return true;
}

bool TerrainEditLayer::ReplaceFromSnapshot(
    const TerrainEditLayer& snapshot,
    std::string* errorMessage) {
    if (!snapshot.Validate(errorMessage)) {
        return false;
    }
    stamps_ = snapshot.stamps_;
    ++revision_;
    return true;
}

void TerrainEditLayer::Clear() {
    if (stamps_.empty()) return;
    stamps_.clear();
    ++revision_;
}

TerrainEditEvaluation TerrainEditLayer::Evaluate(float distance, float angle) const noexcept {
    TerrainEditEvaluation result{};
    for (std::size_t index = 0; index < stamps_.size();) {
        const TerrainBrushStamp& stamp = stamps_[index];
        if (stamp.operation == TerrainEditOperation::CloseCeiling) {
            ++index;
            continue;
        }
        if (stamp.operation == TerrainEditOperation::Smooth) {
            const std::string& strokeGuid = stamp.strokeGuid;
            float weightedCorrection = 0.0f;
            float accumulatedWeight = 0.0f;
            do {
                const TerrainBrushStamp& smoothStamp = stamps_[index];
                const float weight = StampWeight(smoothStamp, distance, angle);
                if (weight > 0.0f) {
                    weightedCorrection += smoothStamp.strength * weight;
                    accumulatedWeight += weight;
                }
                ++index;
            } while (index < stamps_.size() &&
                stamps_[index].operation == TerrainEditOperation::Smooth &&
                stamps_[index].strokeGuid == strokeGuid);

            if (accumulatedWeight > 0.0f) {
                // A Smooth stroke is one filtered field update. Normalizing the
                // overlapping brush basis prevents dense pointer samples from
                // amplifying the correction beyond the immutable source pass.
                const float coverage = (std::min)(accumulatedWeight, 1.0f);
                result.radialOffset +=
                    (weightedCorrection / accumulatedWeight) * coverage;
            }
            continue;
        }

        const float weight = StampWeight(stamp, distance, angle);
        if (weight > 0.0f && stamp.operation == TerrainEditOperation::Paint) {
            result.paintWeights[stamp.materialLayer] +=
                (std::max)(0.0f, stamp.strength) * weight;
        } else if (weight > 0.0f) {
            result.radialOffset += stamp.strength * weight;
        }
        ++index;
    }
    result.radialOffset = (std::clamp)(result.radialOffset, -64.0f, 64.0f);
    return result;
}

float TerrainEditLayer::CeilingClosure(float distance) const noexcept {
    float closure = 0.0f;
    for (const auto& stamp : stamps_) {
        if (stamp.operation != TerrainEditOperation::CloseCeiling) continue;
        const float weight = StampWeight(stamp, distance, stamp.angle);
        closure = (std::max)(closure, weight * std::clamp(stamp.strength, 0.0f, 1.0f));
    }
    return closure;
}

TerrainEditDirtyRegion TerrainEditLayer::DirtyRegionForStroke(
    std::string_view strokeGuid) const noexcept {
    TerrainEditDirtyRegion region{};
    for (const TerrainBrushStamp& stamp : stamps_) {
        if (stamp.strokeGuid != strokeGuid) continue;
        const float minimum = stamp.distance - stamp.radius;
        const float maximum = stamp.distance + stamp.radius;
        if (!region.valid) {
            region = {minimum, maximum, true};
        } else {
            region.minDistance = (std::min)(region.minDistance, minimum);
            region.maxDistance = (std::max)(region.maxDistance, maximum);
        }
    }
    return region;
}

TerrainEditDirtyRegion TerrainEditLayer::DirtyRegionFor(
    const std::vector<TerrainBrushStamp>& stamps) const noexcept {
    TerrainEditDirtyRegion region{};
    for (const TerrainBrushStamp& stamp : stamps) {
        const float minimum = stamp.distance - stamp.radius;
        const float maximum = stamp.distance + stamp.radius;
        if (!region.valid) region = {minimum, maximum, true};
        else {
            region.minDistance = (std::min)(region.minDistance, minimum);
            region.maxDistance = (std::max)(region.maxDistance, maximum);
        }
    }
    return region;
}

uint64_t TerrainEditLayer::ContentHashForRange(float begin, float end) const noexcept {
    uint64_t hash = 1469598103934665603ull;
    for (const TerrainBrushStamp& stamp : stamps_) {
        if (stamp.distance + stamp.radius < begin || stamp.distance - stamp.radius > end) continue;
        hash = Mix(hash, HashText(stamp.strokeGuid));
        hash = Mix(hash, HashText(stamp.stampGuid));
        hash = Mix(hash, static_cast<uint32_t>(stamp.operation));
        hash = Mix(hash, FloatBits(stamp.distance));
        hash = Mix(hash, FloatBits(stamp.angle));
        hash = Mix(hash, FloatBits(stamp.radius));
        hash = Mix(hash, FloatBits(stamp.surfaceRadius));
        hash = Mix(hash, FloatBits(stamp.strength));
        hash = Mix(hash, FloatBits(stamp.hardness));
        hash = Mix(hash, stamp.materialLayer);
    }
    return hash;
}

TerrainEditLayer TerrainEditLayer::Filtered(float begin, float end) const {
    TerrainEditLayer result{};
    for (const TerrainBrushStamp& stamp : stamps_) {
        if (stamp.distance + stamp.radius >= begin && stamp.distance - stamp.radius <= end) {
            result.stamps_.push_back(stamp);
        }
    }
    result.revision_ = revision_;
    return result;
}

bool TerrainEditLayer::Validate(std::string* errorMessage) const {
    if (stamps_.size() > kMaxStamps) {
        if (errorMessage != nullptr) *errorMessage = "Terrain edit layer exceeds its stamp budget.";
        return false;
    }
    std::unordered_set<std::string> stampGuids;
    std::unordered_set<std::string> closedStrokes;
    std::string currentStroke;
    for (const TerrainBrushStamp& stamp : stamps_) {
        if (!ValidateStamp(stamp, errorMessage) || !stampGuids.insert(stamp.stampGuid).second) {
            if (errorMessage != nullptr && errorMessage->empty()) {
                *errorMessage = "Terrain edit layer contains duplicate stamp GUIDs.";
            }
            return false;
        }
        if (stamp.strokeGuid != currentStroke) {
            if (!currentStroke.empty()) closedStrokes.insert(currentStroke);
            if (closedStrokes.contains(stamp.strokeGuid)) {
                if (errorMessage != nullptr) *errorMessage = "Terrain stroke stamps must be contiguous.";
                return false;
            }
            currentStroke = stamp.strokeGuid;
        }
    }
    return true;
}

const char* ToString(TerrainEditOperation operation) noexcept {
    switch (operation) {
    case TerrainEditOperation::Sculpt: return "Sculpt";
    case TerrainEditOperation::Smooth: return "Smooth";
    case TerrainEditOperation::Flatten: return "Flatten";
    case TerrainEditOperation::Paint: return "Paint";
    case TerrainEditOperation::CloseCeiling: return "CloseCeiling";
    }
    return "Sculpt";
}

bool ParseTerrainEditOperation(
    std::string_view text,
    TerrainEditOperation& operation) noexcept {
    if (text == "Sculpt") operation = TerrainEditOperation::Sculpt;
    else if (text == "Smooth") operation = TerrainEditOperation::Smooth;
    else if (text == "Flatten") operation = TerrainEditOperation::Flatten;
    else if (text == "Paint") operation = TerrainEditOperation::Paint;
    else if (text == "CloseCeiling") operation = TerrainEditOperation::CloseCeiling;
    else return false;
    return true;
}

