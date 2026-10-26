#pragma once

#include <array>
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include "terrain/TerrainVolumeField.h"

// Four persistent world-space opening probes. Rebuild only the bin entering
// the local window; camera motion must not repeat terrain queries per actor.
class CaveOpeningLighting final {
public:
    const std::array<Vector4, 4>& Update(const RailPath& rail,
        const TerrainGenerationSettings& settings, float distance,
        const TerrainEditLayer* edits = nullptr, const TerrainEditLayer* preview = nullptr,
        bool sealedStart = false) {
        uint64_t signature = 1469598103934665603ull;
        const auto mix = [&](uint32_t value) { signature = (signature ^ value) * 1099511628211ull; };
        mix(settings.seed);
        mix(sealedStart ? 1u : 0u);
        for (float value : {settings.chunkLength, settings.corridorRadius, settings.canyonHalfWidth,
            settings.wallHeight, settings.volumeRoughness, settings.volumeArchScale, settings.archDensity,
            settings.sdfCarveDensity, settings.sdfCarveStrength, settings.sdfCarveScale,
            settings.openingSilhouetteStrength, settings.openingSilhouetteScale,
            settings.openCanyonStartDistance, settings.openCanyonTransitionLength, settings.openCanyonStrength,
            settings.motherRockErosionStrength, settings.largeScaleErosionStrength, settings.surfaceBreakupDensity})
            mix(std::bit_cast<uint32_t>(value));
        for (const auto& p : rail.ControlPoints()) {
            for (float value : {p.position.x, p.position.y, p.position.z, p.corridorRadius,
                p.incomingTangent.x, p.incomingTangent.y, p.incomingTangent.z,
                p.outgoingTangent.x, p.outgoingTangent.y, p.outgoingTangent.z}) mix(std::bit_cast<uint32_t>(value));
            mix(static_cast<uint32_t>(p.tangentMode));
        }
        mix(edits ? edits->Revision() : 0);
        mix(preview ? preview->Revision() : 0);
        if (signature != signature_) {
            signature_ = signature;
            for (auto& entry : entries_) entry.bin = (std::numeric_limits<int32_t>::min)();
        }
        const int32_t center = static_cast<int32_t>(std::floor((std::max)(0.0f, distance) / kBinLength));
        const TerrainVolumeField field(rail, settings, edits, preview);
        for (int32_t bin = center - 2; bin <= center + 1; ++bin) {
            const uint32_t slot = static_cast<uint32_t>((bin % 4 + 4) % 4);
            auto& entry = entries_[slot];
            if (entry.bin != bin) {
                entry.bin = bin;
                entry.opening = {};
                ++buildCount_;
                if (bin == -1 && !sealedStart) {
                    // The course begins at an open tunnel cross-section.
                    const auto p = rail.Evaluate(0);
                    entry.opening = {p.position.x - p.tangent.x * 18 + p.up.x * 8,
                        p.position.y - p.tangent.y * 18 + p.up.y * 8,
                        p.position.z - p.tangent.z * 18 + p.up.z * 8, 52.0f};
                } else if (settings.openCanyonStrength > 0.0f &&
                    static_cast<int32_t>(settings.openCanyonStartDistance / kBinLength) == bin) {
                    // The main exit is a broad aperture, not a small roof
                    // crack. Let its diffuse light reach back into the tunnel.
                    const auto p = rail.Evaluate((std::min)(rail.Length(),
                        settings.openCanyonStartDistance + settings.openCanyonTransitionLength * 0.18f));
                    entry.opening = {p.position.x + p.up.x * 8, p.position.y + p.up.y * 8,
                        p.position.z + p.up.z * 8,
                        (std::clamp)(settings.corridorRadius * 5.2f, 72.0f, 112.0f)};
                } else if (bin >= 0 && bin * kBinLength < rail.Length()) {
                    float best = 0.46f, bestDistance = 0, bestAngle = 0;
                    for (uint32_t along = 0; along < 9; ++along) {
                        const float d = (std::min)(rail.Length(), (bin + (along + 0.5f) / 9.0f) * kBinLength);
                        for (float angle : {0.25f, 0.65f, 1.15f, 1.57f, 1.99f, 2.49f, 2.89f}) {
                            const float mask = field.OpeningMask(d, angle);
                            if (mask > best) { best = mask; bestDistance = d; bestAngle = angle; }
                        }
                    }
                    if (best > 0.46f) {
                        Vector3 normal;
                        const auto p = field.SurfacePoint(bestDistance, bestAngle, &normal);
                        entry.opening = {p.x - normal.x * 0.8f, p.y - normal.y * 0.8f,
                            p.z - normal.z * 0.8f,
                            (std::clamp)(settings.corridorRadius * (2.1f + best * 0.5f), 24.0f, 64.0f)};
                    }
                }
            }
            openings_[slot] = entry.opening;
        }
        return openings_;
    }
    uint64_t BuildCount() const { return buildCount_; }
private:
    static constexpr float kBinLength = 80.0f;
    struct Entry { int32_t bin = (std::numeric_limits<int32_t>::min)(); Vector4 opening{}; };
    std::array<Entry, 4> entries_{};
    std::array<Vector4, 4> openings_{};
    uint64_t signature_ = 0, buildCount_ = 0;
};
