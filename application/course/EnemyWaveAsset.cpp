#include "EnemyWaveAsset.h"

#include "CourseAssetParsing.h"

#include <cstdint>
#include <fstream>
#include <utility>

using namespace course_asset_parsing;

bool EnemyWaveAsset::LoadFromFile(const std::string& path, std::string* errorMessage) {
    std::ifstream file(path);
    if (!file.is_open()) {
        if (errorMessage != nullptr) {
            *errorMessage = "Could not open enemy wave file: " + path;
        }
        return false;
    }

    EnemyWaveAsset loaded{};
    std::string line;
    uint32_t lineNumber = 0;
    while (std::getline(file, line)) {
        ++lineNumber;
        line = Trim(line);
        if (line.empty() || line[0] == '#') {
            continue;
        }

        const std::vector<std::string> parts = SplitPipe(line);
        if (parts.empty()) {
            continue;
        }

        if (parts[0] == "wave") {
            if (parts.size() >= 2) {
                loaded.id = parts[1];
            }
            if (parts.size() >= 3) {
                loaded.displayName = parts[2];
            }
        } else if (parts[0] == "deployment") {
            if(parts.size()!=2) { if(errorMessage) *errorMessage="Invalid deployment row";return false; }
            if(parts[1]=="automatic") loaded.entrySource=EnemyEntrySource::Automatic;
            else if(parts[1]=="carrier") loaded.entrySource=EnemyEntrySource::Carrier;
            else if(parts[1]=="rock_left") loaded.entrySource=EnemyEntrySource::RockLeft;
            else if(parts[1]=="rock_right") loaded.entrySource=EnemyEntrySource::RockRight;
            else if(parts[1]=="high") loaded.entrySource=EnemyEntrySource::High;
            else if(parts[1]=="side_opening") loaded.entrySource=EnemyEntrySource::SideOpening;
            else {if(errorMessage) *errorMessage="Unknown enemy deployment source";return false;}
        } else if (parts[0] == "formation") {
            if (parts.size() < 4) {
                if (errorMessage != nullptr) {
                    *errorMessage = "Invalid formation row at line " +
                        std::to_string(lineNumber);
                }
                return false;
            }
            EnemyFormationDefinition definition =
                EnemyFormationDefinition::CommercialDefault();
            if (!TryParseEnemyFormationPattern(parts[1], definition.pattern) ||
                !TryParseEnemyEntranceStyle(parts[2], definition.entranceStyle) ||
                !TryParseEnemyExitStyle(parts[3], definition.exitStyle)) {
                if (errorMessage != nullptr) {
                    *errorMessage = "Invalid formation enum at line " +
                        std::to_string(lineNumber);
                }
                return false;
            }
            if (parts.size() > 4) {
                const std::string value = parts[4];
                definition.preserveAuthoredSlots =
                    value != "0" && value != "false" && value != "False";
            }
            definition.slotSpacing = ParseFloatOr(parts, 5, definition.slotSpacing);
            definition.verticalSpacing = ParseFloatOr(parts, 6, definition.verticalSpacing);
            definition.cohesionResponse = ParseFloatOr(parts, 7, definition.cohesionResponse);
            definition.maximumCorrection = ParseFloatOr(parts, 8, definition.maximumCorrection);
            definition.attackStaggerSeconds = ParseFloatOr(parts, 9, definition.attackStaggerSeconds);
            definition.entranceDurationSeconds = ParseFloatOr(parts, 10, definition.entranceDurationSeconds);
            definition.entranceStaggerSeconds = ParseFloatOr(parts, 11, definition.entranceStaggerSeconds);
            definition.entranceForwardDistance = ParseFloatOr(parts, 12, definition.entranceForwardDistance);
            definition.entranceSideDistance = ParseFloatOr(parts, 13, definition.entranceSideDistance);
            definition.exitDurationSeconds = ParseFloatOr(parts, 14, definition.exitDurationSeconds);
            definition.exitForwardDistance = ParseFloatOr(parts, 15, definition.exitForwardDistance);
            definition.exitSideDistance = ParseFloatOr(parts, 16, definition.exitSideDistance);
            if (!definition.Validate(errorMessage)) return false;
            loaded.formationDefinition = std::move(definition);
        } else if (parts[0] == "unit") {
            if (parts.size() < 11) {
                if (errorMessage != nullptr) {
                    *errorMessage = "Invalid unit row at line " + std::to_string(lineNumber);
                }
                return false;
            }
            EnemyWaveUnit unit{};
            unit.role = parts[1].empty() ? unit.role : parts[1];
            unit.distanceOffset = ParseFloatOr(parts, 2, unit.distanceOffset);
            unit.lateralOffset = ParseFloatOr(parts, 3, unit.lateralOffset);
            unit.verticalOffset = ParseFloatOr(parts, 4, unit.verticalOffset);
            unit.forwardSpeed = ParseFloatOr(parts, 5, unit.forwardSpeed);
            unit.radius = ParseFloatOr(parts, 6, unit.radius);
            unit.lifetime = ParseFloatOr(parts, 7, unit.lifetime);
            unit.color.x = ParseFloatOr(parts, 8, unit.color.x);
            unit.color.y = ParseFloatOr(parts, 9, unit.color.y);
            unit.color.z = ParseFloatOr(parts, 10, unit.color.z);
            unit.color.w = ParseFloatOr(parts, 11, unit.color.w);
            loaded.units.push_back(unit);
        } else if (parts[0] == "unitAsset") {
            if (parts.size() < 13) {
                if (errorMessage != nullptr) {
                    *errorMessage = "Invalid unitAsset row at line " + std::to_string(lineNumber);
                }
                return false;
            }
            EnemyWaveUnit unit{};
            unit.role = parts[1].empty() ? unit.role : parts[1];
            unit.actorAssetId = parts[2];
            unit.bulletPatternId = parts[3];
            unit.distanceOffset = ParseFloatOr(parts, 4, unit.distanceOffset);
            unit.lateralOffset = ParseFloatOr(parts, 5, unit.lateralOffset);
            unit.verticalOffset = ParseFloatOr(parts, 6, unit.verticalOffset);
            unit.forwardSpeed = ParseFloatOr(parts, 7, unit.forwardSpeed);
            unit.radius = ParseFloatOr(parts, 8, unit.radius);
            unit.lifetime = ParseFloatOr(parts, 9, unit.lifetime);
            unit.color.x = ParseFloatOr(parts, 10, unit.color.x);
            unit.color.y = ParseFloatOr(parts, 11, unit.color.y);
            unit.color.z = ParseFloatOr(parts, 12, unit.color.z);
            unit.color.w = ParseFloatOr(parts, 13, unit.color.w);
            loaded.units.push_back(unit);
        } else if (errorMessage != nullptr) {
            *errorMessage = "Unknown enemy wave row at line " + std::to_string(lineNumber) + ": " + parts[0];
            return false;
        }
    }

    if (loaded.id.empty()) {
        loaded.id = path;
    }
    if (loaded.displayName.empty()) {
        loaded.displayName = loaded.id;
    }
    if (loaded.formationDefinition.has_value()) {
        loaded.formationDefinition->definitionId = loaded.id;
    }
    if (loaded.units.empty()) {
        if (errorMessage != nullptr) {
            *errorMessage = "Enemy wave has no units: " + path;
        }
        return false;
    }

    *this = std::move(loaded);
    return true;
}
