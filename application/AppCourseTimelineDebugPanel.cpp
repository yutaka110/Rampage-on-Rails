#include "AppCourseTimelineDebugPanel.h"

#if defined(GE3_ENABLE_IMGUI) && GE3_ENABLE_IMGUI

#include "course/CourseAsset.h"
#include "course/CourseCollisionSystem.h"
#include "course/CourseSpawnRuntime.h"
#include "course/CourseGameplayWaveRuntimeBridge.h"
#include "course/CourseValidation.h"
#include "course/PlayerCombatFeelSystem.h"
#include "course/SectionCheckpointSystem.h"
#include "AppRuntimeState.h"
#include "editor/EditorDirtyStateService.h"
#include "editor/EditorDocumentLifecycleService.h"
#include "editor/EditorModalConfirmService.h"
#include "editor/EditorTransactionStack.h"
#include "editor/EditorViewportAuthoringInputGuard.h"
#include "editor/course/CoursePreviewSimulationSystem.h"
#include "editor/course/CoursePreviewActorRuntimeBridge.h"
#include "editor/sequencer/EditorSequencer.h"

#include "../../externals/imgui/imgui.h"

#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <string>
#include <utility>

namespace {
constexpr const char* kCourseAuthoringDirtyId = "course.authoring";

uint32_t CourseAuthoringRevision(const CourseTimelineDebugPanelInput& input) {
    return input.runtimeState != nullptr
        ? input.runtimeState->terrain.courseObjectEditRevision
        : 0u;
}

void MarkCourseAuthoringDirty(
    const CourseTimelineDebugPanelInput& input,
    const char* reason) {
    if (input.dirtyState == nullptr) {
        return;
    }
    input.dirtyState->MarkDirty(
        editor::EditorDirtyDomain::CourseAuthoring,
        kCourseAuthoringDirtyId,
        "Course Authoring",
        reason != nullptr ? reason : "Course authoring changed.",
        CourseAuthoringRevision(input));
}

void ClearCourseAuthoringDirty(const CourseTimelineDebugPanelInput& input) {
    if (input.dirtyState == nullptr) {
        return;
    }
    input.dirtyState->ClearDomain(editor::EditorDirtyDomain::CourseAuthoring);
}

void RequestCourseReloadConfirmation(
    const CourseTimelineDebugPanelInput& input,
    std::string& authoringStatus) {
    if (input.documentLifecycle == nullptr || !input.onReloadCourse) {
        return;
    }

    const editor::EditorDocumentLifecycleResult result =
        input.documentLifecycle->RequestReloadCourse(input.onReloadCourse);
    authoringStatus = result.message;
}

void RequestTerrainPlacementDeleteConfirmation(
    const CourseTimelineDebugPanelInput& input,
    CourseAsset& course,
    int terrainIndex) {
    if (input.confirmService == nullptr) {
        return;
    }

    CourseAsset* coursePtr = &course;
    editor::EditorModalConfirmRequest request{};
    request.severity = editor::EditorModalConfirmSeverity::Warning;
    request.source = "Course Object";
    request.title = "Delete Terrain Placement";
    request.message = "Delete the selected course terrain placement? This cannot be undone after the confirmation is accepted.";
    request.confirmLabel = "Delete";
    request.cancelLabel = "Cancel";
    request.onConfirm = [input, coursePtr, terrainIndex]() {
        if (input.runtimeState == nullptr || coursePtr == nullptr) {
            return;
        }
        TerrainAuthoringState& editor = input.runtimeState->terrain;
        if (terrainIndex < 0 ||
            terrainIndex >= static_cast<int>(coursePtr->terrainPlacements.size())) {
            return;
        }
        coursePtr->terrainPlacements.erase(
            coursePtr->terrainPlacements.begin() + terrainIndex);
        editor.selectedCourseTerrainPlacement = -1;
        ++editor.courseObjectEditRevision;
        MarkCourseAuthoringDirty(input, "Course terrain placement deleted.");
    };
    input.confirmService->Request(std::move(request));
}

void RequestRockClusterDeleteConfirmation(
    const CourseTimelineDebugPanelInput& input,
    CourseAsset& course,
    int clusterIndex) {
    if (input.confirmService == nullptr) {
        return;
    }

    CourseAsset* coursePtr = &course;
    editor::EditorModalConfirmRequest request{};
    request.severity = editor::EditorModalConfirmSeverity::Warning;
    request.source = "Course Object";
    request.title = "Delete Rock Cluster";
    request.message = "Delete the selected course rock cluster? This cannot be undone after the confirmation is accepted.";
    request.confirmLabel = "Delete";
    request.cancelLabel = "Cancel";
    request.onConfirm = [input, coursePtr, clusterIndex]() {
        if (input.runtimeState == nullptr || coursePtr == nullptr) {
            return;
        }
        TerrainAuthoringState& editor = input.runtimeState->terrain;
        if (clusterIndex < 0 ||
            clusterIndex >= static_cast<int>(coursePtr->rockClusters.size())) {
            return;
        }
        coursePtr->rockClusters.erase(
            coursePtr->rockClusters.begin() + clusterIndex);
        editor.selectedCourseRockCluster = -1;
        ++editor.courseObjectEditRevision;
        MarkCourseAuthoringDirty(input, "Course rock cluster deleted.");
    };
    input.confirmService->Request(std::move(request));
}

ImU32 ColorForEventType(const std::string& type) {
    if (type == "enemy_wave") {
        return IM_COL32(255, 92, 48, 255);
    }
    if (type == "obstacle") {
        return IM_COL32(255, 176, 40, 255);
    }
    if (type == "boss" || type == "boss_phase") {
        return IM_COL32(255, 40, 80, 255);
    }
    if (type == "checkpoint") {
        return IM_COL32(80, 255, 140, 255);
    }
    if (type == "setpiece") {
        return IM_COL32(190, 100, 255, 255);
    }
    return IM_COL32(90, 210, 255, 255);
}

ImU32 SectionColor(size_t index) {
    constexpr ImU32 colors[] = {
        IM_COL32(55, 86, 115, 140),
        IM_COL32(82, 72, 118, 140),
        IM_COL32(92, 96, 66, 140),
        IM_COL32(110, 72, 66, 140),
    };
    return colors[index % (sizeof(colors) / sizeof(colors[0]))];
}

float NormalizeDistance(float distance, float length) {
    if (length <= 0.001f) {
        return 0.0f;
    }
    return (std::clamp)(distance / length, 0.0f, 1.0f);
}

float ClampDistance(float distance, float length) {
    if (length <= 0.0f) {
        return (std::max)(0.0f, distance);
    }
    return (std::clamp)(distance, 0.0f, length);
}

float DistanceToTimelineX(float distance, float length, float x, float width) {
    return x + NormalizeDistance(distance, length) * width;
}

float TimelineXToDistance(float mouseX, float length, float x, float width) {
    if (width <= 0.001f) {
        return 0.0f;
    }
    const float t = (std::clamp)((mouseX - x) / width, 0.0f, 1.0f);
    return t * (std::max)(0.0f, length);
}

const CourseEventMarker* FindNextEvent(const CourseAsset& course, float distance) {
    for (const CourseEventMarker& event : course.events) {
        if (event.distance >= distance) {
            return &event;
        }
    }
    return nullptr;
}

bool DrawTimelineDragHandle(
    const char* label,
    float& distance,
    float railLength,
    const ImVec2& canvasPos,
    const ImVec2& canvasSize,
    float y,
    float height,
    ImU32 color,
    const char* tooltip) {
    bool changed = false;
    const float x = DistanceToTimelineX(distance, railLength, canvasPos.x, canvasSize.x);
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->AddCircleFilled(ImVec2(x, y + height * 0.5f), 5.0f, color);
    drawList->AddLine(ImVec2(x, y), ImVec2(x, y + height), color, 2.0f);

    ImGui::SetCursorScreenPos(ImVec2(x - 8.0f, y));
    ImGui::InvisibleButton(label, ImVec2(16.0f, height));
    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f)) {
        distance = ClampDistance(
            TimelineXToDistance(ImGui::GetIO().MousePos.x, railLength, canvasPos.x, canvasSize.x),
            railLength);
        changed = true;
    }
    if (ImGui::IsItemHovered() || ImGui::IsItemActive()) {
        ImGui::SetTooltip("%s %.1fm", tooltip, distance);
    }
    return changed;
}

bool DrawAuthoringTimeline(
    const CourseTimelineDebugPanelInput& input,
    CourseAsset& course,
    std::string& authoringStatus) {
    const float railLength = (std::max)(input.railLength, 0.0f);
    const ImVec2 canvasPos = ImGui::GetCursorScreenPos();
    const float canvasWidth = (std::max)(ImGui::GetContentRegionAvail().x, 320.0f);
    constexpr float canvasHeight = 126.0f;
    const ImVec2 canvasSize(canvasWidth, canvasHeight);
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    bool changed = false;

    drawList->AddRectFilled(
        canvasPos,
        ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y),
        IM_COL32(16, 19, 24, 255),
        4.0f);
    drawList->AddRect(
        canvasPos,
        ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y),
        IM_COL32(76, 88, 104, 255),
        4.0f);

    const float sectionTop = canvasPos.y + 10.0f;
    const float sectionBottom = canvasPos.y + 40.0f;
    const float cameraTop = canvasPos.y + 48.0f;
    const float eventTop = canvasPos.y + 76.0f;
    for (size_t index = 0; index < course.sections.size(); ++index) {
        CourseSection& section = course.sections[index];
        const float x0 = DistanceToTimelineX(section.startDistance, railLength, canvasPos.x, canvasSize.x);
        const float x1 = DistanceToTimelineX(section.endDistance, railLength, canvasPos.x, canvasSize.x);
        drawList->AddRectFilled(
            ImVec2(x0, sectionTop),
            ImVec2((std::max)(x1, x0 + 2.0f), sectionBottom),
            SectionColor(index),
            2.0f);
        drawList->AddText(ImVec2(x0 + 4.0f, sectionTop + 8.0f), IM_COL32(235, 241, 248, 220), section.name.c_str());
    }

    const float currentX = DistanceToTimelineX(input.currentDistance, railLength, canvasPos.x, canvasSize.x);
    drawList->AddLine(
        ImVec2(currentX, canvasPos.y + 4.0f),
        ImVec2(currentX, canvasPos.y + canvasSize.y - 4.0f),
        IM_COL32(255, 255, 255, 255),
        2.0f);

    ImGui::Dummy(canvasSize);

    for (size_t index = 0; index < course.sections.size(); ++index) {
        CourseSection& section = course.sections[index];
        ImGui::PushID(static_cast<int>(index));
        changed |= DrawTimelineDragHandle(
            "##sectionStart",
            section.startDistance,
            railLength,
            canvasPos,
            canvasSize,
            sectionTop - 2.0f,
            sectionBottom - sectionTop + 4.0f,
            IM_COL32(170, 220, 255, 255),
            "Section start");
        changed |= DrawTimelineDragHandle(
            "##sectionEnd",
            section.endDistance,
            railLength,
            canvasPos,
            canvasSize,
            sectionTop - 2.0f,
            sectionBottom - sectionTop + 4.0f,
            IM_COL32(255, 220, 120, 255),
            "Section end");
        if (section.endDistance < section.startDistance) {
            std::swap(section.startDistance, section.endDistance);
            changed = true;
        }
        ImGui::PopID();
    }

    for (size_t index = 0; index < course.cameraKeys.size(); ++index) {
        CourseCameraKey& key = course.cameraKeys[index];
        ImGui::PushID(static_cast<int>(index));
        changed |= DrawTimelineDragHandle(
            "##cameraKey",
            key.distance,
            railLength,
            canvasPos,
            canvasSize,
            cameraTop,
            22.0f,
            IM_COL32(95, 190, 255, 255),
            "Camera key");
        ImGui::PopID();
    }

    for (size_t index = 0; index < course.events.size(); ++index) {
        CourseEventMarker& event = course.events[index];
        ImGui::PushID(static_cast<int>(index));
        changed |= DrawTimelineDragHandle(
            "##event",
            event.distance,
            railLength,
            canvasPos,
            canvasSize,
            eventTop,
            36.0f,
            ColorForEventType(event.type),
            event.id.empty() ? event.type.c_str() : event.id.c_str());
        ImGui::PopID();
    }

    ImGui::SetCursorScreenPos(ImVec2(canvasPos.x, canvasPos.y + canvasSize.y + 6.0f));

    if (ImGui::Button("Sort Dragged Keys")) {
        course.SortForRuntime();
        changed = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Teleport Current Section")) {
        const CourseSection* section = course.FindSection(input.currentDistance);
        if (section != nullptr && input.onTeleportToDistance) {
            input.onTeleportToDistance(section->startDistance);
            authoringStatus = "Teleported to current section start.";
        }
    }

    return changed;
}

bool DrawProviderSequencer(
    const CourseTimelineDebugPanelInput& input,
    bool canMutate,
    std::string& authoringStatus) {
    editor::EditorSequencerService* sequencer = input.sequencer;
    if (sequencer == nullptr) return false;

    sequencer->SetPreviewPosition(input.currentDistance, false);
    bool changed = false;
    const bool keyboardShortcutAllowed =
        ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
        !ImGui::GetIO().WantTextInput && ImGui::GetIO().KeyCtrl;
    const bool copyShortcut = keyboardShortcutAllowed && ImGui::IsKeyPressed(ImGuiKey_C, false);
    const bool pasteShortcut = keyboardShortcutAllowed && ImGui::IsKeyPressed(ImGuiKey_V, false);
    if (ImGui::Button("Copy Keys")) {
        std::string error;
        authoringStatus = sequencer->CopySelection(error)
            ? "Copied " + std::to_string(sequencer->ClipboardCount()) + " Sequencer key(s)."
            : error;
    }
    if (copyShortcut) {
        std::string error;
        authoringStatus = sequencer->CopySelection(error)
            ? "Copied " + std::to_string(sequencer->ClipboardCount()) + " Sequencer key(s)."
            : error;
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(!canMutate || sequencer->ClipboardCount() == 0);
    if (ImGui::Button("Paste Keys")) {
        std::string error;
        changed = sequencer->PasteAt(sequencer->PreviewPosition(), error);
        authoringStatus = changed ? "Pasted Sequencer keys." : error;
    }
    if (pasteShortcut && canMutate && sequencer->ClipboardCount() > 0) {
        std::string error;
        changed = sequencer->PasteAt(sequencer->PreviewPosition(), error);
        authoringStatus = changed ? "Pasted Sequencer keys." : error;
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    bool snapEnabled = sequencer->SnapEnabled();
    if (ImGui::Checkbox("Snap", &snapEnabled)) sequencer->SetSnapEnabled(snapEnabled);
    ImGui::SameLine();
    float snapInterval = static_cast<float>(sequencer->SnapInterval());
    ImGui::SetNextItemWidth(90.0f);
    if (ImGui::DragFloat("##SequencerSnap", &snapInterval, 0.5f, 0.1f, 100.0f, "%.1fm")) {
        sequencer->SetSnapInterval(snapInterval);
    }
    ImGui::SameLine();
    ImGui::TextDisabled(
        "Selected %u | Clipboard %u | Ctrl/Shift-click multi-select",
        static_cast<unsigned int>(sequencer->Selection().size()),
        static_cast<unsigned int>(sequencer->ClipboardCount()));

    const std::vector<editor::EditorSequencerTrack> tracks = sequencer->BuildTracks();
    const float railLength = (std::max)(1.0f, input.railLength);
    const ImVec2 canvasPos = ImGui::GetCursorScreenPos();
    const float canvasWidth = (std::max)(ImGui::GetContentRegionAvail().x, 420.0f);
    constexpr float headerWidth = 138.0f;
    constexpr float rulerHeight = 24.0f;
    constexpr float rowHeight = 28.0f;
    const float canvasHeight = rulerHeight + rowHeight * static_cast<float>(tracks.size());
    const float trackLeft = canvasPos.x + headerWidth;
    const float trackWidth = (std::max)(1.0f, canvasWidth - headerWidth);
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->AddRectFilled(
        canvasPos,
        ImVec2(canvasPos.x + canvasWidth, canvasPos.y + canvasHeight),
        IM_COL32(15, 18, 23, 255), 4.0f);
    drawList->AddRect(
        canvasPos,
        ImVec2(canvasPos.x + canvasWidth, canvasPos.y + canvasHeight),
        IM_COL32(75, 88, 104, 255), 4.0f);
    drawList->AddLine(
        ImVec2(trackLeft, canvasPos.y),
        ImVec2(trackLeft, canvasPos.y + canvasHeight),
        IM_COL32(62, 72, 86, 255));
    for (int tick = 0; tick <= 10; ++tick) {
        const float x = trackLeft + trackWidth * static_cast<float>(tick) / 10.0f;
        drawList->AddLine(
            ImVec2(x, canvasPos.y + rulerHeight),
            ImVec2(x, canvasPos.y + canvasHeight),
            IM_COL32(43, 50, 61, 180));
        char text[32]{};
        std::snprintf(text, sizeof(text), "%.0f", railLength * tick / 10.0f);
        drawList->AddText(ImVec2(x + 2.0f, canvasPos.y + 4.0f), IM_COL32(145, 157, 173, 255), text);
    }
    for (std::size_t trackIndex = 0; trackIndex < tracks.size(); ++trackIndex) {
        const editor::EditorSequencerTrack& track = tracks[trackIndex];
        const float rowTop = canvasPos.y + rulerHeight + rowHeight * static_cast<float>(trackIndex);
        if ((trackIndex & 1u) != 0u) {
            drawList->AddRectFilled(
                ImVec2(canvasPos.x, rowTop),
                ImVec2(canvasPos.x + canvasWidth, rowTop + rowHeight),
                IM_COL32(24, 28, 35, 160));
        }
        drawList->AddText(
            ImVec2(canvasPos.x + 7.0f, rowTop + 6.0f),
            track.locked ? IM_COL32(120, 126, 136, 255) : IM_COL32(218, 225, 234, 255),
            track.label.c_str());
    }
    const float previewX = trackLeft +
        static_cast<float>((std::clamp)(sequencer->PreviewPosition() / railLength, 0.0, 1.0)) * trackWidth;
    drawList->AddLine(
        ImVec2(previewX, canvasPos.y),
        ImVec2(previewX, canvasPos.y + canvasHeight),
        IM_COL32(255, 255, 255, 255), 2.0f);

    ImGui::Dummy(ImVec2(canvasWidth, canvasHeight));
    bool keyHovered = false;
    static float dragStartX = 0.0f;
    for (std::size_t trackIndex = 0; trackIndex < tracks.size(); ++trackIndex) {
        const editor::EditorSequencerTrack& track = tracks[trackIndex];
        const float rowTop = canvasPos.y + rulerHeight + rowHeight * static_cast<float>(trackIndex);
        for (std::size_t keyIndex = 0; keyIndex < track.keys.size(); ++keyIndex) {
            const editor::EditorSequencerKeyState& key = track.keys[keyIndex];
            const float x = trackLeft +
                static_cast<float>((std::clamp)(key.time / railLength, 0.0, 1.0)) * trackWidth;
            const bool selected = sequencer->IsSelected(key.handle);
            const ImU32 color = static_cast<ImU32>(track.colorRgba);
            drawList->AddCircleFilled(ImVec2(x, rowTop + rowHeight * 0.5f), selected ? 6.5f : 5.0f, color);
            if (selected) {
                drawList->AddCircle(ImVec2(x, rowTop + rowHeight * 0.5f), 8.0f, IM_COL32(255, 255, 255, 255), 0, 2.0f);
            }
            ImGui::SetCursorScreenPos(ImVec2(x - 9.0f, rowTop + 3.0f));
            ImGui::PushID(track.id.c_str());
            ImGui::PushID(static_cast<int>(keyIndex));
            ImGui::InvisibleButton("##SequencerKey", ImVec2(18.0f, rowHeight - 6.0f));
            if (ImGui::IsItemHovered()) {
                keyHovered = true;
                ImGui::SetTooltip("%s\n%s  %.2fm%s",
                    track.label.c_str(), key.label.c_str(), key.time,
                    key.locked ? " [Locked]" : "");
            }
            if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
                const ImGuiIO& io = ImGui::GetIO();
                sequencer->Select(key.handle, io.KeyCtrl || io.KeyShift, io.KeyCtrl);
            }
            if (canMutate && !key.locked && ImGui::IsItemActivated()) {
                std::string error;
                if (sequencer->BeginInteractiveEdit(error)) {
                    dragStartX = ImGui::GetIO().MousePos.x;
                } else {
                    authoringStatus = error;
                }
            }
            if (canMutate && sequencer->InteractiveEditActive() && ImGui::IsItemActive() &&
                ImGui::IsMouseDragging(ImGuiMouseButton_Left, 1.0f)) {
                const double delta =
                    (ImGui::GetIO().MousePos.x - dragStartX) / trackWidth * railLength;
                std::string error;
                if (!sequencer->PreviewInteractiveMove(delta, error)) authoringStatus = error;
            }
            if (sequencer->InteractiveEditActive() && ImGui::IsItemDeactivated()) {
                std::string error;
                if (sequencer->CommitInteractiveEdit("Move Sequencer Keys", error)) {
                    changed = true;
                    authoringStatus = "Moved Sequencer key(s).";
                } else {
                    authoringStatus = error;
                }
            }
            ImGui::PopID();
            ImGui::PopID();
        }
    }
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    const bool mouseInTrack = mouse.x >= trackLeft && mouse.x <= trackLeft + trackWidth &&
        mouse.y >= canvasPos.y && mouse.y <= canvasPos.y + canvasHeight;
    if (!keyHovered && mouseInTrack && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        const double position = (mouse.x - trackLeft) / trackWidth * railLength;
        sequencer->SetPreviewPosition(position, true);
        authoringStatus = "Sequencer preview: " + std::to_string(sequencer->PreviewPosition()) + "m";
    }
    ImGui::SetCursorScreenPos(ImVec2(canvasPos.x, canvasPos.y + canvasHeight + 6.0f));
    return changed;
}

void DrawCoursePreviewSimulation(const CourseTimelineDebugPanelInput& input) {
    editor::CoursePreviewSimulationSystem* simulation = input.previewSimulation;
    if (simulation == nullptr) {
        ImGui::TextUnformatted("Course preview simulation service is unavailable.");
        return;
    }

    const auto syncViewportDistance = [&]() {
        if (input.onTeleportToDistance) {
            input.onTeleportToDistance(simulation->Frame().distance);
        }
    };
    std::string error;
    if (!simulation->IsActive()) {
        if (ImGui::Button("Play Preview From Current Distance")) {
            if (simulation->BeginPreview(*input.course, input.currentDistance, &error)) {
                syncViewportDistance();
            }
        }
        ImGui::SameLine();
        ImGui::TextDisabled(
            "Creates an immutable snapshot; Course authoring remains untouched.");
        if (!error.empty()) {
            ImGui::TextColored(ImVec4(1.0f, 0.30f, 0.25f, 1.0f), "%s", error.c_str());
        }
        if (simulation->Frame().playback == editor::CoursePreviewPlaybackState::Error) {
            ImGui::TextColored(
                ImVec4(1.0f, 0.30f, 0.25f, 1.0f),
                "%s",
                simulation->Frame().message.c_str());
        }
        return;
    }

    if (simulation->IsPlaying()) {
        if (ImGui::Button("Pause")) simulation->Pause();
    } else {
        if (ImGui::Button("Play")) simulation->Play();
    }
    ImGui::SameLine();
    if (ImGui::Button("Stop")) simulation->Stop();
    ImGui::SameLine();
    if (ImGui::Button("Restart")) {
        if (simulation->Restart(true, &error)) syncViewportDistance();
    }
    ImGui::SameLine();
    if (ImGui::Button("Step 1 Frame")) {
        if (simulation->Step(&error)) syncViewportDistance();
    }
    ImGui::SameLine();
    if (ImGui::Button("Defeat Active Enemies")) {
        simulation->DefeatActiveEnemies();
    }

    const editor::CoursePreviewSimulationFrame& frame = simulation->Frame();
    ImGui::Text(
        "%s | %.2fm / %.2fm | %.2fs | %.2fm/s | revision %llu",
        editor::ToString(frame.playback),
        frame.distance,
        frame.railLength,
        frame.elapsedSeconds,
        frame.currentSpeed,
        static_cast<unsigned long long>(frame.simulationRevision));
    ImGui::Text(
        "Waves: active %u  prewarm/blocked %u  complete %u | Enemies active %u",
        frame.activeWaves,
        frame.prewarmingWaves,
        frame.completedWaves,
        frame.activeEnemies);
    if (!frame.message.empty()) ImGui::TextDisabled("%s", frame.message.c_str());
    if (!error.empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.30f, 0.25f, 1.0f), "%s", error.c_str());
    }

    float previewDistance = frame.distance;
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::SliderFloat(
            "##CoursePreviewDistance",
            &previewDistance,
            0.0f,
            (std::max)(1.0f, frame.railLength),
            "Preview %.1fm")) {
        if (simulation->Seek(previewDistance, &error)) syncViewportDistance();
    }

    editor::CoursePreviewSimulationSettings settings = simulation->Settings();
    ImGui::SetNextItemWidth(120.0f);
    ImGui::DragFloat(
        "Travel Speed (0 = Rail)", &settings.travelSpeed, 0.5f, 0.0f, 500.0f, "%.1fm/s");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(90.0f);
    ImGui::DragFloat("Playback", &settings.playbackRate, 0.05f, 0.05f, 8.0f, "%.2fx");
    ImGui::SameLine();
    ImGui::Checkbox("Loop", &settings.loop);
    ImGui::SameLine();
    ImGui::Checkbox("Viewport Overlay", &settings.showViewportOverlay);
    ImGui::Checkbox("Automatically Resolve Enemies", &settings.automaticallyDefeatEnemies);
    if (settings.automaticallyDefeatEnemies) {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(110.0f);
        ImGui::DragFloat(
            "Resolve Delay",
            &settings.automaticEnemyDefeatSeconds,
            0.1f,
            0.0f,
            60.0f,
            "%.1fs");
    }

    std::string settingsError;
    if (!simulation->Configure(settings, &settingsError)) {
        ImGui::TextColored(ImVec4(1, 0.3f, 0.25f, 1), "%s", settingsError.c_str());
    }

    if (input.previewActors != nullptr) {
        editor::CoursePreviewActorRuntimeBridge& actors = *input.previewActors;
        editor::CoursePreviewActorRuntimeSettings actorSettings = actors.Settings();
        const editor::CoursePreviewActorRuntimeStats& actorStats = actors.Stats();
        ImGui::SeparatorText("Compiled Actor Runtime");
        ImGui::Text(
            "Program: %s | Waves %u | Actors %u | Materialized %u "
            "(prewarm %u / active %u) | fallback %u",
            actorStats.programValid ? "Valid" : "Waiting",
            actorStats.compiledWaves,
            actorStats.compiledActors,
            actorStats.runtimeActors,
            actorStats.prewarmedActors,
            actorStats.activeActors,
            actorStats.fallbackActors);
        ImGui::Checkbox("Show Prewarmed Actor Models", &actorSettings.showPrewarmedActors);
        ImGui::SameLine();
        ImGui::Checkbox("Simulate Movement", &actorSettings.simulateMovement);
        ImGui::SameLine();
        ImGui::Checkbox("Simulate Enemy Fire", &actorSettings.simulateEnemyFire);
        if (actorSettings.simulateMovement) {
            ImGui::SameLine();
            ImGui::Checkbox(
                "Keep Authored Transform",
                &actorSettings.preserveActorsAtAuthoredTransform);
        }
        ImGui::SetNextItemWidth(100.0f);
        ImGui::DragFloat(
            "Prewarm Opacity",
            &actorSettings.prewarmOpacity,
            0.02f,
            0.05f,
            1.0f,
            "%.2f");
        if (!actors.Configure(actorSettings, &settingsError)) {
            ImGui::TextColored(ImVec4(1, 0.3f, 0.25f, 1), "%s", settingsError.c_str());
        }
        if (!actorStats.message.empty()) {
            ImGui::TextDisabled("%s", actorStats.message.c_str());
        }
        const editor::CourseWaveRuntimeCompileResult& compile = actors.CompileResult();
        if (!compile.diagnostics.empty() && ImGui::TreeNode("Compiler Diagnostics")) {
            for (const editor::CourseWaveRuntimeDiagnostic& diagnostic :
                    compile.diagnostics) {
                const ImVec4 color =
                    diagnostic.severity ==
                        editor::CourseWaveRuntimeDiagnosticSeverity::Error
                    ? ImVec4(1.0f, 0.30f, 0.25f, 1.0f)
                    : diagnostic.severity ==
                        editor::CourseWaveRuntimeDiagnosticSeverity::Warning
                    ? ImVec4(1.0f, 0.72f, 0.22f, 1.0f)
                    : ImVec4(0.48f, 0.78f, 1.0f, 1.0f);
                ImGui::TextColored(
                    color,
                    "[%s] %s: %s",
                    editor::ToString(diagnostic.severity),
                    diagnostic.code.c_str(),
                    diagnostic.message.c_str());
            }
            ImGui::TreePop();
        }
    }

    static std::array<char, 128> scriptedEvent{};
    ImGui::SetNextItemWidth(240.0f);
    ImGui::InputText("Scripted Event", scriptedEvent.data(), scriptedEvent.size());
    ImGui::SameLine();
    ImGui::BeginDisabled(scriptedEvent[0] == '\0');
    if (ImGui::Button("Signal Event")) {
        simulation->SignalEvent(scriptedEvent.data());
    }
    ImGui::EndDisabled();

    if (ImGui::BeginTable(
            "CoursePreviewWaves",
            6,
            ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_ScrollY,
            ImVec2(0.0f, 150.0f))) {
        ImGui::TableSetupColumn("Wave");
        ImGui::TableSetupColumn("Phase");
        ImGui::TableSetupColumn("Trigger");
        ImGui::TableSetupColumn("Active Time");
        ImGui::TableSetupColumn("Members");
        ImGui::TableSetupColumn("Defeated");
        ImGui::TableHeadersRow();
        for (const editor::CoursePreviewWaveState& wave : simulation->Waves()) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn(); ImGui::TextUnformatted(wave.displayName.c_str());
            ImGui::TableNextColumn(); ImGui::TextUnformatted(editor::ToString(wave.phase));
            ImGui::TableNextColumn(); ImGui::Text("%.1f", wave.triggerDistance);
            ImGui::TableNextColumn(); ImGui::Text("%.2f", wave.activeSeconds);
            ImGui::TableNextColumn(); ImGui::Text("%u", wave.memberCount);
            ImGui::TableNextColumn(); ImGui::Text("%u", wave.defeatedMemberCount);
        }
        ImGui::EndTable();
    }

    if (ImGui::TreeNode("Preview Event Log")) {
        if (ImGui::BeginChild("CoursePreviewEvents", ImVec2(0.0f, 120.0f), true)) {
            const auto& events = simulation->Events();
            for (auto event = events.rbegin(); event != events.rend(); ++event) {
                ImGui::Text(
                    "[%6.1fm %5.2fs] %s: %s",
                    event->distance,
                    event->elapsedSeconds,
                    editor::ToString(event->type),
                    event->message.c_str());
            }
        }
        ImGui::EndChild();
        ImGui::TreePop();
    }
}

ImVec4 SeverityColor(CourseValidationSeverity severity) {
    switch (severity) {
    case CourseValidationSeverity::Error:
        return ImVec4(1.0f, 0.25f, 0.22f, 1.0f);
    case CourseValidationSeverity::Warning:
        return ImVec4(1.0f, 0.72f, 0.22f, 1.0f);
    case CourseValidationSeverity::Info:
        return ImVec4(0.48f, 0.78f, 1.0f, 1.0f);
    }
    return ImVec4(0.48f, 0.78f, 1.0f, 1.0f);
}

template <size_t Size>
bool InputString(const char* label, std::string& value) {
    std::array<char, Size> buffer{};
    std::snprintf(buffer.data(), buffer.size(), "%s", value.c_str());
    if (ImGui::InputText(label, buffer.data(), buffer.size())) {
        value = buffer.data();
        return true;
    }
    return false;
}

bool DragVector3(const char* label, Vector3& value, float speed, float minValue, float maxValue) {
    float values[3] = {value.x, value.y, value.z};
    if (ImGui::DragFloat3(label, values, speed, minValue, maxValue, "%.2f")) {
        value = {values[0], values[1], values[2]};
        return true;
    }
    return false;
}

bool ColorEditVector4(const char* label, Vector4& value) {
    float values[4] = {value.x, value.y, value.z, value.w};
    if (ImGui::ColorEdit4(label, values)) {
        value = {values[0], values[1], values[2], values[3]};
        return true;
    }
    return false;
}

bool ComboTerrainLayer(const char* label, CourseTerrainLayer& layer) {
    constexpr CourseTerrainLayer values[] = {
        CourseTerrainLayer::GameplayCollision,
        CourseTerrainLayer::HeroLandmark,
        CourseTerrainLayer::VistaBackground,
    };
    int current = 0;
    for (int index = 0; index < static_cast<int>(sizeof(values) / sizeof(values[0])); ++index) {
        if (values[index] == layer) {
            current = index;
            break;
        }
    }
    bool changed = false;
    if (ImGui::BeginCombo(label, ToCourseTerrainLayerString(layer))) {
        for (int index = 0; index < static_cast<int>(sizeof(values) / sizeof(values[0])); ++index) {
            const bool selected = index == current;
            if (ImGui::Selectable(ToCourseTerrainLayerString(values[index]), selected)) {
                layer = values[index];
                changed = true;
            }
            if (selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
    return changed;
}

bool ComboCollisionMode(const char* label, CourseTerrainCollisionMode& mode) {
    constexpr CourseTerrainCollisionMode values[] = {
        CourseTerrainCollisionMode::None,
        CourseTerrainCollisionMode::Proxy,
        CourseTerrainCollisionMode::Solid,
    };
    int current = 0;
    for (int index = 0; index < static_cast<int>(sizeof(values) / sizeof(values[0])); ++index) {
        if (values[index] == mode) {
            current = index;
            break;
        }
    }
    bool changed = false;
    if (ImGui::BeginCombo(label, ToCourseTerrainCollisionModeString(mode))) {
        for (int index = 0; index < static_cast<int>(sizeof(values) / sizeof(values[0])); ++index) {
            const bool selected = index == current;
            if (ImGui::Selectable(ToCourseTerrainCollisionModeString(values[index]), selected)) {
                mode = values[index];
                changed = true;
            }
            if (selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
    return changed;
}

bool ComboRockAnchor(const char* label, CourseRockClusterAnchor& anchor) {
    constexpr CourseRockClusterAnchor values[] = {
        CourseRockClusterAnchor::LeftWall,
        CourseRockClusterAnchor::RightWall,
        CourseRockClusterAnchor::Floor,
        CourseRockClusterAnchor::CeilingBreak,
        CourseRockClusterAnchor::VistaWall,
    };
    int current = 0;
    for (int index = 0; index < static_cast<int>(sizeof(values) / sizeof(values[0])); ++index) {
        if (values[index] == anchor) {
            current = index;
            break;
        }
    }
    bool changed = false;
    if (ImGui::BeginCombo(label, ToCourseRockClusterAnchorString(anchor))) {
        for (int index = 0; index < static_cast<int>(sizeof(values) / sizeof(values[0])); ++index) {
            const bool selected = index == current;
            if (ImGui::Selectable(ToCourseRockClusterAnchorString(values[index]), selected)) {
                anchor = values[index];
                changed = true;
            }
            if (selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
    return changed;
}

bool ComboRockType(const char* label, CourseRockClusterType& type) {
    constexpr CourseRockClusterType values[] = {
        CourseRockClusterType::AttachedDebris,
        CourseRockClusterType::HeroFracture,
        CourseRockClusterType::FallingDebris,
        CourseRockClusterType::VistaSilhouette,
    };
    int current = 0;
    for (int index = 0; index < static_cast<int>(sizeof(values) / sizeof(values[0])); ++index) {
        if (values[index] == type) {
            current = index;
            break;
        }
    }
    bool changed = false;
    if (ImGui::BeginCombo(label, ToCourseRockClusterTypeString(type))) {
        for (int index = 0; index < static_cast<int>(sizeof(values) / sizeof(values[0])); ++index) {
            const bool selected = index == current;
            if (ImGui::Selectable(ToCourseRockClusterTypeString(values[index]), selected)) {
                type = values[index];
                changed = true;
            }
            if (selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
    return changed;
}

std::string FormatPropertyFloat(float value) {
    char buffer[64]{};
    std::snprintf(buffer, sizeof(buffer), "%.3f", value);
    return buffer;
}

std::string FormatPropertyInt(int value) {
    char buffer[32]{};
    std::snprintf(buffer, sizeof(buffer), "%d", value);
    return buffer;
}

std::string FormatPropertyUInt(uint32_t value) {
    char buffer[32]{};
    std::snprintf(buffer, sizeof(buffer), "%u", value);
    return buffer;
}

std::string FormatPropertyVector3(const Vector3& value) {
    char buffer[128]{};
    std::snprintf(buffer, sizeof(buffer), "%.3f, %.3f, %.3f", value.x, value.y, value.z);
    return buffer;
}

editor::EditorObjectHandle MakeCourseObjectPropertyTarget(
    editor::EditorDomainId domain,
    const char* stablePrefix,
    size_t index,
    uint32_t generation,
    const char* displayPrefix) {
    editor::EditorObjectHandle handle{};
    handle.domain = domain;
    handle.stableId = editor::BuildStableIndexedId(stablePrefix, static_cast<uint64_t>(index));
    handle.localIndex = static_cast<uint64_t>(index);
    handle.generation = generation;
    handle.displayName = std::string(displayPrefix) + " #" + std::to_string(index);
    return handle;
}

void StageCoursePropertyDelta(
    const CourseTimelineDebugPanelInput& input,
    const editor::EditorObjectHandle& target,
    const char* propertyPath,
    const char* displayName,
    const char* valueType,
    std::string beforeValue,
    std::string afterValue) {
    if (input.editorTransactions == nullptr || beforeValue == afterValue) {
        return;
    }

    editor::EditorPropertyChange change{};
    change.target = target;
    change.propertyPath = propertyPath != nullptr ? propertyPath : "";
    const bool hiddenLabel = displayName != nullptr && displayName[0] == '#' && displayName[1] == '#';
    if (displayName != nullptr && !hiddenLabel) {
        change.displayName = displayName;
    } else if (propertyPath != nullptr) {
        change.displayName = propertyPath;
    } else {
        change.displayName = "Course Property Edit";
    }
    change.valueType = valueType != nullptr ? valueType : "";
    change.beforeValue = std::move(beforeValue);
    change.afterValue = std::move(afterValue);
    if (input.runtimeState != nullptr) {
        change.sourceRevision = input.runtimeState->terrain.courseObjectEditRevision;
    }
    input.editorTransactions->StagePropertyDelta(std::move(change));
}

template <size_t Size>
bool TrackedInputString(
    const CourseTimelineDebugPanelInput& input,
    const editor::EditorObjectHandle& target,
    const char* label,
    const char* propertyPath,
    std::string& value) {
    const std::string before = value;
    if (!InputString<Size>(label, value)) {
        return false;
    }
    StageCoursePropertyDelta(input, target, propertyPath, label, "string", before, value);
    return true;
}

bool TrackedDragFloat(
    const CourseTimelineDebugPanelInput& input,
    const editor::EditorObjectHandle& target,
    const char* label,
    const char* propertyPath,
    float& value,
    float speed,
    float minValue,
    float maxValue,
    const char* format) {
    const float before = value;
    if (!ImGui::DragFloat(label, &value, speed, minValue, maxValue, format)) {
        return false;
    }
    StageCoursePropertyDelta(
        input,
        target,
        propertyPath,
        label,
        "float",
        FormatPropertyFloat(before),
        FormatPropertyFloat(value));
    return true;
}

bool TrackedDragInt(
    const CourseTimelineDebugPanelInput& input,
    const editor::EditorObjectHandle& target,
    const char* label,
    const char* propertyPath,
    int& value,
    float speed,
    int minValue,
    int maxValue) {
    const int before = value;
    if (!ImGui::DragInt(label, &value, speed, minValue, maxValue)) {
        return false;
    }
    StageCoursePropertyDelta(
        input,
        target,
        propertyPath,
        label,
        "int",
        FormatPropertyInt(before),
        FormatPropertyInt(value));
    return true;
}

bool TrackedDragVector3(
    const CourseTimelineDebugPanelInput& input,
    const editor::EditorObjectHandle& target,
    const char* label,
    const char* propertyPath,
    Vector3& value,
    float speed,
    float minValue,
    float maxValue) {
    const Vector3 before = value;
    if (!DragVector3(label, value, speed, minValue, maxValue)) {
        return false;
    }
    StageCoursePropertyDelta(
        input,
        target,
        propertyPath,
        label,
        "vec3",
        FormatPropertyVector3(before),
        FormatPropertyVector3(value));
    return true;
}

int FindNearestTerrainMaterialPresetIndex(const CourseAsset& course, float distance) {
    int bestIndex = -1;
    float bestDistance = FLT_MAX;
    for (size_t index = 0; index < course.terrainMaterialPresets.size(); ++index) {
        const float delta = std::abs(course.terrainMaterialPresets[index].distance - distance);
        if (delta < bestDistance) {
            bestDistance = delta;
            bestIndex = static_cast<int>(index);
        }
    }
    return bestIndex;
}

bool DrawTerrainMaterialPresetDetails(CourseAsset& course, float selectedDistance) {
    bool changed = false;
    static int selectedMaterialPreset = -1;
    if (course.terrainMaterialPresets.empty()) {
        if (ImGui::Button("Add Material Preset")) {
            course.terrainMaterialPresets.push_back({selectedDistance, "new_material"});
            selectedMaterialPreset = 0;
            changed = true;
        }
        return changed;
    }

    if (selectedMaterialPreset < 0 ||
        selectedMaterialPreset >= static_cast<int>(course.terrainMaterialPresets.size())) {
        selectedMaterialPreset = FindNearestTerrainMaterialPresetIndex(course, selectedDistance);
    }

    const char* preview = selectedMaterialPreset >= 0
        ? course.terrainMaterialPresets[static_cast<size_t>(selectedMaterialPreset)].id.c_str()
        : "-";
    if (ImGui::BeginCombo("Material Preset", preview)) {
        for (size_t index = 0; index < course.terrainMaterialPresets.size(); ++index) {
            const bool selected = static_cast<int>(index) == selectedMaterialPreset;
            if (ImGui::Selectable(course.terrainMaterialPresets[index].id.c_str(), selected)) {
                selectedMaterialPreset = static_cast<int>(index);
            }
            if (selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (ImGui::Button("Add At Object")) {
        CourseTerrainMaterialPreset preset{};
        preset.distance = selectedDistance;
        preset.id = "object_material";
        course.terrainMaterialPresets.push_back(preset);
        selectedMaterialPreset = static_cast<int>(course.terrainMaterialPresets.size() - 1);
        changed = true;
    }

    if (selectedMaterialPreset < 0 ||
        selectedMaterialPreset >= static_cast<int>(course.terrainMaterialPresets.size())) {
        return changed;
    }

    CourseTerrainMaterialPreset& preset =
        course.terrainMaterialPresets[static_cast<size_t>(selectedMaterialPreset)];
    ImGui::PushID("MaterialPresetDetails");
    changed |= InputString<96>("Material Id", preset.id);
    changed |= ImGui::DragFloat("Material Distance", &preset.distance, 1.0f, 0.0f, 10000.0f, "%.1f");
    changed |= ImGui::DragFloat("Blend Distance", &preset.blendDistance, 1.0f, 0.0f, 1000.0f, "%.1f");
    changed |= ColorEditVector4("Base Color", preset.baseColor);
    changed |= ImGui::DragFloat("Brightness", &preset.brightness, 0.01f, 0.05f, 3.0f, "%.2f");
    changed |= ImGui::DragFloat("Specular", &preset.specularStrength, 0.005f, 0.0f, 0.35f, "%.3f");
    changed |= ImGui::DragFloat("Rim", &preset.rimLightStrength, 0.01f, 0.0f, 2.0f, "%.2f");
    changed |= ImGui::DragFloat("Backlight Rim", &preset.backlightRimBoost, 0.01f, 0.0f, 2.0f, "%.2f");
    changed |= ImGui::DragFloat("Cavity AO", &preset.cavityAoStrength, 0.01f, 0.0f, 1.5f, "%.2f");
    changed |= ImGui::DragFloat("Detail Normal", &preset.detailNormalStrength, 0.01f, 0.0f, 2.0f, "%.2f");
    changed |= ImGui::DragFloat("Micro Detail", &preset.microDetailStrength, 0.01f, 0.0f, 2.0f, "%.2f");
    changed |= ImGui::DragFloat("Sky Fill", &preset.skyFillStrength, 0.01f, 0.0f, 1.2f, "%.2f");
    ImGui::PopID();
    return changed;
}

CourseValidationReport BuildValidationReport(const CourseTimelineDebugPanelInput& input, const CourseAsset& course) {
    CourseValidationOptions options{};
    options.railLength = input.railLength;
    return ValidateCourseAsset(course, options);
}

float CourseSummaryBlockHeight() {
    const float textHeight = ImGui::GetTextLineHeightWithSpacing() * 8.0f;
    const float timelineHeight = 42.0f;
    return textHeight + timelineHeight + ImGui::GetStyle().ItemSpacing.y * 3.0f;
}

void DrawTimelineBar(const CourseTimelineDebugPanelInput& input, const CourseAsset& course) {
    const float railLength = (std::max)(input.railLength, course.railPoints.empty() ? 0.0f : input.railLength);
    const ImVec2 canvasPos = ImGui::GetCursorScreenPos();
    const float canvasWidth = (std::max)(ImGui::GetContentRegionAvail().x, 240.0f);
    const float availableHeight = ImGui::GetContentRegionAvail().y;
    const bool compact = availableHeight > 0.0f && availableHeight < 120.0f;
    const float canvasHeight = compact
        ? (std::clamp)(availableHeight - 6.0f, 26.0f, 42.0f)
        : 92.0f;
    const ImVec2 canvasSize(canvasWidth, canvasHeight);
    ImDrawList* drawList = ImGui::GetWindowDrawList();

    drawList->AddRectFilled(
        canvasPos,
        ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y),
        IM_COL32(18, 22, 27, 255),
        4.0f);
    drawList->AddRect(
        canvasPos,
        ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y),
        IM_COL32(75, 88, 104, 255),
        4.0f);

    const float sectionTop = canvasPos.y + (compact ? 6.0f : 8.0f);
    const float sectionBottom = canvasPos.y + (compact ? 13.0f : 42.0f);
    for (size_t index = 0; index < course.sections.size(); ++index) {
        const CourseSection& section = course.sections[index];
        const float x0 = canvasPos.x + NormalizeDistance(section.startDistance, railLength) * canvasSize.x;
        const float x1 = canvasPos.x + NormalizeDistance(section.endDistance, railLength) * canvasSize.x;
        drawList->AddRectFilled(
            ImVec2(x0, sectionTop),
            ImVec2((std::max)(x1, x0 + 2.0f), sectionBottom),
            SectionColor(index),
            2.0f);
    }

    const float eventTop = compact ? canvasPos.y + 16.0f : canvasPos.y + 48.0f;
    const float eventBottom = compact ? canvasPos.y + canvasHeight - 6.0f : canvasPos.y + 82.0f;
    const float eventBaseY = compact ? (eventTop + eventBottom) * 0.5f : canvasPos.y + 64.0f;
    for (const CourseEventMarker& event : course.events) {
        const float x = canvasPos.x + NormalizeDistance(event.distance, railLength) * canvasSize.x;
        const ImU32 color = ColorForEventType(event.type);
        drawList->AddLine(ImVec2(x, eventTop), ImVec2(x, eventBottom), color, compact ? 1.4f : 2.0f);
        if (!compact) {
            drawList->AddCircleFilled(ImVec2(x, eventBaseY), 4.0f, color);
        }
    }

    const float currentX = canvasPos.x + NormalizeDistance(input.currentDistance, railLength) * canvasSize.x;
    drawList->AddLine(
        ImVec2(currentX, canvasPos.y + 4.0f),
        ImVec2(currentX, canvasPos.y + canvasSize.y - 4.0f),
        IM_COL32(255, 255, 255, 255),
        2.0f);
    drawList->AddTriangleFilled(
        ImVec2(currentX, canvasPos.y + 2.0f),
        ImVec2(currentX - 5.0f, canvasPos.y + (compact ? 10.0f : 12.0f)),
        ImVec2(currentX + 5.0f, canvasPos.y + (compact ? 10.0f : 12.0f)),
        IM_COL32(255, 255, 255, 255));

    ImGui::Dummy(canvasSize);
}

void DrawEventTable(const CourseAsset& course, float currentDistance) {
    if (!ImGui::BeginTable(
            "CourseEventTable",
            5,
            ImGuiTableFlags_Borders |
                ImGuiTableFlags_RowBg |
                ImGuiTableFlags_ScrollY,
            ImVec2(0.0f, 220.0f))) {
        return;
    }

    ImGui::TableSetupColumn("State", ImGuiTableColumnFlags_WidthFixed, 58.0f);
    ImGui::TableSetupColumn("Dist", ImGuiTableColumnFlags_WidthFixed, 70.0f);
    ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 110.0f);
    ImGui::TableSetupColumn("Id", ImGuiTableColumnFlags_WidthFixed, 170.0f);
    ImGui::TableSetupColumn("Payload");
    ImGui::TableHeadersRow();

    for (const CourseEventMarker& event : course.events) {
        const bool fired = event.distance < currentDistance;
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        if (fired) {
            ImGui::TextColored(ImVec4(0.55f, 0.65f, 0.72f, 1.0f), "done");
        } else {
            ImGui::TextColored(ImVec4(0.35f, 1.0f, 0.65f, 1.0f), "next");
        }
        ImGui::TableNextColumn();
        ImGui::Text("%.0f", event.distance);
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(event.type.c_str());
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(event.id.c_str());
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(event.payload.empty() ? "-" : event.payload.c_str());
    }

    ImGui::EndTable();
}

void DrawTerrainPlacementTable(const CourseAsset& course, float currentDistance) {
    if (!ImGui::BeginTable(
            "CourseTerrainPlacementTable",
            8,
            ImGuiTableFlags_Borders |
                ImGuiTableFlags_RowBg |
                ImGuiTableFlags_ScrollY |
                ImGuiTableFlags_Resizable,
            ImVec2(0.0f, 240.0f))) {
        return;
    }

    ImGui::TableSetupColumn("State", ImGuiTableColumnFlags_WidthFixed, 58.0f);
    ImGui::TableSetupColumn("Layer", ImGuiTableColumnFlags_WidthFixed, 135.0f);
    ImGui::TableSetupColumn("Dist", ImGuiTableColumnFlags_WidthFixed, 70.0f);
    ImGui::TableSetupColumn("Id", ImGuiTableColumnFlags_WidthFixed, 210.0f);
    ImGui::TableSetupColumn("Mesh", ImGuiTableColumnFlags_WidthFixed, 110.0f);
    ImGui::TableSetupColumn("Collision", ImGuiTableColumnFlags_WidthFixed, 86.0f);
    ImGui::TableSetupColumn("Scale", ImGuiTableColumnFlags_WidthFixed, 110.0f);
    ImGui::TableSetupColumn("Offset");
    ImGui::TableHeadersRow();

    for (const CourseTerrainPlacement& placement : course.terrainPlacements) {
        const bool passed = placement.distance < currentDistance;
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextColored(
            passed ? ImVec4(0.55f, 0.65f, 0.72f, 1.0f) : ImVec4(0.45f, 0.85f, 1.0f, 1.0f),
            "%s",
            passed ? "seen" : "ahead");
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(ToCourseTerrainLayerString(placement.layer));
        ImGui::TableNextColumn();
        ImGui::Text("%.0f", placement.distance);
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(placement.id.c_str());
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(placement.meshId.c_str());
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(ToCourseTerrainCollisionModeString(placement.collisionMode));
        ImGui::TableNextColumn();
        ImGui::Text("%.1f %.1f %.1f", placement.scale.x, placement.scale.y, placement.scale.z);
        ImGui::TableNextColumn();
        ImGui::Text(
            "side %.1f up %.1f fwd %.1f",
            placement.lateralOffset,
            placement.verticalOffset,
            placement.forwardOffset);
    }

    ImGui::EndTable();
}

void DrawRockClusterTable(const CourseAsset& course, float currentDistance) {
    if (!ImGui::BeginTable(
            "CourseRockClusterTable",
            8,
            ImGuiTableFlags_Borders |
                ImGuiTableFlags_RowBg |
                ImGuiTableFlags_ScrollY |
                ImGuiTableFlags_Resizable,
            ImVec2(0.0f, 220.0f))) {
        return;
    }

    ImGui::TableSetupColumn("State", ImGuiTableColumnFlags_WidthFixed, 58.0f);
    ImGui::TableSetupColumn("Dist", ImGuiTableColumnFlags_WidthFixed, 70.0f);
    ImGui::TableSetupColumn("Id", ImGuiTableColumnFlags_WidthFixed, 210.0f);
    ImGui::TableSetupColumn("Mesh", ImGuiTableColumnFlags_WidthFixed, 130.0f);
    ImGui::TableSetupColumn("Anchor", ImGuiTableColumnFlags_WidthFixed, 100.0f);
    ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 120.0f);
    ImGui::TableSetupColumn("Count", ImGuiTableColumnFlags_WidthFixed, 64.0f);
    ImGui::TableSetupColumn("Rules");
    ImGui::TableHeadersRow();

    for (const CourseRockCluster& cluster : course.rockClusters) {
        const float behind = (std::max)(0.0f, cluster.cullBehindDistance);
        const float ahead = (std::max)(0.0f, cluster.cullAheadDistance);
        const float delta = cluster.distance - currentDistance;
        const bool active = delta >= -behind && delta <= ahead;
        const bool retired = delta < -behind;
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextColored(
            active ? ImVec4(0.42f, 0.92f, 0.56f, 1.0f) :
                retired ? ImVec4(0.55f, 0.65f, 0.72f, 1.0f) :
                ImVec4(0.45f, 0.85f, 1.0f, 1.0f),
            "%s",
            active ? "active" : retired ? "retired" : "ahead");
        ImGui::TableNextColumn();
        ImGui::Text("%.0f", cluster.distance);
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(cluster.id.c_str());
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(cluster.meshId.c_str());
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(ToCourseRockClusterAnchorString(cluster.anchor));
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(ToCourseRockClusterTypeString(cluster.type));
        ImGui::TableNextColumn();
        ImGui::Text("%u", cluster.count);
        ImGui::TableNextColumn();
        ImGui::Text(
            "scale %.1f-%.1f  clear %.1f  spread %.1f %.1f %.1f  rot %.0f %.0f %.0f  overrides %zu  cull %.0f/%.0f",
            cluster.minScale,
            cluster.maxScale,
            cluster.clearLaneRadius,
            cluster.spread.x,
            cluster.spread.y,
            cluster.spread.z,
            cluster.rotation.x * 180.0f / 3.14159265358979323846f,
            cluster.rotation.y * 180.0f / 3.14159265358979323846f,
            cluster.rotation.z * 180.0f / 3.14159265358979323846f,
            cluster.instanceOverrides.size(),
            behind,
            ahead);
    }

    ImGui::EndTable();
}

void DrawVisualPresetTable(const CourseAsset& course, float currentDistance) {
    if (!ImGui::BeginTable(
            "CourseVisualPresetTable",
            6,
            ImGuiTableFlags_Borders |
                ImGuiTableFlags_RowBg |
                ImGuiTableFlags_ScrollY |
                ImGuiTableFlags_Resizable,
            ImVec2(0.0f, 240.0f))) {
        return;
    }

    ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 115.0f);
    ImGui::TableSetupColumn("State", ImGuiTableColumnFlags_WidthFixed, 58.0f);
    ImGui::TableSetupColumn("Dist", ImGuiTableColumnFlags_WidthFixed, 78.0f);
    ImGui::TableSetupColumn("End", ImGuiTableColumnFlags_WidthFixed, 78.0f);
    ImGui::TableSetupColumn("Id", ImGuiTableColumnFlags_WidthFixed, 210.0f);
    ImGui::TableSetupColumn("Key Values");
    ImGui::TableHeadersRow();

    for (const CourseCinematicShotSet& shotSet : course.cinematicShotSets) {
        const bool active = currentDistance >= shotSet.startDistance && currentDistance <= shotSet.endDistance;
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted("Shot Set");
        ImGui::TableNextColumn();
        ImGui::TextColored(
            active ? ImVec4(0.35f, 1.0f, 0.65f, 1.0f) : ImVec4(0.55f, 0.65f, 0.72f, 1.0f),
            "%s",
            active ? "active" : (shotSet.endDistance < currentDistance ? "done" : "ahead"));
        ImGui::TableNextColumn();
        ImGui::Text("%.0f", shotSet.startDistance);
        ImGui::TableNextColumn();
        ImGui::Text("%.0f", shotSet.endDistance);
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(shotSet.id.c_str());
        ImGui::TableNextColumn();
        ImGui::Text(
            "%s  fog %s  L:%s C:%s M:%s  landmarks %zu/%zu",
            shotSet.label.empty() ? "-" : shotSet.label.c_str(),
            shotSet.fogMood.empty() ? "-" : shotSet.fogMood.c_str(),
            shotSet.lightingPresetId.empty() ? "-" : shotSet.lightingPresetId.c_str(),
            shotSet.cameraShotId.empty() ? "-" : shotSet.cameraShotId.c_str(),
            shotSet.terrainMaterialId.empty() ? "-" : shotSet.terrainMaterialId.c_str(),
            shotSet.heroLandmarkIds.size(),
            shotSet.vistaLandmarkIds.size());
    }

    for (const CourseLightingPreset& preset : course.lightingPresets) {
        const bool active = preset.distance <= currentDistance;
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted("Lighting");
        ImGui::TableNextColumn();
        ImGui::TextColored(
            active ? ImVec4(0.55f, 0.65f, 0.72f, 1.0f) : ImVec4(0.45f, 0.85f, 1.0f, 1.0f),
            "%s",
            active ? "seen" : "ahead");
        ImGui::TableNextColumn();
        ImGui::Text("%.0f", preset.distance);
        ImGui::TableNextColumn();
        ImGui::TextUnformatted("-");
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(preset.id.c_str());
        ImGui::TableNextColumn();
        ImGui::Text("sun %.2f  fog %.2f %.0f-%.0f", preset.sunIntensity, preset.fogIntensity, preset.fogStart, preset.fogEnd);
    }

    for (const CourseCameraShotPreset& preset : course.cameraShotPresets) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted("Shot Preset");
        ImGui::TableNextColumn();
        ImGui::TextUnformatted("asset");
        ImGui::TableNextColumn();
        ImGui::TextUnformatted("-");
        ImGui::TableNextColumn();
        ImGui::TextUnformatted("-");
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(preset.id.c_str());
        ImGui::TableNextColumn();
        ImGui::Text(
            "%s  fov %.1f  roll %.1f  shake %.2f",
            preset.mode.c_str(),
            preset.fovOffset * 180.0f / 3.14159265358979323846f,
            preset.rollOffset * 180.0f / 3.14159265358979323846f,
            preset.shakeAmount);
    }

    for (const CourseCameraBlendAsset& blend : course.cameraBlendAssets) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted("Blend Asset");
        ImGui::TableNextColumn();
        ImGui::TextUnformatted("asset");
        ImGui::TableNextColumn();
        ImGui::TextUnformatted("-");
        ImGui::TableNextColumn();
        ImGui::TextUnformatted("-");
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(blend.id.c_str());
        ImGui::TableNextColumn();
        ImGui::Text(
            "in %.0f out %.0f curve %s weight %.2f",
            blend.blendInDistance,
            blend.blendOutDistance,
            blend.curve.c_str(),
            blend.weightScale);
    }

    for (const CourseCinematicCameraShot& shot : course.cinematicCameraShots) {
        const bool active = currentDistance >= shot.startDistance && currentDistance <= shot.endDistance;
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted("Camera Shot");
        ImGui::TableNextColumn();
        ImGui::TextColored(
            active ? ImVec4(0.35f, 1.0f, 0.65f, 1.0f) : ImVec4(0.55f, 0.65f, 0.72f, 1.0f),
            "%s",
            active ? "active" : (shot.endDistance < currentDistance ? "done" : "ahead"));
        ImGui::TableNextColumn();
        ImGui::Text("%.0f", shot.startDistance);
        ImGui::TableNextColumn();
        ImGui::Text("%.0f", shot.endDistance);
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(shot.id.c_str());
        ImGui::TableNextColumn();
        ImGui::Text(
            "%s  preset %s blend %s  fov %.1f  roll %.1f",
            shot.mode.c_str(),
            shot.presetId.empty() ? "-" : shot.presetId.c_str(),
            shot.blendAssetId.empty() ? "-" : shot.blendAssetId.c_str(),
            shot.fovOffset * 180.0f / 3.14159265358979323846f,
            shot.rollOffset * 180.0f / 3.14159265358979323846f);
    }

    for (const CourseTerrainMaterialPreset& preset : course.terrainMaterialPresets) {
        const bool active = preset.distance <= currentDistance;
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted("Material");
        ImGui::TableNextColumn();
        ImGui::TextColored(
            active ? ImVec4(0.55f, 0.65f, 0.72f, 1.0f) : ImVec4(0.45f, 0.85f, 1.0f, 1.0f),
            "%s",
            active ? "seen" : "ahead");
        ImGui::TableNextColumn();
        ImGui::Text("%.0f", preset.distance);
        ImGui::TableNextColumn();
        ImGui::TextUnformatted("-");
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(preset.id.c_str());
        ImGui::TableNextColumn();
        ImGui::Text("bright %.2f  rim %.2f  cavity %.2f", preset.brightness, preset.rimLightStrength, preset.cavityAoStrength);
    }

    ImGui::EndTable();
}

void DrawActiveEnemyTable(const CourseSpawnRuntime& runtime) {
    if (!ImGui::BeginTable(
            "CourseActiveEnemies",
            6,
            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY,
            ImVec2(0.0f, 170.0f))) {
        return;
    }

    ImGui::TableSetupColumn("Id", ImGuiTableColumnFlags_WidthFixed, 48.0f);
    ImGui::TableSetupColumn("Role", ImGuiTableColumnFlags_WidthFixed, 145.0f);
    ImGui::TableSetupColumn("Mesh", ImGuiTableColumnFlags_WidthFixed, 90.0f);
    ImGui::TableSetupColumn("HP", ImGuiTableColumnFlags_WidthFixed, 62.0f);
    ImGui::TableSetupColumn("Dist", ImGuiTableColumnFlags_WidthFixed, 68.0f);
    ImGui::TableSetupColumn("Pattern");
    ImGui::TableHeadersRow();

    for (const CourseEnemyActor& enemy : runtime.Enemies()) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::Text("%u", enemy.actorId);
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(enemy.desc.role.c_str());
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(enemy.desc.meshId.c_str());
        ImGui::TableNextColumn();
        ImGui::Text("%.0f", enemy.desc.hitPoints);
        ImGui::TableNextColumn();
        ImGui::Text("%.0f", enemy.desc.spawnDistance + enemy.desc.distanceOffset);
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(enemy.desc.bulletPatternId.c_str());
    }

    ImGui::EndTable();
}

void DrawActiveObstacleTable(const CourseSpawnRuntime& runtime) {
    if (!ImGui::BeginTable(
            "CourseActiveObstacles",
            5,
            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY,
            ImVec2(0.0f, 140.0f))) {
        return;
    }

    ImGui::TableSetupColumn("Id", ImGuiTableColumnFlags_WidthFixed, 150.0f);
    ImGui::TableSetupColumn("Mesh", ImGuiTableColumnFlags_WidthFixed, 110.0f);
    ImGui::TableSetupColumn("HP", ImGuiTableColumnFlags_WidthFixed, 60.0f);
    ImGui::TableSetupColumn("Dist", ImGuiTableColumnFlags_WidthFixed, 68.0f);
    ImGui::TableSetupColumn("Break");
    ImGui::TableHeadersRow();

    for (const CourseObstacleActor& obstacle : runtime.Obstacles()) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(obstacle.desc.id.c_str());
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(obstacle.desc.meshId.c_str());
        ImGui::TableNextColumn();
        ImGui::Text("%.0f", obstacle.desc.hitPoints);
        ImGui::TableNextColumn();
        ImGui::Text("%.0f", obstacle.desc.spawnDistance + obstacle.desc.distanceOffset);
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(obstacle.desc.breakable ? "yes" : "no");
    }

    ImGui::EndTable();
}

void DrawValidationReportTable(const CourseValidationReport& report) {
    ImGui::Text(
        "Validation: errors %u  warnings %u  info %u",
        report.errorCount,
        report.warningCount,
        report.infoCount);

    if (!ImGui::BeginTable(
            "CourseValidationReport",
            4,
            ImGuiTableFlags_Borders |
                ImGuiTableFlags_RowBg |
                ImGuiTableFlags_ScrollY |
                ImGuiTableFlags_Resizable,
            ImVec2(0.0f, 220.0f))) {
        return;
    }

    ImGui::TableSetupColumn("Severity", ImGuiTableColumnFlags_WidthFixed, 76.0f);
    ImGui::TableSetupColumn("Subject", ImGuiTableColumnFlags_WidthFixed, 150.0f);
    ImGui::TableSetupColumn("Dist", ImGuiTableColumnFlags_WidthFixed, 70.0f);
    ImGui::TableSetupColumn("Message");
    ImGui::TableHeadersRow();

    for (const CourseValidationIssue& issue : report.issues) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextColored(SeverityColor(issue.severity), "%s", ToString(issue.severity));
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(issue.subject.c_str());
        ImGui::TableNextColumn();
        if (issue.distance >= 0.0f) {
            ImGui::Text("%.0f", issue.distance);
        } else {
            ImGui::TextUnformatted("-");
        }
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(issue.message.c_str());
    }

    ImGui::EndTable();
}

bool DrawSectionAuthoringTable(const CourseTimelineDebugPanelInput& input, CourseAsset& course) {
    bool changed = false;
    int removeIndex = -1;

    if (!ImGui::BeginTable(
            "CourseSectionAuthoring",
            7,
            ImGuiTableFlags_Borders |
                ImGuiTableFlags_RowBg |
                ImGuiTableFlags_ScrollY |
                ImGuiTableFlags_Resizable,
            ImVec2(0.0f, 180.0f))) {
        return false;
    }

    ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 34.0f);
    ImGui::TableSetupColumn("Go", ImGuiTableColumnFlags_WidthFixed, 42.0f);
    ImGui::TableSetupColumn("Start", ImGuiTableColumnFlags_WidthFixed, 86.0f);
    ImGui::TableSetupColumn("End", ImGuiTableColumnFlags_WidthFixed, 86.0f);
    ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed, 180.0f);
    ImGui::TableSetupColumn("Category", ImGuiTableColumnFlags_WidthFixed, 160.0f);
    ImGui::TableSetupColumn("Span");
    ImGui::TableHeadersRow();

    for (size_t index = 0; index < course.sections.size(); ++index) {
        CourseSection& section = course.sections[index];
        ImGui::PushID(static_cast<int>(index));
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        if (ImGui::SmallButton("X")) {
            removeIndex = static_cast<int>(index);
        }
        ImGui::TableNextColumn();
        if (ImGui::SmallButton(">") && input.onTeleportToDistance) {
            input.onTeleportToDistance(section.startDistance);
        }
        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(-FLT_MIN);
        changed |= ImGui::InputFloat("##start", &section.startDistance, 5.0f, 25.0f, "%.1f");
        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(-FLT_MIN);
        changed |= ImGui::InputFloat("##end", &section.endDistance, 5.0f, 25.0f, "%.1f");
        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(-FLT_MIN);
        changed |= InputString<96>("##name", section.name);
        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(-FLT_MIN);
        changed |= InputString<96>("##category", section.category);
        ImGui::TableNextColumn();
        ImGui::Text("%.1f", section.endDistance - section.startDistance);
        ImGui::PopID();
    }

    ImGui::EndTable();

    if (removeIndex >= 0 && static_cast<size_t>(removeIndex) < course.sections.size()) {
        course.sections.erase(course.sections.begin() + removeIndex);
        changed = true;
    }
    return changed;
}

bool DrawEventAuthoringTable(CourseAsset& course) {
    bool changed = false;
    int removeIndex = -1;
    constexpr const char* eventTypes[] = {
        "enemy_wave",
        "obstacle",
        "boss",
        "boss_phase",
        "checkpoint",
        "setpiece",
        "vfx",
    };

    if (!ImGui::BeginTable(
            "CourseEventAuthoring",
            6,
            ImGuiTableFlags_Borders |
                ImGuiTableFlags_RowBg |
                ImGuiTableFlags_ScrollY |
                ImGuiTableFlags_Resizable,
            ImVec2(0.0f, 250.0f))) {
        return false;
    }

    ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 34.0f);
    ImGui::TableSetupColumn("Dist", ImGuiTableColumnFlags_WidthFixed, 88.0f);
    ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 130.0f);
    ImGui::TableSetupColumn("Id", ImGuiTableColumnFlags_WidthFixed, 180.0f);
    ImGui::TableSetupColumn("Payload");
    ImGui::TableSetupColumn("Delta", ImGuiTableColumnFlags_WidthFixed, 70.0f);
    ImGui::TableHeadersRow();

    for (size_t index = 0; index < course.events.size(); ++index) {
        CourseEventMarker& event = course.events[index];
        ImGui::PushID(static_cast<int>(index));
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        if (ImGui::SmallButton("X")) {
            removeIndex = static_cast<int>(index);
        }
        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(-FLT_MIN);
        changed |= ImGui::InputFloat("##distance", &event.distance, 5.0f, 25.0f, "%.1f");
        ImGui::TableNextColumn();
        int selectedType = -1;
        for (int typeIndex = 0; typeIndex < static_cast<int>(sizeof(eventTypes) / sizeof(eventTypes[0])); ++typeIndex) {
            if (event.type == eventTypes[typeIndex]) {
                selectedType = typeIndex;
                break;
            }
        }
        const char* preview = selectedType >= 0 ? eventTypes[selectedType] : event.type.c_str();
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::BeginCombo("##type", preview)) {
            for (int typeIndex = 0; typeIndex < static_cast<int>(sizeof(eventTypes) / sizeof(eventTypes[0])); ++typeIndex) {
                const bool selected = typeIndex == selectedType;
                if (ImGui::Selectable(eventTypes[typeIndex], selected)) {
                    event.type = eventTypes[typeIndex];
                    changed = true;
                }
                if (selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(-FLT_MIN);
        changed |= InputString<128>("##id", event.id);
        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(-FLT_MIN);
        changed |= InputString<256>("##payload", event.payload);
        ImGui::TableNextColumn();
        if (index == 0) {
            ImGui::TextUnformatted("-");
        } else {
            ImGui::Text("%.1f", event.distance - course.events[index - 1].distance);
        }
        ImGui::PopID();
    }

    ImGui::EndTable();

    if (removeIndex >= 0 && static_cast<size_t>(removeIndex) < course.events.size()) {
        course.events.erase(course.events.begin() + removeIndex);
        changed = true;
    }
    return changed;
}

bool DrawCameraAuthoringTable(CourseAsset& course) {
    bool changed = false;
    int removeIndex = -1;

    if (!ImGui::BeginTable(
            "CourseCameraAuthoring",
            7,
            ImGuiTableFlags_Borders |
                ImGuiTableFlags_RowBg |
                ImGuiTableFlags_ScrollY |
                ImGuiTableFlags_Resizable,
            ImVec2(0.0f, 180.0f))) {
        return false;
    }

    ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 34.0f);
    ImGui::TableSetupColumn("Dist", ImGuiTableColumnFlags_WidthFixed, 78.0f);
    ImGui::TableSetupColumn("Back", ImGuiTableColumnFlags_WidthFixed, 70.0f);
    ImGui::TableSetupColumn("Up", ImGuiTableColumnFlags_WidthFixed, 70.0f);
    ImGui::TableSetupColumn("Side", ImGuiTableColumnFlags_WidthFixed, 70.0f);
    ImGui::TableSetupColumn("LookAhead", ImGuiTableColumnFlags_WidthFixed, 90.0f);
    ImGui::TableSetupColumn("FovDeg");
    ImGui::TableHeadersRow();

    for (size_t index = 0; index < course.cameraKeys.size(); ++index) {
        CourseCameraKey& key = course.cameraKeys[index];
        float fovDegrees = key.fovY * 180.0f / 3.14159265358979323846f;
        ImGui::PushID(static_cast<int>(index));
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        if (ImGui::SmallButton("X")) {
            removeIndex = static_cast<int>(index);
        }
        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(-FLT_MIN);
        changed |= ImGui::InputFloat("##distance", &key.distance, 5.0f, 25.0f, "%.1f");
        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(-FLT_MIN);
        changed |= ImGui::InputFloat("##back", &key.backDistance, 1.0f, 5.0f, "%.1f");
        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(-FLT_MIN);
        changed |= ImGui::InputFloat("##up", &key.verticalOffset, 1.0f, 5.0f, "%.1f");
        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(-FLT_MIN);
        changed |= ImGui::InputFloat("##side", &key.lateralOffset, 1.0f, 5.0f, "%.1f");
        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(-FLT_MIN);
        changed |= ImGui::InputFloat("##lookahead", &key.lookAheadDistance, 1.0f, 5.0f, "%.1f");
        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::InputFloat("##fov", &fovDegrees, 1.0f, 5.0f, "%.1f")) {
            key.fovY = fovDegrees * 3.14159265358979323846f / 180.0f;
            changed = true;
        }
        ImGui::PopID();
    }

    ImGui::EndTable();

    if (removeIndex >= 0 && static_cast<size_t>(removeIndex) < course.cameraKeys.size()) {
        course.cameraKeys.erase(course.cameraKeys.begin() + removeIndex);
        changed = true;
    }
    return changed;
}

bool DrawCourseObjectEditor(const CourseTimelineDebugPanelInput& input, CourseAsset& course) {
    if (input.runtimeState == nullptr) {
        ImGui::TextUnformatted("Runtime state unavailable.");
        return false;
    }

    TerrainAuthoringState& editor = input.runtimeState->terrain;
    const editor::EditorViewportAuthoringInputGuard inputGuard =
        editor::MakeEditorViewportAuthoringInputGuard(input.canMutateAuthoring);
    const bool canMutate = inputGuard.CanMutate();
    bool changed = false;
    ImGui::Text("Authoring input: %s", inputGuard.StateLabel());
    if (!canMutate) {
        ImGui::TextDisabled("%s", inputGuard.DisabledReason());
    }
    ImGui::BeginDisabled(!canMutate);
    ImGui::Checkbox("Viewport Pick", &editor.enableCourseObjectViewportEditing);
    ImGui::SameLine();
    const char* gizmoModes[] = {"Move", "Scale", "Rotate"};
    ImGui::SetNextItemWidth(112.0f);
    ImGui::Combo("Gizmo", &editor.courseObjectGizmoMode, gizmoModes, IM_ARRAYSIZE(gizmoModes));
    ImGui::SameLine();
    ImGui::Checkbox("Snap", &editor.courseObjectSnapEnabled);
    ImGui::SameLine();
    const char* gizmoSpaces[] = {"World", "Local"};
    ImGui::SetNextItemWidth(96.0f);
    ImGui::Combo("Space", &editor.courseObjectGizmoSpace, gizmoSpaces, IM_ARRAYSIZE(gizmoSpaces));
    ImGui::SameLine();
    const char* pivotModes[] = {"Active", "Median", "Individual"};
    ImGui::SetNextItemWidth(104.0f);
    ImGui::Combo("Pivot", &editor.courseObjectPivotMode, pivotModes, IM_ARRAYSIZE(pivotModes));
    if (ImGui::Button("Undo")) {
        editor.courseObjectUndoRequested = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Redo")) {
        editor.courseObjectRedoRequested = true;
    }
    ImGui::SameLine();
    ImGui::Text("History %u/%u", editor.courseObjectUndoDepth, editor.courseObjectRedoDepth);
    ImGui::Checkbox("Frame Box", &editor.showCourseObjectFrame);
    ImGui::SameLine();
    ImGui::Checkbox("Depth Test", &editor.courseObjectFrameDepthTest);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(120.0f);
    ImGui::DragFloat("Frame Padding", &editor.courseObjectFramePadding, 0.01f, 1.0f, 2.0f, "%.2f");
    ImGui::SetNextItemWidth(140.0f);
    ImGui::DragFloat("Move Sens", &editor.courseObjectMoveSensitivity, 0.005f, 0.001f, 2.0f, "%.3f");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(140.0f);
    ImGui::DragFloat("Scale Sens", &editor.courseObjectScaleSensitivity, 0.001f, 0.0001f, 0.2f, "%.4f");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(140.0f);
    ImGui::DragFloat("Rotate Sens", &editor.courseObjectRotateSensitivity, 0.0005f, 0.0001f, 0.1f, "%.4f");
    ImGui::SetNextItemWidth(140.0f);
    ImGui::DragFloat("Move Snap", &editor.courseObjectMoveSnap, 0.05f, 0.01f, 20.0f, "%.2f");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(140.0f);
    ImGui::DragFloat("Scale Snap", &editor.courseObjectScaleSnap, 0.01f, 0.01f, 5.0f, "%.2f");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(140.0f);
    ImGui::DragFloat("Rotate Snap", &editor.courseObjectRotateSnapDegrees, 0.5f, 0.1f, 90.0f, "%.1f deg");

    if (ImGui::Button("Add Terrain Placement")) {
        CourseTerrainPlacement placement{};
        placement.distance = input.currentDistance;
        placement.id = "new_terrain";
        placement.meshId = "curved_canyon_wall";
        placement.layer = CourseTerrainLayer::HeroLandmark;
        placement.scale = {4.0f, 8.0f, 8.0f};
        placement.cullBehindDistance = 160.0f;
        placement.cullAheadDistance = 320.0f;
        course.terrainPlacements.push_back(placement);
        editor.courseObjectSelectionType = 0;
        editor.selectedCourseTerrainPlacement = static_cast<int>(course.terrainPlacements.size() - 1);
        editor.selectedCourseRockCluster = -1;
        changed = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Add Rock Cluster")) {
        CourseRockCluster cluster{};
        cluster.distance = input.currentDistance;
        cluster.id = "new_rock_cluster";
        cluster.meshId = "curved_canyon_wall";
        cluster.anchor = CourseRockClusterAnchor::LeftWall;
        cluster.type = CourseRockClusterType::AttachedDebris;
        cluster.count = 3;
        cluster.minScale = 0.18f;
        cluster.maxScale = 0.36f;
        cluster.clearLaneRadius = 32.0f;
        cluster.cullBehindDistance = 90.0f;
        cluster.cullAheadDistance = 150.0f;
        course.rockClusters.push_back(cluster);
        editor.courseObjectSelectionType = 1;
        editor.selectedCourseRockCluster = static_cast<int>(course.rockClusters.size() - 1);
        editor.selectedCourseTerrainPlacement = -1;
        changed = true;
    }
    ImGui::EndDisabled();

    ImGui::Separator();
    ImGui::BeginChild("CourseObjectOutliner", ImVec2(0.0f, 240.0f), true);
    if (ImGui::BeginTable(
            "CourseObjectOutlinerTable",
            5,
            ImGuiTableFlags_Borders |
                ImGuiTableFlags_RowBg |
                ImGuiTableFlags_ScrollY |
                ImGuiTableFlags_Resizable)) {
        ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 86.0f);
        ImGui::TableSetupColumn("Dist", ImGuiTableColumnFlags_WidthFixed, 72.0f);
        ImGui::TableSetupColumn("Id", ImGuiTableColumnFlags_WidthFixed, 230.0f);
        ImGui::TableSetupColumn("Mesh", ImGuiTableColumnFlags_WidthFixed, 150.0f);
        ImGui::TableSetupColumn("State");
        ImGui::TableHeadersRow();

        for (size_t index = 0; index < course.terrainPlacements.size(); ++index) {
            const CourseTerrainPlacement& placement = course.terrainPlacements[index];
            const bool selected =
                editor.courseObjectSelectionType == 0 &&
                editor.selectedCourseTerrainPlacement == static_cast<int>(index);
            ImGui::PushID(static_cast<int>(index));
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            if (ImGui::Selectable("Terrain", selected, ImGuiSelectableFlags_SpanAllColumns)) {
                editor.courseObjectSelectionType = 0;
                editor.selectedCourseTerrainPlacement = static_cast<int>(index);
                editor.selectedCourseRockCluster = -1;
            }
            ImGui::TableNextColumn();
            ImGui::Text("%.0f", placement.distance);
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(placement.id.c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(placement.meshId.c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(ToCourseTerrainLayerString(placement.layer));
            ImGui::PopID();
        }

        for (size_t index = 0; index < course.rockClusters.size(); ++index) {
            const CourseRockCluster& cluster = course.rockClusters[index];
            const bool selected =
                editor.courseObjectSelectionType == 1 &&
                editor.selectedCourseRockCluster == static_cast<int>(index);
            ImGui::PushID(10000 + static_cast<int>(index));
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            if (ImGui::Selectable("Rock", selected, ImGuiSelectableFlags_SpanAllColumns)) {
                editor.courseObjectSelectionType = 1;
                editor.selectedCourseRockCluster = static_cast<int>(index);
                editor.selectedCourseTerrainPlacement = -1;
            }
            ImGui::TableNextColumn();
            ImGui::Text("%.0f", cluster.distance);
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(cluster.id.c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(cluster.meshId.c_str());
            ImGui::TableNextColumn();
            ImGui::Text("%s / %s", ToCourseRockClusterAnchorString(cluster.anchor), ToCourseRockClusterTypeString(cluster.type));
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    ImGui::EndChild();

    int removeTerrain = -1;
    int removeRock = -1;
    ImGui::SeparatorText("Details");
    if (editor.courseObjectSelectionType == 0 &&
        editor.selectedCourseTerrainPlacement >= 0 &&
        editor.selectedCourseTerrainPlacement < static_cast<int>(course.terrainPlacements.size())) {
        CourseTerrainPlacement& placement =
            course.terrainPlacements[static_cast<size_t>(editor.selectedCourseTerrainPlacement)];
        const size_t placementIndex = static_cast<size_t>(editor.selectedCourseTerrainPlacement);
        const editor::EditorObjectHandle propertyTarget = MakeCourseObjectPropertyTarget(
            editor::EditorDomainId::CourseTerrainPlacement,
            "course-terrain",
            placementIndex,
            editor.courseObjectEditRevision,
            "Course Terrain");
        ImGui::PushID("TerrainDetails");
        ImGui::BeginDisabled(!canMutate);
        changed |= TrackedInputString<128>(input, propertyTarget, "Id", "CourseTerrainPlacement.id", placement.id);
        changed |= TrackedInputString<128>(input, propertyTarget, "Mesh", "CourseTerrainPlacement.meshId", placement.meshId);
        const CourseTerrainLayer beforeLayer = placement.layer;
        if (ComboTerrainLayer("Layer", placement.layer)) {
            StageCoursePropertyDelta(
                input,
                propertyTarget,
                "CourseTerrainPlacement.layer",
                "Layer",
                "enum",
                ToCourseTerrainLayerString(beforeLayer),
                ToCourseTerrainLayerString(placement.layer));
            changed = true;
        }
        const CourseTerrainCollisionMode beforeCollision = placement.collisionMode;
        if (ComboCollisionMode("Collision", placement.collisionMode)) {
            StageCoursePropertyDelta(
                input,
                propertyTarget,
                "CourseTerrainPlacement.collisionMode",
                "Collision",
                "enum",
                ToCourseTerrainCollisionModeString(beforeCollision),
                ToCourseTerrainCollisionModeString(placement.collisionMode));
            changed = true;
        }
        changed |= TrackedDragFloat(input, propertyTarget, "Distance", "CourseTerrainPlacement.distance", placement.distance, 1.0f, 0.0f, input.railLength, "%.1f");
        changed |= TrackedDragFloat(input, propertyTarget, "Lateral", "CourseTerrainPlacement.lateralOffset", placement.lateralOffset, 0.25f, -500.0f, 500.0f, "%.2f");
        changed |= TrackedDragFloat(input, propertyTarget, "Vertical", "CourseTerrainPlacement.verticalOffset", placement.verticalOffset, 0.25f, -500.0f, 500.0f, "%.2f");
        changed |= TrackedDragFloat(input, propertyTarget, "Forward", "CourseTerrainPlacement.forwardOffset", placement.forwardOffset, 0.25f, -500.0f, 500.0f, "%.2f");
        changed |= TrackedDragVector3(input, propertyTarget, "Scale", "CourseTerrainPlacement.scale", placement.scale, 0.10f, 0.01f, 200.0f);
        Vector3 rotationDegrees = {
            placement.rotation.x * 180.0f / 3.14159265358979323846f,
            placement.rotation.y * 180.0f / 3.14159265358979323846f,
            placement.rotation.z * 180.0f / 3.14159265358979323846f,
        };
        const Vector3 beforeRotationDegrees = rotationDegrees;
        if (DragVector3("Rotation Deg", rotationDegrees, 0.25f, -360.0f, 360.0f)) {
            placement.rotation = {
                rotationDegrees.x * 3.14159265358979323846f / 180.0f,
                rotationDegrees.y * 3.14159265358979323846f / 180.0f,
                rotationDegrees.z * 3.14159265358979323846f / 180.0f,
            };
            StageCoursePropertyDelta(
                input,
                propertyTarget,
                "CourseTerrainPlacement.rotation",
                "Rotation Deg",
                "vec3",
                FormatPropertyVector3(beforeRotationDegrees),
                FormatPropertyVector3(rotationDegrees));
            changed = true;
        }
        changed |= TrackedDragInt(input, propertyTarget, "Render Priority", "CourseTerrainPlacement.renderPriority", placement.renderPriority, 1.0f, -100, 100);
        changed |= TrackedDragFloat(input, propertyTarget, "Cull Behind", "CourseTerrainPlacement.cullBehindDistance", placement.cullBehindDistance, 1.0f, -1.0f, 2000.0f, "%.1f");
        changed |= TrackedDragFloat(input, propertyTarget, "Cull Ahead", "CourseTerrainPlacement.cullAheadDistance", placement.cullAheadDistance, 1.0f, -1.0f, 3000.0f, "%.1f");
        if (ImGui::Button("Teleport To Object") && input.onTeleportToDistance) {
            input.onTeleportToDistance(placement.distance);
        }
        ImGui::SameLine();
        if (ImGui::Button("Delete Object")) {
            if (input.confirmService != nullptr) {
                RequestTerrainPlacementDeleteConfirmation(
                    input,
                    course,
                    editor.selectedCourseTerrainPlacement);
            } else {
                removeTerrain = editor.selectedCourseTerrainPlacement;
            }
        }
        ImGui::SeparatorText("Material");
        changed |= DrawTerrainMaterialPresetDetails(course, placement.distance);
        ImGui::EndDisabled();
        ImGui::PopID();
    } else if (editor.courseObjectSelectionType == 1 &&
        editor.selectedCourseRockCluster >= 0 &&
        editor.selectedCourseRockCluster < static_cast<int>(course.rockClusters.size())) {
        CourseRockCluster& cluster =
            course.rockClusters[static_cast<size_t>(editor.selectedCourseRockCluster)];
        const size_t clusterIndex = static_cast<size_t>(editor.selectedCourseRockCluster);
        const editor::EditorObjectHandle propertyTarget = MakeCourseObjectPropertyTarget(
            editor::EditorDomainId::CourseRockCluster,
            "course-rock",
            clusterIndex,
            editor.courseObjectEditRevision,
            "Course Rock Cluster");
        ImGui::PushID("RockClusterDetails");
        ImGui::BeginDisabled(!canMutate);
        changed |= TrackedInputString<128>(input, propertyTarget, "Id", "CourseRockCluster.id", cluster.id);
        changed |= TrackedInputString<128>(input, propertyTarget, "Mesh", "CourseRockCluster.meshId", cluster.meshId);
        const CourseRockClusterAnchor beforeAnchor = cluster.anchor;
        if (ComboRockAnchor("Anchor", cluster.anchor)) {
            StageCoursePropertyDelta(
                input,
                propertyTarget,
                "CourseRockCluster.anchor",
                "Anchor",
                "enum",
                ToCourseRockClusterAnchorString(beforeAnchor),
                ToCourseRockClusterAnchorString(cluster.anchor));
            changed = true;
        }
        const CourseRockClusterType beforeType = cluster.type;
        if (ComboRockType("Type", cluster.type)) {
            StageCoursePropertyDelta(
                input,
                propertyTarget,
                "CourseRockCluster.type",
                "Type",
                "enum",
                ToCourseRockClusterTypeString(beforeType),
                ToCourseRockClusterTypeString(cluster.type));
            changed = true;
        }
        Vector3 clusterRotationDegrees = {
            cluster.rotation.x * 180.0f / 3.14159265358979323846f,
            cluster.rotation.y * 180.0f / 3.14159265358979323846f,
            cluster.rotation.z * 180.0f / 3.14159265358979323846f,
        };
        const Vector3 beforeClusterRotationDegrees = clusterRotationDegrees;
        if (DragVector3("Rotation Deg", clusterRotationDegrees, 0.25f, -360.0f, 360.0f)) {
            cluster.rotation = {
                clusterRotationDegrees.x * 3.14159265358979323846f / 180.0f,
                clusterRotationDegrees.y * 3.14159265358979323846f / 180.0f,
                clusterRotationDegrees.z * 3.14159265358979323846f / 180.0f,
            };
            StageCoursePropertyDelta(
                input,
                propertyTarget,
                "CourseRockCluster.rotation",
                "Rotation Deg",
                "vec3",
                FormatPropertyVector3(beforeClusterRotationDegrees),
                FormatPropertyVector3(clusterRotationDegrees));
            changed = true;
        }
        changed |= TrackedDragFloat(input, propertyTarget, "Distance", "CourseRockCluster.distance", cluster.distance, 1.0f, 0.0f, input.railLength, "%.1f");
        int count = static_cast<int>(cluster.count);
        if (ImGui::DragInt("Count", &count, 1.0f, 0, 32)) {
            const uint32_t beforeCount = cluster.count;
            cluster.count = static_cast<uint32_t>((std::clamp)(count, 0, 32));
            StageCoursePropertyDelta(
                input,
                propertyTarget,
                "CourseRockCluster.count",
                "Count",
                "uint",
                FormatPropertyUInt(beforeCount),
                FormatPropertyUInt(cluster.count));
            changed = true;
        }
        changed |= TrackedDragFloat(input, propertyTarget, "Min Scale", "CourseRockCluster.minScale", cluster.minScale, 0.01f, 0.01f, 20.0f, "%.2f");
        changed |= TrackedDragFloat(input, propertyTarget, "Max Scale", "CourseRockCluster.maxScale", cluster.maxScale, 0.01f, 0.01f, 20.0f, "%.2f");
        changed |= TrackedDragVector3(input, propertyTarget, "Spread", "CourseRockCluster.spread", cluster.spread, 0.10f, 0.0f, 500.0f);
        changed |= TrackedDragFloat(input, propertyTarget, "Clear Lane", "CourseRockCluster.clearLaneRadius", cluster.clearLaneRadius, 0.25f, 0.0f, 200.0f, "%.1f");
        changed |= TrackedDragFloat(input, propertyTarget, "Cull Behind", "CourseRockCluster.cullBehindDistance", cluster.cullBehindDistance, 1.0f, 0.0f, 2000.0f, "%.1f");
        changed |= TrackedDragFloat(input, propertyTarget, "Cull Ahead", "CourseRockCluster.cullAheadDistance", cluster.cullAheadDistance, 1.0f, 0.0f, 3000.0f, "%.1f");
        if (ImGui::Button("Teleport To Cluster") && input.onTeleportToDistance) {
            input.onTeleportToDistance(cluster.distance);
        }
        ImGui::SameLine();
        if (ImGui::Button("Delete Cluster")) {
            if (input.confirmService != nullptr) {
                RequestRockClusterDeleteConfirmation(
                    input,
                    course,
                    editor.selectedCourseRockCluster);
            } else {
                removeRock = editor.selectedCourseRockCluster;
            }
        }
        ImGui::SeparatorText("Instance Overrides");
        if (ImGui::Button("Add Instance Override")) {
            CourseRockCluster::InstanceTransformOverride transformOverride{};
            transformOverride.index = cluster.instanceOverrides.empty()
                ? 0u
                : cluster.instanceOverrides.back().index + 1u;
            if (cluster.count > 0) {
                transformOverride.index = (std::min)(transformOverride.index, cluster.count - 1u);
            }
            cluster.instanceOverrides.push_back(transformOverride);
            changed = true;
        }
        int removeOverride = -1;
        if (ImGui::BeginTable(
                "RockInstanceOverrideTable",
                5,
                ImGuiTableFlags_Borders |
                    ImGuiTableFlags_RowBg |
                    ImGuiTableFlags_Resizable)) {
            ImGui::TableSetupColumn("Index", ImGuiTableColumnFlags_WidthFixed, 64.0f);
            ImGui::TableSetupColumn("Local Offset", ImGuiTableColumnFlags_WidthFixed, 180.0f);
            ImGui::TableSetupColumn("Scale", ImGuiTableColumnFlags_WidthFixed, 170.0f);
            ImGui::TableSetupColumn("Rotation", ImGuiTableColumnFlags_WidthFixed, 180.0f);
            ImGui::TableSetupColumn("Edit", ImGuiTableColumnFlags_WidthFixed, 70.0f);
            ImGui::TableHeadersRow();
            for (size_t overrideIndex = 0; overrideIndex < cluster.instanceOverrides.size(); ++overrideIndex) {
                CourseRockCluster::InstanceTransformOverride& transformOverride =
                    cluster.instanceOverrides[overrideIndex];
                ImGui::PushID(static_cast<int>(overrideIndex));
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                int instanceIndex = static_cast<int>(transformOverride.index);
                if (ImGui::DragInt("##Index", &instanceIndex, 1.0f, 0, 31)) {
                    const uint32_t beforeIndex = transformOverride.index;
                    transformOverride.index = static_cast<uint32_t>((std::clamp)(instanceIndex, 0, 31));
                    StageCoursePropertyDelta(
                        input,
                        propertyTarget,
                        "CourseRockCluster.instanceOverrides.index",
                        "Instance Index",
                        "uint",
                        FormatPropertyUInt(beforeIndex),
                        FormatPropertyUInt(transformOverride.index));
                    changed = true;
                }
                ImGui::TableNextColumn();
                if (TrackedDragVector3(
                        input,
                        propertyTarget,
                        "##Offset",
                        "CourseRockCluster.instanceOverrides.localOffset",
                        transformOverride.localOffset,
                        0.10f,
                        -500.0f,
                        500.0f)) {
                    changed = true;
                }
                ImGui::TableNextColumn();
                if (TrackedDragVector3(
                        input,
                        propertyTarget,
                        "##Scale",
                        "CourseRockCluster.instanceOverrides.scale",
                        transformOverride.scale,
                        0.02f,
                        0.01f,
                        20.0f)) {
                    changed = true;
                }
                ImGui::TableNextColumn();
                Vector3 overrideRotationDegrees = {
                    transformOverride.rotation.x * 180.0f / 3.14159265358979323846f,
                    transformOverride.rotation.y * 180.0f / 3.14159265358979323846f,
                    transformOverride.rotation.z * 180.0f / 3.14159265358979323846f,
                };
                const Vector3 beforeOverrideRotationDegrees = overrideRotationDegrees;
                if (DragVector3("##Rotation", overrideRotationDegrees, 0.25f, -360.0f, 360.0f)) {
                    transformOverride.rotation = {
                        overrideRotationDegrees.x * 3.14159265358979323846f / 180.0f,
                        overrideRotationDegrees.y * 3.14159265358979323846f / 180.0f,
                        overrideRotationDegrees.z * 3.14159265358979323846f / 180.0f,
                    };
                    StageCoursePropertyDelta(
                        input,
                        propertyTarget,
                        "CourseRockCluster.instanceOverrides.rotation",
                        "Instance Rotation",
                        "vec3",
                        FormatPropertyVector3(beforeOverrideRotationDegrees),
                        FormatPropertyVector3(overrideRotationDegrees));
                    changed = true;
                }
                ImGui::TableNextColumn();
                if (ImGui::Button("Remove")) {
                    removeOverride = static_cast<int>(overrideIndex);
                }
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        if (removeOverride >= 0 &&
            removeOverride < static_cast<int>(cluster.instanceOverrides.size())) {
            cluster.instanceOverrides.erase(cluster.instanceOverrides.begin() + removeOverride);
            changed = true;
        }
        ImGui::SeparatorText("Material");
        changed |= DrawTerrainMaterialPresetDetails(course, cluster.distance);
        ImGui::EndDisabled();
        ImGui::PopID();
    } else {
        ImGui::TextUnformatted("No object selected.");
    }

    if (canMutate &&
        removeTerrain >= 0 &&
        removeTerrain < static_cast<int>(course.terrainPlacements.size())) {
        course.terrainPlacements.erase(course.terrainPlacements.begin() + removeTerrain);
        editor.selectedCourseTerrainPlacement = -1;
        changed = true;
    }
    if (canMutate &&
        removeRock >= 0 &&
        removeRock < static_cast<int>(course.rockClusters.size())) {
        course.rockClusters.erase(course.rockClusters.begin() + removeRock);
        editor.selectedCourseRockCluster = -1;
        changed = true;
    }
    if (changed && canMutate) {
        ++editor.courseObjectEditRevision;
    }
    return changed && canMutate;
}

void DrawCourseAuthoring(
    const CourseTimelineDebugPanelInput& input,
    CourseAsset& course,
    CourseValidationReport& validationReport,
    bool& validationReady,
    bool& dirty,
    std::string& authoringStatus) {
    if (input.coursePath != nullptr) {
        ImGui::Text("Path: %s", input.coursePath->c_str());
    }
    const editor::EditorViewportAuthoringInputGuard inputGuard =
        editor::MakeEditorViewportAuthoringInputGuard(input.canMutateAuthoring);
    const bool canMutate = inputGuard.CanMutate();
    if (!canMutate) {
        ImGui::TextDisabled("%s", inputGuard.DisabledReason());
    }
    if (input.dirtyState != nullptr) {
        dirty = input.dirtyState->HasDirtyDomain(editor::EditorDirtyDomain::CourseAuthoring);
    }
    bool changed = false;
    ImGui::BeginDisabled(!canMutate);
    ImGui::SetNextItemWidth(340.0f);
    changed |= InputString<160>("Course Name", course.name);
    ImGui::EndDisabled();

    if (ImGui::Button("Validate")) {
        validationReport = BuildValidationReport(input, course);
        validationReady = true;
        authoringStatus = validationReport.HasErrors() ? "Validation failed." : "Validation passed.";
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(!canMutate);
    if (ImGui::Button("Apply")) {
        course.SortForRuntime();
        if (input.onApplyCourse) {
            input.onApplyCourse();
        }
        validationReport = BuildValidationReport(input, course);
        validationReady = true;
        dirty = false;
        ClearCourseAuthoringDirty(input);
        authoringStatus = "Applied course to runtime.";
    }
    ImGui::SameLine();
    if (ImGui::Button("Save")) {
        std::string error;
        if (input.onSaveCourse && input.onSaveCourse(&error)) {
            validationReport = BuildValidationReport(input, course);
            validationReady = true;
            dirty = false;
            ClearCourseAuthoringDirty(input);
            authoringStatus = "Saved course.";
        } else {
            validationReport = BuildValidationReport(input, course);
            validationReady = true;
            authoringStatus = error.empty() ? "Save failed." : "Save failed. " + error;
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Reload")) {
        if (input.documentLifecycle != nullptr && input.onReloadCourse) {
            RequestCourseReloadConfirmation(input, authoringStatus);
            if (input.dirtyState != nullptr) {
                dirty = input.dirtyState->HasDirtyDomain(editor::EditorDirtyDomain::CourseAuthoring);
            }
        } else if (input.onReloadCourse) {
            input.onReloadCourse();
            validationReport = BuildValidationReport(input, course);
            validationReady = true;
            dirty = false;
            ClearCourseAuthoringDirty(input);
            authoringStatus = "Reloaded course from disk.";
        }
    }
    ImGui::EndDisabled();

    if (!authoringStatus.empty()) {
        ImGui::TextUnformatted(authoringStatus.c_str());
    }
    if (dirty) {
        ImGui::TextColored(ImVec4(1.0f, 0.72f, 0.22f, 1.0f), "Unsaved authoring changes.");
    }

    if (input.sequencer != nullptr) {
        changed |= DrawProviderSequencer(input, canMutate, authoringStatus);
    } else {
        ImGui::BeginDisabled(!canMutate);
        changed |= DrawAuthoringTimeline(input, course, authoringStatus);
        ImGui::EndDisabled();
    }

    if (ImGui::BeginTabBar("CourseAuthoringTabs")) {
        if (ImGui::BeginTabItem("Events")) {
            ImGui::BeginDisabled(!canMutate);
            if (ImGui::Button("Add Event")) {
                course.events.push_back({input.currentDistance, "enemy_wave", "new_wave", {}});
                changed = true;
            }
            ImGui::SameLine();
            if (ImGui::Button("Sort Events")) {
                course.SortForRuntime();
                changed = true;
            }
            changed |= DrawEventAuthoringTable(course);
            ImGui::EndDisabled();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Sections")) {
            ImGui::BeginDisabled(!canMutate);
            if (ImGui::Button("Add Section")) {
                course.sections.push_back({input.currentDistance, input.currentDistance + 120.0f, "New Section", "Authoring"});
                changed = true;
            }
            ImGui::SameLine();
            if (ImGui::Button("Sort Sections")) {
                course.SortForRuntime();
                changed = true;
            }
            changed |= DrawSectionAuthoringTable(input, course);
            ImGui::EndDisabled();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Camera")) {
            ImGui::BeginDisabled(!canMutate);
            if (ImGui::Button("Add Camera Key")) {
                course.cameraKeys.push_back({input.currentDistance});
                changed = true;
            }
            ImGui::SameLine();
            if (ImGui::Button("Sort Camera")) {
                course.SortForRuntime();
                changed = true;
            }
            changed |= DrawCameraAuthoringTable(course);
            ImGui::EndDisabled();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Validation")) {
            if (!validationReady) {
                validationReport = BuildValidationReport(input, course);
                validationReady = true;
            }
            DrawValidationReportTable(validationReport);
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    if (changed && canMutate) {
        dirty = true;
        validationReady = false;
        MarkCourseAuthoringDirty(input, "Course authoring fields changed.");
    }
}
} // namespace

void DrawCourseTimelineDebugPanel(const CourseTimelineDebugPanelInput& input) {
    if (input.course == nullptr) {
        ImGui::TextUnformatted("No course loaded.");
        return;
    }

    CourseAsset& course = *input.course;
    const float railLength = (std::max)(0.0f, input.railLength);
    const CourseSection* section = course.FindSection(input.currentDistance);
    const CourseCinematicShotSet* shotSet = course.FindCinematicShotSet(input.currentDistance);
    const CourseEventMarker* nextEvent = FindNextEvent(course, input.currentDistance);
    static CourseValidationReport validationReport{};
    static bool validationReady = false;
    static bool dirty = false;
    static uint32_t seenCourseObjectEditRevision = 0;
    static std::string authoringStatus;

    if (input.dirtyState != nullptr &&
        input.dirtyState->HasDirtyDomain(editor::EditorDirtyDomain::CourseAuthoring)) {
        dirty = true;
    }
    if (input.runtimeState != nullptr &&
        input.runtimeState->terrain.courseObjectEditRevision != seenCourseObjectEditRevision) {
        seenCourseObjectEditRevision = input.runtimeState->terrain.courseObjectEditRevision;
        dirty = true;
        validationReady = false;
        MarkCourseAuthoringDirty(input, "Course object edit revision changed.");
        authoringStatus = "Viewport object edit pending.";
    }

    const float summaryHeight =
        (std::min)(CourseSummaryBlockHeight(), (std::max)(ImGui::GetContentRegionAvail().y, 72.0f));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.015f, 0.018f, 0.022f, 1.0f));
    if (ImGui::BeginChild(
            "CourseTimelineSummary",
            ImVec2(0.0f, summaryHeight),
            true,
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
        if (input.loadStatus != nullptr) {
            ImGui::TextUnformatted(input.loadStatus->c_str());
        }
        ImGui::Text("Course: %s", course.name.c_str());
        ImGui::Text(
            "Distance: %.1f / %.1f  progress %.1f%%",
            input.currentDistance,
            railLength,
            railLength > 0.0f ? input.currentDistance / railLength * 100.0f : 0.0f);
        ImGui::Text(
            "Section: %s / %s",
            section != nullptr ? section->name.c_str() : "-",
            section != nullptr ? section->category.c_str() : "-");
        ImGui::Text(
            "Next: %s %s %.1fm",
            nextEvent != nullptr ? nextEvent->type.c_str() : "-",
            nextEvent != nullptr ? nextEvent->id.c_str() : "-",
            nextEvent != nullptr ? nextEvent->distance - input.currentDistance : 0.0f);
        ImGui::Text(
            "Shot Set: %s / %s",
            shotSet != nullptr ? shotSet->id.c_str() : "-",
            shotSet != nullptr ? shotSet->label.c_str() : "-");
        ImGui::Text(
            "Terrain placements: %zu  rock clusters: %zu",
            course.terrainPlacements.size(),
            course.rockClusters.size());
        ImGui::Text(
            "Visual presets: sets %zu  lighting %zu  shots %zu  materials %zu",
            course.cinematicShotSets.size(),
            course.lightingPresets.size(),
            course.cinematicCameraShots.size(),
            course.terrainMaterialPresets.size());

        ImGui::Spacing();
        DrawTimelineBar(input, course);
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();

    if (input.spawnRuntime != nullptr) {
        const CourseSpawnRuntime& runtime = *input.spawnRuntime;
        ImGui::Text(
            "Active: enemies %zu  bullets %zu  obstacles %zu  vfx %zu",
            runtime.ActiveEnemyCount(),
            runtime.ActiveBulletCount(),
            runtime.ActiveObstacleCount(),
            runtime.ActiveVfxCueCount());
    }

    if (input.gameplayWaves != nullptr && input.gameplayWaves->IsBound()) {
        const CourseGameplayWaveRuntimeStats& stats = input.gameplayWaves->Stats();
        ImGui::Text(
            "Cooked Waves: pending %u  prewarm %u  active %u  completed %u",
            stats.pendingWaves,
            stats.prewarmingWaves,
            stats.activeWaves,
            stats.completedWaves);
        ImGui::Text(
            "Cooked Actors: active %u  defeated %u  fingerprint %016llx",
            stats.activeActors,
            stats.defeatedActors,
            static_cast<unsigned long long>(stats.programFingerprint));
    }

    if (input.collisionSystem != nullptr) {
        const CourseCollisionSystem& collision = *input.collisionSystem;
        const CourseCollisionFrameStats& stats = collision.LastFrameStats();
        ImGui::Text(
            "Player: hp %.0f  invul %.2f",
            collision.Player().hitPoints,
            collision.Player().invulnerabilityTime);
        ImGui::Text(
            "Collision: damage %.0f  bulletHits %u  obstacleHits %u  world/enemy/obstacle/terrain %u/%u/%u/%u",
            stats.playerDamage,
            stats.enemyBulletHits,
            stats.obstacleHits,
            stats.playerShotWorldHits,
            stats.playerShotEnemyHits,
            stats.playerShotObstacleHits,
            stats.playerShotTerrainHits);
    }

    if (input.checkpointSystem != nullptr) {
        const SectionCheckpointStats& stats = input.checkpointSystem->LastStats();
        ImGui::Text(
            "Checkpoint: #%d %s / %s  start %.1f  transitions %u  teleports %u",
            stats.currentSectionIndex,
            stats.currentSectionName.empty() ? "-" : stats.currentSectionName.c_str(),
            stats.currentSectionCategory.empty() ? "-" : stats.currentSectionCategory.c_str(),
            stats.checkpointDistance,
            stats.sectionTransitions,
            stats.authoringTeleports);
    }

    if (input.combatFeelSystem != nullptr) {
        const PlayerCombatFeelStats& stats = input.combatFeelSystem->LastStats();
        ImGui::Text(
            "Combat Feel: score %u  combo %u/%u  hit %.2f  damage %.2f  lock %s %.1fm",
            stats.score,
            stats.combo,
            stats.maxCombo,
            stats.hitFlash,
            stats.damageFlash,
            stats.lockOnActive ? stats.lockOnTarget.c_str() : "-",
            stats.lockOnDistance);
    }

    if (ImGui::BeginTabBar("CourseTimelineTabs")) {
        if (ImGui::BeginTabItem("Preview")) {
            DrawCoursePreviewSimulation(input);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Authoring")) {
            DrawCourseAuthoring(
                input,
                course,
                validationReport,
                validationReady,
                dirty,
                authoringStatus);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Object Editor")) {
            if (DrawCourseObjectEditor(input, course)) {
                dirty = true;
                validationReady = false;
                MarkCourseAuthoringDirty(input, "Course object editor changed.");
            }
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Events")) {
            DrawEventTable(course, input.currentDistance);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Terrain")) {
            DrawTerrainPlacementTable(course, input.currentDistance);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Rock Clusters")) {
            DrawRockClusterTable(course, input.currentDistance);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Visuals")) {
            DrawVisualPresetTable(course, input.currentDistance);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Enemies")) {
            if (input.spawnRuntime != nullptr) {
                DrawActiveEnemyTable(*input.spawnRuntime);
            }
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Obstacles")) {
            if (input.spawnRuntime != nullptr) {
                DrawActiveObstacleTable(*input.spawnRuntime);
            }
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
}

#endif
