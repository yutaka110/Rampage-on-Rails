#include "EnemyProjectileVisualDefinitionAsset.h"

#include "CourseAssetParsing.h"

#include <cmath>
#include <fstream>
#include <utility>

using namespace course_asset_parsing;

namespace {
void SetError(std::string* errorMessage, std::string message) {
    if (errorMessage != nullptr) *errorMessage = std::move(message);
}
} // namespace

bool EnemyProjectileVisualDefinitionAsset::LoadFromFile(
    const std::string& path,
    std::string* errorMessage) {
    std::ifstream file(path);
    if (!file.is_open()) {
        SetError(errorMessage, "Could not open enemy projectile visual asset: " + path);
        return false;
    }

    EnemyProjectileVisualDefinitionAsset loaded{};
    std::string line;
    unsigned int lineNumber = 0;
    while (std::getline(file, line)) {
        ++lineNumber;
        line = Trim(line);
        if (line.empty() || line[0] == '#') continue;
        const std::vector<std::string> parts = SplitPipe(line);
        if (parts.size() < 28 || parts[0] != "visual") {
            SetError(
                errorMessage,
                "Invalid enemy projectile visual row at line " +
                    std::to_string(lineNumber));
            return false;
        }
        loaded.id = parts[1];
        loaded.projectileDefinitionId = parts[2];
        if (!TryParseEnemyProjectileVisualStyle(parts[3], loaded.style)) {
            SetError(
                errorMessage,
                "Unknown projectile visual style at line " +
                    std::to_string(lineNumber));
            return false;
        }
        loaded.coreEffectId = parts[4];
        loaded.haloEffectId = parts[5];
        loaded.coreColor = {
            ParseFloatOr(parts, 6, loaded.coreColor.x),
            ParseFloatOr(parts, 7, loaded.coreColor.y),
            ParseFloatOr(parts, 8, loaded.coreColor.z),
            ParseFloatOr(parts, 9, loaded.coreColor.w)};
        loaded.haloColor = {
            ParseFloatOr(parts, 10, loaded.haloColor.x),
            ParseFloatOr(parts, 11, loaded.haloColor.y),
            ParseFloatOr(parts, 12, loaded.haloColor.z),
            ParseFloatOr(parts, 13, loaded.haloColor.w)};
        loaded.trailColor = {
            ParseFloatOr(parts, 14, loaded.trailColor.x),
            ParseFloatOr(parts, 15, loaded.trailColor.y),
            ParseFloatOr(parts, 16, loaded.trailColor.z),
            ParseFloatOr(parts, 17, loaded.trailColor.w)};
        loaded.coreRadiusScale = ParseFloatOr(parts, 18, loaded.coreRadiusScale);
        loaded.haloRadiusScale = ParseFloatOr(parts, 19, loaded.haloRadiusScale);
        loaded.minimumAngularRadius = ParseFloatOr(
            parts, 20, loaded.minimumAngularRadius);
        loaded.maximumWorldRadius = ParseFloatOr(
            parts, 21, loaded.maximumWorldRadius);
        loaded.threatRadiusScale = ParseFloatOr(parts, 22, loaded.threatRadiusScale);
        loaded.pulseAmplitude = ParseFloatOr(parts, 23, loaded.pulseAmplitude);
        loaded.pulseFrequencyHz = ParseFloatOr(parts, 24, loaded.pulseFrequencyHz);
        loaded.trailLengthInRadii = ParseFloatOr(
            parts, 25, loaded.trailLengthInRadii);
        loaded.trailWidthScale = ParseFloatOr(parts, 26, loaded.trailWidthScale);
        loaded.enabled = ParseBoolOr(parts, 27, loaded.enabled);
    }
    if (!loaded.Validate(errorMessage)) return false;
    *this = std::move(loaded);
    return true;
}

bool EnemyProjectileVisualDefinitionAsset::Validate(
    std::string* errorMessage) const {
    const auto finiteColor = [](const Vector4& color) {
        return std::isfinite(color.x) && std::isfinite(color.y) &&
            std::isfinite(color.z) && std::isfinite(color.w);
    };
    const bool finite = finiteColor(coreColor) && finiteColor(haloColor) &&
        finiteColor(trailColor) && std::isfinite(coreRadiusScale) &&
        std::isfinite(haloRadiusScale) &&
        std::isfinite(minimumAngularRadius) &&
        std::isfinite(maximumWorldRadius) &&
        std::isfinite(threatRadiusScale) &&
        std::isfinite(pulseAmplitude) &&
        std::isfinite(pulseFrequencyHz) &&
        std::isfinite(trailLengthInRadii) &&
        std::isfinite(trailWidthScale);
    if (id.empty() || projectileDefinitionId.empty() || coreEffectId.empty() ||
        haloEffectId.empty() || !finite || coreRadiusScale <= 0.0f ||
        haloRadiusScale < 1.0f || minimumAngularRadius <= 0.0f ||
        maximumWorldRadius <= 0.0f || threatRadiusScale < 1.0f ||
        pulseAmplitude < 0.0f || pulseAmplitude > 0.75f ||
        pulseFrequencyHz < 0.0f || trailLengthInRadii <= 0.0f ||
        trailWidthScale <= 0.0f || coreColor.w < 0.0f ||
        haloColor.w < 0.0f || trailColor.w < 0.0f) {
        SetError(
            errorMessage,
            "Enemy projectile visual definition contains invalid commercial values.");
        return false;
    }
    return true;
}

EnemyProjectileVisualDefinitionAsset
EnemyProjectileVisualDefinitionAsset::CommercialDefault(
    EnemyProjectileTrajectory trajectory) {
    EnemyProjectileVisualDefinitionAsset result{};
    result.id = "fallback_direct";
    result.projectileDefinitionId = "*";
    switch (trajectory) {
    case EnemyProjectileTrajectory::Direct:
        result.style = EnemyProjectileVisualStyle::Orb;
        break;
    case EnemyProjectileTrajectory::Predictive:
        result.id = "fallback_predictive";
        result.style = EnemyProjectileVisualStyle::Orb;
        break;
    case EnemyProjectileTrajectory::Homing:
        result.id = "fallback_homing";
        result.style = EnemyProjectileVisualStyle::Missile;
        result.haloColor = {0.96f, 0.10f, 1.0f, 0.92f};
        result.trailColor = {0.72f, 0.04f, 1.0f, 0.72f};
        result.haloRadiusScale = 2.75f;
        result.trailLengthInRadii = 12.0f;
        break;
    case EnemyProjectileTrajectory::Arc:
        result.id = "fallback_arc";
        result.style = EnemyProjectileVisualStyle::Arc;
        result.haloColor = {1.0f, 0.18f, 0.42f, 0.92f};
        result.trailColor = {1.0f, 0.08f, 0.28f, 0.70f};
        result.coreRadiusScale = 1.9f;
        result.maximumWorldRadius = 5.5f;
        break;
    }
    if(result.style==EnemyProjectileVisualStyle::Orb) {
        result.haloColor={1.0f,0.20f,0.025f,0.40f};
        result.trailColor={1.0f,0.32f,0.045f,0.55f};
        result.haloRadiusScale=1.35f;
        result.threatRadiusScale=1.0f;
        result.pulseAmplitude=0;
        result.trailLengthInRadii=3.0f;
        result.trailWidthScale=0.28f;
    }
    return result;
}

bool TryParseEnemyProjectileVisualStyle(
    const std::string& text,
    EnemyProjectileVisualStyle& style) noexcept {
    if (text == "bolt") style = EnemyProjectileVisualStyle::Bolt;
    else if (text == "orb") style = EnemyProjectileVisualStyle::Orb;
    else if (text == "missile") style = EnemyProjectileVisualStyle::Missile;
    else if (text == "arc") style = EnemyProjectileVisualStyle::Arc;
    else return false;
    return true;
}

const char* ToString(EnemyProjectileVisualStyle style) noexcept {
    switch (style) {
    case EnemyProjectileVisualStyle::Bolt: return "Bolt";
    case EnemyProjectileVisualStyle::Orb: return "Orb";
    case EnemyProjectileVisualStyle::Missile: return "Missile";
    case EnemyProjectileVisualStyle::Arc: return "Arc";
    }
    return "Unknown";
}
