#include "RailVehicleControlDefinitionAsset.h"

#include "CourseAssetParsing.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace {

constexpr uintmax_t kMaximumAssetBytes = 64u * 1024u;

std::string Lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

bool ParseUInt(std::string_view text, uint32_t& output) {
    uint32_t parsed = 0;
    const auto result = std::from_chars(
        text.data(), text.data() + text.size(), parsed);
    if (text.empty() || result.ec != std::errc{} ||
        result.ptr != text.data() + text.size()) {
        return false;
    }
    output = parsed;
    return true;
}

bool ParseFloat(std::string_view text, float& output) {
    std::string owned{text};
    char* end = nullptr;
    const float parsed = std::strtof(owned.c_str(), &end);
    if (owned.empty() || end == owned.c_str() || *end != '\0' ||
        !std::isfinite(parsed)) {
        return false;
    }
    output = parsed;
    return true;
}

bool ParseBool(std::string value, bool& output) {
    value = Lower(course_asset_parsing::Trim(std::move(value)));
    if (value == "1" || value == "true" || value == "yes") {
        output = true;
        return true;
    }
    if (value == "0" || value == "false" || value == "no") {
        output = false;
        return true;
    }
    return false;
}

bool ValidId(const std::string& value) {
    return !value.empty() && value.size() <= 96 &&
        std::all_of(value.begin(), value.end(), [](unsigned char c) {
            return std::isalnum(c) || c == '_' || c == '-' || c == '.' || c == '/';
        });
}

bool FiniteRange(float value, float minimum, float maximum) {
    return std::isfinite(value) && value >= minimum && value <= maximum;
}

} // namespace

bool RailVehicleControlDefinitionAsset::LoadFromFile(
    const std::filesystem::path& path,
    std::string* errorMessage) {
    const auto reject = [errorMessage, &path](const std::string& message) {
        if (errorMessage != nullptr) {
            *errorMessage = path.generic_string() + ": " + message;
        }
        return false;
    };
    std::error_code fileError;
    const uintmax_t bytes = std::filesystem::file_size(path, fileError);
    if (fileError || bytes == 0 || bytes > kMaximumAssetBytes) {
        return reject("file is missing, empty, or exceeds the 64 KiB limit");
    }
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) return reject("could not open rail vehicle control asset");

    std::unordered_map<std::string, std::string> values;
    std::string line;
    uint32_t lineNumber = 0;
    bool headerRead = false;
    while (std::getline(file, line)) {
        ++lineNumber;
        line = course_asset_parsing::Trim(std::move(line));
        if (line.empty() || line[0] == '#') continue;
        if (!headerRead) {
            const std::vector<std::string> header =
                course_asset_parsing::SplitPipe(line);
            uint32_t schema = 0;
            if (header.size() != 2 || header[0] != "RAIL_VEHICLE_CONTROL" ||
                !ParseUInt(header[1], schema) || schema < 1u ||
                schema > kRailVehicleControlAssetSchemaVersion) {
                return reject("unsupported or missing RAIL_VEHICLE_CONTROL schema header");
            }
            headerRead = true;
            continue;
        }
        const size_t separator = line.find('=');
        if (separator == std::string::npos) {
            return reject("expected key=value at line " + std::to_string(lineNumber));
        }
        const std::string key = course_asset_parsing::Trim(line.substr(0, separator));
        const std::string value = course_asset_parsing::Trim(line.substr(separator + 1));
        if (key.empty() || !values.emplace(key, value).second) {
            return reject("empty or duplicate key at line " + std::to_string(lineNumber));
        }
    }
    if (!headerRead) return reject("rail vehicle control header was not found");

    const std::unordered_set<std::string> allowedKeys{
        "presetId", "displayName", "vehicle.vehicleId", "vehicle.mountedMovementMode",
        "vehicle.collisionHalfExtentsX", "vehicle.collisionHalfExtentsY",
        "vehicle.collisionHalfExtentsZ", "vehicle.bodyVerticalOffset",
        "vehicle.maximumSpeed", "vehicle.acceleration",
        "vehicle.serviceBrakeDeceleration", "vehicle.emergencyBrakeDeceleration",
        "vehicle.maximumLateralAcceleration", "vehicle.curvatureLookAheadDistance",
        "vehicle.endStopTolerance", "vehicle.maximumHitPoints", "vehicle.allowDerail",
        "speedPolicy.enabled", "speedPolicy.minSpeed", "speedPolicy.maxSpeed",
        "speedPolicy.normalSpeedScale",
        "speedPolicy.cruiseMultiplier", "speedPolicy.combatMultiplier",
        "speedPolicy.highSpeedMultiplier", "speedPolicy.tunnelMultiplier",
        "speedPolicy.bossMultiplier", "speedPolicy.setpieceMultiplier",
        "speedPolicy.cinematicMultiplier", "speedPolicy.recoveryMultiplier",
        "speedPolicy.eventSlowMultiplier", "speedPolicy.eventBoostMultiplier",
        "speedPolicy.eventBlendDuration",
        "mount.playerX", "mount.playerY", "mount.playerZ",
        "mount.weaponX", "mount.weaponY", "mount.weaponZ",
        "mount.cameraX", "mount.cameraY", "mount.cameraZ",
        "mount.damageVfxX", "mount.damageVfxY", "mount.damageVfxZ",
        "hitbox.profileId", "hitbox.bodyHalfExtentsX",
        "hitbox.bodyHalfExtentsY", "hitbox.bodyHalfExtentsZ",
        "hitbox.occupantHurtRadius", "hitbox.occupantNearMissOuterRadius",
        "hitbox.evasionHurtRadiusScale", "hitbox.minimumHurtRadius",
        "hitbox.occupantForwardOffset", "hitbox.occupantLateralOffset",
        "hitbox.occupantVerticalOffset", "hitbox.clearanceSafetyMargin",
        "hitbox.motionHistoryResetDistance", "hitbox.obstacleContactDamage",
        "hitbox.terrainContactDamage", "hitbox.proceduralTerrainContactDamage",
        "hitbox.postContactInvulnerabilitySeconds",
        "hitbox.repeatedContactIntervalSeconds",
        "hitbox.minimumDamageImpactSpeed", "hitbox.maximumObstacleCandidates",
        "hitbox.maximumTerrainCandidates", "hitbox.proceduralSweepSamples",
        "hitbox.proceduralRefinementSteps", "hitbox.includeDynamicObstacles",
        "hitbox.includeTerrainPlacements", "hitbox.includeProceduralTerrain",
        "evasion.evadeDurationSeconds", "evasion.recoveryDurationSeconds",
        "evasion.cooldownSeconds", "evasion.invulnerabilityDurationSeconds",
        "evasion.lateralDistance", "evasion.verticalDistance",
        "evasion.minimumDirectionalInput", "evasion.maximumSubstepSeconds",
        "clearance.occupantRadius", "clearance.safetyMargin",
        "clearance.contactFractionEpsilon", "clearance.maximumObstacleCandidates",
        "clearance.maximumTerrainCandidates", "clearance.proceduralSweepSamples",
        "clearance.proceduralRefinementSteps", "clearance.includeDynamicObstacles",
        "clearance.includeTerrainPlacements", "clearance.includeProceduralTerrain",
        "constraint.minimumStartFraction", "constraint.rejectStartedPenetrating",
        "constraint.failClosedWithoutClearance", "camera.enabled", "camera.anchorBlend",
        "camera.maximumAnchorCorrection", "camera.evasionLateralFollow",
        "camera.evasionVerticalFollow", "camera.targetEvasionFollow",
        "camera.maximumEvasionRollDegrees", "trackContact.enabled",
        "trackContact.wheelbase", "trackContact.trackGauge",
        "trackContact.bodyPivotHeightAboveContacts",
        "trackContact.contactClearance", "trackContact.railHeadVerticalOffset",
        "trackContact.maximumSuspensionTravel",
        "trackContact.minimumContactSeparation", "vehiclePresentation.enabled",
        "vehiclePresentation.wheelRadius", "vehiclePresentation.maximumVisualBankDegrees",
        "vehiclePresentation.maximumVisualPitchDegrees",
        "vehiclePresentation.suspensionAmplitude", "vehiclePresentation.railJointSpacing",
        "vehiclePresentation.smoothingResponse",
        "vehiclePresentation.jointImpactSpeedThreshold",
        "vehiclePresentation.brakeSparkDecelerationThreshold",
        "rideDynamics.enabled", "rideDynamics.rollFrequencyHz",
        "rideDynamics.rollDampingRatio", "rideDynamics.pitchFrequencyHz",
        "rideDynamics.pitchDampingRatio", "rideDynamics.yawFrequencyHz",
        "rideDynamics.yawDampingRatio", "rideDynamics.suspensionFrequencyHz",
        "rideDynamics.suspensionDampingRatio",
        "rideDynamics.maximumBankDegrees",
        "rideDynamics.maximumPitchDegrees",
        "rideDynamics.maximumYawLagDegrees", "rideDynamics.yawLagSeconds",
        "rideDynamics.gradePitchScale", "rideDynamics.accelerationPitchScale",
        "rideDynamics.jerkPitchDegreesPerUnit",
        "rideDynamics.maximumJerkPitchDegrees",
        "rideDynamics.suspensionAmplitude", "rideDynamics.railJointSpacing",
        "rideDynamics.distanceDiscontinuityThreshold",
        "rideDynamics.maximumSubstepSeconds",
        "occupantPresentation.enabled", "occupantPresentation.maximumLateralLeanDegrees",
        "occupantPresentation.maximumVerticalLeanDegrees",
        "occupantPresentation.maximumCounterYawDegrees",
        "occupantPresentation.suspensionFollow", "occupantPresentation.poseResponse",
        "occupantPresentation.afterimageStrength", "vehicleAudio.enabled",
        "vehicleAudio.masterVolume", "vehicleAudio.rollingPulseSlowInterval",
        "vehicleAudio.rollingPulseFastInterval", "vehicleAudio.brakeRetriggerInterval",
        "vehicleAudio.referenceDistance", "vehicleAudio.spatialPanWidth",
        "evasionFeedback.enabled", "evasionFeedback.audioEnabled",
        "evasionFeedback.vfxEnabled", "evasionFeedback.hapticsEnabled",
        "evasionFeedback.masterVolume", "evasionFeedback.afterimageIntervalSeconds",
        "evasionFeedback.startHapticDurationSeconds",
        "evasionFeedback.maximumVfxCommandsPerFrame",
        "collisionFeedback.enabled", "collisionFeedback.audioEnabled",
        "collisionFeedback.vfxEnabled", "collisionFeedback.bodyKickDistance",
        "collisionFeedback.maximumBankDegrees",
        "collisionFeedback.maximumPitchDegrees",
        "collisionFeedback.maximumYawDegrees",
        "collisionFeedback.responseDurationSeconds",
        "collisionFeedback.oscillationFrequencyHz",
        "collisionFeedback.cameraShake", "collisionFeedback.sparkRadius",
        "collisionFeedback.sparkLifetimeSeconds", "collisionFeedback.sparkSpread",
        "collisionFeedback.sparkBurstCount", "collisionFeedback.impactVolume",
        "collisionFeedback.slowdownSpeedMultiplier",
        "collisionFeedback.slowdownDurationSeconds"};
    for (const auto& [key, value] : values) {
        (void)value;
        if (!allowedKeys.contains(key)) return reject("unknown key: " + key);
    }

    RailVehicleControlDefinitionAsset loaded{};
    const auto find = [&values](const char* key) -> const std::string* {
        const auto found = values.find(key);
        return found != values.end() ? &found->second : nullptr;
    };
    const std::string* presetId = find("presetId");
    if (presetId == nullptr || presetId->empty()) return reject("presetId is required");
    loaded.presetId = *presetId;
    if (const std::string* value = find("displayName")) loaded.displayName = *value;
    if (const std::string* value = find("vehicle.vehicleId")) loaded.vehicle.vehicleId = *value;
    if (const std::string* value = find("hitbox.profileId")) {
        loaded.hitbox.profileId = *value;
    }
    if (const std::string* value = find("vehicle.mountedMovementMode")) {
        const std::string mode = Lower(*value);
        if (mode == "vehiclemounted" || mode == "vehicle_mounted") {
            loaded.vehicle.mountedMovementMode = RailVehicleMountedMovementMode::VehicleMounted;
        } else if (mode == "freeoffset" || mode == "free_offset") {
            loaded.vehicle.mountedMovementMode = RailVehicleMountedMovementMode::FreeOffset;
        } else {
            return reject("vehicle.mountedMovementMode is invalid");
        }
    }
    const auto parseFloat = [&find](const char* key, float& target) {
        const std::string* value = find(key);
        return value == nullptr || ParseFloat(*value, target);
    };
    const auto parseUInt = [&find](const char* key, uint32_t& target) {
        const std::string* value = find(key);
        return value == nullptr || ParseUInt(*value, target);
    };
    const auto parseBool = [&find](const char* key, bool& target) {
        const std::string* value = find(key);
        return value == nullptr || ParseBool(*value, target);
    };
#define RV_FLOAT(key, field) if (!parseFloat(key, loaded.field)) return reject("malformed value: " key)
#define RV_UINT(key, field) if (!parseUInt(key, loaded.field)) return reject("malformed value: " key)
#define RV_BOOL(key, field) if (!parseBool(key, loaded.field)) return reject("malformed value: " key)
    RV_FLOAT("vehicle.collisionHalfExtentsX", hitbox.bodyHalfExtents.x);
    RV_FLOAT("vehicle.collisionHalfExtentsY", hitbox.bodyHalfExtents.y);
    RV_FLOAT("vehicle.collisionHalfExtentsZ", hitbox.bodyHalfExtents.z);
    RV_FLOAT("vehicle.bodyVerticalOffset", vehicle.bodyVerticalOffset);
    RV_FLOAT("vehicle.maximumSpeed", vehicle.maximumSpeed);
    RV_FLOAT("vehicle.acceleration", vehicle.acceleration);
    RV_FLOAT("vehicle.serviceBrakeDeceleration", vehicle.serviceBrakeDeceleration);
    RV_FLOAT("vehicle.emergencyBrakeDeceleration", vehicle.emergencyBrakeDeceleration);
    RV_FLOAT("vehicle.maximumLateralAcceleration", vehicle.maximumLateralAcceleration);
    RV_FLOAT("vehicle.curvatureLookAheadDistance", vehicle.curvatureLookAheadDistance);
    RV_FLOAT("vehicle.endStopTolerance", vehicle.endStopTolerance);
    RV_FLOAT("vehicle.maximumHitPoints", vehicle.maximumHitPoints);
    RV_BOOL("vehicle.allowDerail", vehicle.allowDerail);
    RV_BOOL("speedPolicy.enabled", speedPolicy.enabled);
    RV_FLOAT("speedPolicy.normalSpeedScale", speedPolicy.normalSpeedScale);
    RV_FLOAT("speedPolicy.minSpeed", speedPolicy.minSpeed);
    RV_FLOAT("speedPolicy.maxSpeed", speedPolicy.maxSpeed);
    RV_FLOAT("speedPolicy.cruiseMultiplier", speedPolicy.cruiseMultiplier);
    RV_FLOAT("speedPolicy.combatMultiplier", speedPolicy.combatMultiplier);
    RV_FLOAT("speedPolicy.highSpeedMultiplier", speedPolicy.highSpeedMultiplier);
    RV_FLOAT("speedPolicy.tunnelMultiplier", speedPolicy.tunnelMultiplier);
    RV_FLOAT("speedPolicy.bossMultiplier", speedPolicy.bossMultiplier);
    RV_FLOAT("speedPolicy.setpieceMultiplier", speedPolicy.setpieceMultiplier);
    RV_FLOAT("speedPolicy.cinematicMultiplier", speedPolicy.cinematicMultiplier);
    RV_FLOAT("speedPolicy.recoveryMultiplier", speedPolicy.recoveryMultiplier);
    RV_FLOAT("speedPolicy.eventSlowMultiplier", speedPolicy.eventSlowMultiplier);
    RV_FLOAT("speedPolicy.eventBoostMultiplier", speedPolicy.eventBoostMultiplier);
    RV_FLOAT("speedPolicy.eventBlendDuration", speedPolicy.eventBlendDuration);
    RV_FLOAT("mount.playerX", vehicle.mounts.player.x);
    RV_FLOAT("mount.playerY", vehicle.mounts.player.y);
    RV_FLOAT("mount.playerZ", vehicle.mounts.player.z);
    RV_FLOAT("mount.weaponX", vehicle.mounts.weapon.x);
    RV_FLOAT("mount.weaponY", vehicle.mounts.weapon.y);
    RV_FLOAT("mount.weaponZ", vehicle.mounts.weapon.z);
    RV_FLOAT("mount.cameraX", vehicle.mounts.camera.x);
    RV_FLOAT("mount.cameraY", vehicle.mounts.camera.y);
    RV_FLOAT("mount.cameraZ", vehicle.mounts.camera.z);
    RV_FLOAT("mount.damageVfxX", vehicle.mounts.damageVfx.x);
    RV_FLOAT("mount.damageVfxY", vehicle.mounts.damageVfx.y);
    RV_FLOAT("mount.damageVfxZ", vehicle.mounts.damageVfx.z);
    RV_FLOAT("hitbox.bodyHalfExtentsX", hitbox.bodyHalfExtents.x);
    RV_FLOAT("hitbox.bodyHalfExtentsY", hitbox.bodyHalfExtents.y);
    RV_FLOAT("hitbox.bodyHalfExtentsZ", hitbox.bodyHalfExtents.z);
    RV_FLOAT("hitbox.occupantHurtRadius", hitbox.occupantHurtRadius);
    RV_FLOAT("hitbox.occupantNearMissOuterRadius", hitbox.occupantNearMissOuterRadius);
    RV_FLOAT("hitbox.evasionHurtRadiusScale", hitbox.evasionHurtRadiusScale);
    RV_FLOAT("hitbox.minimumHurtRadius", hitbox.minimumHurtRadius);
    RV_FLOAT("hitbox.occupantForwardOffset", hitbox.occupantForwardOffset);
    RV_FLOAT("hitbox.occupantLateralOffset", hitbox.occupantLateralOffset);
    RV_FLOAT("hitbox.occupantVerticalOffset", hitbox.occupantVerticalOffset);
    RV_FLOAT("hitbox.clearanceSafetyMargin", hitbox.clearanceSafetyMargin);
    RV_FLOAT("hitbox.motionHistoryResetDistance", hitbox.motionHistoryResetDistance);
    RV_FLOAT("hitbox.obstacleContactDamage", hitbox.obstacleContactDamage);
    RV_FLOAT("hitbox.terrainContactDamage", hitbox.terrainContactDamage);
    RV_FLOAT("hitbox.proceduralTerrainContactDamage", hitbox.proceduralTerrainContactDamage);
    RV_FLOAT("hitbox.postContactInvulnerabilitySeconds", hitbox.postContactInvulnerabilitySeconds);
    RV_FLOAT("hitbox.repeatedContactIntervalSeconds", hitbox.repeatedContactIntervalSeconds);
    RV_FLOAT("hitbox.minimumDamageImpactSpeed", hitbox.minimumDamageImpactSpeed);
    RV_UINT("hitbox.maximumObstacleCandidates", hitbox.maximumObstacleCandidates);
    RV_UINT("hitbox.maximumTerrainCandidates", hitbox.maximumTerrainCandidates);
    RV_UINT("hitbox.proceduralSweepSamples", hitbox.proceduralSweepSamples);
    RV_UINT("hitbox.proceduralRefinementSteps", hitbox.proceduralRefinementSteps);
    RV_BOOL("hitbox.includeDynamicObstacles", hitbox.includeDynamicObstacles);
    RV_BOOL("hitbox.includeTerrainPlacements", hitbox.includeTerrainPlacements);
    RV_BOOL("hitbox.includeProceduralTerrain", hitbox.includeProceduralTerrain);
    RV_FLOAT("evasion.evadeDurationSeconds", evasion.evadeDurationSeconds);
    RV_FLOAT("evasion.recoveryDurationSeconds", evasion.recoveryDurationSeconds);
    RV_FLOAT("evasion.cooldownSeconds", evasion.cooldownSeconds);
    RV_FLOAT("evasion.invulnerabilityDurationSeconds", evasion.invulnerabilityDurationSeconds);
    RV_FLOAT("evasion.lateralDistance", evasion.lateralDistance);
    RV_FLOAT("evasion.verticalDistance", evasion.verticalDistance);
    RV_FLOAT("evasion.minimumDirectionalInput", evasion.minimumDirectionalInput);
    RV_FLOAT("evasion.maximumSubstepSeconds", evasion.maximumSubstepSeconds);
    RV_FLOAT("clearance.occupantRadius", hitbox.occupantHurtRadius);
    RV_FLOAT("clearance.safetyMargin", hitbox.clearanceSafetyMargin);
    RV_FLOAT("clearance.contactFractionEpsilon", clearance.contactFractionEpsilon);
    RV_UINT("clearance.maximumObstacleCandidates", clearance.maximumObstacleCandidates);
    RV_UINT("clearance.maximumTerrainCandidates", clearance.maximumTerrainCandidates);
    RV_UINT("clearance.proceduralSweepSamples", clearance.proceduralSweepSamples);
    RV_UINT("clearance.proceduralRefinementSteps", clearance.proceduralRefinementSteps);
    RV_BOOL("clearance.includeDynamicObstacles", clearance.includeDynamicObstacles);
    RV_BOOL("clearance.includeTerrainPlacements", clearance.includeTerrainPlacements);
    RV_BOOL("clearance.includeProceduralTerrain", clearance.includeProceduralTerrain);
    RV_FLOAT("constraint.minimumStartFraction", constraint.minimumStartFraction);
    RV_BOOL("constraint.rejectStartedPenetrating", constraint.rejectStartedPenetrating);
    RV_BOOL("constraint.failClosedWithoutClearance", constraint.failClosedWithoutClearance);
    RV_BOOL("camera.enabled", camera.enabled);
    RV_FLOAT("camera.anchorBlend", camera.anchorBlend);
    RV_FLOAT("camera.maximumAnchorCorrection", camera.maximumAnchorCorrection);
    RV_FLOAT("camera.evasionLateralFollow", camera.evasionLateralFollow);
    RV_FLOAT("camera.evasionVerticalFollow", camera.evasionVerticalFollow);
    RV_FLOAT("camera.targetEvasionFollow", camera.targetEvasionFollow);
    RV_FLOAT("camera.maximumEvasionRollDegrees", camera.maximumEvasionRollDegrees);
    RV_BOOL("trackContact.enabled", trackContact.enabled);
    RV_FLOAT("trackContact.wheelbase", trackContact.wheelbase);
    RV_FLOAT("trackContact.trackGauge", trackContact.trackGauge);
    RV_FLOAT("trackContact.bodyPivotHeightAboveContacts", trackContact.bodyPivotHeightAboveContacts);
    RV_FLOAT("trackContact.contactClearance", trackContact.contactClearance);
    RV_FLOAT("trackContact.railHeadVerticalOffset", trackContact.railHeadVerticalOffset);
    RV_FLOAT("trackContact.maximumSuspensionTravel", trackContact.maximumSuspensionTravel);
    RV_FLOAT("trackContact.minimumContactSeparation", trackContact.minimumContactSeparation);
    RV_BOOL("vehiclePresentation.enabled", vehiclePresentation.enabled);
    RV_FLOAT("vehiclePresentation.wheelRadius", vehiclePresentation.wheelRadius);
    RV_FLOAT("vehiclePresentation.maximumVisualBankDegrees", vehiclePresentation.maximumVisualBankDegrees);
    RV_FLOAT("vehiclePresentation.maximumVisualPitchDegrees", vehiclePresentation.maximumVisualPitchDegrees);
    RV_FLOAT("vehiclePresentation.suspensionAmplitude", vehiclePresentation.suspensionAmplitude);
    RV_FLOAT("vehiclePresentation.railJointSpacing", vehiclePresentation.railJointSpacing);
    RV_FLOAT("vehiclePresentation.smoothingResponse", vehiclePresentation.smoothingResponse);
    RV_FLOAT("vehiclePresentation.jointImpactSpeedThreshold", vehiclePresentation.jointImpactSpeedThreshold);
    RV_FLOAT("vehiclePresentation.brakeSparkDecelerationThreshold", vehiclePresentation.brakeSparkDecelerationThreshold);
    RV_BOOL("rideDynamics.enabled", rideDynamics.enabled);
    RV_FLOAT("rideDynamics.rollFrequencyHz", rideDynamics.rollFrequencyHz);
    RV_FLOAT("rideDynamics.rollDampingRatio", rideDynamics.rollDampingRatio);
    RV_FLOAT("rideDynamics.pitchFrequencyHz", rideDynamics.pitchFrequencyHz);
    RV_FLOAT("rideDynamics.pitchDampingRatio", rideDynamics.pitchDampingRatio);
    RV_FLOAT("rideDynamics.yawFrequencyHz", rideDynamics.yawFrequencyHz);
    RV_FLOAT("rideDynamics.yawDampingRatio", rideDynamics.yawDampingRatio);
    RV_FLOAT("rideDynamics.suspensionFrequencyHz", rideDynamics.suspensionFrequencyHz);
    RV_FLOAT("rideDynamics.suspensionDampingRatio", rideDynamics.suspensionDampingRatio);
    RV_FLOAT("rideDynamics.maximumBankDegrees", rideDynamics.maximumBankDegrees);
    RV_FLOAT("rideDynamics.maximumPitchDegrees", rideDynamics.maximumPitchDegrees);
    RV_FLOAT("rideDynamics.maximumYawLagDegrees", rideDynamics.maximumYawLagDegrees);
    RV_FLOAT("rideDynamics.yawLagSeconds", rideDynamics.yawLagSeconds);
    RV_FLOAT("rideDynamics.gradePitchScale", rideDynamics.gradePitchScale);
    RV_FLOAT("rideDynamics.accelerationPitchScale", rideDynamics.accelerationPitchScale);
    RV_FLOAT("rideDynamics.jerkPitchDegreesPerUnit", rideDynamics.jerkPitchDegreesPerUnit);
    RV_FLOAT("rideDynamics.maximumJerkPitchDegrees", rideDynamics.maximumJerkPitchDegrees);
    RV_FLOAT("rideDynamics.suspensionAmplitude", rideDynamics.suspensionAmplitude);
    RV_FLOAT("rideDynamics.railJointSpacing", rideDynamics.railJointSpacing);
    RV_FLOAT("rideDynamics.distanceDiscontinuityThreshold", rideDynamics.distanceDiscontinuityThreshold);
    RV_FLOAT("rideDynamics.maximumSubstepSeconds", rideDynamics.maximumSubstepSeconds);
    RV_BOOL("occupantPresentation.enabled", occupantPresentation.enabled);
    RV_FLOAT("occupantPresentation.maximumLateralLeanDegrees", occupantPresentation.maximumLateralLeanDegrees);
    RV_FLOAT("occupantPresentation.maximumVerticalLeanDegrees", occupantPresentation.maximumVerticalLeanDegrees);
    RV_FLOAT("occupantPresentation.maximumCounterYawDegrees", occupantPresentation.maximumCounterYawDegrees);
    RV_FLOAT("occupantPresentation.suspensionFollow", occupantPresentation.suspensionFollow);
    RV_FLOAT("occupantPresentation.poseResponse", occupantPresentation.poseResponse);
    RV_FLOAT("occupantPresentation.afterimageStrength", occupantPresentation.afterimageStrength);
    RV_BOOL("vehicleAudio.enabled", vehicleAudio.enabled);
    RV_FLOAT("vehicleAudio.masterVolume", vehicleAudio.masterVolume);
    RV_FLOAT("vehicleAudio.rollingPulseSlowInterval", vehicleAudio.rollingPulseSlowInterval);
    RV_FLOAT("vehicleAudio.rollingPulseFastInterval", vehicleAudio.rollingPulseFastInterval);
    RV_FLOAT("vehicleAudio.brakeRetriggerInterval", vehicleAudio.brakeRetriggerInterval);
    RV_FLOAT("vehicleAudio.referenceDistance", vehicleAudio.referenceDistance);
    RV_FLOAT("vehicleAudio.spatialPanWidth", vehicleAudio.spatialPanWidth);
    RV_BOOL("evasionFeedback.enabled", evasionFeedback.enabled);
    RV_BOOL("evasionFeedback.audioEnabled", evasionFeedback.audioEnabled);
    RV_BOOL("evasionFeedback.vfxEnabled", evasionFeedback.vfxEnabled);
    RV_BOOL("evasionFeedback.hapticsEnabled", evasionFeedback.hapticsEnabled);
    RV_FLOAT("evasionFeedback.masterVolume", evasionFeedback.masterVolume);
    RV_FLOAT("evasionFeedback.afterimageIntervalSeconds", evasionFeedback.afterimageIntervalSeconds);
    RV_FLOAT("evasionFeedback.startHapticDurationSeconds", evasionFeedback.startHapticDurationSeconds);
    RV_UINT("evasionFeedback.maximumVfxCommandsPerFrame", evasionFeedback.maximumVfxCommandsPerFrame);
    RV_BOOL("collisionFeedback.enabled", collisionFeedback.enabled);
    RV_BOOL("collisionFeedback.audioEnabled", collisionFeedback.audioEnabled);
    RV_BOOL("collisionFeedback.vfxEnabled", collisionFeedback.vfxEnabled);
    RV_FLOAT("collisionFeedback.bodyKickDistance", collisionFeedback.bodyKickDistance);
    RV_FLOAT("collisionFeedback.maximumBankDegrees", collisionFeedback.maximumBankDegrees);
    RV_FLOAT("collisionFeedback.maximumPitchDegrees", collisionFeedback.maximumPitchDegrees);
    RV_FLOAT("collisionFeedback.maximumYawDegrees", collisionFeedback.maximumYawDegrees);
    RV_FLOAT("collisionFeedback.responseDurationSeconds", collisionFeedback.responseDurationSeconds);
    RV_FLOAT("collisionFeedback.oscillationFrequencyHz", collisionFeedback.oscillationFrequencyHz);
    RV_FLOAT("collisionFeedback.cameraShake", collisionFeedback.cameraShake);
    RV_FLOAT("collisionFeedback.sparkRadius", collisionFeedback.sparkRadius);
    RV_FLOAT("collisionFeedback.sparkLifetimeSeconds", collisionFeedback.sparkLifetimeSeconds);
    RV_FLOAT("collisionFeedback.sparkSpread", collisionFeedback.sparkSpread);
    RV_UINT("collisionFeedback.sparkBurstCount", collisionFeedback.sparkBurstCount);
    RV_FLOAT("collisionFeedback.impactVolume", collisionFeedback.impactVolume);
    RV_FLOAT("collisionFeedback.slowdownSpeedMultiplier", collisionFeedback.slowdownSpeedMultiplier);
    RV_FLOAT("collisionFeedback.slowdownDurationSeconds", collisionFeedback.slowdownDurationSeconds);
#undef RV_FLOAT
#undef RV_UINT
#undef RV_BOOL
    loaded.hitbox.ApplyAuthoritativeShapes(
        loaded.vehicle, loaded.clearance);
    std::string validationError;
    if (!loaded.Validate(&validationError)) return reject(validationError);
    *this = std::move(loaded);
    if (errorMessage != nullptr) errorMessage->clear();
    return true;
}

bool RailVehicleControlDefinitionAsset::Validate(
    std::string* errorMessage) const {
    const auto reject = [errorMessage](const std::string& message) {
        if (errorMessage != nullptr) *errorMessage = message;
        return false;
    };
    if (schemaVersion != kRailVehicleControlAssetSchemaVersion) {
        return reject("unsupported rail vehicle control schema version");
    }
    if (!ValidId(presetId) || displayName.empty() || displayName.size() > 128) {
        return reject("preset identity is invalid");
    }
    std::string nestedError;
    if (!speedPolicy.Validate(&nestedError) || !hitbox.Validate(&nestedError) ||
        !vehicle.Validate(&nestedError) || !evasion.Validate(&nestedError) ||
        !clearance.Validate(&nestedError) || !constraint.Validate(&nestedError) ||
        !camera.Validate(&nestedError) || !trackContact.Validate(&nestedError) ||
        !rideDynamics.Validate(&nestedError) ||
        !occupantPresentation.Validate(&nestedError) ||
        !evasionFeedback.Validate(&nestedError) ||
        !collisionFeedback.Validate(&nestedError)) {
        return reject(nestedError);
    }
    const auto same = [](float left, float right) {
        return std::abs(left - right) <= 0.0001f;
    };
    if (!same(vehicle.collisionHalfExtents.x, hitbox.bodyHalfExtents.x) ||
        !same(vehicle.collisionHalfExtents.y, hitbox.bodyHalfExtents.y) ||
        !same(vehicle.collisionHalfExtents.z, hitbox.bodyHalfExtents.z) ||
        !same(clearance.occupantRadius, hitbox.occupantHurtRadius) ||
        !same(clearance.safetyMargin, hitbox.clearanceSafetyMargin)) {
        return reject("vehicle, occupant and clearance shapes must come from hitbox profile");
    }
    PlayerHitboxDefinition playerHitbox =
        hitbox.BuildPlayerHitboxDefinition();
    if (!playerHitbox.Validate(&nestedError)) return reject(nestedError);
    const RailVehiclePresentationSettings& p = vehiclePresentation;
    if (!FiniteRange(p.wheelRadius, 0.01f, 100.0f) ||
        !FiniteRange(p.maximumVisualBankDegrees, 0.0f, 90.0f) ||
        !FiniteRange(p.maximumVisualPitchDegrees, 0.0f, 90.0f) ||
        !FiniteRange(p.suspensionAmplitude, 0.0f, 10.0f) ||
        !FiniteRange(p.railJointSpacing, 0.05f, 1000.0f) ||
        !FiniteRange(p.smoothingResponse, 0.01f, 200.0f) ||
        !FiniteRange(p.jointImpactSpeedThreshold, 0.0f, 10000.0f) ||
        !FiniteRange(p.brakeSparkDecelerationThreshold, 0.0f, 10000.0f)) {
        return reject("vehicle presentation setting is outside commercial limits");
    }
    const RailVehicleAudioSettings& a = vehicleAudio;
    if (!FiniteRange(a.masterVolume, 0.0f, 2.0f) ||
        !FiniteRange(a.rollingPulseSlowInterval, 0.01f, 10.0f) ||
        !FiniteRange(a.rollingPulseFastInterval, 0.01f, 10.0f) ||
        a.rollingPulseFastInterval > a.rollingPulseSlowInterval ||
        !FiniteRange(a.brakeRetriggerInterval, 0.01f, 10.0f) ||
        !FiniteRange(a.referenceDistance, 0.01f, 100000.0f) ||
        !FiniteRange(a.spatialPanWidth, 0.01f, 100000.0f)) {
        return reject("vehicle audio setting is outside commercial limits");
    }
    if (errorMessage != nullptr) errorMessage->clear();
    return true;
}
