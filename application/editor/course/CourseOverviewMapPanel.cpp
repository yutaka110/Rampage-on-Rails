#include "CourseOverviewMapPanel.h"

#if defined(_MSC_VER)
#pragma execution_character_set("utf-8")
#endif

#include "../../externals/imgui/imgui.h"

#include <algorithm>
#include <cstdio>
#include <limits>

namespace editor {
namespace {

const char* MapItemLabel(CourseOverviewMapItemKind kind) {
    switch (kind) {
    case CourseOverviewMapItemKind::RailSegment: return "レール区間";
    case CourseOverviewMapItemKind::RailControlPoint: return "レール制御点";
    case CourseOverviewMapItemKind::EnemyPlacement: return "敵の配置";
    case CourseOverviewMapItemKind::Wave: return "出現グループ";
    case CourseOverviewMapItemKind::WavePrewarm: return "出現の事前準備";
    case CourseOverviewMapItemKind::WaveTransition: return "出現グループの遷移";
    case CourseOverviewMapItemKind::Playhead: return "現在のコース位置";
    case CourseOverviewMapItemKind::None: return "なし";
    }
    return "不明";
}

template <typename Owner, typename Settings>
void ApplyValidatedSettings(Owner& owner, const Settings& candidate) {
    std::string error;
    if (!owner.SetSettings(candidate, &error)) {
        ImGui::TextWrapped("設定を反映できません: %s", error.c_str());
    }
}


ImVec2 ToIm(Vector2 value) { return {value.x, value.y}; }

Vector2 WithOffset(Vector2 value, Vector2 offset) {
    return {value.x + offset.x, value.y + offset.y};
}

bool IsMapPanButtonDown() {
    return ImGui::IsMouseDown(ImGuiMouseButton_Right) ||
        ImGui::IsMouseDown(ImGuiMouseButton_Middle);
}

Vector2 MapPanPressPosition(const ImGuiIO& io) {
    const int button = ImGui::IsMouseDown(ImGuiMouseButton_Right)
        ? ImGuiMouseButton_Right : ImGuiMouseButton_Middle;
    return {io.MouseClickedPos[button].x, io.MouseClickedPos[button].y};
}

bool MatchesSelection(
    const EditorSelection* selection,
    EditorDomainId domain,
    uint64_t localIndex) {
    if (selection == nullptr) return false;
    return std::any_of(selection->Handles().begin(), selection->Handles().end(),
        [domain, localIndex](const EditorObjectHandle& handle) {
            return handle.domain == domain && handle.localIndex == localIndex;
        });
}

std::vector<Vector2> SelectedMapPoints(
    const CourseOverviewMapFrame& overview,
    const CourseMapSceneVisualizationFrame* scene,
    const EditorSelection* selection) {
    std::vector<Vector2> result;
    for (const CourseOverviewMapMarker& marker : overview.markers) {
        if (marker.selected || MatchesSelection(selection,
                marker.handle.domain, marker.handle.localIndex)) {
            result.push_back(marker.position);
        }
    }
    if (scene != nullptr) {
        for (const CourseMapScreenSpaceProxy& proxy : scene->screenSpaceProxies) {
            if (proxy.selected || MatchesSelection(selection,
                    proxy.domain, proxy.localIndex)) {
                result.push_back(proxy.center);
            }
        }
        for (const CourseMapSceneActorProxy& actor : scene->actors) {
            if (actor.selected) result.push_back(actor.center);
        }
    }
    return result;
}

const CourseMapScreenSpaceProxy* PickScreenSpaceProxy(
    const CourseMapSceneVisualizationFrame* scene,
    Vector2 mapPosition,
    Vector2 presentationOffset = {}) {
    if (scene == nullptr) return nullptr;
    const CourseMapScreenSpaceProxy* best = nullptr;
    float bestDistance = (std::numeric_limits<float>::max)();
    for (const CourseMapScreenSpaceProxy& proxy : scene->screenSpaceProxies) {
        const Vector2 center = WithOffset(proxy.center, presentationOffset);
        const float x = mapPosition.x - center.x;
        const float y = mapPosition.y - center.y;
        const float distance = x * x + y * y;
        const float tolerance = proxy.radiusPixels + 5.0f;
        if (distance <= tolerance * tolerance && distance < bestDistance) {
            best = &proxy;
            bestDistance = distance;
        }
    }
    return best;
}

void DrawToolbar(
    CourseOverviewMapController& controller,
    CourseMap3DViewportController* viewport3D,
    CourseRailEditorController* rail,
    EditorSelection* selection,
    CourseOverviewMapEditTool* tool,
    CourseOverviewMapSnapService* snapping,
    CourseRailSketchTool* sketch,
    CourseOverviewMapMultiViewCoordinator* multiView,
    CourseMapVisualBakePipeline* visualBake,
    CourseMapCartographyBakePipeline* cartographyBake,
    CourseMapCartographyRenderer* cartographyRenderer,
    CourseTerrainMapBakePipeline* terrainMapBake,
    CourseTerrainMapRenderer* terrainMapRenderer,
    CourseMapHybridCartographyCompositor* hybridCompositor,
    CourseMapHologramRenderer* hologramRenderer,
    CourseMapSceneVisualizationPipeline* sceneVisualization,
    CourseMapEditCategory category) {
    if (rail != nullptr && rail->Transactions() != nullptr) {
        EditorTransactionStack* history = rail->Transactions();
        const bool interactionBusy =
            (tool != nullptr && tool->State().dragging) ||
            (sketch != nullptr && sketch->State().drawing);
        const bool canUndo = !interactionBusy && rail->State().authoringAllowed &&
            history->CanUndo();
        const bool canRedo = !interactionBusy && rail->State().authoringAllowed &&
            history->CanRedo();
        const EditorTransactionRecord* undo = history->NextUndoTransaction();
        const EditorTransactionRecord* redo = history->NextRedoTransaction();
        if (!canUndo) ImGui::BeginDisabled();
        if (ImGui::Button("取り消し")) {
            std::string ignored;
            if (rail->Undo(&ignored) && selection != nullptr) selection->Clear();
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip("%s", undo != nullptr
                ? undo->label.c_str()
                : interactionBusy ? "現在の編集を確定するか取り消してください。"
                                  : "取り消せる編集がありません。");
        }
        if (!canUndo) ImGui::EndDisabled();
        ImGui::SameLine();
        if (!canRedo) ImGui::BeginDisabled();
        if (ImGui::Button("やり直し")) {
            std::string ignored;
            if (rail->Redo(&ignored) && selection != nullptr) selection->Clear();
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip("%s", redo != nullptr
                ? redo->label.c_str()
                : interactionBusy ? "現在の編集を確定するか取り消してください。"
                                  : "やり直せる編集がありません。");
        }
        if (!canRedo) ImGui::EndDisabled();
        ImGui::SameLine();
    }
    if (multiView != nullptr) {
        const bool enabled = multiView->State().enabled;
        if (enabled) ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(56, 122, 156, 255));
        if (ImGui::Button("複数画面")) {
            multiView->SetEnabled(!enabled);
            if (!enabled && viewport3D != nullptr) viewport3D->SetActive(false);
        }
        if (enabled) ImGui::PopStyleColor();
        ImGui::SameLine();
    }
    const auto modeButton = [&](const char* label, CourseOverviewMapProjectionMode mode) {
        const bool selected = (viewport3D == nullptr || !viewport3D->State().active) &&
            controller.State().mode == mode;
        if (selected) ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(55, 115, 145, 255));
        if (ImGui::Button(label)) {
            if (viewport3D != nullptr) viewport3D->SetActive(false);
            controller.SetMode(mode);
        }
        if (selected) ImGui::PopStyleColor();
        ImGui::SameLine();
    };
    modeButton("上から", CourseOverviewMapProjectionMode::Top);
    modeButton("横から", CourseOverviewMapProjectionMode::Side);
    modeButton("距離で表示", CourseOverviewMapProjectionMode::RailUnwrapped);
    modeButton("自由投影", CourseOverviewMapProjectionMode::Free);
    if (viewport3D != nullptr) {
        const bool selected = viewport3D->State().active;
        if (selected) ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(55, 115, 145, 255));
        if (ImGui::Button(selected ? "2Dに戻る" : "3Dで確認")) {
            viewport3D->SetActive(!selected);
            if (!selected && multiView != nullptr) multiView->SetEnabled(false);
        }
        if (selected) ImGui::PopStyleColor();
        ImGui::SameLine();
    }
    if (ImGui::Button("全体を表示")) {
        if (viewport3D != nullptr && viewport3D->State().active) {
            viewport3D->FrameAll(terrainMapBake != nullptr
                    ? terrainMapBake->CurrentAsset() : nullptr,
                sceneVisualization != nullptr
                    ? sceneVisualization->CurrentFrame(
                        controller.Projection().Settings().mode) : nullptr);
        } else {
            controller.FrameAll();
            if (multiView != nullptr) multiView->FrameAll();
        }
    }
    if (tool != nullptr) {
        ImGui::Separator();
        const auto toolButton = [tool, viewport3D](const char* label, CourseOverviewMapEditMode mode) {
            const bool selected = tool->State().mode == mode;
            if (selected) ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(48, 132, 92, 255));
            if (ImGui::SmallButton(label)) {
                tool->SetMode(mode);
                if (viewport3D) viewport3D->SetActive(false);
            }
            if (selected) ImGui::PopStyleColor();
            ImGui::SameLine();
        };
        toolButton("選択・移動", CourseOverviewMapEditMode::SelectMove);
        if (category != CourseMapEditCategory::Encounters) toolButton("制御点を追加", CourseOverviewMapEditMode::AddRailPoint);
        if (category != CourseMapEditCategory::Course) toolButton("敵を追加", CourseOverviewMapEditMode::AddEnemy);
        if (category != CourseMapEditCategory::Course) toolButton("出現グループを追加", CourseOverviewMapEditMode::AddWave);
        toolButton("削除", CourseOverviewMapEditMode::Delete);
        ImGui::NewLine();
    }
    if (sketch != nullptr && category != CourseMapEditCategory::Encounters) {
        const bool sketchActive = sketch->State().active;
        if (sketchActive) {
            ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(138, 84, 42, 255));
        }
        if (ImGui::SmallButton(sketchActive ? "描画を終了" : "レールを描く")) {
            sketch->SetActive(!sketchActive);
            if (!sketchActive && viewport3D) viewport3D->SetActive(false);
        }
        if (sketchActive) ImGui::PopStyleColor();
        if (sketch->State().active) {
            ImGui::SameLine();
            const auto sketchButton = [sketch](const char* label, CourseRailSketchMode mode) {
                const bool selected = sketch->State().mode == mode;
                if (selected) {
                    ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(162, 104, 55, 255));
                }
                if (ImGui::SmallButton(label)) sketch->SetMode(mode);
                if (selected) ImGui::PopStyleColor();
                ImGui::SameLine();
            };
            sketchButton("終端に追加", CourseRailSketchMode::Append);
            sketchButton("始端に追加", CourseRailSketchMode::Prepend);
            sketchButton("区間を置き換え", CourseRailSketchMode::ReplaceSegment);
            ImGui::TextDisabled("入力 %u / 制御点 %u / 長さ %.1f / 最小半径 %.1f",
                sketch->State().rawSamples,
                sketch->State().fittedControlPoints,
                sketch->State().fittedLength,
                sketch->State().minimumTurnRadius);
        }
    }
    if (snapping != nullptr && ImGui::CollapsingHeader("配置補助（スナップ）")) {
        CourseOverviewMapSnapSettings settings = snapping->Settings();
        bool changed = ImGui::Checkbox("距離を揃える", &settings.railDistanceEnabled);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(70.0f);
        changed |= ImGui::DragFloat("##OverviewDistanceStep", &settings.railDistanceStep, 0.1f, 0.01f, 1000.0f, "%.2f");
        ImGui::SameLine();
        changed |= ImGui::Checkbox("横位置を揃える", &settings.lateralOffsetEnabled);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(70.0f);
        changed |= ImGui::DragFloat("##OverviewOffsetStep", &settings.lateralOffsetStep, 0.1f, 0.01f, 1000.0f, "%.2f");
        ImGui::SameLine();
        changed |= ImGui::Checkbox("制御点に吸着", &settings.controlPointMagnetEnabled);
        if (changed) ApplyValidatedSettings(*snapping, settings);
    }
    ImGui::TextWrapped("2D: 左ドラッグで編集 / 右・中ドラッグで移動 / ホイールで拡大 / Escで取り消し");
    if (ImGui::CollapsingHeader("表示の詳細設定（地形・表示更新）")) {
    if (visualBake != nullptr) {
        ImGui::Separator();
        ImGui::TextDisabled("表示データ: %s", ToString(visualBake->LastResult().status));
        ImGui::SameLine();
        if (ImGui::SmallButton("表示データを更新")) visualBake->RequestRebuild();
    }
    if (cartographyBake != nullptr) {
        ImGui::SameLine();
        const CourseMapCartographyBakeResult& result =
            cartographyBake->LastResult();
        ImGui::TextDisabled("領域: %s（形状 %u / 簡易 %u）",
            ToString(result.status), result.stats.exactRegions,
            result.stats.fallbackRegions);
        ImGui::SameLine();
        if (ImGui::SmallButton("地形の形を更新")) {
            cartographyBake->RequestRebuild();
        }
    }
    if (cartographyRenderer != nullptr) {
        CourseMapCartographyRenderSettings settings =
            cartographyRenderer->Settings();
        ImGui::SameLine();
        bool changed = ImGui::Checkbox("領域の形状", &settings.enabled);
        ImGui::SameLine();
        if (!settings.enabled) ImGui::BeginDisabled();
        changed |= ImGui::Checkbox("メッシュ面", &settings.showSurfaceTriangles);
        ImGui::SameLine();
        changed |= ImGui::Checkbox("簡易ボックス", &settings.showFallbackGeometry);
        if (!settings.enabled) ImGui::EndDisabled();
        if (changed) ApplyValidatedSettings(*cartographyRenderer, settings);
    }
    if (terrainMapBake != nullptr) {
        ImGui::SameLine();
        const CourseTerrainMapBakeResult& result = terrainMapBake->LastResult();
        ImGui::TextDisabled("地形: %s（%u タイル）",
            ToString(result.status), result.stats.tiles);
        ImGui::SameLine();
        if (ImGui::SmallButton("地形マップを更新")) {
            terrainMapBake->RequestRebuild();
        }
    }
    if (terrainMapRenderer != nullptr) {
        CourseTerrainMapRenderSettings settings = terrainMapRenderer->Settings();
        ImGui::SameLine();
        bool changed = ImGui::Checkbox("地形の面", &settings.enabled);
        ImGui::SameLine();
        changed |= ImGui::Checkbox("地形の等高線", &settings.showContours);
        if (changed) ApplyValidatedSettings(*terrainMapRenderer, settings);
    }
    if (hybridCompositor != nullptr) {
        CourseMapHybridCartographySettings settings =
            hybridCompositor->Settings();
        ImGui::SameLine();
        if (ImGui::Checkbox("地形表示を合成", &settings.enabled)) {
            ApplyValidatedSettings(*hybridCompositor, settings);
        }
    }
    if (hologramRenderer != nullptr) {
        CourseMapHologramSettings settings = hologramRenderer->Settings();
        ImGui::SameLine();
        bool changed = ImGui::Checkbox("簡易表示で補完", &settings.enabled);
        ImGui::SameLine();
        changed |= ImGui::Checkbox("輪郭線", &settings.showContours);
        if (changed) ApplyValidatedSettings(*hologramRenderer, settings);
    }
    if (sceneVisualization != nullptr) {
        ImGui::Separator();
        CourseMapSceneVisualizationSettings settings = sceneVisualization->Settings();
        bool changed = ImGui::Checkbox("配置物を表示", &settings.enabled);
        ImGui::SameLine();
        if (!settings.enabled) ImGui::BeginDisabled();
        changed |= ImGui::Checkbox("地形", &settings.showTerrain);
        ImGui::SameLine();
        changed |= ImGui::Checkbox("岩", &settings.showRockClusters);
        ImGui::SameLine();
        changed |= ImGui::Checkbox("建造物", &settings.showSceneStructures);
        ImGui::SameLine();
        changed |= ImGui::Checkbox("配置した敵", &settings.showAuthoredEnemies);
        ImGui::SameLine();
        changed |= ImGui::Checkbox("出現プレビュー", &settings.showEncounterPreview);
        ImGui::SameLine();
        changed |= ImGui::Checkbox("名前を表示", &settings.showLabels);
        if (!settings.enabled) ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::SmallButton("表示素材を再読み込み")) {
            sceneVisualization->ReloadVisualAssets();
        }
        if (changed) ApplyValidatedSettings(*sceneVisualization, settings);
    }
    }
}

void DrawCartography(
    const CourseMapCartographyFrame* frame,
    Vector2 presentationOffset = {}) {
    if (frame == nullptr || !frame->valid) return;
    ImDrawList* draw = ImGui::GetWindowDrawList();
    // AddTriangleFilled builds and clears a temporary path for every triangle.
    // Exact terrain maps contain thousands of triangles, making that helper a
    // dominant Debug/Release editor cost. Emit retained triangles directly in
    // bounded ImDrawList batches instead.
    constexpr std::size_t kTriangleBatchSize = 4096u;
    const ImVec2 whiteUv = ImGui::GetFontTexUvWhitePixel();
    const Vector2 offset{
        frame->presentationOffset.x + presentationOffset.x,
        frame->presentationOffset.y + presentationOffset.y};
    for (std::size_t first = 0; first < frame->triangles.size();
         first += kTriangleBatchSize) {
        const std::size_t count = (std::min)(
            kTriangleBatchSize, frame->triangles.size() - first);
        draw->PrimReserve(static_cast<int>(count * 3u),
            static_cast<int>(count * 3u));
        for (std::size_t index = first; index < first + count; ++index) {
            const CourseMapCartographyTriangle& triangle =
                frame->triangles[index];
            const ImDrawIdx base = static_cast<ImDrawIdx>(draw->_VtxCurrentIdx);
            draw->PrimWriteIdx(base);
            draw->PrimWriteIdx(static_cast<ImDrawIdx>(base + 1u));
            draw->PrimWriteIdx(static_cast<ImDrawIdx>(base + 2u));
            draw->PrimWriteVtx(ToIm({triangle.a.x + offset.x,
                triangle.a.y + offset.y}), whiteUv, triangle.fillColor);
            draw->PrimWriteVtx(ToIm({triangle.b.x + offset.x,
                triangle.b.y + offset.y}), whiteUv, triangle.fillColor);
            draw->PrimWriteVtx(ToIm({triangle.c.x + offset.x,
                triangle.c.y + offset.y}), whiteUv, triangle.fillColor);
        }
    }
    for (const CourseMapCartographyOutline& outline : frame->outlines) {
        if (outline.points.size() < 2u) continue;
        const ImDrawFlags flags = outline.closed
            ? ImDrawFlags_Closed : ImDrawFlags_None;
        if (outline.glowColor != 0u) {
            draw->PathClear();
            for (Vector2 point : outline.points) {
                draw->PathLineTo(ToIm(
                    {point.x + offset.x, point.y + offset.y}));
            }
            draw->PathStroke(outline.glowColor, flags, outline.glowThickness);
        }
        draw->PathClear();
        for (Vector2 point : outline.points) {
            draw->PathLineTo(ToIm(
                {point.x + offset.x, point.y + offset.y}));
        }
        draw->PathStroke(outline.locked
                ? IM_COL32(214, 122, 255, 235) : outline.color,
            flags, outline.thickness);
    }
}

void DrawTerrainMap(
    const CourseTerrainMapFrame* frame,
    Vector2 presentationOffset = {}) {
    if (frame == nullptr || !frame->valid) return;
    ImDrawList* draw = ImGui::GetWindowDrawList();
    constexpr std::size_t kTriangleBatchSize = 4096u;
    const ImVec2 whiteUv = ImGui::GetFontTexUvWhitePixel();
    for (std::size_t first = 0; first < frame->triangles.size();
         first += kTriangleBatchSize) {
        const std::size_t count = (std::min)(
            kTriangleBatchSize, frame->triangles.size() - first);
        draw->PrimReserve(static_cast<int>(count * 3u),
            static_cast<int>(count * 3u));
        for (std::size_t index = first; index < first + count; ++index) {
            const CourseTerrainMapTriangle& triangle = frame->triangles[index];
            const ImDrawIdx base = static_cast<ImDrawIdx>(draw->_VtxCurrentIdx);
            draw->PrimWriteIdx(base);
            draw->PrimWriteIdx(static_cast<ImDrawIdx>(base + 1u));
            draw->PrimWriteIdx(static_cast<ImDrawIdx>(base + 2u));
            draw->PrimWriteVtx(ToIm(WithOffset(triangle.a, presentationOffset)),
                whiteUv, triangle.color);
            draw->PrimWriteVtx(ToIm(WithOffset(triangle.b, presentationOffset)),
                whiteUv, triangle.color);
            draw->PrimWriteVtx(ToIm(WithOffset(triangle.c, presentationOffset)),
                whiteUv, triangle.color);
        }
    }
    for (const CourseTerrainMapPolyline& contour : frame->contours) {
        if (contour.points.size() < 2u) continue;
        draw->PathClear();
        for (Vector2 point : contour.points) {
            draw->PathLineTo(ToIm(WithOffset(point, presentationOffset)));
        }
        draw->PathStroke(contour.color, ImDrawFlags_None, contour.thickness);
    }
}

void DrawHologram(
    const CourseMapHologramFrame* frame,
    Vector2 presentationOffset = {}) {
    if (frame == nullptr || !frame->valid) return;
    ImDrawList* draw = ImGui::GetWindowDrawList();
    for (const CourseMapHologramPolygon& polygon : frame->polygons) {
        if (polygon.points.size() < 3u) continue;
        if (polygon.glowColor != 0u) {
            draw->PathClear();
            for (Vector2 point : polygon.points) {
                draw->PathLineTo(ToIm(WithOffset(point, presentationOffset)));
            }
            draw->PathStroke(polygon.glowColor, ImDrawFlags_Closed,
                polygon.glowThickness);
        }
        draw->PathClear();
        for (Vector2 point : polygon.points) {
            draw->PathLineTo(ToIm(WithOffset(point, presentationOffset)));
        }
        draw->PathFillConvex(polygon.fillColor);
        draw->PathClear();
        for (Vector2 point : polygon.points) {
            draw->PathLineTo(ToIm(WithOffset(point, presentationOffset)));
        }
        draw->PathStroke(polygon.locked
                ? IM_COL32(214, 122, 255, 235) : polygon.outlineColor,
            ImDrawFlags_Closed, polygon.outlineThickness);
    }
    for (const CourseMapHologramLineBatch& contour : frame->contours) {
        if (contour.points.size() < 2u) continue;
        if (contour.glowColor != 0u) {
            draw->PathClear();
            for (Vector2 point : contour.points) {
                draw->PathLineTo(ToIm(WithOffset(point, presentationOffset)));
            }
            draw->PathStroke(contour.glowColor, ImDrawFlags_Closed,
                contour.glowThickness);
        }
        draw->PathClear();
        for (Vector2 point : contour.points) {
            draw->PathLineTo(ToIm(WithOffset(point, presentationOffset)));
        }
        draw->PathStroke(contour.color, ImDrawFlags_Closed, contour.thickness);
    }
}

void DrawSceneVisualizationBackground(
    const CourseMapSceneVisualizationFrame* frame,
    bool hologramGrid,
    Vector2 presentationOffset = {}) {
    if (frame == nullptr || !frame->valid) return;
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 minimum{frame->rect.x, frame->rect.y};
    const ImVec2 maximum{frame->rect.x + frame->rect.width,
        frame->rect.y + frame->rect.height};
    if (hologramGrid) {
        constexpr float spacing = 64.0f;
        for (float x = frame->rect.x + spacing; x < maximum.x; x += spacing) {
            draw->AddLine({x, minimum.y}, {x, maximum.y},
                IM_COL32(39, 112, 126, 28), 1.0f);
        }
        for (float y = frame->rect.y + spacing; y < maximum.y; y += spacing) {
            draw->AddLine({minimum.x, y}, {maximum.x, y},
                IM_COL32(39, 112, 126, 28), 1.0f);
        }
    }
    for (const CourseMapScenePolygon& polygon : frame->polygons) {
        if (polygon.points.size() < 3) continue;
        draw->PathClear();
        for (Vector2 point : polygon.points) {
            draw->PathLineTo(ToIm(WithOffset(point, presentationOffset)));
        }
        draw->PathFillConvex(polygon.fillColor);
        draw->PathClear();
        for (Vector2 point : polygon.points) {
            draw->PathLineTo(ToIm(WithOffset(point, presentationOffset)));
        }
        draw->PathStroke(polygon.locked
                ? IM_COL32(214, 122, 255, 235)
                : polygon.outlineColor,
            ImDrawFlags_Closed, polygon.outlineThickness);
    }
}

void DrawSceneVisualizationForeground(
    const CourseMapSceneVisualizationFrame* frame,
    Vector2 presentationOffset = {}) {
    if (frame == nullptr || !frame->valid) return;
    ImDrawList* draw = ImGui::GetWindowDrawList();
    for (const CourseMapScreenSpaceProxy& proxy : frame->screenSpaceProxies) {
        const ImVec2 center = ToIm(WithOffset(proxy.center, presentationOffset));
        const float radius = proxy.radiusPixels;
        if (proxy.kind == CourseMapSceneVisualKind::SceneStructure) {
            draw->AddRectFilled({center.x - radius, center.y - radius},
                {center.x + radius, center.y + radius}, proxy.fillColor, 2.0f);
            draw->AddRect({center.x - radius, center.y - radius},
                {center.x + radius, center.y + radius}, proxy.outlineColor,
                2.0f, 0, proxy.selected ? 2.4f : 1.4f);
        } else {
            draw->AddQuadFilled({center.x, center.y - radius},
                {center.x + radius, center.y}, {center.x, center.y + radius},
                {center.x - radius, center.y}, proxy.fillColor);
            draw->AddQuad({center.x, center.y - radius},
                {center.x + radius, center.y}, {center.x, center.y + radius},
                {center.x - radius, center.y}, proxy.outlineColor,
                proxy.selected ? 2.4f : 1.4f);
        }
        if (proxy.selected) {
            draw->AddCircle(center, radius + 4.0f,
                IM_COL32(232, 255, 240, 245), 24, 2.0f);
        }
        if (proxy.locked) {
            draw->AddLine({center.x - radius * 0.6f, center.y + radius * 0.6f},
                {center.x + radius * 0.6f, center.y - radius * 0.6f},
                IM_COL32(224, 132, 255, 235), 1.8f);
        }
    }
    for (const CourseMapSceneActorProxy& actor : frame->actors) {
        if (actor.silhouette.size() < 3) continue;
        draw->PathClear();
        for (Vector2 point : actor.silhouette) {
            draw->PathLineTo(ToIm(WithOffset(point, presentationOffset)));
        }
        draw->PathFillConvex(actor.fillColor);
        draw->PathClear();
        for (Vector2 point : actor.silhouette) {
            draw->PathLineTo(ToIm(WithOffset(point, presentationOffset)));
        }
        draw->PathStroke(actor.outlineColor, ImDrawFlags_Closed,
            actor.selected ? 2.4f : 1.35f);
        draw->AddLine(ToIm(WithOffset(actor.center, presentationOffset)),
            ToIm(WithOffset(actor.headingEnd, presentationOffset)),
            actor.outlineColor, actor.selected ? 2.2f : 1.2f);
        draw->AddCircleFilled(ToIm(WithOffset(actor.center, presentationOffset)), 1.8f,
            IM_COL32(222, 255, 255, 230), 8);
        if (actor.selected) {
            draw->AddCircle(ToIm(WithOffset(actor.center, presentationOffset)),
                actor.radiusPixels + 4.0f, IM_COL32(232, 255, 240, 245),
                24, 2.0f);
        }
    }
    for (const CourseMapSceneLabel& label : frame->labels) {
        if (label.drawLeaderLine) {
            draw->AddLine(ToIm(WithOffset(label.leaderStart, presentationOffset)),
                ToIm(WithOffset(label.leaderEnd, presentationOffset)),
                (label.color & 0x00ffffffu) | 0x78000000u, 1.0f);
            draw->AddCircleFilled(ToIm(WithOffset(label.anchor, presentationOffset)), 1.7f,
                (label.color & 0x00ffffffu) | 0xb0000000u, 8);
        }
        draw->AddText(ToIm(WithOffset(label.position, presentationOffset)),
            label.color, label.text.c_str());
    }
}

void DrawCourseMap3DFrame(
    const CourseMap3DFrame& frame,
    const CourseMap3DPickResult& hovered,
    const CourseMap3DDynamicOverlay* overlay, bool cameraEditing = false) {
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 minimum{frame.viewport.x, frame.viewport.y};
    const ImVec2 maximum{frame.viewport.x + frame.viewport.width,
        frame.viewport.y + frame.viewport.height};
    draw->AddRectFilled(minimum, maximum, IM_COL32(8, 14, 19, 255));
    draw->AddRect(minimum, maximum, IM_COL32(58, 93, 108, 255));
    draw->PushClipRect(minimum, maximum, true);

    for (const CourseMap3DLine& line : frame.gridLines) {
        draw->AddLine(ToIm(line.start), ToIm(line.end),
            line.color, line.thickness);
    }
    for (const CourseMap3DTriangle& triangle : frame.terrainTriangles) {
        draw->AddTriangleFilled(ToIm(triangle.a), ToIm(triangle.b),
            ToIm(triangle.c), triangle.color);
    }
    for (const CourseMap3DLine& line : frame.lines) {
        const bool hot = hovered.hit && line.selectable &&
            line.handle.SameObject(hovered.handle);
        draw->AddLine(ToIm(line.start), ToIm(line.end),
            hot ? IM_COL32(255, 255, 255, 245) : line.color,
            line.thickness + (hot ? 2.0f : line.selected ? 1.0f : 0.0f));
    }
    for (const CourseMap3DMarker& marker : frame.markers) {
        const bool hot = hovered.hit && marker.selectable &&
            marker.handle.SameObject(hovered.handle);
        const float radius = marker.radiusPixels + (hot ? 2.0f : 0.0f);
        const ImVec2 point = ToIm(marker.screen);
        if (marker.visualKind == CourseMapSceneVisualKind::SceneStructure) {
            draw->AddRectFilled({point.x - radius, point.y - radius},
                {point.x + radius, point.y + radius}, marker.color, 2.0f);
        } else if (marker.visualKind == CourseMapSceneVisualKind::HeroLandmark) {
            draw->AddQuadFilled({point.x, point.y - radius},
                {point.x + radius, point.y}, {point.x, point.y + radius},
                {point.x - radius, point.y}, marker.color);
        } else if (marker.kind == CourseOverviewMapItemKind::Wave) {
            draw->AddQuadFilled({point.x, point.y - radius},
                {point.x + radius, point.y}, {point.x, point.y + radius},
                {point.x - radius, point.y}, marker.color);
        } else if (marker.kind == CourseOverviewMapItemKind::EnemyPlacement) {
            draw->AddTriangleFilled({point.x, point.y - radius},
                {point.x + radius, point.y + radius},
                {point.x - radius, point.y + radius}, marker.color);
        } else {
            draw->AddCircleFilled(point, radius, marker.color, 18);
        }
        if (marker.selected || hot) {
            draw->AddCircle(point, radius + 2.5f,
                IM_COL32(255, 255, 255, 235), 20, 1.7f);
        }
        if (marker.locked) {
            draw->AddLine({point.x - radius * 0.65f, point.y + radius * 0.65f},
                {point.x + radius * 0.65f, point.y - radius * 0.65f},
                IM_COL32(224, 132, 255, 235), 1.5f);
        }
    }
    for (const CourseMap3DLabel& label : frame.labels) {
        draw->AddText(ToIm(label.screen), label.color, label.text.c_str());
    }
    if (overlay != nullptr && overlay->valid) {
        draw->AddLine(ToIm(overlay->heading.start), ToIm(overlay->heading.end),
            overlay->heading.color, overlay->heading.thickness);
        const ImVec2 center = ToIm(overlay->player.screen);
        const float dx = overlay->heading.end.x - overlay->heading.start.x;
        const float dy = overlay->heading.end.y - overlay->heading.start.y;
        const float length = std::sqrt(dx * dx + dy * dy);
        const float fx = length > 0.001f ? dx / length : 0.0f;
        const float fy = length > 0.001f ? dy / length : -1.0f;
        const float rx = -fy;
        const float ry = fx;
        const float radius = overlay->player.radiusPixels;
        draw->AddTriangleFilled(
            {center.x + fx * radius, center.y + fy * radius},
            {center.x - fx * radius * 0.7f + rx * radius * 0.75f,
                center.y - fy * radius * 0.7f + ry * radius * 0.75f},
            {center.x - fx * radius * 0.7f - rx * radius * 0.75f,
                center.y - fy * radius * 0.7f - ry * radius * 0.75f},
            overlay->player.color);
        draw->AddCircle(center, radius + 3.0f,
            IM_COL32(220, 255, 255, 235), 20, 1.5f);
        draw->AddText(ToIm(overlay->label.screen), overlay->label.color,
            overlay->label.text.c_str());
    }
    draw->AddText({minimum.x + 9.0f, minimum.y + 7.0f},
        IM_COL32(184, 215, 228, 235),
        cameraEditing ? "3D表示: 右ドラッグで回転 / 中ドラッグで移動 / ホイールで拡大 / Fで撮影位置へ" :
            "3D確認: 右ドラッグで回転 / 中ドラッグで移動 / ホイールで拡大 / Fで選択対象へ");
    draw->PopClipRect();
}

void DrawMapCameraRangeOverlay(const CourseMap3DFrame& frame,
    const CourseOverviewMapPanelContext& context,Vector2 mouse) {
    if (!context.cameraRangeTool || !context.cameraPanel || !context.rail || !context.rail->Model()) return;
    const auto& tool=*context.cameraRangeTool;
    const auto& path=context.rail->Model()->RuntimePath();
    auto* draw=ImGui::GetWindowDrawList();const auto& rect=frame.viewport;
    draw->PushClipRect({rect.x,rect.y},{rect.x+rect.width,rect.y+rect.height},true);
    const auto range=[&](float start,float end,ImU32 color,float width) {
        auto a=ProjectCourseMap3DPoint(path.Evaluate(start).position,frame.camera,rect);
        const int count=(std::clamp)(int(std::ceil(std::abs(end-start)/3)),16,1024);
        for (int i=1;i<=count;++i) {
            const auto b=ProjectCourseMap3DPoint(path.Evaluate(start+(end-start)*i/count).position,frame.camera,rect);
            if (a.valid && b.valid) draw->AddLine(ToIm(a.screen),ToIm(b.screen),color,width);
            a=b;
        }
    };
    const auto* selected=context.cameraPanel->SelectedShot();
    if (context.cameraContext.course) for (const auto& shot:context.cameraContext.course->cinematicCameraShots) {
        if (!selected || shot.id!=selected->id) range(shot.startDistance,shot.endDistance,IM_COL32(125,151,205,155),3);
    }
    if (selected) {
        const float start=tool.Dragging()?tool.Start():selected->startDistance;
        const float end=tool.Dragging()?tool.End():selected->endDistance;
        range(start,end,tool.Dragging()?IM_COL32(255,204,80,255):IM_COL32(94,166,255,255),6);
        if (tool.Mode()==CourseMapCameraEditMode::Range) {
            const auto hot=CourseMapCameraRangeTool::PickHandle(frame,path,*selected,mouse);
            const auto endpoint=[&](float distance,bool isStart) {
                const auto p=ProjectCourseMap3DPoint(path.Evaluate(distance).position,frame.camera,rect);
                if (!p.valid) return;
                const ImVec2 point{p.screen.x,p.screen.y+(isStart?-12.0f:12.0f)};
                const ImU32 color=isStart?IM_COL32(100,245,150,255):IM_COL32(255,115,115,255);
                draw->AddLine(ToIm(p.screen),point,color,2);
                if (isStart) draw->AddCircleFilled(point,9,color,20);
                else draw->AddRectFilled({point.x-8,point.y-8},{point.x+8,point.y+8},color,2);
                if (hot==(isStart?CourseMapRangeHandle::Start:CourseMapRangeHandle::End) ||
                    (tool.Dragging() && tool.Handle()==(isStart?CourseMapRangeHandle::Start:CourseMapRangeHandle::End)))
                    draw->AddCircle(point,13,IM_COL32(255,255,255,255),20,2);
                char label[80];snprintf(label,sizeof(label),isStart?"開始 %.1f m":"終了 %.1f m",distance);
                draw->AddText({(std::min)(point.x+15,rect.x+rect.width-125),point.y-9},color,label);
            };
            endpoint(start,true);endpoint(end,false);
        }
    }
    if (tool.Mode()==CourseMapCameraEditMode::Create) {
        if (tool.Creating()) {
            const auto anchor=ProjectCourseMap3DPoint(path.Evaluate(tool.Anchor()).position,frame.camera,rect);
            if (anchor.valid) { draw->AddCircleFilled(ToIm(anchor.screen),9,IM_COL32(100,245,150,255),20);
                draw->AddText({anchor.screen.x+13,anchor.screen.y-18},IM_COL32(100,245,150,255),"開始候補"); }
            if (tool.Hover().hit) range(tool.Anchor(),tool.Hover().distance,IM_COL32(255,204,80,255),6);
        }
        if (tool.Hover().hit) {
            const auto point=ProjectCourseMap3DPoint(tool.Hover().world,frame.camera,rect);
            if (point.valid) {
                draw->AddCircle(ToIm(point.screen),11,IM_COL32(255,235,130,255),20,2);
                char label[96];snprintf(label,sizeof(label),tool.Creating()?"終了候補 %.1f m":"開始候補 %.1f m",tool.Hover().distance);
                draw->AddText({point.screen.x+14,point.screen.y+10},IM_COL32(255,235,130,255),label);
            }
        }
    }
    if (tool.Mode()!=CourseMapCameraEditMode::Composition)
        draw->AddText({rect.x+9,rect.y+28},IM_COL32(255,225,145,255),context.preview && context.preview->IsPlaying()
            ? "再生中: 一時停止すると区間の範囲を調整できます" : tool.Mode()==CourseMapCameraEditMode::Create
            ? (tool.Creating()?"終了位置をクリック（1m以上 / Escで取消）":"レール上の開始位置をクリック")
            : "区間をクリックして選択 / 緑の開始・赤の終了をドラッグ / Escで取消");
    draw->PopClipRect();
}

void DrawMapCameraOverlay(const CourseMap3DFrame& frame,
    const CourseOverviewMapPanelContext& context, Vector2 mouse) {
    if (!context.cameraPanel || !context.cameraTool) return;
    const auto& composition=context.cameraPanel->Composition();
    const auto geometry=CourseMapCameraEditTool::BuildGeometry(composition);
    if (!geometry.valid || !context.cameraPanel->SelectedShot() ||
        (context.cameraRangeTool && context.cameraRangeTool->Mode()!=CourseMapCameraEditMode::Composition)) return;
    auto* draw=ImGui::GetWindowDrawList();
    const auto& rect=frame.viewport;
    draw->PushClipRect({rect.x,rect.y},{rect.x+rect.width,rect.y+rect.height},true);
    const auto line=[&](Vector3 a,Vector3 b,ImU32 color,float width) {
        const auto pa=ProjectCourseMap3DPoint(a,frame.camera,rect);
        const auto pb=ProjectCourseMap3DPoint(b,frame.camera,rect);
        if (pa.valid && pb.valid) draw->AddLine(ToIm(pa.screen),ToIm(pb.screen),color,width);
    };
    const auto* shot=context.cameraPanel->SelectedShot();
    for (const auto& edge:geometry.frustum) line(edge.start,edge.end,IM_COL32(72,223,235,220),1.5f);
    const auto selected=context.cameraTool->SelectedHandle();
    const auto hot=CourseMapCameraEditTool::Pick(frame,geometry,mouse,selected);
    const auto marker=[&](Vector3 world,CourseMapCameraHandle handle,const char* name,ImU32 color) {
        const auto p=ProjectCourseMap3DPoint(world,frame.camera,rect);
        if (!p.valid) return;
        const bool hovered=hot.hit && hot.handle==handle && hot.axis==CourseMapCameraAxis::ViewPlane;
        const auto pos=ToIm(p.screen);
        if (handle==CourseMapCameraHandle::Eye)
            draw->AddRectFilled({pos.x-8,pos.y-6},{pos.x+8,pos.y+6},color,2);
        else {
            draw->AddCircleFilled(pos,7,color,20);
            draw->AddLine({pos.x-11,pos.y},{pos.x+11,pos.y},color,1.5f);
            draw->AddLine({pos.x,pos.y-11},{pos.x,pos.y+11},color,1.5f);
        }
        if (hovered || selected==handle) draw->AddCircle(pos,12,IM_COL32(255,255,255,240),20,1.5f);
        draw->AddText({pos.x+14,pos.y+9},color,name);
    };
    marker(geometry.eye,CourseMapCameraHandle::Eye,"撮影位置",IM_COL32(100,240,255,255));
    marker(geometry.target,CourseMapCameraHandle::Target,"注視点",IM_COL32(255,195,85,255));
    if (composition.Ready() && context.cameraContext.canMutateAuthoring) {
        const auto origin=selected==CourseMapCameraHandle::Eye?geometry.eye:geometry.target;
        const float length=CourseMapCameraEditTool::AxisLength(frame,origin);
        for (auto axis:{CourseMapCameraAxis::X,CourseMapCameraAxis::Y,CourseMapCameraAxis::Z}) {
            const auto direction=CourseMapCameraEditTool::AxisDirection(axis);
            const Vector3 end{origin.x+direction.x*length,origin.y+direction.y*length,origin.z+direction.z*length};
            const ImU32 color=axis==CourseMapCameraAxis::X?IM_COL32(255,100,100,255):
                axis==CourseMapCameraAxis::Y?IM_COL32(110,255,135,255):IM_COL32(105,155,255,255);
            line(origin,end,color,hot.hit && hot.axis==axis?4.0f:2.5f);
            const auto p=ProjectCourseMap3DPoint(end,frame.camera,rect);
            if (p.valid) {
                draw->AddCircleFilled(ToIm(p.screen),4,color,12);
                draw->AddText({p.screen.x+5,p.screen.y-12},color,
                    axis==CourseMapCameraAxis::X?"X":axis==CourseMapCameraAxis::Y?"Y":"Z");
            }
        }
    }
    draw->AddText({rect.x+9,rect.y+27},IM_COL32(235,225,170,255),
        composition.Ready()?"左ドラッグ: 撮影位置・注視点を移動 / Esc: 移動を取り消す":"右側の「構図を調整する」で移動を開始できます");
    if (!context.cameraTool->Message().empty())
        draw->AddText({rect.x+9,rect.y+47},IM_COL32(255,200,120,255),context.cameraTool->Message().c_str());
    draw->PopClipRect();
}

void DrawFrame(
    const CourseOverviewMapFrame& frame,
    const CourseOverviewMapVisibleFrame& visible,
    const CourseOverviewMapPickResult& hovered,
    const CourseTerrainMapFrame* terrainFrame = nullptr,
    const CourseMapCartographyFrame* cartographyFrame = nullptr,
    const CourseMapHologramFrame* hologramFrame = nullptr,
    const CourseMapSceneVisualizationFrame* sceneFrame = nullptr,
    bool hologramGrid = true,
    Vector2 presentationOffset = {}) {
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 minimum{frame.rect.x, frame.rect.y};
    const ImVec2 maximum{frame.rect.x + frame.rect.width, frame.rect.y + frame.rect.height};
    draw->AddRectFilled(minimum, maximum, IM_COL32(11, 18, 23, 255));
    draw->AddRect(minimum, maximum, IM_COL32(58, 93, 108, 255));
    draw->PushClipRect(minimum, maximum, true);
    DrawSceneVisualizationBackground(sceneFrame, hologramGrid, presentationOffset);
    DrawTerrainMap(terrainFrame, presentationOffset);
    DrawHologram(hologramFrame, presentationOffset);
    DrawCartography(cartographyFrame, presentationOffset);
    DrawSceneVisualizationForeground(sceneFrame, presentationOffset);
    for (const CourseOverviewMapDrawBatch& batch : visible.lineBatches) {
        if (batch.points.size() < 2u) continue;
        if (batch.topology == CourseOverviewMapDrawBatchTopology::Segment) {
            draw->AddLine(ToIm(WithOffset(batch.points[0], presentationOffset)),
                ToIm(WithOffset(batch.points[1], presentationOffset)),
                batch.color, batch.thickness);
            continue;
        }
        draw->PathClear();
        for (Vector2 point : batch.points) {
            draw->PathLineTo(ToIm(WithOffset(point, presentationOffset)));
        }
        draw->PathStroke(batch.color, ImDrawFlags_None, batch.thickness);
    }
    for (uint32_t markerIndex : visible.markerIndices) {
        if (markerIndex >= frame.markers.size()) continue;
        const CourseOverviewMapMarker& marker = frame.markers[markerIndex];
        const bool hot = hovered.hit && marker.handle.SameObject(hovered.handle);
        const float radius = marker.radius + (hot ? 2.5f : 0.0f);
        const ImVec2 p = ToIm(WithOffset(marker.position, presentationOffset));
        if (marker.kind == CourseOverviewMapItemKind::Wave) {
            draw->AddQuadFilled({p.x, p.y - radius}, {p.x + radius, p.y},
                {p.x, p.y + radius}, {p.x - radius, p.y}, marker.color);
        } else if (marker.kind == CourseOverviewMapItemKind::EnemyPlacement) {
            draw->AddTriangleFilled({p.x, p.y - radius}, {p.x + radius, p.y + radius},
                {p.x - radius, p.y + radius}, marker.color);
        } else {
            draw->AddCircleFilled(p, radius, marker.color, 16);
        }
        if (marker.selected || hot) {
            draw->AddCircle(p, radius + 2.0f, IM_COL32(255, 255, 255, 230), 20, 1.5f);
        }
    }
    for (uint32_t labelIndex : visible.labelIndices) {
        if (labelIndex >= frame.labels.size()) continue;
        const CourseOverviewMapLabel& label = frame.labels[labelIndex];
        draw->AddText(ToIm(WithOffset(label.position, presentationOffset)),
            label.color, label.text.c_str());
    }
    draw->PopClipRect();
}

void DrawPlayheadOverlay(
    const CourseOverviewMapDynamicPlayheadOverlay& overlay,
    Vector2 presentationOffset = {}) {
    if (!overlay.valid || !overlay.visible) return;
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->PushClipRect(
        {overlay.rect.x, overlay.rect.y},
        {overlay.rect.x + overlay.rect.width, overlay.rect.y + overlay.rect.height},
        true);
    const ImVec2 position = ToIm(WithOffset(overlay.position, presentationOffset));
    draw->AddCircleFilled(position, overlay.radius, overlay.color, 16);
    draw->AddCircle(position, overlay.radius + 2.0f,
        IM_COL32(255, 255, 255, 150), 20, 1.25f);
    draw->PopClipRect();
}

void DrawStrokePreview(
    const CourseRailStrokePreviewFrame& frame,
    const CourseOverviewMapRect& rect) {
    if (!frame.visible) return;
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->PushClipRect({rect.x, rect.y}, {rect.x + rect.width, rect.y + rect.height}, true);
    for (const CourseRailStrokePreviewLine& line : frame.lines) {
        draw->AddLine(ToIm(line.start), ToIm(line.end), line.color, line.thickness);
    }
    for (const CourseRailStrokePreviewMarker& marker : frame.markers) {
        draw->AddCircleFilled(ToIm(marker.position), marker.radius, marker.color, 12);
    }
    draw->PopClipRect();
}

void DrawCrosshair(Vector2 position, const CourseOverviewMapRect& rect,
    Vector2 presentationOffset = {}) {
    position = WithOffset(position, presentationOffset);
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->PushClipRect({rect.x, rect.y}, {rect.x + rect.width, rect.y + rect.height}, true);
    draw->AddLine({rect.x, position.y}, {rect.x + rect.width, position.y},
        IM_COL32(255, 255, 255, 62), 1.0f);
    draw->AddLine({position.x, rect.y}, {position.x, rect.y + rect.height},
        IM_COL32(255, 255, 255, 82), 1.0f);
    draw->AddCircle(ToIm(position), 5.0f, IM_COL32(255, 255, 255, 220), 16, 1.5f);
    draw->PopClipRect();
}

void DrawConstraintIssues(
    const CourseRailConstraintReport& report,
    const CourseOverviewMapProjection& projection,
    Vector2 presentationOffset = {}) {
    if (!report.valid) return;
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const auto& rect = projection.State().rect;
    draw->PushClipRect({rect.x, rect.y}, {rect.x + rect.width, rect.y + rect.height}, true);
    for (const CourseRailConstraintIssue& issue : report.issues) {
        const auto projected =
            projection.ProjectWorldScreenOnly(issue.worldPosition);
        if (!projected.valid) continue;
        const ImU32 color = issue.severity == CourseRailConstraintSeverity::Error
            ? IM_COL32(255, 74, 74, 235)
            : issue.severity == CourseRailConstraintSeverity::Warning
                ? IM_COL32(255, 183, 70, 225) : IM_COL32(116, 213, 160, 210);
        draw->AddCircle(ToIm(WithOffset(
            projected.mapPosition, presentationOffset)), 7.0f, color, 16, 2.0f);
    }
    draw->PopClipRect();
}

void DrawElevationProfile(const CourseRailElevationProfileFrame& frame) {
    if (!frame.valid) return;
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 minimum{frame.rect.x, frame.rect.y};
    const ImVec2 maximum{frame.rect.x + frame.rect.width, frame.rect.y + frame.rect.height};
    draw->AddRectFilled(minimum, maximum, IM_COL32(10, 16, 21, 255));
    draw->AddRect(minimum, maximum, IM_COL32(58, 93, 108, 255));
    draw->PushClipRect(minimum, maximum, true);
    for (const auto& line : frame.gridLines) {
        draw->AddLine(ToIm(line.start), ToIm(line.end), line.color, line.thickness);
    }
    for (const auto& line : frame.profileLines) {
        draw->AddLine(ToIm(line.start), ToIm(line.end), line.color, line.thickness);
    }
    for (const auto& marker : frame.markers) {
        draw->AddCircleFilled(ToIm(marker.position), marker.radius, marker.color, 16);
        if (marker.selected) {
            draw->AddCircle(ToIm(marker.position), marker.radius + 2.0f,
                IM_COL32(82, 255, 138, 245), 18, 2.0f);
        }
    }
    draw->AddText({frame.rect.x + 8.0f, frame.rect.y + 6.0f},
        IM_COL32(190, 215, 225, 255), "コースの高さ（距離 / 高さ）");
    draw->PopClipRect();
}

void DrawElevationPlayheadOverlay(
    const CourseRailElevationDynamicPlayheadOverlay& overlay) {
    if (!overlay.valid || !overlay.visible) return;
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->PushClipRect(
        {overlay.rect.x, overlay.rect.y},
        {overlay.rect.x + overlay.rect.width, overlay.rect.y + overlay.rect.height},
        true);
    draw->AddLine({overlay.position.x, overlay.rect.y},
        {overlay.position.x, overlay.rect.y + overlay.rect.height},
        IM_COL32(255, 255, 255, 180), 1.5f);
    draw->AddCircleFilled(ToIm(overlay.position), 3.5f,
        IM_COL32(255, 255, 255, 225), 12);
    draw->PopClipRect();
}

CourseMapSceneVisualizationInput MakeSceneVisualizationInput(
    const CourseOverviewMapPanelContext& context,
    const CourseOverviewMapProjection& projection,
    CourseMapCoarseGeometryVisibility coarseGeometry) {
    CourseMapSceneVisualizationInput input{};
    input.projection = &projection;
    input.rail = context.rail != nullptr ? context.rail->Model() : nullptr;
    input.enemies = context.enemies != nullptr ? context.enemies->Model() : nullptr;
    input.course = context.rail != nullptr ? context.rail->Course() : nullptr;
    input.scene = context.scene;
    input.selection = context.selection;
    input.courseRevision = context.rail != nullptr
        ? context.rail->State().mutationRevision : 0;
    input.railGeneration = context.rail != nullptr
        ? context.rail->State().bindingGeneration : 0;
    input.enemyGeneration = context.enemies != nullptr
        ? context.enemies->State().bindingGeneration : 0;
    input.coarseGeometry = coarseGeometry;
    return input;
}

CourseMapHybridCartographyFrame ComposeHybridCartography(
    CourseMapHybridCartographyCompositor* compositor,
    const CourseMapCartographyFrame* cartography) {
    if (compositor != nullptr) return compositor->Compose(cartography);
    CourseMapHybridCartographyFrame result{};
    result.valid = true;
    result.drawCartography = cartography != nullptr && cartography->valid;
    result.requestHologramFallback = !result.drawCartography;
    result.coarseGeometry = {!result.drawCartography,
        !result.drawCartography, !result.drawCartography};
    result.message = "Legacy exclusive cartography composition.";
    return result;
}

CourseMapVisualBakeInput MakeVisualBakeInput(
    const CourseOverviewMapPanelContext& context) {
    CourseMapVisualBakeInput input{};
    input.rail = context.rail != nullptr ? context.rail->Model() : nullptr;
    input.enemies = context.enemies != nullptr ? context.enemies->Model() : nullptr;
    input.course = context.rail != nullptr ? context.rail->Course() : nullptr;
    input.scene = context.scene;
    input.courseRevision = context.rail != nullptr
        ? context.rail->State().mutationRevision : 0;
    input.enemyRevision = context.enemies != nullptr
        ? context.enemies->State().mutationRevision : 0;
    input.railGeneration = context.rail != nullptr
        ? context.rail->State().bindingGeneration : 0;
    input.enemyGeneration = context.enemies != nullptr
        ? context.enemies->State().bindingGeneration : 0;
    return input;
}

CourseMapCartographyBakeInput MakeCartographyBakeInput(
    const CourseOverviewMapPanelContext& context,
    const CourseMapVisualAsset* visualAsset) {
    CourseMapCartographyBakeInput input{};
    input.visualAsset = visualAsset;
    input.assets = context.assets;
    input.visualRevision = visualAsset != nullptr
        ? visualAsset->contentRevision : 0u;
    input.assetRegistryRevision = context.assets != nullptr
        ? context.assets->Revision() : 0u;
    return input;
}

CourseTerrainMapBakeInput MakeTerrainMapBakeInput(
    const CourseOverviewMapPanelContext& context) {
    CourseTerrainMapBakeInput input{};
    input.rail = context.rail != nullptr ? context.rail->Model() : nullptr;
    input.terrainSettings = context.terrainSettings;
    if (context.rail != nullptr && context.rail->Course() != nullptr) {
        input.terrainEdits = &context.rail->Course()->terrainEditLayer;
        input.courseName = context.rail->Course()->name;
    }
    input.courseRevision = context.rail != nullptr
        ? context.rail->State().mutationRevision : 0u;
    return input;
}

CourseMapSceneBoundsInput MakeSceneBoundsInput(
    const CourseOverviewMapPanelContext& context) {
    CourseMapSceneBoundsInput input{};
    input.rail = context.rail != nullptr ? context.rail->Model() : nullptr;
    input.enemies = context.enemies != nullptr ? context.enemies->Model() : nullptr;
    input.course = context.rail != nullptr ? context.rail->Course() : nullptr;
    input.scene = context.scene;
    input.courseRevision = context.rail != nullptr
        ? context.rail->State().mutationRevision : 0;
    input.enemyRevision = context.enemies != nullptr
        ? context.enemies->State().mutationRevision : 0;
    input.railGeneration = context.rail != nullptr
        ? context.rail->State().bindingGeneration : 0;
    input.enemyGeneration = context.enemies != nullptr
        ? context.enemies->State().bindingGeneration : 0;
    return input;
}

void DrawMultiView(
    CourseOverviewMapController& controller,
    const CourseOverviewMapPanelContext& context,
    Vector2 mouse,
    const CourseOverviewMapRect& canvasRect,
    float courseDistance,
    bool canvasActive) {
    CourseOverviewMapMultiViewCoordinator& multi = *context.multiView;
    CourseRailElevationProfileEditor& elevation = *context.elevationProfile;
    std::string error;
    if (!multi.Bind({context.rail, context.enemies, context.waves,
            context.selection, context.preview}, &error) ||
        !elevation.Bind(context.rail, context.selection, &controller, &multi, &error)) {
        ImGui::SetCursorScreenPos({canvasRect.x + 12.0f, canvasRect.y + 12.0f});
        ImGui::TextWrapped("%s", error.c_str());
        return;
    }
    constexpr float gap = 6.0f;
    const float mapHeight = (std::max)(80.0f, canvasRect.height * 0.62f);
    const CourseOverviewMapRect mapRect{canvasRect.x, canvasRect.y,
        canvasRect.width, mapHeight};
    const CourseOverviewMapRect elevationRect{canvasRect.x,
        canvasRect.y + mapHeight + gap, canvasRect.width,
        (std::max)(40.0f, canvasRect.height - mapHeight - gap)};

    // Input consumes the retained frame built at the end of the previous UI
    // frame. This keeps hit testing stable and avoids a speculative rebuild
    // before we know whether input changed projection or preview state.
    ImGuiIO& io = ImGui::GetIO();
    const CourseOverviewMapViewId view = multi.ViewAt(mouse);
    const bool inElevation = elevation.Frame().valid &&
        elevation.Frame().rect.Contains(mouse);
    const bool panActive = canvasActive && IsMapPanButtonDown();
    const Vector2 panOrigin = panActive ? MapPanPressPosition(io) : mouse;
    const CourseOverviewMapViewId panView = panActive
        ? multi.ViewAt(panOrigin) : CourseOverviewMapViewId::None;
    const bool panElevation = panActive && elevation.Frame().valid &&
        elevation.Frame().rect.Contains(panOrigin);
    if (!elevation.State().dragging && panView != CourseOverviewMapViewId::None) {
        multi.BeginInteractivePan(panView);
        multi.Pan(panView, {io.MouseDelta.x, io.MouseDelta.y});
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
    } else if (!elevation.State().dragging && panElevation) {
        if (multi.InteractivePanActive()) multi.EndInteractivePan();
        elevation.Pan({io.MouseDelta.x, io.MouseDelta.y});
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
    } else if (multi.InteractivePanActive()) {
        multi.EndInteractivePan();
    }
    if (elevation.State().dragging) {
        elevation.Tick({mouse, false,
            ImGui::IsMouseDown(ImGuiMouseButton_Left),
            ImGui::IsMouseReleased(ImGuiMouseButton_Left),
            ImGui::IsKeyPressed(ImGuiKey_Escape)});
    } else if (view != CourseOverviewMapViewId::None) {
        multi.HoverAt(mouse);
        if (io.MouseWheel != 0.0f) {
            multi.ZoomAt(view, mouse, io.MouseWheel > 0.0f ? 1.12f : 1.0f / 1.12f);
        }
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            multi.SelectAt(mouse, io.KeyCtrl, io.KeyAlt, io.KeyShift);
        }
    } else if (inElevation) {
        if (io.MouseWheel != 0.0f) {
            const float factor = io.MouseWheel > 0.0f ? 1.12f : 1.0f / 1.12f;
            elevation.ZoomAt(mouse, factor, factor);
        }
        elevation.Tick({mouse,
            ImGui::IsMouseClicked(ImGuiMouseButton_Left),
            ImGui::IsMouseDown(ImGuiMouseButton_Left),
            ImGui::IsMouseReleased(ImGuiMouseButton_Left),
            ImGui::IsKeyPressed(ImGuiKey_Escape)});
    }

    const bool mapFocusGesture = view != CourseOverviewMapViewId::None &&
        !io.WantTextInput && (ImGui::IsKeyPressed(ImGuiKey_F) ||
            ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left));
    if (mapFocusGesture) {
        const CourseOverviewMapProjectionMode mode =
            view == CourseOverviewMapViewId::Top
            ? CourseOverviewMapProjectionMode::Top
            : CourseOverviewMapProjectionMode::Side;
        const CourseMapSceneVisualizationFrame* retained =
            context.sceneVisualization != nullptr
            ? context.sceneVisualization->CurrentFrame(mode) : nullptr;
        const CourseOverviewMapFrame& overview =
            view == CourseOverviewMapViewId::Top
            ? multi.TopFrame() : multi.SideFrame();
        multi.FrameMapPoints(view,
            SelectedMapPoints(overview, retained, context.selection));
    }

    const CourseRailConstraintReport emptyConstraints{};
    const CourseRailConstraintReport* constraints = &emptyConstraints;
    if (context.constraintValidation != nullptr && context.rail->Model() != nullptr) {
        constraints = &context.constraintValidation->ValidateCached(
            *context.rail->Model(),
            CourseRailConstraintReportRevisionKey{
                context.rail->State().mutationRevision,
                context.rail->State().bindingGeneration,
                0});
    }
    // Top/Side and Elevation are each rebuilt exactly once, after all input
    // has updated pan, zoom, selection, crosshair or the private preview.
    if (context.sceneBounds != nullptr) {
        const CourseMapSceneBoundsFrame& bounds = context.sceneBounds->Build(
            MakeSceneBoundsInput(context));
        multi.SetSceneFitPoints(bounds.valid ? &bounds.fitPoints : nullptr,
            bounds.valid ? bounds.revision : 0u);
    } else {
        multi.SetSceneFitPoints(nullptr, 0u);
    }
    multi.SetViewport(mapRect);
    elevation.SetViewport(elevationRect);
    if (!multi.InteractivePanActive()) {
        multi.Rebuild(courseDistance, nullptr);
    }
    elevation.Rebuild(courseDistance, constraints, nullptr);

    const CourseMapSceneVisualizationFrame* topScene = nullptr;
    const CourseMapSceneVisualizationFrame* sideScene = nullptr;
    const CourseMapCartographyFrame* topCartography = nullptr;
    const CourseMapCartographyFrame* sideCartography = nullptr;
    const CourseTerrainMapFrame* topTerrain = nullptr;
    const CourseTerrainMapFrame* sideTerrain = nullptr;
    const CourseMapHologramFrame* topHologram = nullptr;
    const CourseMapHologramFrame* sideHologram = nullptr;
    CourseMapHybridCartographyFrame topComposition{};
    CourseMapHybridCartographyFrame sideComposition{};
    if (context.terrainMapBake != nullptr &&
        context.terrainMapRenderer != nullptr &&
        context.terrainSettings != nullptr) {
        context.terrainMapBake->Ensure(MakeTerrainMapBakeInput(context));
        const CourseTerrainMapAsset* terrain =
            context.terrainMapBake->CurrentAsset();
        if (terrain != nullptr) {
            topTerrain = &context.terrainMapRenderer->Build(
                terrain, multi.TopProjection(),
                {panActive && panView == CourseOverviewMapViewId::Top});
            sideTerrain = &context.terrainMapRenderer->Build(
                terrain, multi.SideProjection(),
                {panActive && panView == CourseOverviewMapViewId::Side});
        }
    }
    if (context.visualBake != nullptr) {
        const CourseMapVisualBakeResult& bake = context.visualBake->Ensure(
            MakeVisualBakeInput(context));
        const CourseMapCartographyBakeResult* cartographyBake = nullptr;
        if (context.cartographyBake != nullptr &&
            context.visualBake->CurrentAsset() != nullptr) {
            cartographyBake = &context.cartographyBake->Ensure(
                MakeCartographyBakeInput(
                    context, context.visualBake->CurrentAsset()));
        }
        if (cartographyBake != nullptr &&
            context.cartographyRenderer != nullptr) {
            topCartography = &context.cartographyRenderer->Build(
                context.cartographyBake->CurrentAsset(), cartographyBake->status,
                multi.TopProjection(),
                {panActive && panView == CourseOverviewMapViewId::Top});
            sideCartography = &context.cartographyRenderer->Build(
                context.cartographyBake->CurrentAsset(), cartographyBake->status,
                multi.SideProjection(),
                {panActive && panView == CourseOverviewMapViewId::Side});
        }
        topComposition = ComposeHybridCartography(
            context.hybridCompositor, topCartography);
        sideComposition = ComposeHybridCartography(
            context.hybridCompositor, sideCartography);
        if (context.hologramRenderer != nullptr &&
            (topComposition.requestHologramFallback ||
                sideComposition.requestHologramFallback)) {
            if (topComposition.requestHologramFallback) {
                topHologram = &context.hologramRenderer->Build(
                    context.visualBake->CurrentAsset(), bake.status,
                    multi.TopProjection());
            }
            if (sideComposition.requestHologramFallback) {
                sideHologram = &context.hologramRenderer->Build(
                    context.visualBake->CurrentAsset(), bake.status,
                    multi.SideProjection());
            }
        }
    }
    if (!topComposition.valid) {
        topComposition = ComposeHybridCartography(
            context.hybridCompositor, topCartography);
    }
    if (!sideComposition.valid) {
        sideComposition = ComposeHybridCartography(
            context.hybridCompositor, sideCartography);
    }
    if (context.sceneVisualization != nullptr) {
        topScene = &context.sceneVisualization->Build(
            MakeSceneVisualizationInput(context, multi.TopProjection(),
                topComposition.coarseGeometry));
        sideScene = &context.sceneVisualization->Build(
            MakeSceneVisualizationInput(context, multi.SideProjection(),
                sideComposition.coarseGeometry));
    }
    const bool hologramGrid = context.sceneVisualization == nullptr ||
        context.sceneVisualization->Settings().hologramGrid;

    DrawFrame(multi.TopFrame(), multi.TopVisibleFrame(),
        multi.State().hoveredView == CourseOverviewMapViewId::Top
        ? multi.State().hovered : CourseOverviewMapPickResult{},
        topTerrain, topCartography, topHologram, topScene, hologramGrid,
        multi.PresentationOffset(CourseOverviewMapViewId::Top));
    DrawFrame(multi.SideFrame(), multi.SideVisibleFrame(),
        multi.State().hoveredView == CourseOverviewMapViewId::Side
        ? multi.State().hovered : CourseOverviewMapPickResult{},
        sideTerrain, sideCartography, sideHologram, sideScene, hologramGrid,
        multi.PresentationOffset(CourseOverviewMapViewId::Side));
    DrawConstraintIssues(*constraints, multi.TopProjection(),
        multi.PresentationOffset(CourseOverviewMapViewId::Top));
    DrawConstraintIssues(*constraints, multi.SideProjection(),
        multi.PresentationOffset(CourseOverviewMapViewId::Side));
    DrawPlayheadOverlay(multi.TopPlayheadOverlay(),
        multi.PresentationOffset(CourseOverviewMapViewId::Top));
    DrawPlayheadOverlay(multi.SidePlayheadOverlay(),
        multi.PresentationOffset(CourseOverviewMapViewId::Side));
    if (multi.State().crosshair.valid) {
        DrawCrosshair(multi.State().crosshair.topPosition, multi.TopFrame().rect,
            multi.PresentationOffset(CourseOverviewMapViewId::Top));
        DrawCrosshair(multi.State().crosshair.sidePosition, multi.SideFrame().rect,
            multi.PresentationOffset(CourseOverviewMapViewId::Side));
    }
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddText({multi.TopFrame().rect.x + 8.0f, multi.TopFrame().rect.y + 6.0f},
        IM_COL32(190, 215, 225, 255), "上から（X/Z）");
    draw->AddText({multi.SideFrame().rect.x + 8.0f, multi.SideFrame().rect.y + 6.0f},
        IM_COL32(190, 215, 225, 255), "横から（距離 / 高さ）");
    DrawElevationProfile(elevation.Frame());
    DrawElevationPlayheadOverlay(elevation.PlayheadOverlay());
    const ImU32 statusColor = constraints->errors > 0
        ? IM_COL32(255, 94, 94, 255) : constraints->warnings > 0
            ? IM_COL32(255, 190, 78, 255) : IM_COL32(102, 224, 148, 255);
    char summary[160]{};
    std::snprintf(summary, sizeof(summary), "Constraints: %u errors / %u warnings",
        constraints->errors, constraints->warnings);
    draw->AddText({elevationRect.x + elevationRect.width - 230.0f,
        elevationRect.y + 6.0f}, statusColor, summary);
}

} // namespace

void DrawCourseOverviewMapPanel(
    CourseOverviewMapController& controller,
    const CourseOverviewMapPanelContext& context) {
    if (context.rail == nullptr || context.selection == nullptr ||
        context.rail->Model() == nullptr) {
        ImGui::TextDisabled("コースを開くとマップを編集できます。");
        return;
    }
    std::string bindingError;
    if (!controller.Bind({context.rail, context.enemies, context.waves,
            context.selection, context.preview}, &bindingError)) {
        ImGui::TextWrapped("%s", bindingError.c_str());
        return;
    }
    if (context.viewport3D != nullptr && !context.viewport3D->Bind(
            {context.rail, context.enemies, context.waves, context.selection},
            &bindingError)) {
        ImGui::TextWrapped("%s", bindingError.c_str());
        return;
    }
    if (context.editTool != nullptr) {
        context.editTool->Bind(&controller, context.rail, context.enemies,
            context.waves, context.selection, context.snapping);
    }
    if (context.dragDrop != nullptr) {
        context.dragDrop->Bind(context.enemies, context.selection, context.snapping);
    }
    if (context.sketchTool != nullptr) {
        context.sketchTool->Bind(
            &controller, context.rail, context.selection, context.curveFit);
    }

    if (context.editTool) context.editTool->SetEditableKinds(
        context.editCategory != CourseMapEditCategory::Encounters,
        context.editCategory != CourseMapEditCategory::Course);
    if (context.cameraEditing && context.cameraTool) {
        ImGui::TextUnformatted("3Dマップで区間と構図を編集");
        if (context.cameraRangeTool && context.cameraPanel) {
            auto& range=*context.cameraRangeTool;auto& composition=context.cameraPanel->Composition();
            const auto mode=[&](const char* label,CourseMapCameraEditMode value) {
                if (ImGui::RadioButton(label,range.Mode()==value)) {
                    context.cameraTool->Cancel(composition);range.SetMode(value);
                    if (value!=CourseMapCameraEditMode::Composition) {
                        composition.Stop();
                        if (context.preview && context.preview->IsPlaying()) context.preview->Pause();
                    }
                }
            };
            ImGui::BeginDisabled(composition.Changed() || range.Dragging());
            mode("区間を選択・調整",CourseMapCameraEditMode::Range);
            ImGui::SameLine();mode("2点で区間を作成",CourseMapCameraEditMode::Create);
            ImGui::SameLine();mode("構図を移動",CourseMapCameraEditMode::Composition);
            ImGui::EndDisabled();
        } else {
            bool camera=context.cameraTool->SelectedHandle()==CourseMapCameraHandle::Eye;
            if (ImGui::RadioButton("撮影位置を動かす",camera)) context.cameraTool->SelectHandle(CourseMapCameraHandle::Eye);
            ImGui::SameLine();
            if (ImGui::RadioButton("注視点を動かす",!camera)) context.cameraTool->SelectHandle(CourseMapCameraHandle::Target);
        }
    } else DrawToolbar(controller, context.viewport3D, context.rail, context.selection,
        context.editTool, context.snapping, context.sketchTool,
        context.multiView, context.visualBake, context.cartographyBake,
        context.cartographyRenderer, context.terrainMapBake,
        context.terrainMapRenderer, context.hybridCompositor,
        context.hologramRenderer,
        context.sceneVisualization, context.editCategory);
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImVec2 available = ImGui::GetContentRegionAvail();
    available.x = (std::max)(available.x, 160.0f);
    available.y = (std::max)(available.y, 120.0f);
    ImGui::InvisibleButton("##CourseOverviewMapCanvas", available,
        ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle |
        ImGuiButtonFlags_MouseButtonRight);
    const bool hoveredCanvas = ImGui::IsItemHovered();
    const bool activeCanvas = ImGui::IsItemActive();
    ImGuiIO& io = ImGui::GetIO();
    const Vector2 mouse{io.MousePos.x, io.MousePos.y};

    if (!context.cameraEditing && context.viewport3D != nullptr && context.viewport3D->State().active &&
        !io.WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        context.viewport3D->SetActive(false);
    }
    if (context.viewport3D != nullptr && context.viewport3D->State().active) {
        CourseMap3DViewportController& viewport3D = *context.viewport3D;
        if (context.sketchTool != nullptr && context.sketchTool->State().drawing) {
            context.sketchTool->Cancel(
                "Rail Sketch cancelled when the 3D viewport took input ownership.");
        }
        if (context.editTool != nullptr && context.editTool->State().dragging) {
            context.editTool->Cancel(
                "Overview drag cancelled when the 3D viewport took input ownership.");
        }

        const CourseTerrainMapAsset* terrain = nullptr;
        if (context.terrainMapBake != nullptr && context.terrainSettings != nullptr) {
            context.terrainMapBake->Ensure(MakeTerrainMapBakeInput(context));
            terrain = context.terrainMapBake->CurrentAsset();
        }
        viewport3D.SetViewport({origin.x, origin.y, available.x, available.y});
        const bool cameraDragging=(context.cameraTool && context.cameraTool->Dragging()) ||
            (context.cameraRangeTool && context.cameraRangeTool->Dragging());
        const bool orbiting = !cameraDragging && activeCanvas &&
            ImGui::IsMouseDown(ImGuiMouseButton_Right);
        const bool panning = !cameraDragging && activeCanvas &&
            ImGui::IsMouseDown(ImGuiMouseButton_Middle);
        if (orbiting) {
            viewport3D.Orbit({io.MouseDelta.x, io.MouseDelta.y});
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        } else if (panning) {
            viewport3D.Pan({io.MouseDelta.x, io.MouseDelta.y});
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
        }
        if (!cameraDragging && hoveredCanvas && io.MouseWheel != 0.0f) {
            viewport3D.Dolly(io.MouseWheel);
        }
        const bool focusGesture = !cameraDragging && hoveredCanvas && !io.WantTextInput &&
            (ImGui::IsKeyPressed(ImGuiKey_F) ||
                ((!context.cameraRangeTool || context.cameraRangeTool->Mode()!=CourseMapCameraEditMode::Create) &&
                    ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)));
        if (focusGesture) {
            if (context.cameraEditing && context.cameraPanel && context.cameraRangeTool &&
                context.cameraRangeTool->Mode()!=CourseMapCameraEditMode::Composition && context.cameraPanel->SelectedShot() && context.rail && context.rail->Model()) {
                const auto& shot=*context.cameraPanel->SelectedShot();
                viewport3D.ConfigureCamera(CourseMapCameraRangeTool::FocusRange(context.rail->Model()->RuntimePath(),
                    shot.startDistance,shot.endDistance,viewport3D.Camera(),available.x/available.y));
            } else if (context.cameraEditing && context.cameraPanel)
                viewport3D.ConfigureCamera(CourseMapCameraEditTool::FocusCamera(context.cameraPanel->Composition(),viewport3D.Camera(),available.x/available.y));
            else viewport3D.FrameSelected();
        }

        const CourseMapSceneVisualizationFrame* semanticScene = nullptr;
        controller.SetViewport({origin.x, origin.y, available.x, available.y});
        controller.Rebuild(context.courseDistance, nullptr);
        if (context.sceneVisualization != nullptr && controller.Frame().valid) {
            semanticScene = &context.sceneVisualization->Build(
                MakeSceneVisualizationInput(context, controller.Projection(),
                    {true, true, true}));
        }
        viewport3D.Rebuild(terrain, semanticScene);
        const float playheadDistance = context.preview != nullptr &&
                context.preview->IsActive()
            ? context.preview->Frame().distance : context.courseDistance;
        viewport3D.UpdatePlayhead(playheadDistance);
        CourseMap3DPickResult hot{};
        if (context.cameraEditing && context.cameraRangeTool && context.cameraPanel && context.rail && context.rail->Model()) {
            auto& range=*context.cameraRangeTool;auto& panel=*context.cameraPanel;
            const auto& path=context.rail->Model()->RuntimePath();
            const bool canEdit=context.cameraContext.canMutateAuthoring && !panel.Composition().Changed() &&
                !(context.preview && context.preview->IsPlaying());
            const bool cancelled=io.AppFocusLost || io.WantTextInput ||
                ImGui::IsPopupOpen(nullptr,ImGuiPopupFlags_AnyPopupId|ImGuiPopupFlags_AnyPopupLevel) || ImGui::IsKeyPressed(ImGuiKey_Escape);
            if (cancelled || !canEdit || (range.Dragging() && !range.Matches(viewport3D.Frame(),panel.SelectedShot()))) range.Cancel();
            if (range.Mode()!=CourseMapCameraEditMode::Composition) {
                range.SetHover(hoveredCanvas?CourseMapCameraRangeTool::PickRail(viewport3D.Frame(),path,mouse):CourseMapRailPick{});
                if (canEdit && hoveredCanvas && !focusGesture && !cancelled && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                    if (range.Mode()==CourseMapCameraEditMode::Create) {
                        if (range.ClickCreate(range.Hover(),path.Length()) && panel.AddMapSection(context.cameraContext,range.Start(),range.End()))
                            range.SetMode(CourseMapCameraEditMode::Range);
                    } else {
                        const auto* shot=panel.SelectedShot();
                        const auto handle=shot?CourseMapCameraRangeTool::PickHandle(viewport3D.Frame(),path,*shot,mouse):CourseMapRangeHandle::None;
                        if (shot && handle!=CourseMapRangeHandle::None) range.BeginDrag(viewport3D.Frame(),*shot,handle);
                        else if (range.Hover().hit && context.cameraContext.course) {
                            const CourseCinematicCameraShot* found=nullptr;
                            for (const auto& candidate:context.cameraContext.course->cinematicCameraShots) {
                                if (range.Hover().distance<candidate.startDistance || range.Hover().distance>candidate.endDistance) continue;
                                if (!found || candidate.view.priority>found->view.priority ||
                                    (candidate.view.priority==found->view.priority && candidate.endDistance-candidate.startDistance<found->endDistance-found->startDistance)) found=&candidate;
                            }
                            if (found) panel.SelectMapSection(context.cameraContext,found->id);
                        }
                    }
                }
                if (range.Dragging()) {
                    if (ImGui::IsMouseDown(ImGuiMouseButton_Left) || ImGui::IsMouseReleased(ImGuiMouseButton_Left)) range.Drag(path,mouse);
                    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
                        panel.SetMapSectionRange(context.cameraContext,range.ShotId(),range.Start(),range.End(),range.Handle()==CourseMapRangeHandle::Start);
                        range.EndDrag();
                    } else if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) range.Cancel();
                    ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
                }
            }
        }
        if (context.cameraEditing && context.cameraTool && context.cameraPanel &&
            (!context.cameraRangeTool || context.cameraRangeTool->Mode()==CourseMapCameraEditMode::Composition)) {
            auto& tool=*context.cameraTool;
            auto& composition=context.cameraPanel->Composition();
            const auto& dragRect=tool.DragViewport();
            const auto& rect=viewport3D.Frame().viewport;
            const bool resized=dragRect.x!=rect.x || dragRect.y!=rect.y ||
                dragRect.width!=rect.width || dragRect.height!=rect.height;
            const bool canEdit=composition.Ready() && context.cameraContext.canMutateAuthoring && context.cameraContext.canPreview;
            if (tool.Dragging() && (!canEdit || io.AppFocusLost || io.WantTextInput || resized ||
                    ImGui::IsPopupOpen(nullptr,ImGuiPopupFlags_AnyPopupId|ImGuiPopupFlags_AnyPopupLevel) ||
                    ImGui::IsKeyPressed(ImGuiKey_Escape))) tool.Cancel(composition);
            if (canEdit && hoveredCanvas && !focusGesture && !io.WantTextInput &&
                ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                const auto pick=CourseMapCameraEditTool::Pick(viewport3D.Frame(),
                    CourseMapCameraEditTool::BuildGeometry(composition),mouse,tool.SelectedHandle());
                tool.Begin(composition,viewport3D.Frame(),pick,mouse);
            }
            if (tool.Dragging()) {
                if (ImGui::IsMouseDown(ImGuiMouseButton_Left) || ImGui::IsMouseReleased(ImGuiMouseButton_Left))
                    tool.Drag(composition,mouse);
                if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) tool.End();
                ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
            }
        }
        if (!context.cameraEditing && hoveredCanvas && !orbiting && !panning) {
            hot = viewport3D.HoverAt(mouse);
        }
        if (!context.cameraEditing && hoveredCanvas && !focusGesture &&
            ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            hot = viewport3D.SelectAt(mouse, io.KeyCtrl, io.KeyAlt, io.KeyShift);
            viewport3D.Rebuild(terrain, semanticScene);
        }
        if (viewport3D.Frame().valid) {
            DrawCourseMap3DFrame(viewport3D.Frame(), hot,
                &viewport3D.DynamicOverlay(),context.cameraEditing);
            if (context.cameraEditing) {
                DrawMapCameraRangeOverlay(viewport3D.Frame(),context,mouse);
                DrawMapCameraOverlay(viewport3D.Frame(),context,mouse);
            }
        } else {
            ImDrawList* draw = ImGui::GetWindowDrawList();
            draw->AddRectFilled(origin, {origin.x + available.x, origin.y + available.y},
                IM_COL32(8, 14, 19, 255));
            draw->AddText({origin.x + 12.0f, origin.y + 12.0f},
                IM_COL32(255, 170, 110, 255), viewport3D.State().message.c_str());
        }
        if (hot.hit && hoveredCanvas) {
            ImGui::SetTooltip("%s\n視点からの距離: %.2f m",
                MapItemLabel(hot.kind), hot.rayDistance);
        }
        return;
    }

    if (context.multiView != nullptr && context.elevationProfile != nullptr &&
        context.multiView->State().enabled) {
        if (context.sketchTool != nullptr && context.sketchTool->State().drawing) {
            context.sketchTool->Cancel("Rail Sketch cancelled when Multi View took input ownership.");
        }
        if (context.editTool != nullptr && context.editTool->State().dragging) {
            context.editTool->Cancel("Overview drag cancelled when Multi View took input ownership.");
        }
        DrawMultiView(controller, context, mouse,
            {origin.x, origin.y, available.x, available.y}, context.courseDistance,
            activeCanvas);
        return;
    }

    // Normal Overview follows the same retained-input ordering as Multi View:
    // hit testing consumes the previous frame, then one rebuild publishes all
    // input changes for rendering and the next UI frame.
    const bool inputReady = controller.Frame().valid;
    const bool sketchActive =
        context.sketchTool != nullptr && context.sketchTool->State().active;
    const bool sketchDrawing =
        sketchActive && context.sketchTool->State().drawing;
    const bool editingDrag =
        context.editTool != nullptr && context.editTool->State().dragging;
    if (sketchActive && editingDrag) {
        context.editTool->Cancel("Overview object drag cancelled because Rail Sketch took input ownership.");
    }
    const bool mapPanActive = inputReady && activeCanvas && !editingDrag &&
        !sketchDrawing && IsMapPanButtonDown();
    if (mapPanActive) {
        controller.BeginInteractivePan();
        controller.PanPixels({io.MouseDelta.x, io.MouseDelta.y});
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
    } else if (controller.InteractivePanActive()) {
        // Commit all accumulated deltas as one projection revision. The
        // rebuild below is therefore performed exactly once on release.
        controller.EndInteractivePan();
    }
    if (inputReady && hoveredCanvas && !sketchDrawing && io.MouseWheel != 0.0f) {
        controller.ZoomAt(mouse, io.MouseWheel > 0.0f ? 1.12f : 1.0f / 1.12f);
    }
    const CourseOverviewMapPickResult hot = inputReady && hoveredCanvas
        ? controller.HoverAt(mouse) : CourseOverviewMapPickResult{};
    const CourseMapSceneVisualizationFrame* retainedScene =
        context.sceneVisualization != nullptr
        ? context.sceneVisualization->CurrentFrame(
            controller.Projection().Settings().mode)
        : nullptr;
    const CourseMapScreenSpaceProxy* proxyHot = inputReady && hoveredCanvas
        ? PickScreenSpaceProxy(retainedScene, mouse,
            controller.PresentationOffset())
        : nullptr;
    const bool primaryPressed = inputReady && hoveredCanvas &&
        ImGui::IsMouseClicked(ImGuiMouseButton_Left);
    const bool focusGesture = inputReady && hoveredCanvas &&
        !io.WantTextInput && (ImGui::IsKeyPressed(ImGuiKey_F) ||
            ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left));
    if (!sketchActive && primaryPressed && (context.editTool == nullptr ||
            context.editTool->State().mode == CourseOverviewMapEditMode::SelectMove)) {
        if (hot.hit) {
            controller.SelectAt(mouse, io.KeyCtrl, io.KeyAlt, io.KeyShift);
        } else if (proxyHot != nullptr && context.selection != nullptr) {
            EditorObjectHandle handle{};
            handle.domain = proxyHot->domain;
            handle.stableId = proxyHot->stableId;
            handle.localIndex = proxyHot->localIndex;
            handle.displayName = proxyHot->displayName;
            if (io.KeyAlt) context.selection->Toggle(std::move(handle));
            else if (io.KeyCtrl) context.selection->Add(std::move(handle));
            else context.selection->SetPrimary(std::move(handle));
        } else if (context.selection != nullptr) {
            context.selection->Clear();
        }
    }
    if (!sketchActive && context.editTool != nullptr) {
        context.editTool->Tick({mouse, hot, primaryPressed && !focusGesture,
            ImGui::IsMouseDown(ImGuiMouseButton_Left),
            ImGui::IsMouseReleased(ImGuiMouseButton_Left),
            ImGui::IsKeyPressed(ImGuiKey_Escape),
            inputReady && hoveredCanvas && ImGui::IsKeyPressed(ImGuiKey_Delete)});
    }
    if (context.sketchTool != nullptr) {
        context.sketchTool->Tick({mouse, hot, primaryPressed && !focusGesture,
            ImGui::IsMouseDown(ImGuiMouseButton_Left),
            ImGui::IsMouseReleased(ImGuiMouseButton_Left),
            ImGui::IsKeyPressed(ImGuiKey_Escape)});
    }
    if (focusGesture) {
        const std::vector<Vector2> selectedPoints = SelectedMapPoints(
            controller.Frame(), retainedScene, context.selection);
        controller.FrameMapPoints(selectedPoints);
    }
    if (inputReady && !sketchDrawing && context.dragDrop != nullptr &&
        context.assets != nullptr &&
        ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* raw = ImGui::AcceptDragDropPayload(kEditorAssetDragDropPayloadId)) {
            if (raw->Data != nullptr && raw->DataSize == sizeof(EditorAssetDragDropPayload)) {
                const auto& payload = *static_cast<const EditorAssetDragDropPayload*>(raw->Data);
                const auto result = context.dragDrop->DropActorAsset(
                    {context.assets, payload.guid.data(), mouse}, controller.Projection());
                if (result.succeeded && context.assetSelection != nullptr) {
                    if (const EditorAssetRecord* record = context.assets->FindByGuid(payload.guid.data())) {
                        context.assetSelection->SetPrimary(
                            MakeEditorAssetHandle(*record, context.assets->Revision()));
                    }
                }
            }
        }
        ImGui::EndDragDropTarget();
    }
    if (context.sceneBounds != nullptr) {
        const CourseMapSceneBoundsFrame& bounds = context.sceneBounds->Build(
            MakeSceneBoundsInput(context));
        controller.SetSceneFitPoints(bounds.valid ? &bounds.fitPoints : nullptr,
            bounds.valid ? bounds.revision : 0u);
    } else {
        controller.SetSceneFitPoints(nullptr, 0u);
    }
    controller.SetViewport({origin.x, origin.y, available.x, available.y});
    if (!mapPanActive) {
        controller.Rebuild(context.courseDistance, nullptr);
    }
    if (controller.Frame().valid) {
        const CourseTerrainMapFrame* terrainFrame = nullptr;
        const CourseMapCartographyFrame* cartographyFrame = nullptr;
        const CourseMapHologramFrame* hologramFrame = nullptr;
        CourseMapHybridCartographyFrame composition{};
        if (context.terrainMapBake != nullptr &&
            context.terrainMapRenderer != nullptr &&
            context.terrainSettings != nullptr) {
            context.terrainMapBake->Ensure(MakeTerrainMapBakeInput(context));
            const CourseTerrainMapAsset* terrain =
                context.terrainMapBake->CurrentAsset();
            if (terrain != nullptr) {
                terrainFrame = &context.terrainMapRenderer->Build(
                    terrain, controller.Projection(), {mapPanActive});
            }
        }
        if (context.visualBake != nullptr) {
            const CourseMapVisualBakeResult& bake = context.visualBake->Ensure(
                MakeVisualBakeInput(context));
            const CourseMapCartographyBakeResult* cartographyBake = nullptr;
            if (context.cartographyBake != nullptr &&
                context.visualBake->CurrentAsset() != nullptr) {
                cartographyBake = &context.cartographyBake->Ensure(
                    MakeCartographyBakeInput(
                        context, context.visualBake->CurrentAsset()));
            }
            if (cartographyBake != nullptr &&
                context.cartographyRenderer != nullptr) {
                cartographyFrame = &context.cartographyRenderer->Build(
                    context.cartographyBake->CurrentAsset(),
                    cartographyBake->status, controller.Projection(),
                    {mapPanActive});
            }
            composition = ComposeHybridCartography(
                context.hybridCompositor, cartographyFrame);
            if (composition.requestHologramFallback &&
                context.hologramRenderer != nullptr) {
                hologramFrame = &context.hologramRenderer->Build(
                    context.visualBake->CurrentAsset(), bake.status,
                    controller.Projection());
            }
        }
        if (!composition.valid) {
            composition = ComposeHybridCartography(
                context.hybridCompositor, cartographyFrame);
        }
        const CourseMapSceneVisualizationFrame* sceneFrame = nullptr;
        if (context.sceneVisualization != nullptr) {
            sceneFrame = &context.sceneVisualization->Build(
                MakeSceneVisualizationInput(context, controller.Projection(),
                    composition.coarseGeometry));
        }
        DrawFrame(controller.Frame(), controller.VisibleFrame(), hot,
            terrainFrame, cartographyFrame, hologramFrame, sceneFrame,
            context.sceneVisualization == nullptr ||
                context.sceneVisualization->Settings().hologramGrid,
            controller.PresentationOffset());
        DrawPlayheadOverlay(controller.PlayheadOverlay(),
            controller.PresentationOffset());
    }
    else if (!controller.State().message.empty()) {
        ImGui::SetCursorScreenPos({origin.x + 12.0f, origin.y + 12.0f});
        ImGui::TextWrapped("%s", controller.State().message.c_str());
    }
    if (context.sketchTool != nullptr && context.strokeRenderer != nullptr) {
        DrawStrokePreview(
            context.strokeRenderer->Build(
                *context.sketchTool, controller.Projection()),
            controller.Frame().rect);
    }

    if (hoveredCanvas && (hot.hit || proxyHot != nullptr)) {
        ImGui::BeginTooltip();
        if (hot.hit) {
            ImGui::TextUnformatted(MapItemLabel(hot.kind));
            if (hot.kind != CourseOverviewMapItemKind::RailSegment &&
                hot.kind != CourseOverviewMapItemKind::RailControlPoint)
                ImGui::TextDisabled("%s", hot.handle.displayName.c_str());
            ImGui::TextDisabled("%s", hot.handle.stableId.c_str());
        } else {
            ImGui::TextUnformatted("画面上の編集用マーカー");
            ImGui::TextDisabled("%s", proxyHot->displayName.c_str());
            ImGui::TextDisabled("F / ダブルクリック: 選択対象を表示");
        }
        ImGui::EndTooltip();
    }
    if (context.sketchTool != nullptr && context.sketchTool->State().active &&
        !context.sketchTool->State().message.empty()) {
        ImGui::SetCursorScreenPos({origin.x + 8.0f, origin.y + available.y - 22.0f});
        ImGui::TextColored({0.95f, 0.72f, 0.38f, 1.0f}, "%s",
            context.sketchTool->State().message.c_str());
    } else if (context.editTool != nullptr && !context.editTool->State().message.empty()) {
        ImGui::SetCursorScreenPos({origin.x + 8.0f, origin.y + available.y - 22.0f});
        ImGui::TextColored({0.65f, 0.82f, 0.90f, 1.0f}, "%s",
            context.editTool->State().message.c_str());
    } else if (context.dragDrop != nullptr &&
        !context.dragDrop->LastResult().message.empty()) {
        ImGui::SetCursorScreenPos({origin.x + 8.0f, origin.y + available.y - 22.0f});
        const auto& result = context.dragDrop->LastResult();
        ImGui::TextColored(
            result.succeeded ? ImVec4{0.45f, 0.90f, 0.58f, 1.0f}
                             : ImVec4{1.0f, 0.62f, 0.35f, 1.0f},
            "%s", result.message.c_str());
    }
}

} // namespace editor
