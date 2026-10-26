#include "CourseSequencerTrackProvider.h"

#include "../world/CourseWorldIdentity.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <type_traits>
#include <utility>

namespace editor {
namespace {

constexpr const char* kEventTrack = "course.events";
constexpr const char* kPlacementTrack = "course.placements";
constexpr const char* kCameraTrack = "course.camera";
constexpr const char* kLightingTrack = "course.lighting";
constexpr const char* kMaterialTrack = "course.material";
constexpr const char* kVfxTrack = "course.vfx";
constexpr const char* kGameplayTrack = "course.gameplay";

bool StartsWith(std::string_view value, std::string_view prefix) {
    return value.size() >= prefix.size() && value.substr(0, prefix.size()) == prefix;
}

bool IsGameplayEvent(std::string_view type) {
    return type == "enemy_wave" || type == "obstacle" || type == "boss" ||
        type == "boss_phase" || type == "checkpoint";
}

const char* EventTrackId(const CourseEventMarker& event) {
    if (event.type == "vfx") return kVfxTrack;
    if (IsGameplayEvent(event.type)) return kGameplayTrack;
    return kEventTrack;
}

EditorSequencerKeyState MakeState(
    const char* trackId,
    std::string keyId,
    std::string label,
    double time,
    double duration = 0.0,
    bool locked = false) {
    EditorSequencerKeyState state;
    state.exists = true;
    state.handle = {"course", trackId, std::move(keyId)};
    state.label = std::move(label);
    state.time = time;
    state.duration = duration;
    state.locked = locked;
    return state;
}

template<class T, class Predicate>
T* FindMutable(std::vector<T>& values, Predicate predicate) {
    const auto found = std::find_if(values.begin(), values.end(), predicate);
    return found == values.end() ? nullptr : &*found;
}

template<class T, class Predicate>
const T* FindConst(const std::vector<T>& values, Predicate predicate) {
    const auto found = std::find_if(values.begin(), values.end(), predicate);
    return found == values.end() ? nullptr : &*found;
}

template<class T, class Predicate>
bool EraseFirst(std::vector<T>& values, Predicate predicate) {
    const auto found = std::find_if(values.begin(), values.end(), predicate);
    if (found == values.end()) return false;
    values.erase(found);
    return true;
}

std::string AfterPrefix(std::string_view value, std::string_view prefix) {
    return StartsWith(value, prefix) ? std::string(value.substr(prefix.size())) : std::string{};
}

} // namespace

void CourseSequencerTrackProvider::Bind(CourseAsset* course) {
    if (course_ != course) cloneTemplates_.clear();
    course_ = course;
    if (course_ != nullptr) EnsureCourseWorldObjectGuids(*course_, course_->name);
}

bool CourseSequencerTrackProvider::IsCameraShotHandle(
    const EditorSequencerKeyHandle& handle) const {
    return handle.providerId == ProviderId() && handle.trackId == kCameraTrack &&
        StartsWith(handle.keyId, "shot:");
}

std::string CourseSequencerTrackProvider::CameraShotId(
    const EditorSequencerKeyHandle& handle) const {
    return IsCameraShotHandle(handle)
        ? AfterPrefix(handle.keyId, "shot:") : std::string{};
}

std::string CourseSequencerTrackProvider::EncodeCameraShot(
    const CourseCinematicCameraShot& shot) {
    std::ostringstream stream;
    stream.precision(9);
    stream << "camera-shot-v2 " << std::quoted(shot.id) << ' '
        << std::quoted(shot.mode) << ' ' << std::quoted(shot.presetId) << ' '
        << std::quoted(shot.blendAssetId) << ' ' << shot.startDistance << ' '
        << shot.endDistance << ' ' << shot.blendInDistance << ' '
        << shot.blendOutDistance << ' ' << shot.weightScale << ' '
        << shot.backDistanceOffset << ' ' << shot.verticalOffset << ' '
        << shot.lateralOffset << ' ' << shot.lookAheadOffset << ' '
        << shot.lookUpOffset << ' ' << shot.lookForwardOffset << ' '
        << shot.fovOffset << ' ' << shot.rollOffset << ' ' << shot.shakeAmount << ' '
        << static_cast<int>(shot.view.mode) << ' '
        << shot.view.positionOffset.x << ' ' << shot.view.positionOffset.y << ' ' << shot.view.positionOffset.z << ' '
        << shot.view.yaw << ' ' << shot.view.pitch << ' ' << shot.view.fovY << ' '
        << shot.view.hideVehicle << ' ' << shot.view.priority;
    return stream.str();
}

bool CourseSequencerTrackProvider::DecodeCameraShot(
    std::string_view payload,
    CourseCinematicCameraShot& shot,
    std::string& errorMessage) {
    std::istringstream stream{std::string(payload)};
    std::string version;
    if (!(stream >> version) || (version != "camera-shot-v1" && version != "camera-shot-v2") ||
        !(stream >> std::quoted(shot.id) >> std::quoted(shot.mode) >>
            std::quoted(shot.presetId) >> std::quoted(shot.blendAssetId) >>
            shot.startDistance >> shot.endDistance >> shot.blendInDistance >>
            shot.blendOutDistance >> shot.weightScale >> shot.backDistanceOffset >>
            shot.verticalOffset >> shot.lateralOffset >> shot.lookAheadOffset >>
            shot.lookUpOffset >> shot.lookForwardOffset >> shot.fovOffset >>
            shot.rollOffset >> shot.shakeAmount)) {
        errorMessage = "Malformed Course Camera Shot Sequencer payload.";
        return false;
    }
    shot.view = {};
    if (version == "camera-shot-v2") {
        int mode = 0, hidden = 0;
        if (!(stream >> mode >> shot.view.positionOffset.x >> shot.view.positionOffset.y >> shot.view.positionOffset.z
            >> shot.view.yaw >> shot.view.pitch >> shot.view.fovY >> hidden >> shot.view.priority) ||
            mode < 0 || mode > static_cast<int>(CourseCameraViewMode::Custom) || hidden < 0 || hidden > 1) {
            errorMessage = "Malformed Camera View payload.";
            return false;
        }
        shot.view.mode = static_cast<CourseCameraViewMode>(mode);
        shot.view.hideVehicle = hidden != 0;
        if (!shot.view.Validate(&errorMessage)) return false;
    }
    if (shot.id.empty() || !std::isfinite(shot.startDistance) ||
        !std::isfinite(shot.endDistance) || shot.endDistance <= shot.startDistance ||
        !std::isfinite(shot.blendInDistance) || shot.blendInDistance < 0.0f ||
        !std::isfinite(shot.blendOutDistance) || shot.blendOutDistance < 0.0f ||
        !std::isfinite(shot.weightScale) || shot.weightScale < 0.0f) {
        errorMessage = "Course Camera Shot payload contains an invalid range.";
        return false;
    }
    errorMessage.clear();
    return true;
}

EditorSequencerKeyState CourseSequencerTrackProvider::BuildCameraShotState(
    const CourseCinematicCameraShot& shot) const {
    EditorSequencerKeyState state = MakeState(
        kCameraTrack, "shot:" + shot.id,
        shot.id.empty() ? "Camera Shot" : shot.id,
        shot.startDistance,
        (std::max)(0.0f, shot.endDistance - shot.startDistance));
    state.payload = EncodeCameraShot(shot);
    return state;
}

std::vector<EditorSequencerTrack> CourseSequencerTrackProvider::BuildTracks() const {
    std::vector<EditorSequencerTrack> tracks{
        {kEventTrack, "Course Events", EditorSequencerTrackKind::Event, 0xff5ad2ffu},
        {kPlacementTrack, "Course Placements", EditorSequencerTrackKind::Placement, 0xff74c365u},
        {kCameraTrack, "Camera", EditorSequencerTrackKind::Camera, 0xffffbe5fu},
        {kLightingTrack, "Lighting", EditorSequencerTrackKind::Lighting, 0xfff0d060u},
        {kMaterialTrack, "Material", EditorSequencerTrackKind::Material, 0xffc080ffu},
        {kVfxTrack, "VFX", EditorSequencerTrackKind::Vfx, 0xffff7090u},
        {kGameplayTrack, "Gameplay Triggers", EditorSequencerTrackKind::GameplayTrigger, 0xff6080ffu},
    };
    if (course_ == nullptr) return tracks;
    const auto track = [&tracks](std::string_view id) -> EditorSequencerTrack& {
        return *std::find_if(tracks.begin(), tracks.end(), [&](const auto& value) { return value.id == id; });
    };
    for (const CourseEventMarker& value : course_->events) {
        track(EventTrackId(value)).keys.push_back(MakeState(
            EventTrackId(value), "event:" + value.editorGuid,
            value.id.empty() ? value.type : value.id, value.distance, 0.0, value.editorLocked));
    }
    for (const CourseTerrainPlacement& value : course_->terrainPlacements) {
        track(kPlacementTrack).keys.push_back(MakeState(
            kPlacementTrack, "terrain:" + value.editorGuid,
            value.id.empty() ? "Terrain" : value.id, value.distance, 0.0, value.editorLocked));
    }
    for (const CourseRockCluster& value : course_->rockClusters) {
        track(kPlacementTrack).keys.push_back(MakeState(
            kPlacementTrack, "rock:" + value.editorGuid,
            value.id.empty() ? "Rock Cluster" : value.id, value.distance, 0.0, value.editorLocked));
    }
    for (const CourseCameraKey& value : course_->cameraKeys) {
        track(kCameraTrack).keys.push_back(MakeState(
            kCameraTrack, "camera:" + value.editorGuid,
            "Camera Key", value.distance, 0.0, value.editorLocked));
    }
    for (const CourseCinematicCameraShot& value : course_->cinematicCameraShots) {
        track(kCameraTrack).keys.push_back(BuildCameraShotState(value));
    }
    for (const CourseLightingPreset& value : course_->lightingPresets) {
        track(kLightingTrack).keys.push_back(MakeState(
            kLightingTrack, "lighting:" + value.id,
            value.id.empty() ? "Lighting" : value.id, value.distance));
    }
    for (const CourseTerrainMaterialPreset& value : course_->terrainMaterialPresets) {
        track(kMaterialTrack).keys.push_back(MakeState(
            kMaterialTrack, "material:" + value.id,
            value.id.empty() ? "Material" : value.id, value.distance));
    }
    return tracks;
}

bool CourseSequencerTrackProvider::CaptureKey(
    const EditorSequencerKeyHandle& handle,
    EditorSequencerKeyState& state,
    std::string& errorMessage) const {
    if (course_ == nullptr || handle.providerId != ProviderId()) {
        errorMessage = "Course Sequencer provider is not bound.";
        return false;
    }
    const std::string_view id = handle.keyId;
    if (StartsWith(id, "event:")) {
        if (const CourseEventMarker* value = FindConst(course_->events, [&](const auto& item) {
                return item.editorGuid == AfterPrefix(id, "event:");
            })) {
            state = MakeState(EventTrackId(*value), handle.keyId,
                value->id.empty() ? value->type : value->id,
                value->distance, 0.0, value->editorLocked);
            return true;
        }
    } else if (StartsWith(id, "terrain:")) {
        if (const CourseTerrainPlacement* value = FindConst(course_->terrainPlacements, [&](const auto& item) {
                return item.editorGuid == AfterPrefix(id, "terrain:");
            })) {
            state = MakeState(kPlacementTrack, handle.keyId,
                value->id.empty() ? "Terrain" : value->id,
                value->distance, 0.0, value->editorLocked);
            return true;
        }
    } else if (StartsWith(id, "rock:")) {
        if (const CourseRockCluster* value = FindConst(course_->rockClusters, [&](const auto& item) {
                return item.editorGuid == AfterPrefix(id, "rock:");
            })) {
            state = MakeState(kPlacementTrack, handle.keyId,
                value->id.empty() ? "Rock Cluster" : value->id,
                value->distance, 0.0, value->editorLocked);
            return true;
        }
    } else if (StartsWith(id, "camera:")) {
        if (const CourseCameraKey* value = FindConst(course_->cameraKeys, [&](const auto& item) {
                return item.editorGuid == AfterPrefix(id, "camera:");
            })) {
            state = MakeState(kCameraTrack, handle.keyId, "Camera Key",
                value->distance, 0.0, value->editorLocked);
            return true;
        }
    } else if (StartsWith(id, "shot:")) {
        if (const CourseCinematicCameraShot* value = FindConst(course_->cinematicCameraShots, [&](const auto& item) {
                return item.id == AfterPrefix(id, "shot:");
            })) {
            state = BuildCameraShotState(*value);
            return true;
        }
    } else if (StartsWith(id, "lighting:")) {
        if (const CourseLightingPreset* value = FindConst(course_->lightingPresets, [&](const auto& item) {
                return item.id == AfterPrefix(id, "lighting:");
            })) {
            state = MakeState(kLightingTrack, handle.keyId, value->id, value->distance);
            return true;
        }
    } else if (StartsWith(id, "material:")) {
        if (const CourseTerrainMaterialPreset* value = FindConst(course_->terrainMaterialPresets, [&](const auto& item) {
                return item.id == AfterPrefix(id, "material:");
            })) {
            state = MakeState(kMaterialTrack, handle.keyId, value->id, value->distance);
            return true;
        }
    }
    errorMessage = "Course Sequencer key no longer exists: " + handle.keyId;
    return false;
}

std::string CourseSequencerTrackProvider::NextCloneToken() {
    return "course-sequencer-clone-" + std::to_string(++cloneSerial_);
}

bool CourseSequencerTrackProvider::BuildDuplicate(
    const EditorSequencerKeyState& source,
    double newTime,
    EditorSequencerKeyState& duplicate,
    std::string& errorMessage) {
    if (course_ == nullptr) {
        errorMessage = "Course Sequencer provider is not bound.";
        return false;
    }
    const std::string token = NextCloneToken();
    const std::string suffix = "_copy_" + std::to_string(cloneSerial_);
    const std::string_view id = source.handle.keyId;
    if (StartsWith(id, "event:")) {
        const CourseEventMarker* current = FindConst(course_->events, [&](const auto& item) {
            return item.editorGuid == AfterPrefix(id, "event:");
        });
        if (current == nullptr) { errorMessage = "Source event key is missing."; return false; }
        CourseEventMarker value = *current;
        value.distance = static_cast<float>(newTime);
        value.editorGuid = token;
        value.id += suffix;
        cloneTemplates_[token] = value;
        duplicate = MakeState(source.handle.trackId.c_str(), "event:" + token, value.id, newTime);
    } else if (StartsWith(id, "terrain:")) {
        const CourseTerrainPlacement* current = FindConst(course_->terrainPlacements, [&](const auto& item) {
            return item.editorGuid == AfterPrefix(id, "terrain:");
        });
        if (current == nullptr) { errorMessage = "Source placement key is missing."; return false; }
        CourseTerrainPlacement value = *current;
        value.distance = static_cast<float>(newTime);
        value.editorGuid = token;
        value.id += suffix;
        cloneTemplates_[token] = value;
        duplicate = MakeState(kPlacementTrack, "terrain:" + token, value.id, newTime);
    } else if (StartsWith(id, "rock:")) {
        const CourseRockCluster* current = FindConst(course_->rockClusters, [&](const auto& item) {
            return item.editorGuid == AfterPrefix(id, "rock:");
        });
        if (current == nullptr) { errorMessage = "Source rock key is missing."; return false; }
        CourseRockCluster value = *current;
        value.distance = static_cast<float>(newTime);
        value.editorGuid = token;
        value.id += suffix;
        cloneTemplates_[token] = value;
        duplicate = MakeState(kPlacementTrack, "rock:" + token, value.id, newTime);
    } else if (StartsWith(id, "camera:")) {
        const CourseCameraKey* current = FindConst(course_->cameraKeys, [&](const auto& item) {
            return item.editorGuid == AfterPrefix(id, "camera:");
        });
        if (current == nullptr) { errorMessage = "Source camera key is missing."; return false; }
        CourseCameraKey value = *current;
        value.distance = static_cast<float>(newTime);
        value.editorGuid = token;
        cloneTemplates_[token] = value;
        duplicate = MakeState(kCameraTrack, "camera:" + token, "Camera Key", newTime);
    } else if (StartsWith(id, "shot:")) {
        const CourseCinematicCameraShot* current = FindConst(course_->cinematicCameraShots, [&](const auto& item) {
            return item.id == AfterPrefix(id, "shot:");
        });
        if (current == nullptr) { errorMessage = "Source camera shot is missing."; return false; }
        CourseCinematicCameraShot value = *current;
        const float duration = value.endDistance - value.startDistance;
        value.startDistance = static_cast<float>(newTime);
        value.endDistance = value.startDistance + duration;
        value.id += suffix;
        cloneTemplates_[token] = value;
        duplicate = MakeState(kCameraTrack, "shot:" + value.id, value.id, newTime, duration);
    } else if (StartsWith(id, "lighting:")) {
        const CourseLightingPreset* current = FindConst(course_->lightingPresets, [&](const auto& item) {
            return item.id == AfterPrefix(id, "lighting:");
        });
        if (current == nullptr) { errorMessage = "Source lighting key is missing."; return false; }
        CourseLightingPreset value = *current;
        value.distance = static_cast<float>(newTime);
        value.id += suffix;
        cloneTemplates_[token] = value;
        duplicate = MakeState(kLightingTrack, "lighting:" + value.id, value.id, newTime);
    } else if (StartsWith(id, "material:")) {
        const CourseTerrainMaterialPreset* current = FindConst(course_->terrainMaterialPresets, [&](const auto& item) {
            return item.id == AfterPrefix(id, "material:");
        });
        if (current == nullptr) { errorMessage = "Source material key is missing."; return false; }
        CourseTerrainMaterialPreset value = *current;
        value.distance = static_cast<float>(newTime);
        value.id += suffix;
        cloneTemplates_[token] = value;
        duplicate = MakeState(kMaterialTrack, "material:" + value.id, value.id, newTime);
    } else {
        errorMessage = "Course key type cannot be duplicated.";
        return false;
    }
    duplicate.payload = token;
    return true;
}

bool CourseSequencerTrackProvider::SetKeyTime(
    const EditorSequencerKeyState& state,
    std::string& errorMessage) {
    const std::string_view id = state.handle.keyId;
    if (StartsWith(id, "event:")) {
        if (auto* value = FindMutable(course_->events, [&](const auto& item) { return item.editorGuid == AfterPrefix(id, "event:"); })) {
            value->distance = static_cast<float>(state.time); return true;
        }
    } else if (StartsWith(id, "terrain:")) {
        if (auto* value = FindMutable(course_->terrainPlacements, [&](const auto& item) { return item.editorGuid == AfterPrefix(id, "terrain:"); })) {
            value->distance = static_cast<float>(state.time); return true;
        }
    } else if (StartsWith(id, "rock:")) {
        if (auto* value = FindMutable(course_->rockClusters, [&](const auto& item) { return item.editorGuid == AfterPrefix(id, "rock:"); })) {
            value->distance = static_cast<float>(state.time); return true;
        }
    } else if (StartsWith(id, "camera:")) {
        if (auto* value = FindMutable(course_->cameraKeys, [&](const auto& item) { return item.editorGuid == AfterPrefix(id, "camera:"); })) {
            value->distance = static_cast<float>(state.time); return true;
        }
    } else if (StartsWith(id, "shot:")) {
        if (auto* value = FindMutable(course_->cinematicCameraShots, [&](const auto& item) { return item.id == AfterPrefix(id, "shot:"); })) {
            CourseCinematicCameraShot restored = *value;
            if (StartsWith(state.payload, "camera-shot-v") &&
                !DecodeCameraShot(state.payload, restored, errorMessage)) return false;
            restored.id = AfterPrefix(id, "shot:");
            restored.startDistance = static_cast<float>((std::max)(0.0, state.time));
            restored.endDistance = restored.startDistance +
                static_cast<float>((std::max)(0.001, state.duration));
            *value = std::move(restored);
            return true;
        }
    } else if (StartsWith(id, "lighting:")) {
        if (auto* value = FindMutable(course_->lightingPresets, [&](const auto& item) { return item.id == AfterPrefix(id, "lighting:"); })) {
            value->distance = static_cast<float>(state.time); return true;
        }
    } else if (StartsWith(id, "material:")) {
        if (auto* value = FindMutable(course_->terrainMaterialPresets, [&](const auto& item) { return item.id == AfterPrefix(id, "material:"); })) {
            value->distance = static_cast<float>(state.time); return true;
        }
    }
    errorMessage = "Course Sequencer key no longer exists: " + state.handle.keyId;
    return false;
}

bool CourseSequencerTrackProvider::RemoveKey(
    const EditorSequencerKeyHandle& handle,
    std::string& errorMessage) {
    const std::string_view id = handle.keyId;
    bool removed = false;
    if (StartsWith(id, "event:")) removed = EraseFirst(course_->events, [&](const auto& item) { return item.editorGuid == AfterPrefix(id, "event:"); });
    else if (StartsWith(id, "terrain:")) removed = EraseFirst(course_->terrainPlacements, [&](const auto& item) { return item.editorGuid == AfterPrefix(id, "terrain:"); });
    else if (StartsWith(id, "rock:")) removed = EraseFirst(course_->rockClusters, [&](const auto& item) { return item.editorGuid == AfterPrefix(id, "rock:"); });
    else if (StartsWith(id, "camera:")) removed = EraseFirst(course_->cameraKeys, [&](const auto& item) { return item.editorGuid == AfterPrefix(id, "camera:"); });
    else if (StartsWith(id, "shot:")) removed = EraseFirst(course_->cinematicCameraShots, [&](const auto& item) { return item.id == AfterPrefix(id, "shot:"); });
    else if (StartsWith(id, "lighting:")) removed = EraseFirst(course_->lightingPresets, [&](const auto& item) { return item.id == AfterPrefix(id, "lighting:"); });
    else if (StartsWith(id, "material:")) removed = EraseFirst(course_->terrainMaterialPresets, [&](const auto& item) { return item.id == AfterPrefix(id, "material:"); });
    if (!removed) errorMessage = "Course Sequencer key could not be removed: " + handle.keyId;
    return removed;
}

bool CourseSequencerTrackProvider::InsertKey(
    const EditorSequencerKeyState& state,
    std::string& errorMessage) {
    if (StartsWith(state.handle.keyId, "shot:") && StartsWith(state.payload, "camera-shot-v")) {
        CourseCinematicCameraShot shot;
        if (!DecodeCameraShot(state.payload, shot, errorMessage)) return false;
        shot.id = AfterPrefix(state.handle.keyId, "shot:");
        course_->cinematicCameraShots.push_back(shot);
        return SetKeyTime(state, errorMessage);
    }
    const auto found = cloneTemplates_.find(state.payload);
    if (found == cloneTemplates_.end()) {
        errorMessage = "Course Sequencer clone template is unavailable.";
        return false;
    }
    std::visit([&](const auto& value) {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, CourseEventMarker>) course_->events.push_back(value);
        else if constexpr (std::is_same_v<T, CourseTerrainPlacement>) course_->terrainPlacements.push_back(value);
        else if constexpr (std::is_same_v<T, CourseRockCluster>) course_->rockClusters.push_back(value);
        else if constexpr (std::is_same_v<T, CourseCameraKey>) course_->cameraKeys.push_back(value);
        else if constexpr (std::is_same_v<T, CourseCinematicCameraShot>) course_->cinematicCameraShots.push_back(value);
        else if constexpr (std::is_same_v<T, CourseLightingPreset>) course_->lightingPresets.push_back(value);
        else if constexpr (std::is_same_v<T, CourseTerrainMaterialPreset>) course_->terrainMaterialPresets.push_back(value);
    }, found->second);
    return SetKeyTime(state, errorMessage);
}

bool CourseSequencerTrackProvider::ApplyMutations(
    const std::vector<EditorSequencerKeyMutation>& mutations,
    EditorTransactionApplyMode mode,
    std::string& errorMessage) {
    if (course_ == nullptr) {
        errorMessage = "Course Sequencer provider is not bound.";
        return false;
    }
    const CourseAsset backup = *course_;
    for (const EditorSequencerKeyMutation& mutation : mutations) {
        const EditorSequencerKeyState& desired =
            mode == EditorTransactionApplyMode::Undo ? mutation.before : mutation.after;
        const EditorSequencerKeyState& previous =
            mode == EditorTransactionApplyMode::Undo ? mutation.after : mutation.before;
        bool applied = false;
        if (!desired.exists) {
            applied = RemoveKey(previous.handle, errorMessage);
        } else {
            EditorSequencerKeyState current;
            std::string captureError;
            if (CaptureKey(desired.handle, current, captureError)) {
                applied = SetKeyTime(desired, errorMessage);
            } else {
                applied = InsertKey(desired, errorMessage);
            }
        }
        if (!applied) {
            *course_ = backup;
            return false;
        }
    }
    course_->SortForRuntime();
    return true;
}

} // namespace editor
