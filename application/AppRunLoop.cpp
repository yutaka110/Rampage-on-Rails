#include "AppRunLoop.h"
#include "AppLogFile.h"
#include "diagnostics/DronePerformanceProfile.h"
#include "course/RailWorldRaycast.h"

#include <DirectXMath.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <memory>
#include <sstream>
#include <string_view>
#include <thread>
#include <ctime>
#include <utility>

#include "AppFrameRenderer.h"
#include "AppImGuiLayer.h"
#include "AppParticleSystem.h"
#include "AppPipelines.h"
#include "AppRenderResources.h"
#include "RuntimeAuthoringPolicy.h"
#include "AppRuntimeState.h"
#include "AppSceneResources.h"
#include "EngineContext.h"
#include "editor/CourseObjectPropertyAdapter.h"
#include "editor/EditorPropertyEditService.h"
#include "editor/EditorPropertyEditSession.h"
#include "editor/EditorPropertyRegistry.h"
#include "editor/EditorTransformGizmoMath.h"
#include "editor/EditorViewportCoordinateService.h"
#include "editor/EditorViewportOverlay.h"
#include "editor/course/CourseEditorExecutionService.h"
#include "editor/course/CoursePropertyUndoCommand.h"
#include "editor/io/EditorFileRecoveryService.h"
#include "editor/io/EditorFileTransaction.h"
#include "utils/dx12/BufferHelper.h"

#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#include "../externals/imgui/imstb_truetype.h"
#undef STB_TRUETYPE_IMPLEMENTATION
#undef STBTT_STATIC

#if defined(GE3_ENABLE_IMGUI) && GE3_ENABLE_IMGUI
#include "../externals/imgui/imgui.h"
#endif

using namespace DirectX;
using namespace Microsoft::WRL;

namespace {
std::string HudUtf8(std::u8string_view text) {
    return {
        reinterpret_cast<const char*>(text.data()),
        reinterpret_cast<const char*>(text.data() + text.size())};
}

constexpr uint32_t kRailWatchdogStartFrame = 400;
constexpr DWORD kRailWatchdogStallMs = 3000;
constexpr double kRailGpuTimingLogThresholdMs = 10.0;
constexpr double kRailFramePacingLogThresholdMs = 8.0;
// One native atlas backs the lock-on HUD, Japanese gameplay text and the
// submission HUD.  Texture creation and every UV producer must share these
// dimensions; duplicating the old 256px height caused geometry to sample the
// Japanese glyph rows instead of the solid-white and marker cells.
constexpr uint32_t kRailHudAtlasWidth = 1024;
constexpr uint32_t kRailHudAtlasHeight = 1280;
// The font/marker region ends at y=512. Leave a transparent sampling gutter.
constexpr uint32_t kTitleLogoAtlasX = 32;
constexpr uint32_t kTitleLogoAtlasY = 544;
constexpr uint32_t kTitleLogoAtlasWidth = 960;
constexpr uint32_t kTitleLogoAtlasHeight = 640;
static_assert(kTitleLogoAtlasX+kTitleLogoAtlasWidth < kRailHudAtlasWidth &&
              kTitleLogoAtlasY+kTitleLogoAtlasHeight < kRailHudAtlasHeight);

struct RenderViewportMetrics {
    uint32_t width = 1;
    uint32_t height = 1;
    bool editorViewport = false;

    float AspectRatio() const {
        return height > 0
            ? static_cast<float>(width) / static_cast<float>(height)
            : 16.0f / 9.0f;
    }
};

RenderViewportMetrics ResolveRenderViewportMetrics(
    const editor::EditorViewportRenderTargetState& editorViewport,
    uint32_t windowWidth,
    uint32_t windowHeight) {
    if (editorViewport.enabled && editorViewport.Valid()) {
        return {
            editorViewport.renderWidth,
            editorViewport.renderHeight,
            true};
    }

    return {
        (std::max)(1u, windowWidth),
        (std::max)(1u, windowHeight),
        false};
}

std::atomic<bool> gRailWatchdogStarted{false};
std::atomic<uint32_t> gRailStageFrame{0};
std::atomic<DWORD> gRailStageTick{0};
std::atomic<const char*> gRailStageName{nullptr};

using RailPerfClock = std::chrono::steady_clock;

struct RailPerfFrameSample {
    uint32_t frame = 0;
    float distance = 0.0f;
    double updateMs = 0.0;
    double collisionMs = 0.0;
    double visualPresetMs = 0.0;
    double vfxUpdateMs = 0.0;
    double terrainUpdateMs = 0.0;
    double particleUpdateMs = 0.0;
    double renderMs = 0.0;
    double waitFrameSlotMs = 0.0;
    double commandListBeginMs = 0.0;
    double gpuParticleInitMs = 0.0;
    double sceneTransformsMs = 0.0;
    double syncCourseMeshMs = 0.0;
    double imguiMs = 0.0;
    double imguiBuildUiMs = 0.0;
    double imguiEndFrameMs = 0.0;
    double sceneRuntimeSyncMs = 0.0;
    double registerPassesMs = 0.0;
    double prepareGraphResourcesMs = 0.0;
    double renderGraphDebugMs = 0.0;
    double cascadeShadowMs = 0.0;
    double renderGraphExecuteMs = 0.0;
    double telemetryMs = 0.0;
    double endFrameMs = 0.0;
    double endAndExecuteMs = 0.0;
    double signalMs = 0.0;
    double presentMs = 0.0;
};

RailPerfFrameSample gRailPerfFrame{};

double ElapsedMs(RailPerfClock::time_point begin, RailPerfClock::time_point end) {
    return std::chrono::duration<double, std::milli>(end - begin).count();
}

void ResetRailPerfFrame(uint32_t frameIndex, float distance) {
    drone_perf::ResetFrame();
    gRailPerfFrame = {};
    gRailPerfFrame.frame = frameIndex;
    gRailPerfFrame.distance = distance;
}

void WriteRailWatchdogLine(const char* message) {
    OutputDebugStringA(message);
    std::ofstream log = app::OpenRotatingLog("logs/rail_watchdog.log");
    if (log) {
        log << message;
    }
}

void StartRailWatchdogOnce() {
    bool expected = false;
    if (!gRailWatchdogStarted.compare_exchange_strong(expected, true)) {
        return;
    }
    std::thread([]() {
        uint32_t lastLoggedFrame = 0;
        const char* lastLoggedStage = nullptr;
        for (;;) {
            Sleep(1000);
            const uint32_t frame = gRailStageFrame.load(std::memory_order_relaxed);
            if (frame < kRailWatchdogStartFrame) {
                continue;
            }
            const DWORD tick = gRailStageTick.load(std::memory_order_relaxed);
            const DWORD idleMs = GetTickCount() - tick;
            const char* stage = gRailStageName.load(std::memory_order_relaxed);
            if (idleMs < kRailWatchdogStallMs) {
                continue;
            }
            if (frame == lastLoggedFrame && stage == lastLoggedStage) {
                continue;
            }
            lastLoggedFrame = frame;
            lastLoggedStage = stage;
            char message[256]{};
            std::snprintf(
                message,
                sizeof(message),
                "[RailWatchdog] stalled frame=%u idleMs=%lu stage=%s\n",
                frame,
                static_cast<unsigned long>(idleMs),
                stage != nullptr ? stage : "unknown");
            WriteRailWatchdogLine(message);
        }
    }).detach();
}

void RecordRailFrameStage(uint32_t frameIndex, const char* stage) {
    if (frameIndex < kRailWatchdogStartFrame) {
        return;
    }
    StartRailWatchdogOnce();
    gRailStageName.store(stage != nullptr ? stage : "unknown", std::memory_order_relaxed);
    gRailStageFrame.store(frameIndex, std::memory_order_relaxed);
    gRailStageTick.store(GetTickCount(), std::memory_order_relaxed);
}

void ApplyRailShooterTerrainBudget(TerrainAuthoringState& terrain) {
    terrain.autoReloadPreset = false;
    terrain.showShadowDebugView = false;
    terrain.cascadeShadowEnabled = false;
    terrain.enableDebrisRendering = false;
    terrain.settings.visibleAheadChunks =
        (std::min)(terrain.settings.visibleAheadChunks, 4u);
    terrain.settings.visibleBehindChunks =
        (std::min)(terrain.settings.visibleBehindChunks, 1u);
    terrain.settings.surfaceLongitudinalSteps =
        (std::min)(terrain.settings.surfaceLongitudinalSteps, 24u);
    terrain.settings.surfaceRadialSegments =
        (std::min)(terrain.settings.surfaceRadialSegments, 36u);
    terrain.settings.lodNearDistance =
        (std::min)(terrain.settings.lodNearDistance, 140.0f);
    terrain.settings.lodFarDistance =
        (std::min)(terrain.settings.lodFarDistance, 320.0f);
    terrain.settings.rockPillarDensity =
        (std::min)(terrain.settings.rockPillarDensity, 0.22f);
    terrain.settings.rockScatterDensity =
        (std::min)(terrain.settings.rockScatterDensity, 0.24f);
    terrain.settings.rockContactPebbleDensity =
        (std::min)(terrain.settings.rockContactPebbleDensity, 0.24f);
    terrain.settings.floorPebbleDensity =
        (std::min)(terrain.settings.floorPebbleDensity, 0.18f);
    terrain.settings.dustZoneDensity =
        (std::min)(terrain.settings.dustZoneDensity, 0.16f);
    terrain.debrisOcclusionUpdateInterval =
        (std::max)(terrain.debrisOcclusionUpdateInterval, 8u);
}

bool ShouldTraceRailFrame(uint32_t frameIndex) {
    static const bool enabled = []() {
        char value[8] = {};
        const DWORD length = GetEnvironmentVariableA(
            "CG5_RAIL_FRAME_TRACE",
            value,
            static_cast<DWORD>(std::size(value)));
        if (length == 0u) {
            return false;
        }
        return value[0] != '0';
    }();
    return enabled && frameIndex >= 470u && frameIndex <= 700u;
}

bool RailShaderHotReloadEnabled() {
    static const bool enabled = []() {
        char value[8] = {};
        const DWORD length = GetEnvironmentVariableA(
            "CG5_RAIL_SHADER_HOT_RELOAD",
            value,
            static_cast<DWORD>(std::size(value)));
        return length > 0u && value[0] != '0';
    }();
    return enabled;
}


void LogRailFrameStage(uint32_t frameIndex, float distance, const char* stage) {
    RecordRailFrameStage(frameIndex, stage);
    if (!ShouldTraceRailFrame(frameIndex)) {
        return;
    }
    std::ostringstream line;
    line << "[RailFrameTrace] frame=" << frameIndex
         << " distance=" << distance
         << " stage=" << (stage != nullptr ? stage : "unknown")
         << "\n";
    OutputDebugStringA(line.str().c_str());
    std::ofstream log = app::OpenRotatingLog("logs/rail_frame_trace.log");
    if (log) {
        log << line.str();
    }
}

std::string CsvEscape(const std::string& value) {
    bool needsQuote = false;
    for (char c : value) {
        if (c == ',' || c == '"' || c == '\n' || c == '\r') {
            needsQuote = true;
            break;
        }
    }
    if (!needsQuote) {
        return value;
    }

    std::string escaped;
    escaped.reserve(value.size() + 2);
    escaped.push_back('"');
    for (char c : value) {
        if (c == '"') {
            escaped.push_back('"');
        }
        escaped.push_back(c);
    }
    escaped.push_back('"');
    return escaped;
}

const char* BoolCsv(bool value) {
    return value ? "1" : "0";
}

std::string BuildRailTuningCsvPath() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t nowTime = std::chrono::system_clock::to_time_t(now);
    std::tm localTime{};
    localtime_s(&localTime, &nowTime);
    std::ostringstream name;
    name << "logs/rail_camera_tuning_"
         << std::put_time(&localTime, "%Y%m%d_%H%M%S")
         << ".csv";
    return name.str();
}

Vector3 NormalizeOr(const Vector3& value, const Vector3& fallback) {
    const float len2 = value.x * value.x + value.y * value.y + value.z * value.z;
    if (len2 <= 0.000001f) {
        return fallback;
    }
    const float invLen = 1.0f / std::sqrt(len2);
    return {value.x * invLen, value.y * invLen, value.z * invLen};
}

Vector3 Add(const Vector3& a, const Vector3& b) {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}

Vector3 Subtract(const Vector3& a, const Vector3& b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

Vector3 Scale(const Vector3& value, float scale) {
    return {value.x * scale, value.y * scale, value.z * scale};
}

float Dot(const Vector3& a, const Vector3& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

Vector3 Cross(const Vector3& a, const Vector3& b) {
    return {
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x,
    };
}

Vector3 RotateAroundAxis(const Vector3& value, const Vector3& axis, float radians) {
    const Vector3 n = NormalizeOr(axis, {0.0f, 0.0f, 1.0f});
    const float c = std::cos(radians);
    const float s = std::sin(radians);
    return Add(
        Add(Scale(value, c), Scale(Cross(n, value), s)),
        Scale(n, Dot(n, value) * (1.0f - c)));
}

void TransitionSceneDepthIfNeeded(
    ID3D12GraphicsCommandList* commandList,
    ID3D12Resource* depthResource,
    D3D12_RESOURCE_STATES& currentState,
    D3D12_RESOURCE_STATES nextState) {
    if (commandList == nullptr || depthResource == nullptr || currentState == nextState) {
        return;
    }

    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = depthResource;
    barrier.Transition.StateBefore = currentState;
    barrier.Transition.StateAfter = nextState;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    commandList->ResourceBarrier(1, &barrier);
    currentState = nextState;
}

Vector3 TransformCoord(const Vector3& point, const Matrix4x4& matrix) {
    const float x =
        point.x * matrix.m[0][0] + point.y * matrix.m[1][0] + point.z * matrix.m[2][0] + matrix.m[3][0];
    const float y =
        point.x * matrix.m[0][1] + point.y * matrix.m[1][1] + point.z * matrix.m[2][1] + matrix.m[3][1];
    const float z =
        point.x * matrix.m[0][2] + point.y * matrix.m[1][2] + point.z * matrix.m[2][2] + matrix.m[3][2];
    const float w =
        point.x * matrix.m[0][3] + point.y * matrix.m[1][3] + point.z * matrix.m[2][3] + matrix.m[3][3];
    if (std::abs(w) <= 0.00001f) {
        return {x, y, z};
    }
    return {x / w, y / w, z / w};
}

Matrix4x4 ToMatrix4x4(FXMMATRIX matrix) {
    XMFLOAT4X4 stored{};
    XMStoreFloat4x4(&stored, matrix);
    Matrix4x4 result{};
    for (uint32_t row = 0; row < 4; ++row) {
        for (uint32_t column = 0; column < 4; ++column) {
            result.m[row][column] = stored.m[row][column];
        }
    }
    return result;
}

Matrix4x4 MakeLookAtMatrix(
    const Vector3& eye,
    const Vector3& target,
    const Vector3& up) {
    const XMVECTOR eyeVector = XMVectorSet(eye.x, eye.y, eye.z, 1.0f);
    const XMVECTOR targetVector = XMVectorSet(target.x, target.y, target.z, 1.0f);
    const XMVECTOR upVector = XMVectorSet(up.x, up.y, up.z, 0.0f);
    return ToMatrix4x4(XMMatrixLookAtLH(eyeVector, targetVector, upVector));
}

void ConfigureViewportAndScissor(
    AppRuntimeState& runtimeState,
    uint32_t windowWidth,
    uint32_t windowHeight) {
    runtimeState.viewport.Width = static_cast<float>(windowWidth);
    runtimeState.viewport.Height = static_cast<float>(windowHeight);
    runtimeState.viewport.TopLeftX = 0.0f;
    runtimeState.viewport.TopLeftY = 0.0f;
    runtimeState.viewport.MinDepth = 0.0f;
    runtimeState.viewport.MaxDepth = 1.0f;
    runtimeState.scissorRect.left = 0;
    runtimeState.scissorRect.top = 0;
    runtimeState.scissorRect.right = static_cast<LONG>(windowWidth);
    runtimeState.scissorRect.bottom = static_cast<LONG>(windowHeight);
}

uint32_t ReadEnvironmentUInt(const char* name, uint32_t fallback) {
    char value[32]{};
    const DWORD length = GetEnvironmentVariableA(name, value, static_cast<DWORD>(sizeof(value)));
    if (length == 0 || length >= sizeof(value)) {
        return fallback;
    }
    char* end = nullptr;
    const unsigned long parsed = std::strtoul(value, &end, 10);
    if (end == value) {
        return fallback;
    }
    return static_cast<uint32_t>(parsed);
}

Vector3 RailLocalPoint(
    const RailPath& railPath,
    float distance,
    float lateral,
    float vertical,
    float forward) {
    const RailPathSample sample = railPath.Evaluate(distance + forward);
    return Add(
        Add(sample.position, Scale(sample.right, lateral)),
        Scale(sample.up, vertical));
}

struct RailOverlayProjectedPoint {
    Vector2 screen{};
    float depth = 0.0f;
    bool behind = false;
    bool inDepth = false;
    bool onscreen = false;
};

RailOverlayProjectedPoint ProjectRailOverlayPoint(
    const Vector3& point,
    const Matrix4x4& matrix,
    uint32_t width,
    uint32_t height) {
    editor::EditorViewportCoordinateService coordinates;
    coordinates.Update(editor::EditorViewportCoordinateContext{
        editor::EditorPanelRect{
            0.0f,
            0.0f,
            static_cast<float>(width),
            static_cast<float>(height)},
        width,
        height,
        matrix});
    const editor::EditorViewportProjectedPoint projected = coordinates.ProjectWorld(point);
    return RailOverlayProjectedPoint{
        Vector2{projected.render.x, projected.render.y},
        projected.depth,
        projected.behind,
        projected.inDepth,
        projected.onscreen};
}

struct CourseObjectBounds {
    int type = -1;
    int index = -1;
    Vector3 center{};
    Vector3 extents{};
    Vector3 axisX{1.0f, 0.0f, 0.0f};
    Vector3 axisY{0.0f, 1.0f, 0.0f};
    Vector3 axisZ{0.0f, 0.0f, 1.0f};
};

bool BuildCourseTerrainPlacementBounds(
    const CourseTerrainPlacement& placement,
    int index,
    const RailPath& railPath,
    CourseObjectBounds& outBounds) {
    if (railPath.Length() <= 0.0f) {
        return false;
    }

    const RailPathSample sample = railPath.Evaluate(placement.distance + placement.forwardOffset);
    outBounds.type = 0;
    outBounds.index = index;
    outBounds.center = Add(
        Add(sample.position, Scale(sample.right, placement.lateralOffset)),
        Scale(sample.up, placement.verticalOffset));
    outBounds.extents = {
        (std::max)(2.0f, std::abs(placement.scale.x) * 2.5f),
        (std::max)(2.0f, std::abs(placement.scale.y) * 2.5f),
        (std::max)(2.0f, std::abs(placement.scale.z) * 2.5f),
    };
    outBounds.axisX = NormalizeOr(sample.right, {1.0f, 0.0f, 0.0f});
    outBounds.axisY = NormalizeOr(sample.up, {0.0f, 1.0f, 0.0f});
    outBounds.axisZ = NormalizeOr(sample.tangent, {0.0f, 0.0f, 1.0f});
    return true;
}

bool BuildCourseRockClusterBounds(
    const CourseRockCluster& cluster,
    int index,
    const RailPath& railPath,
    CourseObjectBounds& outBounds) {
    if (railPath.Length() <= 0.0f) {
        return false;
    }

    const RailPathSample sample = railPath.Evaluate(cluster.distance);
    float lateral = 0.0f;
    float vertical = 2.0f;
    float forward = 0.0f;
    switch (cluster.anchor) {
    case CourseRockClusterAnchor::LeftWall:
        lateral = -cluster.clearLaneRadius - cluster.spread.x;
        vertical = (std::max)(1.0f, cluster.spread.y * 0.4f);
        break;
    case CourseRockClusterAnchor::RightWall:
        lateral = cluster.clearLaneRadius + cluster.spread.x;
        vertical = (std::max)(1.0f, cluster.spread.y * 0.4f);
        break;
    case CourseRockClusterAnchor::Floor:
        lateral = 0.0f;
        vertical = 1.0f;
        break;
    case CourseRockClusterAnchor::CeilingBreak:
        lateral = 0.0f;
        vertical = (std::max)(8.0f, cluster.spread.y * 1.35f);
        break;
    case CourseRockClusterAnchor::VistaWall:
        lateral = cluster.clearLaneRadius + cluster.spread.x;
        vertical = (std::max)(12.0f, cluster.spread.y * 0.8f);
        forward = cluster.spread.z * 1.45f;
        break;
    }

    outBounds.type = 1;
    outBounds.index = index;
    outBounds.center = Add(
        Add(sample.position, Scale(sample.right, lateral)),
        Add(Scale(sample.up, vertical), Scale(sample.tangent, forward)));
    const float scaleExtent = (std::max)(cluster.maxScale, cluster.minScale) * 6.0f;
    outBounds.extents = {
        (std::max)(4.0f, cluster.spread.x + scaleExtent),
        (std::max)(4.0f, cluster.spread.y + scaleExtent),
        (std::max)(4.0f, cluster.spread.z + scaleExtent),
    };
    outBounds.axisX = NormalizeOr(sample.right, {1.0f, 0.0f, 0.0f});
    outBounds.axisY = NormalizeOr(sample.up, {0.0f, 1.0f, 0.0f});
    outBounds.axisZ = NormalizeOr(sample.tangent, {0.0f, 0.0f, 1.0f});
    return true;
}

bool BuildSelectedCourseObjectBounds(
    const CourseAsset& course,
    const RailPath& railPath,
    const TerrainAuthoringState& authoring,
    CourseObjectBounds& outBounds) {
    if (authoring.courseObjectSelectionType == 0 &&
        authoring.selectedCourseTerrainPlacement >= 0 &&
        authoring.selectedCourseTerrainPlacement < static_cast<int>(course.terrainPlacements.size())) {
        return BuildCourseTerrainPlacementBounds(
            course.terrainPlacements[static_cast<size_t>(authoring.selectedCourseTerrainPlacement)],
            authoring.selectedCourseTerrainPlacement,
            railPath,
            outBounds);
    }
    if (authoring.courseObjectSelectionType == 1 &&
        authoring.selectedCourseRockCluster >= 0 &&
        authoring.selectedCourseRockCluster < static_cast<int>(course.rockClusters.size())) {
        return BuildCourseRockClusterBounds(
            course.rockClusters[static_cast<size_t>(authoring.selectedCourseRockCluster)],
            authoring.selectedCourseRockCluster,
            railPath,
            outBounds);
    }
    return false;
}

bool MakeScreenRay(
    POINT clientPoint,
    uint32_t windowWidth,
    uint32_t windowHeight,
    const Matrix4x4& viewProjection,
    Vector3& outOrigin,
    Vector3& outDirection) {
    if (windowWidth == 0 || windowHeight == 0) {
        return false;
    }
    if (clientPoint.x < 0 ||
        clientPoint.y < 0 ||
        clientPoint.x >= static_cast<LONG>(windowWidth) ||
        clientPoint.y >= static_cast<LONG>(windowHeight)) {
        return false;
    }

    editor::EditorViewportCoordinateService coordinates;
    coordinates.Update(editor::EditorViewportCoordinateContext{
        editor::EditorPanelRect{
            0.0f,
            0.0f,
            static_cast<float>(windowWidth),
            static_cast<float>(windowHeight)},
        windowWidth,
        windowHeight,
        viewProjection});
    const editor::EditorViewportWorldRay ray =
        coordinates.RenderToWorldRay(static_cast<float>(clientPoint.x), static_cast<float>(clientPoint.y));
    if (!ray.valid) {
        return false;
    }
    outOrigin = ray.origin;
    outDirection = ray.direction;
    return true;
}

bool RayIntersectsAabb(
    const Vector3& origin,
    const Vector3& direction,
    const Vector3& center,
    const Vector3& extents,
    float& outT) {
    const Vector3 min = {center.x - extents.x, center.y - extents.y, center.z - extents.z};
    const Vector3 max = {center.x + extents.x, center.y + extents.y, center.z + extents.z};
    float tMin = 0.0f;
    float tMax = 1000000.0f;

    const auto testAxis = [&](float originAxis, float directionAxis, float minAxis, float maxAxis) {
        if (std::abs(directionAxis) <= 0.00001f) {
            return originAxis >= minAxis && originAxis <= maxAxis;
        }
        float t1 = (minAxis - originAxis) / directionAxis;
        float t2 = (maxAxis - originAxis) / directionAxis;
        if (t1 > t2) {
            std::swap(t1, t2);
        }
        tMin = (std::max)(tMin, t1);
        tMax = (std::min)(tMax, t2);
        return tMin <= tMax;
    };

    if (!testAxis(origin.x, direction.x, min.x, max.x) ||
        !testAxis(origin.y, direction.y, min.y, max.y) ||
        !testAxis(origin.z, direction.z, min.z, max.z)) {
        return false;
    }

    outT = tMin;
    return tMax >= 0.0f;
}

float LengthSquared(const Vector3& value) {
    return Dot(value, value);
}

float DistanceRayToSegment(
    const Vector3& rayOrigin,
    const Vector3& rayDirection,
    const Vector3& segmentStart,
    const Vector3& segmentEnd,
    float& outRayT) {
    const Vector3 u = rayDirection;
    const Vector3 v = Subtract(segmentEnd, segmentStart);
    const Vector3 w = Subtract(rayOrigin, segmentStart);
    const float a = Dot(u, u);
    const float b = Dot(u, v);
    const float c = Dot(v, v);
    const float d = Dot(u, w);
    const float e = Dot(v, w);
    const float denom = a * c - b * b;

    float rayT = 0.0f;
    float segmentT = 0.0f;
    if (denom > 0.00001f) {
        rayT = (b * e - c * d) / denom;
        segmentT = (a * e - b * d) / denom;
    }
    rayT = (std::max)(0.0f, rayT);
    segmentT = (std::clamp)(segmentT, 0.0f, 1.0f);

    const Vector3 rayPoint = Add(rayOrigin, Scale(u, rayT));
    const Vector3 segmentPoint = Add(segmentStart, Scale(v, segmentT));
    outRayT = rayT;
    return std::sqrt(LengthSquared(Subtract(rayPoint, segmentPoint)));
}

float CourseObjectGizmoLength(const CourseObjectBounds& bounds, float padding) {
    const float maxExtent = (std::max)(bounds.extents.x, (std::max)(bounds.extents.y, bounds.extents.z));
    return (std::max)(6.0f, maxExtent * (std::clamp)(padding, 1.0f, 2.0f) * 1.45f);
}

bool PickCourseObjectGizmoAxis(
    const CourseObjectBounds& bounds,
    const Vector3& rayOrigin,
    const Vector3& rayDirection,
    float padding,
    int gizmoMode,
    int& outAxis) {
    const float length = CourseObjectGizmoLength(bounds, padding);
    const float threshold = (std::clamp)(length * 0.075f, 0.65f, 4.0f);
    const Vector3 axes[3] = {bounds.axisX, bounds.axisY, bounds.axisZ};
    float bestRayT = 1000000.0f;
    int bestAxis = -1;
    if (gizmoMode != 2) {
        constexpr int pairs[3][2] = {{0, 1}, {1, 2}, {2, 0}};
        for (int plane = 0; plane < 3; ++plane) {
            const Vector3& first = axes[pairs[plane][0]];
            const Vector3& second = axes[pairs[plane][1]];
            const Vector3 normal = NormalizeOr(Cross(first, second), {0.0f, 1.0f, 0.0f});
            Vector3 point{};
            float rayT = 0.0f;
            if (!editor::IntersectEditorGizmoRayPlane(
                    rayOrigin, rayDirection, bounds.center, normal, &point, &rayT)) {
                continue;
            }
            const Vector3 offset = Subtract(point, bounds.center);
            const float firstDistance = Dot(offset, first);
            const float secondDistance = Dot(offset, second);
            const float minimum = length * 0.12f;
            const float maximum = length * 0.38f;
            if (firstDistance >= minimum && firstDistance <= maximum &&
                secondDistance >= minimum && secondDistance <= maximum && rayT < bestRayT) {
                bestRayT = rayT;
                bestAxis = 3 + plane;
            }
        }
    }
    for (int axis = 0; axis < 3; ++axis) {
        const Vector3 start = bounds.center;
        const Vector3 end = Add(bounds.center, Scale(axes[axis], length));
        float rayT = 0.0f;
        const float distance = DistanceRayToSegment(rayOrigin, rayDirection, start, end, rayT);
        if (distance <= threshold && rayT < bestRayT) {
            bestRayT = rayT;
            bestAxis = axis;
        }
    }
    if (gizmoMode == 1) {
        float centerRayT = 0.0f;
        const float centerExtent = (std::max)(0.5f, length * 0.07f);
        if (RayIntersectsAabb(
                rayOrigin, rayDirection, bounds.center,
                {centerExtent, centerExtent, centerExtent}, centerRayT) && centerRayT < bestRayT) {
            bestAxis = 6;
        }
    }
    outAxis = bestAxis;
    return bestAxis >= 0;
}

bool PickCourseObject(
    const CourseAsset& course,
    const RailPath& railPath,
    const Vector3& rayOrigin,
    const Vector3& rayDirection,
    float padding,
    CourseObjectBounds& outHit) {
    const float safePadding = (std::clamp)(padding, 1.0f, 2.0f);
    bool hit = false;
    float bestT = 1000000.0f;

    for (size_t index = 0; index < course.terrainPlacements.size(); ++index) {
        CourseObjectBounds bounds{};
        if (!BuildCourseTerrainPlacementBounds(
                course.terrainPlacements[index],
                static_cast<int>(index),
                railPath,
                bounds)) {
            continue;
        }
        bounds.extents = Scale(bounds.extents, safePadding);
        float t = 0.0f;
        if (RayIntersectsAabb(rayOrigin, rayDirection, bounds.center, bounds.extents, t) && t < bestT) {
            bestT = t;
            outHit = bounds;
            hit = true;
        }
    }

    for (size_t index = 0; index < course.rockClusters.size(); ++index) {
        CourseObjectBounds bounds{};
        if (!BuildCourseRockClusterBounds(
                course.rockClusters[index],
                static_cast<int>(index),
                railPath,
                bounds)) {
            continue;
        }
        bounds.extents = Scale(bounds.extents, safePadding);
        float t = 0.0f;
        if (RayIntersectsAabb(rayOrigin, rayDirection, bounds.center, bounds.extents, t) && t < bestT) {
            bestT = t;
            outHit = bounds;
            hit = true;
        }
    }

    return hit;
}

float SnapCourseObjectValue(float value, float step) {
    return editor::SnapEditorGizmoValue(value, step);
}

Vector3 SnapCourseObjectVector(const Vector3& value, float step) {
    return {
        SnapCourseObjectValue(value.x, step),
        SnapCourseObjectValue(value.y, step),
        SnapCourseObjectValue(value.z, step),
    };
}

std::string FormatGizmoFloat(float value) {
    char buffer[64]{};
    std::snprintf(buffer, sizeof(buffer), "%.6f", value);
    return buffer;
}

std::string FormatGizmoVector3(const Vector3& value) {
    char buffer[128]{};
    std::snprintf(buffer, sizeof(buffer), "%.6f, %.6f, %.6f", value.x, value.y, value.z);
    return buffer;
}

constexpr float kCourseGizmoPropertyEpsilon = 0.0001f;
constexpr float kCourseGizmoRadiansToDegrees = 180.0f / 3.14159265358979323846f;

bool CourseGizmoFloatChanged(float before, float after) {
    return std::fabs(before - after) > kCourseGizmoPropertyEpsilon;
}

bool CourseGizmoVectorChanged(const Vector3& before, const Vector3& after) {
    return CourseGizmoFloatChanged(before.x, after.x) ||
        CourseGizmoFloatChanged(before.y, after.y) ||
        CourseGizmoFloatChanged(before.z, after.z);
}

Vector3 CourseGizmoRotationDegrees(const Vector3& radians) {
    return {
        radians.x * kCourseGizmoRadiansToDegrees,
        radians.y * kCourseGizmoRadiansToDegrees,
        radians.z * kCourseGizmoRadiansToDegrees,
    };
}

editor::EditorPropertyValue CourseGizmoFloatValue(float value) {
    editor::EditorPropertyValue propertyValue{};
    propertyValue.floatValue = value;
    return propertyValue;
}

editor::EditorPropertyValue CourseGizmoVec3Value(const Vector3& value) {
    editor::EditorPropertyValue propertyValue{};
    propertyValue.vec3Value = value;
    return propertyValue;
}

editor::EditorObjectHandle MakeCourseGizmoHandle(
    editor::EditorDomainId domain,
    const char* stablePrefix,
    const char* displayPrefix,
    std::size_t index,
    uint32_t generation) {
    editor::EditorObjectHandle handle{};
    handle.domain = domain;
    handle.stableId = editor::BuildStableIndexedId(stablePrefix, static_cast<uint64_t>(index));
    handle.localIndex = static_cast<uint64_t>(index);
    handle.generation = generation;
    handle.displayName = std::string(displayPrefix) + " #" + std::to_string(index);
    return handle;
}

void RegisterCourseObjectViewportGizmoProperties(editor::EditorPropertyRegistry& registry) {
    const auto registerTransform =
        [&](editor::EditorDomainId domain,
            const char* propertyPath,
            const char* displayName) {
            editor::EditorPropertyDescriptor descriptor{};
            descriptor.domain = domain;
            descriptor.name = propertyPath;
            descriptor.displayName = displayName;
            descriptor.kind = editor::EditorPropertyKind::String;
            descriptor.category = "Viewport Gizmo";
            descriptor.valueType = "transform";
            registry.Register(std::move(descriptor));
        };

    registerTransform(
        editor::EditorDomainId::CourseTerrainPlacement,
        "CourseTerrainPlacement.transform.translate",
        "Transform Gizmo Translate");
    registerTransform(
        editor::EditorDomainId::CourseTerrainPlacement,
        "CourseTerrainPlacement.transform.scale",
        "Transform Gizmo Scale");
    registerTransform(
        editor::EditorDomainId::CourseTerrainPlacement,
        "CourseTerrainPlacement.transform.rotation",
        "Transform Gizmo Rotate");
    registerTransform(
        editor::EditorDomainId::CourseRockCluster,
        "CourseRockCluster.transform.translate",
        "Transform Gizmo Translate");
    registerTransform(
        editor::EditorDomainId::CourseRockCluster,
        "CourseRockCluster.transform.scale",
        "Transform Gizmo Scale");
    registerTransform(
        editor::EditorDomainId::CourseRockCluster,
        "CourseRockCluster.transform.rotation",
        "Transform Gizmo Rotate");
}

std::vector<editor::EditorPropertyEditSessionProperty> BuildCourseGizmoSessionProperties(
    const editor::EditorPropertyRegistry& registry,
    const editor::EditorObjectHandle& target,
    int type,
    int gizmoMode) {
    std::vector<editor::EditorPropertyEditSessionProperty> properties;
    const auto addProperty =
        [&](const char* propertyPath) {
            const editor::EditorPropertyDescriptor* descriptor =
                registry.Find(target.domain, propertyPath);
            if (descriptor == nullptr) {
                return;
            }
            properties.push_back(
                editor::EditorPropertyEditSessionProperty{
                    target,
                    *descriptor});
        };

    const bool scaleMode = gizmoMode == 1;
    const bool rotateMode = gizmoMode == 2;
    if (type == 0) {
        if (rotateMode) {
            addProperty("CourseTerrainPlacement.rotation");
        } else if (scaleMode) {
            addProperty("CourseTerrainPlacement.scale");
        } else {
            addProperty("CourseTerrainPlacement.lateralOffset");
            addProperty("CourseTerrainPlacement.verticalOffset");
            addProperty("CourseTerrainPlacement.forwardOffset");
        }
    } else if (type == 1) {
        if (rotateMode) {
            addProperty("CourseRockCluster.rotation");
        } else if (scaleMode) {
            addProperty("CourseRockCluster.minScale");
            addProperty("CourseRockCluster.maxScale");
            addProperty("CourseRockCluster.spread");
        } else {
            addProperty("CourseRockCluster.clearLaneRadius");
            addProperty("CourseRockCluster.distance");
            addProperty("CourseRockCluster.spread");
        }
    }
    return properties;
}

std::string FormatGizmoTerrainTransform(
    float distance,
    float lateral,
    float vertical,
    float forward,
    const Vector3& scale,
    const Vector3& rotation) {
    std::ostringstream stream;
    stream << "distance=" << FormatGizmoFloat(distance)
           << " lateral=" << FormatGizmoFloat(lateral)
           << " vertical=" << FormatGizmoFloat(vertical)
           << " forward=" << FormatGizmoFloat(forward)
           << " scale=(" << FormatGizmoVector3(scale)
           << ") rotationRad=(" << FormatGizmoVector3(rotation)
           << ")";
    return stream.str();
}

std::string FormatGizmoRockTransform(
    float distance,
    float minScale,
    float maxScale,
    const Vector3& spread,
    float clearLaneRadius,
    const Vector3& rotation) {
    std::ostringstream stream;
    stream << "distance=" << FormatGizmoFloat(distance)
           << " minScale=" << FormatGizmoFloat(minScale)
           << " maxScale=" << FormatGizmoFloat(maxScale)
           << " spread=(" << FormatGizmoVector3(spread)
           << ") clearLane=" << FormatGizmoFloat(clearLaneRadius)
           << " rotationRad=(" << FormatGizmoVector3(rotation)
           << ")";
    return stream.str();
}

const char* CourseGizmoPropertySuffix(int gizmoMode) {
    switch (gizmoMode) {
    case 1:
        return "scale";
    case 2:
        return "rotation";
    case 0:
    default:
        return "translate";
    }
}

const char* CourseGizmoLabel(int gizmoMode) {
    switch (gizmoMode) {
    case 1:
        return "Transform Gizmo Scale";
    case 2:
        return "Transform Gizmo Rotate";
    case 0:
    default:
        return "Transform Gizmo Translate";
    }
}

void AddSelectionFrameBox(
    ge3::debug::DebugDrawSystem& debugDraw,
    const Vector3& center,
    const Vector3& extents,
    const Vector3& axisX,
    const Vector3& axisY,
    const Vector3& axisZ,
    float padding,
    const Vector4& color,
    int gizmoMode,
    bool drawGizmo = true,
    bool drawFrame = true) {
    const float safePadding = (std::clamp)(padding, 1.0f, 2.0f);
    const Vector3 e = {
        (std::max)(0.25f, std::abs(extents.x) * safePadding),
        (std::max)(0.25f, std::abs(extents.y) * safePadding),
        (std::max)(0.25f, std::abs(extents.z) * safePadding),
    };
    if (drawFrame) {
        debugDraw.AddBox(
            {center.x - e.x, center.y - e.y, center.z - e.z},
            {center.x + e.x, center.y + e.y, center.z + e.z},
            color);
        debugDraw.AddPoint(center, 1.25f, color);
    }
    if (!drawGizmo) return;
    CourseObjectBounds visualBounds{};
    visualBounds.center = center;
    visualBounds.extents = extents;
    visualBounds.axisX = axisX;
    visualBounds.axisY = axisY;
    visualBounds.axisZ = axisZ;
    const float gizmoLength = CourseObjectGizmoLength(visualBounds, padding);
    debugDraw.AddLine(
        center,
        Add(center, Scale(axisX, gizmoLength)),
        {1.0f, 0.18f, 0.12f, 1.0f});
    debugDraw.AddLine(
        center,
        Add(center, Scale(axisY, gizmoLength)),
        {0.24f, 1.0f, 0.22f, 1.0f});
    debugDraw.AddLine(
        center,
        Add(center, Scale(axisZ, gizmoLength)),
        {0.25f, 0.55f, 1.0f, 1.0f});
    debugDraw.AddPoint(Add(center, Scale(axisX, gizmoLength)), 1.0f, {1.0f, 0.18f, 0.12f, 1.0f});
    debugDraw.AddPoint(Add(center, Scale(axisY, gizmoLength)), 1.0f, {0.24f, 1.0f, 0.22f, 1.0f});
    debugDraw.AddPoint(Add(center, Scale(axisZ, gizmoLength)), 1.0f, {0.25f, 0.55f, 1.0f, 1.0f});
    if (gizmoMode == 2) {
        const float radius = (std::max)(3.0f, gizmoLength * 0.72f);
        debugDraw.AddCircle(center, axisY, axisZ, radius, {1.0f, 0.18f, 0.12f, 1.0f}, 48);
        debugDraw.AddCircle(center, axisX, axisZ, radius, {0.24f, 1.0f, 0.22f, 1.0f}, 48);
        debugDraw.AddCircle(center, axisX, axisY, radius, {0.25f, 0.55f, 1.0f, 1.0f}, 48);
    } else {
        constexpr int pairs[3][2] = {{0, 1}, {1, 2}, {2, 0}};
        const Vector3 axes[3] = {axisX, axisY, axisZ};
        const Vector4 colors[3] = {
            {1.0f, 0.85f, 0.18f, 0.8f},
            {0.18f, 1.0f, 0.85f, 0.8f},
            {0.85f, 0.18f, 1.0f, 0.8f}};
        for (int plane = 0; plane < 3; ++plane) {
            const Vector3 a = Scale(axes[pairs[plane][0]], gizmoLength * 0.28f);
            const Vector3 b = Scale(axes[pairs[plane][1]], gizmoLength * 0.28f);
            const Vector3 cornerA = Add(center, a);
            const Vector3 cornerB = Add(center, b);
            const Vector3 cornerAB = Add(cornerA, b);
            debugDraw.AddLine(cornerA, cornerAB, colors[plane]);
            debugDraw.AddLine(cornerAB, cornerB, colors[plane]);
        }
    }
}

void AppendCourseObjectSelectionDebugDraw(
    ge3::debug::DebugDrawSystem& debugDraw,
    const CourseAsset& course,
    const RailPath& railPath,
    const TerrainAuthoringState& authoring) {
    if (!authoring.showCourseObjectFrame || railPath.Length() <= 0.0f) {
        return;
    }

    constexpr Vector4 kTerrainColor = {0.25f, 0.92f, 1.0f, 1.0f};
    constexpr Vector4 kRockColor = {1.0f, 0.84f, 0.22f, 1.0f};
    Vector3 medianCenter{};
    uint32_t medianCount = 0;
    CourseObjectBounds medianTemplate{};
    Vector4 medianColor = kTerrainColor;
    const auto drawBounds = [&](CourseObjectBounds bounds, const Vector4& color, bool active) {
        const Vector3 axisX = authoring.courseObjectGizmoSpace == 0
            ? Vector3{1.0f, 0.0f, 0.0f} : bounds.axisX;
        const Vector3 axisY = authoring.courseObjectGizmoSpace == 0
            ? Vector3{0.0f, 1.0f, 0.0f} : bounds.axisY;
        const Vector3 axisZ = authoring.courseObjectGizmoSpace == 0
            ? Vector3{0.0f, 0.0f, 1.0f} : bounds.axisZ;
        AddSelectionFrameBox(
            debugDraw, bounds.center, bounds.extents, axisX, axisY, axisZ,
            authoring.courseObjectFramePadding, color, authoring.courseObjectGizmoMode,
            authoring.courseObjectPivotMode == 2 ||
                (authoring.courseObjectPivotMode == 0 && active));
        if (authoring.courseObjectPivotMode == 1 &&
            bounds.type == authoring.courseObjectSelectionType) {
            medianCenter = Add(medianCenter, bounds.center);
            ++medianCount;
            if (active) {
                medianTemplate = bounds;
                medianColor = color;
            }
        }
    };

    std::vector<int> terrainSelection = authoring.selectedCourseTerrainPlacements;
    if (terrainSelection.empty() && authoring.courseObjectSelectionType == 0 &&
        authoring.selectedCourseTerrainPlacement >= 0) {
        terrainSelection.push_back(authoring.selectedCourseTerrainPlacement);
    }
    for (const int index : terrainSelection) {
        if (index < 0 || index >= static_cast<int>(course.terrainPlacements.size())) continue;
        CourseObjectBounds bounds{};
        if (!BuildCourseTerrainPlacementBounds(
                course.terrainPlacements[static_cast<size_t>(index)], index, railPath, bounds)) continue;
        drawBounds(bounds, kTerrainColor,
            authoring.courseObjectSelectionType == 0 &&
            index == authoring.selectedCourseTerrainPlacement);
    }

    std::vector<int> rockSelection = authoring.selectedCourseRockClusters;
    if (rockSelection.empty() && authoring.courseObjectSelectionType == 1 &&
        authoring.selectedCourseRockCluster >= 0) {
        rockSelection.push_back(authoring.selectedCourseRockCluster);
    }
    for (const int index : rockSelection) {
        if (index < 0 || index >= static_cast<int>(course.rockClusters.size())) continue;
        CourseObjectBounds bounds{};
        if (!BuildCourseRockClusterBounds(
                course.rockClusters[static_cast<size_t>(index)], index, railPath, bounds)) continue;
        drawBounds(bounds, kRockColor,
            authoring.courseObjectSelectionType == 1 &&
            index == authoring.selectedCourseRockCluster);
    }
    if (authoring.courseObjectPivotMode == 1 && medianCount > 0) {
        medianTemplate.center = Scale(medianCenter, 1.0f / medianCount);
        const Vector3 axisX = authoring.courseObjectGizmoSpace == 0
            ? Vector3{1.0f, 0.0f, 0.0f} : medianTemplate.axisX;
        const Vector3 axisY = authoring.courseObjectGizmoSpace == 0
            ? Vector3{0.0f, 1.0f, 0.0f} : medianTemplate.axisY;
        const Vector3 axisZ = authoring.courseObjectGizmoSpace == 0
            ? Vector3{0.0f, 0.0f, 1.0f} : medianTemplate.axisZ;
        AddSelectionFrameBox(
            debugDraw, medianTemplate.center, medianTemplate.extents,
            axisX, axisY, axisZ, authoring.courseObjectFramePadding,
            medianColor, authoring.courseObjectGizmoMode, true, false);
    }
}

void WriteGpuDiagnosticLine(const char* message) {
    OutputDebugStringA(message);
    std::ofstream log = app::OpenRotatingLog("logs/gpu_fence_wait.log");
    if (log) {
        log << message;
    }
}

void WriteRailGpuTimingLine(const std::string& message) {
    OutputDebugStringA(message.c_str());
    std::filesystem::create_directories("logs");
    std::ofstream log = app::OpenRotatingLog("logs/rail_gpu_timing.log");
    if (log) {
        log << message;
    }
}

void LogGpuFailure(const char* context, HRESULT hr, HRESULT deviceRemovedReason) {
    char message[384]{};
    std::snprintf(
        message,
        sizeof(message),
        "[AppRunLoop] %s failed: hr=0x%08X deviceRemoved=0x%08X\n",
        context,
        static_cast<unsigned int>(hr),
        static_cast<unsigned int>(deviceRemovedReason));
    WriteGpuDiagnosticLine(message);
}

void DumpDredBreadcrumbs(ID3D12Device* device) {
    if (device == nullptr) {
        return;
    }

    ComPtr<ID3D12DeviceRemovedExtendedData1> dred;
    if (FAILED(device->QueryInterface(IID_PPV_ARGS(&dred)))) {
        WriteGpuDiagnosticLine("[DRED] ID3D12DeviceRemovedExtendedData1 unavailable.\n");
        return;
    }

    D3D12_DRED_AUTO_BREADCRUMBS_OUTPUT1 breadcrumbs{};
    if (SUCCEEDED(dred->GetAutoBreadcrumbsOutput1(&breadcrumbs))) {
        WriteGpuDiagnosticLine("[DRED] AutoBreadcrumbs:\n");
        uint32_t nodeIndex = 0;
        for (const D3D12_AUTO_BREADCRUMB_NODE1* node = breadcrumbs.pHeadAutoBreadcrumbNode;
             node != nullptr && nodeIndex < 16;
             node = node->pNext, ++nodeIndex) {
            const UINT lastValue = node->pLastBreadcrumbValue != nullptr ? *node->pLastBreadcrumbValue : 0;
            char message[512]{};
            std::snprintf(
                message,
                sizeof(message),
                "[DRED] node=%u list=%s queue=%s breadcrumbs=%u last=%u contexts=%u\n",
                nodeIndex,
                node->pCommandListDebugNameA != nullptr ? node->pCommandListDebugNameA : "(unnamed)",
                node->pCommandQueueDebugNameA != nullptr ? node->pCommandQueueDebugNameA : "(unnamed)",
                node->BreadcrumbCount,
                lastValue,
                node->BreadcrumbContextsCount);
            WriteGpuDiagnosticLine(message);

            const UINT begin = lastValue > 8 ? lastValue - 8 : 0;
            const UINT end = (std::min)(node->BreadcrumbCount, lastValue + 8);
            for (UINT i = begin; i < end; ++i) {
                std::snprintf(
                    message,
                    sizeof(message),
                    "[DRED]   op[%u]=%u%s\n",
                    i,
                    node->pCommandHistory != nullptr ? static_cast<unsigned int>(node->pCommandHistory[i]) : 0u,
                    i == lastValue ? " <last>" : "");
                WriteGpuDiagnosticLine(message);
            }

            for (UINT i = 0; i < node->BreadcrumbContextsCount && i < 16; ++i) {
                const D3D12_DRED_BREADCRUMB_CONTEXT& context = node->pBreadcrumbContexts[i];
                char contextMessage[384]{};
                std::snprintf(
                    contextMessage,
                    sizeof(contextMessage),
                    "[DRED]   context[%u] breadcrumb=%u\n",
                    i,
                    context.BreadcrumbIndex);
                WriteGpuDiagnosticLine(contextMessage);
            }
        }
    }

    D3D12_DRED_PAGE_FAULT_OUTPUT1 pageFault{};
    if (SUCCEEDED(dred->GetPageFaultAllocationOutput1(&pageFault))) {
        char message[256]{};
        std::snprintf(
            message,
            sizeof(message),
            "[DRED] PageFaultVA=0x%llX\n",
            static_cast<unsigned long long>(pageFault.PageFaultVA));
        WriteGpuDiagnosticLine(message);

        auto logAllocations = [](const char* label, const D3D12_DRED_ALLOCATION_NODE1* node) {
            uint32_t index = 0;
            while (node != nullptr && index < 24) {
                char allocationMessage[512]{};
                std::snprintf(
                    allocationMessage,
                    sizeof(allocationMessage),
                    "[DRED]   %s[%u] type=%u name=%s\n",
                    label,
                    index,
                    static_cast<unsigned int>(node->AllocationType),
                    node->ObjectNameA != nullptr ? node->ObjectNameA : "(unnamed)");
                WriteGpuDiagnosticLine(allocationMessage);
                node = node->pNext;
                ++index;
            }
        };
        logAllocations("existing", pageFault.pHeadExistingAllocationNode);
        logAllocations("recentFreed", pageFault.pHeadRecentFreedAllocationNode);
    }
}

VfxFrameTelemetryOptions BuildVfxTelemetryOptions(
    const AppVfxRuntimeState& vfx,
    uint32_t frameIndex,
    bool developerDiagnosticsVisible) {
    constexpr uint32_t kVfxHealthTelemetryInterval = 12;
    const bool healthSampleFrame =
        developerDiagnosticsVisible &&
        (frameIndex % kVfxHealthTelemetryInterval) == 0;

    VfxFrameTelemetryOptions options{};
    options.trailMeshStream =
        vfx.enableTrailMeshStreamStartupTelemetry ||
        (healthSampleFrame && vfx.enableTrailMeshStreamAutoFallback);
    options.particlePool =
        vfx.enableParticleDedicatedProbeTelemetry ||
        (healthSampleFrame && vfx.enableParticleDedicatedResourceProbe);
    options.particleDedicatedReadback =
        vfx.enableParticleDedicatedProbeTelemetry ||
        (healthSampleFrame && vfx.enableParticleDedicatedResourceProbe);
    options.distortionDedicatedReadback =
        vfx.enableDistortionDedicatedTelemetry ||
        (healthSampleFrame &&
            vfx.enableDistortionDedicatedResources &&
            vfx.enableDistortionDedicatedAutoFallback);
    options.beamDedicatedReadback =
        vfx.enableBeamDedicatedTelemetry ||
        (healthSampleFrame && vfx.enableBeamDedicatedAutoFallback);
    return options;
}

std::string CsvQuote(const std::string& value) {
    std::string escaped;
    escaped.reserve(value.size() + 2);
    escaped.push_back('"');
    for (const char ch : value) {
        if (ch == '"') {
            escaped.push_back('"');
        }
        escaped.push_back(ch);
    }
    escaped.push_back('"');
    return escaped;
}

std::array<float, AppSceneResources::kCascadeShadowCount> GetCascadeShadowSplits(
    const TerrainAuthoringState& terrain) {
    std::array<float, AppSceneResources::kCascadeShadowCount> splits = {
        terrain.cascadeShadowSplit0,
        terrain.cascadeShadowSplit1,
        terrain.cascadeShadowSplit2,
        terrain.cascadeShadowSplit3,
    };
    splits[0] = (std::clamp)(splits[0], 20.0f, 240.0f);
    for (uint32_t index = 1; index < splits.size(); ++index) {
        splits[index] = (std::max)(splits[index], splits[index - 1] + 10.0f);
    }
    return splits;
}

bool IntersectScreenPointWithZPlane(
    POINT clientPoint,
    uint32_t windowWidth,
    uint32_t windowHeight,
    const Matrix4x4& viewProjection,
    float planeZ,
    Vector3& outPosition) {
    if (windowWidth == 0 || windowHeight == 0) {
        return false;
    }
    editor::EditorViewportCoordinateService coordinates;
    coordinates.Update(editor::EditorViewportCoordinateContext{
        editor::EditorPanelRect{
            0.0f,
            0.0f,
            static_cast<float>(windowWidth),
            static_cast<float>(windowHeight)},
        windowWidth,
        windowHeight,
        viewProjection});
    const editor::EditorViewportWorldRay ray =
        coordinates.RenderToWorldRay(static_cast<float>(clientPoint.x), static_cast<float>(clientPoint.y));
    if (!ray.valid) {
        return false;
    }
    const Vector3& nearPoint = ray.origin;
    const Vector3& direction = ray.direction;
    if (std::abs(direction.z) <= 0.00001f) {
        return false;
    }

    const float t = (planeZ - nearPoint.z) / direction.z;
    if (t < 0.0f) {
        return false;
    }

    outPosition = {
        nearPoint.x + direction.x * t,
        nearPoint.y + direction.y * t,
        planeZ,
    };
    return true;
}

size_t ShowcaseIndex(AppVfxRuntimeState::ShowcaseEffect effect) {
    return static_cast<size_t>(effect);
}

const char* ShowcaseEffectName(AppVfxRuntimeState::ShowcaseEffect effect) {
    switch (effect) {
    case AppVfxRuntimeState::ShowcaseEffect::ElectricOrbStrike:
        return "Electric Orb Strike";
    case AppVfxRuntimeState::ShowcaseEffect::IceProjectile:
        return "Ice Projectile";
    case AppVfxRuntimeState::ShowcaseEffect::BlackHole:
        return "Black Hole";
    default:
        return "Showcase";
    }
}

} // namespace

AppRunLoop::AppRunLoop(
    DebugCamera& debugCamera,
    AppRuntimeState& runtimeState,
    AppSceneResources& scene,
    AppParticleSystem& particleSystem,
    AppImGuiLayer& imguiLayer,
    AppFrameRenderer& frameRenderer,
    AppPipelines& appPipelines,
    AppRenderResources& renderResources,
    AppAudio& audio,
    graphics::SwapChain& swapChain,
    core::CommandListPool& clPool,
    EngineContext& engineContext,
    ge3::core::DescriptorHeapSet& heaps,
    core::Device& dev,
    HWND hwnd,
    ComPtr<ID3D12DescriptorHeap> srvDescriptorHeap,
    Matrix4x4* wvpData,
    uint32_t windowWidth,
    uint32_t windowHeight,
    FrameLoopState& frameState,
    ID3D12CommandQueue* commandQueue,
    ID3D12Fence* fence,
    HANDLE fenceEvent,
    AppStartupScene startupScene)
    : debugCamera_(debugCamera),
      runtimeState_(runtimeState),
      scene_(scene),
      particleSystem_(particleSystem),
      imguiLayer_(imguiLayer),
      frameRenderer_(frameRenderer),
      appPipelines_(appPipelines),
      renderResources_(renderResources),
      audio_(audio),
      swapChain_(swapChain),
      clPool_(clPool),
      engineContext_(engineContext),
      heaps_(heaps),
      dev_(dev),
      hwnd_(hwnd),
      srvDescriptorHeap_(srvDescriptorHeap),
      wvpData_(wvpData),
      windowWidth_(windowWidth),
      windowHeight_(windowHeight),
      frameState_(frameState),
      commandQueue_(commandQueue),
      frameCoordinator_(swapChain, engineContext, dev, commandQueue, fence, fenceEvent) {
    railTelegraphAcquiredSound_ = audio_.CreateTone(
        "rail-telegraph-acquired", 720.0f, 0.055f, 0.42f);
    railTelegraphImminentSound_ = audio_.CreateTone(
        "rail-telegraph-imminent", 1040.0f, 0.080f, 0.50f);
    railTelegraphFiredSound_ = audio_.CreateTone(
        "rail-telegraph-fired", 440.0f, 0.100f, 0.58f);
    railSessionStartSound_ = audio_.CreateTone(
        "rail-session-start", 760.0f, 0.120f, 0.46f);
    railSessionCheckpointSound_ = audio_.CreateTone(
        "rail-session-checkpoint", 980.0f, 0.110f, 0.48f);
    railSessionDamageSound_ = audio_.CreateTone(
        "rail-session-damage", 210.0f, 0.100f, 0.52f);
    railSessionVictorySound_ = audio_.CreateTone(
        "rail-session-victory", 1180.0f, 0.280f, 0.56f);
    railSessionDefeatSound_ = audio_.CreateTone(
        "rail-session-defeat", 150.0f, 0.320f, 0.58f);
    railSessionRetrySound_ = audio_.CreateTone(
        "rail-session-retry", 680.0f, 0.160f, 0.50f);
    railTitleAmbience_ = audio_.LoadWave("Resources/audio/title_rail_ambience.wav");
    railVehicleRollingSound_ = audio_.CreateTone(
        "rail-vehicle-rolling", 105.0f, 0.055f, 0.16f);
    railVehicleJointSound_ = audio_.CreateTone(
        "rail-vehicle-joint", 72.0f, 0.075f, 0.24f);
    railVehicleBrakeSound_ = audio_.CreateTone(
        "rail-vehicle-brake", 185.0f, 0.120f, 0.18f);
    railVehicleStopSound_ = audio_.CreateTone(
        "rail-vehicle-stop", 54.0f, 0.160f, 0.26f);
    railVehicleCollisionSound_ = audio_.LoadWave("Resources/audio/vehicle_metal_impact.wav");
    if (!railVehicleCollisionSound_.IsValid()) railVehicleCollisionSound_ = audio_.CreateTone(
        "rail-vehicle-body-collision", 128.0f, 0.145f, 0.48f);
    railVehicleEvasionStartSound_ = audio_.CreateTone(
        "rail-vehicle-evasion-start", 760.0f, 0.095f, 0.34f);
    railVehicleEvasionRecoverSound_ = audio_.CreateTone(
        "rail-vehicle-evasion-recover", 430.0f, 0.085f, 0.25f);
    railVehicleEvasionReadySound_ = audio_.CreateTone(
        "rail-vehicle-evasion-ready", 980.0f, 0.055f, 0.18f);
    railEnemySpawnSound_ = audio_.CreateTone(
        "rail-enemy-spawn", 560.0f, 0.095f, 0.34f);
    railEnemyEngageSound_ = audio_.CreateTone(
        "rail-enemy-engage", 690.0f, 0.075f, 0.30f);
    railEnemyAttackSound_ = audio_.CreateTone(
        "rail-enemy-attack", 330.0f, 0.085f, 0.38f);
    railEnemyHitReactSound_ = audio_.CreateTone(
        "rail-enemy-hit-react", 240.0f, 0.070f, 0.40f);
    railEnemyDeathSound_ = audio_.CreateTone(
        "rail-enemy-death", 118.0f, 0.220f, 0.58f);
    railEnemyProjectileLaunchSound_ = audio_.CreateTone(
        "rail-enemy-projectile-launch", 390.0f, 0.060f, 0.28f);
    railEnemyProjectileFlyBySound_ = audio_.CreateTone(
        "rail-enemy-projectile-flyby", 880.0f, 0.075f, 0.30f);
    railEnemyProjectileImpactSound_ = audio_.CreateTone(
        "rail-enemy-projectile-impact", 165.0f, 0.095f, 0.38f);
    railGrazeSound_ = audio_.CreateTone(
        "rail-graze-reward", 1260.0f, 0.070f, 0.34f);
    railGrazeChainSound_ = audio_.CreateTone(
        "rail-graze-chain", 1580.0f, 0.105f, 0.42f);
    railThreatCriticalSound_ = audio_.CreateTone(
        "rail-threat-critical", 132.0f, 0.150f, 0.34f);
    railThreatClearSound_ = audio_.CreateTone(
        "rail-threat-clear", 920.0f, 0.080f, 0.24f);
    railDefenseOutcomeSuccessSound_ = audio_.CreateTone(
        "rail-defense-success", 1120.0f, 0.090f, 0.38f);
    railDefenseInterruptSound_ = audio_.CreateTone(
        "rail-defense-interrupt-break", 620.0f, 0.140f, 0.52f);
    railDefenseShootDownSound_ = audio_.CreateTone(
        "rail-defense-shoot-down-burst", 1540.0f, 0.075f, 0.44f);
    railDefenseEvadeSound_ = audio_.CreateTone(
        "rail-defense-evasion-flow", 980.0f, 0.110f, 0.38f);
    railDefenseOutcomePerfectSound_ = audio_.CreateTone(
        "rail-defense-perfect", 1480.0f, 0.125f, 0.46f);
    railDefenseOutcomeFailedSound_ = audio_.CreateTone(
        "rail-defense-failed", 190.0f, 0.100f, 0.30f);
    std::string grazeScoreError;
    if (!railGrazeScoreSystem_.Initialize({}, &grazeScoreError)) {
        OutputDebugStringA(
            ("[GrazeScore] initialize failed: " + grazeScoreError + "\n").c_str());
    }
    editor::EditorFramePacingSettings framePacingSettings{};
    framePacingSettings.enabled =
        ReadEnvironmentUInt("GE3_EDITOR_FRAME_PACING", 1) != 0;
    framePacingSettings.playFps =
        ReadEnvironmentUInt("GE3_EDITOR_PLAY_FPS", 60);
    framePacingSettings.interactionFps =
        ReadEnvironmentUInt("GE3_EDITOR_INTERACTION_FPS", 60);
    framePacingSettings.realtimeViewportFps =
        ReadEnvironmentUInt("GE3_EDITOR_REALTIME_FPS", 60);
    framePacingSettings.idleEditorFps =
        ReadEnvironmentUInt("GE3_EDITOR_IDLE_FPS", 30);
    framePacingSettings.backgroundFps =
        ReadEnvironmentUInt("GE3_EDITOR_BACKGROUND_FPS", 15);
    framePacingSettings.minimizedFps =
        ReadEnvironmentUInt("GE3_EDITOR_MINIMIZED_FPS", 5);
    imguiLayer_.FramePacingService().SetSettings(framePacingSettings);
    editorFramePacingProfilingMode_ =
        ReadEnvironmentUInt("GE3_EDITOR_UNCAPPED_FRAME_RATE", 0) != 0;

    const editor::EditorFileRecoveryReport recovery =
        editor::EditorFileRecoveryService(std::filesystem::current_path()).Recover();
    if (!recovery.succeeded || recovery.recoveredPreparedCount > 0) {
        std::ostringstream message;
        message << "[EditorFileRecovery] recovered=" << recovery.recoveredPreparedCount
                << " finalized=" << recovery.finalizedCommittedCount
                << " errors=" << recovery.errors.size() << '\n';
        for (const std::string& error : recovery.errors) {
            message << "  " << error << '\n';
        }
        OutputDebugStringA(message.str().c_str());
    }
    if (startupScene == AppStartupScene::MultiMaterialShowcase) {
        sceneStateManager_.Initialize(
            std::make_unique<MultiMaterialShowcaseSceneState>(), *this);
        OutputDebugStringA(
            "[AppRunLoop] Startup scene: MultiMaterialShowcase "
            "(--multi-material-showcase).\n");
    } else if (startupScene == AppStartupScene::VfxPreview) {
        sceneStateManager_.Initialize(std::make_unique<VfxPreviewSceneState>(), *this);
        OutputDebugStringA("[AppRunLoop] Startup scene: VfxPreview (--vfx-preview).\n");
    } else {
        railDefenseUiProofVariant_ =
            ResolveCombatLoopDefenseUiProofVariantFromCommandLine();
        SetWindowTextW(hwnd_, L"\u30ec\u30fc\u30eb\u3067\u3042\u3070\u30ec\u30fc\u30eb");
        railDefenseUiProofEnabled_ = railDefenseUiProofVariant_ !=
            CombatLoopDefenseUiProofVariant::Disabled;
        const bool combatLoopExpansionEnabled =
            ResolveCombatLoopExpansionModeFromCommandLine();
        if (combatLoopExpansionEnabled ||
            ResolveCombatLoop10SecondModeFromCommandLine() ||
            railDefenseUiProofEnabled_) {
            railShooterCoursePath_ = combatLoopExpansionEnabled
                ? "Resources/courses/CombatLoopExpansion.course"
                : "Resources/courses/CombatLoop10s.course";
            // Development and Debug keep the tools switch visible. Presentation
            // builds run the combat loop with an unobstructed viewport.
#if !defined(_DEBUG) && !defined(DEVELOP)
            imguiLayer_.SetVisible(false);
#endif
            // The focused encounter promises a full decision window. Keep this
            // local to the lab course until the telegraph presentation is tuned
            // and promoted to the production route.
            railEnemyAttackTelegraphSettings_.leadSeconds = 1.25f;
            railEnemyAttackTelegraphSettings_.imminentSeconds = 0.38f;
            railEnemyAttackLaneTelegraphRendererSettings_.baseLaneWidth = 0.22f;
            railEnemyAttackLaneTelegraphRendererSettings_.sourceMarkerRadius = 0.68f;
            railEnemyAttackLaneTelegraphRendererSettings_.targetMarkerRadius = 0.64f;
            railEnemyAttackLaneTelegraphRendererSettings_.imminentScale = 1.10f;
            railEnemyAttackTelegraphFeedbackSettings_.masterVolume = 0.84f;
            // Preserve the proven screen-presence contract while allowing the
            // expansion route to show a full formation without visual overlap.
            railEnemyEncounterReadabilitySettings_.presence.
                minimumIdleDiameterPixels = combatLoopExpansionEnabled
                    ? 52.0f : 72.0f;
            railEnemyEncounterReadabilitySettings_.presence.
                minimumEngagedDiameterPixels = combatLoopExpansionEnabled
                    ? 72.0f : 96.0f;
            railEnemyEncounterReadabilitySettings_.presence.
                maximumPresentationScale = combatLoopExpansionEnabled
                    ? 12.0f : 20.0f;
            railEnemyEncounterReadabilitySettings_.presence.
                minimumPresentationAlpha = 0.95f;
            railEnemyEncounterReadabilitySettings_.presence.
                unreadableColorBoost = 1.30f;
            railEnemyEncounterReadabilitySettings_.safeAreaPixels = 64.0f;
            RailCameraEncounterFramingSettings combatFraming =
                railShooterCameraDirector_.EncounterFramingSettings();
            combatFraming.singleThreatFovTightenDeg =
                combatLoopExpansionEnabled ? 4.25f : 6.25f;
            combatFraming.singleThreatBackDistancePullIn =
                combatLoopExpansionEnabled ? 1.10f : 1.65f;
            combatFraming.singleThreatLookAheadReduction =
                combatLoopExpansionEnabled ? 1.50f : 2.25f;
            (void)railShooterCameraDirector_.ConfigureEncounterFraming(combatFraming);
            if (combatLoopExpansionEnabled) {
                // Video review: keep one actionable prompt primary and fold a
                // previous success into the score HUD before the next tell.
                railEnemyAttackDefensePresentationSettings_.
                    maximumVisiblePrompts = 1;
                railEnemyAttackDefensePresentationSettings_.
                    projectilePromptHoldSeconds = 0.18f;
                railEnemyAttackDefenseOutcomeFeedbackSettings_.
                    displayDurationSeconds = 0.95f;
                railEnemyAttackDefenseOutcomeFeedbackSettings_.
                    failureDisplayDurationSeconds = 0.62f;
            }
            OutputDebugStringA(combatLoopExpansionEnabled
                ? "[AppRunLoop] Combat loop expansion: CombatLoopExpansion.course.\n"
                : "[AppRunLoop] Combat loop lab: CombatLoop10s.course.\n");
            if (railDefenseUiProofEnabled_) {
                // Keep each proof result on screen long enough for deterministic
                // capture; normal gameplay retains the production timing.
                railEnemyAttackDefenseOutcomeFeedbackSettings_.displayDurationSeconds =
                    4.00f;
                OutputDebugStringA(
                    "[AppRunLoop] Defense UI proof: interrupt, shoot-down, evade.\n");
            }
        }
        sceneStateManager_.Initialize(std::make_unique<RailShooterSceneState>(), *this);
        std::string presetError;
        terrainPresetStore_.Load(runtimeState_.terrain, &presetError);
        LoadRailShooterCourse();
        ApplyRailShooterCourse();
        railTitleScreenVisible_ = !combatLoopExpansionEnabled &&
            !ResolveCombatLoop10SecondModeFromCommandLine() &&
            !railDefenseUiProofEnabled_;
        if (railTitleScreenVisible_) {
            railTitleSavedTerrainEnabled_ = runtimeState_.terrain.enabled;
            railTitleSavedLight_ = runtimeState_.directionalLightData;
            railTitleSavedPointLight_ = runtimeState_.pointLightData;
            railTitleSavedSpotLight_ = runtimeState_.spotLight;
            for(int i=0;i<4;++i) railTitleSavedClearColor_[i]=runtimeState_.clearColor[i];
            railTitleSavedSkybox_ = runtimeState_.showSkybox;
            railTitleSavedBackdrop_ = runtimeState_.showProceduralBackdrop;
            // Development and Debug expose the tools switch on the title screen.
#if !defined(_DEBUG) && !defined(DEVELOP)
            imguiLayer_.SetVisible(false);
#endif
        }
        OutputDebugStringA("[AppRunLoop] Startup scene: RailShooter.\n");
    }
    frameCoordinator_.Initialize();
}

void AppRunLoop::InitializeBeam(
    ID3D12Device* device,
    ID3D12DescriptorHeap* srvDescriptorHeap,
    uint32_t descriptorSizeSRV,
    DXGI_FORMAT rtvFormat,
    DXGI_FORMAT dsvFormat) {
    vfxEngine_.InitializeBeam(
        device,
        srvDescriptorHeap,
        descriptorSizeSRV,
        scene_.textureSrvHandleCPU,
        scene_.textureSrvHandleCPU2,
        rtvFormat,
        dsvFormat);
}

void AppRunLoop::LoadRailShooterCourse() {
    std::string error;
    if (railShooterCourse_.LoadFromFile(railShooterCoursePath_, &error)) {
        railShooterCourseLoadStatus_ =
            "Loaded course \"" + railShooterCourse_.name + "\" from " + railShooterCoursePath_;
        OutputDebugStringA(("[Course] " + railShooterCourseLoadStatus_ + "\n").c_str());
        return;
    }

    railShooterCourse_.BuildFallbackCanyon(runtimeState_.terrain.settings.corridorRadius);
    railShooterCourseLoadStatus_ = "Fallback course active. " + error;
    OutputDebugStringA(("[Course] " + railShooterCourseLoadStatus_ + "\n").c_str());
}

bool AppRunLoop::ApplyRailVehicleControlPreset(
    bool preserveRuntimeState,
    std::string* errorMessage) {
    const RailVehicleControlDefinitionAsset* asset =
        railVehicleControlPresetRegistry_.Find(railVehicleControlPresetId_);
    if (asset == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = "Unknown rail vehicle control preset: " +
                railVehicleControlPresetId_;
        }
        return false;
    }
    std::string validationError;
    if (!asset->Validate(&validationError)) {
        if (errorMessage != nullptr) *errorMessage = validationError;
        return false;
    }

    const RailVehicleDefinition previousDefinition =
        railShooterVehicleMovement_.Definition();
    const RailVehicleRuntimeState previousVehicle =
        railShooterVehicleMovement_.State();
    RailVehicleCameraMountDefinition cameraMountDefinition = asset->camera;
    const bool combatLoopExpansionCourse =
        std::filesystem::path{railShooterCoursePath_}.filename() ==
            "CombatLoopExpansion.course";
    if (combatLoopExpansionCourse) {
        // The authored camera already supplies deliberate distance and FOV.
        // A strong vehicle-anchor correction pulled it back toward the cart
        // during the rib transition and magnified foreground occluders.
        cameraMountDefinition.anchorBlend = 0.10f;
        cameraMountDefinition.maximumAnchorCorrection = 2.5f;
    }
    std::string initializationError;
    if (!railShooterSpeedDirector_.Configure(
            asset->speedPolicy, preserveRuntimeState, &initializationError) ||
        !railShooterVehicleMovement_.Initialize(
            asset->vehicle, &initializationError) ||
        !railShooterMountedEvasion_.Initialize(
            asset->evasion, &initializationError) ||
        !railShooterOccupantClearance_.Initialize(
            asset->clearance, &initializationError) ||
        !railShooterEvasionConstraintResolver_.Initialize(
            asset->constraint, &initializationError) ||
        !railShooterCameraMountBridge_.Initialize(
            cameraMountDefinition, &initializationError) ||
        !railShooterVehicleBodyCollision_.Initialize(
            asset->hitbox, &initializationError) ||
        !railShooterVehicleDamageCoordinator_.Initialize(
            asset->hitbox, &initializationError) ||
        !railShooterCollisionSystem_.PlayerHitbox().Initialize(
            asset->hitbox.BuildPlayerHitboxDefinition(),
            &initializationError)) {
        if (errorMessage != nullptr) *errorMessage = initializationError;
        return false;
    }

    railShooterVehiclePresentationSettings_ = asset->vehiclePresentation;
    railShooterVehicleRideDynamicsSettings_ = asset->rideDynamics;
    railShooterTrackContactPoseSettings_ = asset->trackContact;
    railShooterMountedEvasionPresentationSettings_ =
        asset->occupantPresentation;
    railShooterVehicleAudioSettings_ = asset->vehicleAudio;
    railShooterEvasionFeedbackSettings_ = asset->evasionFeedback;
    railShooterVehicleCollisionFeedbackSettings_ = asset->collisionFeedback;
    if (combatLoopExpansionCourse) {
        railShooterVehiclePresentationSettings_.maximumVisualBankDegrees = 10.0f;
        railShooterVehicleRideDynamicsSettings_.maximumBankDegrees = 10.0f;
        railShooterVehicleRideDynamicsSettings_.maximumPitchDegrees = 6.0f;
    }

    if (preserveRuntimeState && previousVehicle.initialized) {
        RailVehicleRuntimeState restored = previousVehicle;
        restored.vehicleId = asset->vehicle.vehicleId;
        restored.speed = (std::clamp)(
            restored.speed, 0.0f, asset->vehicle.maximumSpeed);
        restored.requestedSpeed = (std::clamp)(
            restored.requestedSpeed, 0.0f, asset->vehicle.maximumSpeed);
        restored.safeSpeed = (std::clamp)(
            restored.safeSpeed, 0.0f, asset->vehicle.maximumSpeed);
        const float previousHealthRatio = previousDefinition.maximumHitPoints > 0.0f
            ? (std::clamp)(
                previousVehicle.hitPoints / previousDefinition.maximumHitPoints,
                0.0f, 1.0f)
            : 1.0f;
        restored.hitPoints =
            previousHealthRatio * asset->vehicle.maximumHitPoints;
        restored.distance = (std::clamp)(
            restored.distance, 0.0f, railPath_.Length());
        restored.previousDistance = (std::clamp)(
            restored.previousDistance, 0.0f, railPath_.Length());
        if (!railShooterVehicleMovement_.RestoreState(
                restored, &railPath_, &initializationError)) {
            railShooterVehicleMovement_.Reset(
                restored.distance, restored.speed, &railPath_);
        }
    }
    railShooterPlayerVehicleMount_.Reset();
    railShooterCombatMountBridge_.Reset();
    railShooterMountedEvasionPresentation_.Reset();
    railShooterEvasionFeedback_.Reset();
    railShooterVehicleBodyCollision_.Reset();
    railShooterVehicleDamageCoordinator_.Reset();
    railShooterVehicleCollisionFeedback_.Reset();
    railShooterVehicleRideDynamics_.Reset();
    railShooterTrackContactPose_.Reset();
    railShooterCameraInertia_.Reset();
    railShooterPlayerLateralOffset_ =
        railShooterVehicleMovement_.Definition().mounts.player.x;
    railShooterPlayerVerticalOffset_ =
        railShooterVehicleMovement_.Definition().bodyVerticalOffset +
        railShooterVehicleMovement_.Definition().mounts.player.y;
    railVehicleControlAppliedPresetId_ = railVehicleControlPresetId_;
    railVehicleControlAppliedRevision_ =
        railVehicleControlPresetRegistry_.Stats().revision;
    if (errorMessage != nullptr) errorMessage->clear();
    return true;
}

void AppRunLoop::ApplyRailShooterCourse() {
    // Applying/reloading authoring data invalidates the immutable preview
    // snapshot. Stop first so the next preview starts from the new source.
    coursePreviewSimulationSystem_.Stop();
    coursePreviewActorRuntimeBridge_.Reset();
    railShooterRetryCoordinator_.Unbind();
    railShooterGameplayWaveBridge_.Unbind();
    railShooterHudDefinition_ = RailShooterHudDefinitionAsset::Defaults();
    std::string hudLoadError;
    if (!railShooterHudDefinition_.LoadFromFile(
            std::filesystem::path{"Resources/hud/rail_shooter_default.railhud"},
            &hudLoadError)) {
        railShooterHudDefinition_ = RailShooterHudDefinitionAsset::Defaults();
        railShooterHudLoadStatus_ = "Fallback HUD active: " + hudLoadError;
        OutputDebugStringA(
            ("[RailShooterHud] " + railShooterHudLoadStatus_ + "\n").c_str());
    } else {
        railShooterHudLoadStatus_ =
            "Loaded " + railShooterHudDefinition_.assetId;
    }
    railShooterHudRuntimeModel_.Reset();
    railShooterHudPresentation_.Reset();
    railShooterHudRenderer_.Reset();
    railShooterDefensePromptRenderer_.Reset();
    if (!railShooterCourse_.IsValid()) {
        railShooterCourse_.BuildFallbackCanyon(runtimeState_.terrain.settings.corridorRadius);
    }
    railShooterCourse_.ApplyToRailPath(railPath_);
    railShooterTrackDefinition_ = CourseRailTrackDefinitionAsset::MineCartDefaults();
    const std::string trackAssetId = railShooterCourse_.railTrackAssetId.empty()
        ? std::string{"mine_cart_standard"}
        : railShooterCourse_.railTrackAssetId;
    const bool safeTrackAssetId = std::all_of(
        trackAssetId.begin(), trackAssetId.end(), [](unsigned char c) {
            return std::isalnum(c) || c == '_' || c == '-' || c == '.';
        });
    std::string trackLoadError;
    if (!safeTrackAssetId || !railShooterTrackDefinition_.LoadFromFile(
            std::filesystem::path{"Resources/rail_tracks"} /
                (trackAssetId + ".railtrack"),
            &trackLoadError)) {
        railShooterTrackDefinition_ = CourseRailTrackDefinitionAsset::MineCartDefaults();
        railShooterTrackLoadStatus_ = "Fallback track active: " + trackLoadError;
        OutputDebugStringA(("[CourseRailTrack] " + railShooterTrackLoadStatus_ + "\n").c_str());
    } else {
        railShooterTrackLoadStatus_ = "Loaded " + railShooterTrackDefinition_.assetId;
    }
    railShooterCourseRuntime_.Bind(&railShooterCourse_);
    railShooterCourseRuntime_.Reset(runtimeState_.terrain.previewDistance);
    railShooterSpawnRuntime_.Reset();

    const uint64_t sourceHash = ComputeCourseAssetSourceHash(railShooterCourse_);
    const std::string runtimeProgramPath =
        editor::CourseRuntimeCookPipeline::DefaultOutputPath(railShooterCoursePath_);
    CourseRuntimeProgramAsset loadedProgram{};
#if defined(NDEBUG)
    constexpr CourseRuntimeBuildConfiguration runtimeConfiguration =
        CourseRuntimeBuildConfiguration::Release;
#else
    constexpr CourseRuntimeBuildConfiguration runtimeConfiguration =
        CourseRuntimeBuildConfiguration::Debug;
#endif
    const bool loadedCurrentProgram =
        loadedProgram.LoadFromFile(runtimeProgramPath, nullptr) &&
        loadedProgram.IsSourceCurrent(sourceHash) &&
        loadedProgram.AreDependenciesCurrent() &&
        loadedProgram.buildConfiguration == runtimeConfiguration;
    if (loadedCurrentProgram) {
        railShooterRuntimeProgram_ = std::move(loadedProgram);
        OutputDebugStringA(
            ("[CourseRuntime] Loaded current cooked ProgramAsset: " +
             runtimeProgramPath + "\n").c_str());
    } else {
        editor::CourseRuntimeCookOptions cookOptions{};
        cookOptions.configuration = runtimeConfiguration;
        cookOptions.allowDebugFallbackAssets = true;
        const editor::CourseRuntimeCookResult cook =
            railShooterRuntimeCookPipeline_.Cook(railShooterCourse_, cookOptions);
        if (cook.succeeded) {
            railShooterRuntimeProgram_ = cook.program;
            OutputDebugStringA(
                ("[CourseRuntime] In-memory cook: " + cook.message + "\n").c_str());
        } else {
            railShooterRuntimeProgram_ = {};
            OutputDebugStringA(
                ("[CourseRuntime] Cook failed; authored Wave runtime disabled: " +
                 cook.message + "\n").c_str());
        }
    }
    if (railShooterRuntimeProgram_.sourceFingerprint != 0) {
        std::string gameplayWaveError;
        if (!railShooterGameplayWaveBridge_.Bind(
                &railShooterRuntimeProgram_,
                &railShooterSpawnRuntime_,
                railShooterCourseRuntime_.Distance(),
                &gameplayWaveError)) {
            OutputDebugStringA(
                ("[CourseGameplayWaves] " + gameplayWaveError + "\n").c_str());
        }
    }
    railEnemyAttackTelegraphSystem_.Reset();
    StopRailEnemyAttackFeedback();
    railShooterCollisionSystem_.Reset();
    railPlayerDamagePresentationBridge_.Reset();
    railShooterPlayerMovement_.Reset();
    railShooterPlayerDodge_.Reset();
    if (railVehicleControlPresetRegistry_.Directory().empty()) {
        std::string controlLoadError;
        if (!railVehicleControlPresetRegistry_.LoadDirectory(
                RailVehicleControlPresetRegistry::DefaultDirectory(),
                &controlLoadError)) {
            OutputDebugStringA(
                ("[RailVehicleControls] Fallback preset active: " +
                 controlLoadError + "\n").c_str());
        }
    }
    if (railVehicleControlAppliedPresetId_ != railVehicleControlPresetId_ ||
        railVehicleControlAppliedRevision_ !=
            railVehicleControlPresetRegistry_.Stats().revision) {
        std::string controlApplyError;
        if (!ApplyRailVehicleControlPreset(false, &controlApplyError)) {
            OutputDebugStringA(
                ("[RailVehicleControls] Apply failed: " +
                 controlApplyError + "\n").c_str());
        } else {
            OutputDebugStringA(
                ("[RailVehicleControls] Applied " +
                 railVehicleControlPresetId_ + " revision=" +
                 std::to_string(railVehicleControlAppliedRevision_) +
                 "\n").c_str());
        }
    }
    // The physical track asset owns rail-head gauge and height. The four-wheel
    // contact solver consumes those exact values, preventing visual/gameplay
    // drift when a course selects a different track standard.
    railShooterTrackContactPoseSettings_.trackGauge =
        railShooterTrackDefinition_.trackGauge;
    railShooterTrackContactPoseSettings_.railHeadVerticalOffset =
        railShooterTrackDefinition_.railHeadVerticalOffset;
    std::string trackBakeError;
    if (!railShooterTrackMeshBake_.Bake(
            railShooterTrackDefinition_, railPath_, &trackBakeError)) {
        railShooterTrackLoadStatus_ = "Track bake failed: " + trackBakeError;
        OutputDebugStringA(("[CourseRailTrack] " + railShooterTrackLoadStatus_ + "\n").c_str());
    } else {
        railShooterTrackLoadStatus_ += "; " +
            railShooterTrackMeshBake_.Result().status;
    }
    railShooterVehicleMovement_.Reset(
        railShooterCourseRuntime_.Distance(),
        0.0f,
        &railPath_);
    railShooterPlayerVehicleMount_.Reset();
    railShooterMountedEvasion_.Reset();
    railShooterMountedDefense_.Reset();
    railEnemyAttackDefenseValidation_.Reset();
    railShooterVehicleBodyCollision_.Reset();
    railShooterVehicleDamageCoordinator_.Reset();
    railShooterVehicleCollisionFeedback_.Reset();
    railShooterOccupantClearance_.Reset();
    railShooterEvasionConstraintResolver_.Reset();
    railShooterCombatMountBridge_.Reset();
    railShooterCameraMountBridge_.Reset();
    railShooterMountedEvasionPresentation_.Reset();
    railShooterOccupantActor_.Reset();
    railShooterEvasionFeedback_.Reset();
    railShooterPlayerLateralOffset_ =
        railShooterVehicleMovement_.Definition().mounts.player.x;
    railShooterPlayerVerticalOffset_ =
        railShooterVehicleMovement_.Definition().bodyVerticalOffset +
        railShooterVehicleMovement_.Definition().mounts.player.y;
    railShooterVehiclePresentation_.Reset();
    railShooterVehicleRideDynamics_.Reset();
    railShooterTrackContactPose_.Reset();
    railShooterWheelContactPresentation_.Reset();
    railShooterCameraInertia_.Reset();
    railShooterVehicleActor_.Reset();
    railShooterVehicleRenderer_.Reset();
    railShooterVehicleAudioBridge_.Reset();
    railEnemyCombatPresentationBridge_.Reset();
    ResetEnemyProjectilePresentation();
    if (railEnemyProjectileVfxRenderer_.Directory().empty()) {
        std::string projectileVisualError;
        if (!railEnemyProjectileVfxRenderer_.LoadDirectory(
                EnemyProjectileVfxRenderer::DefaultDirectory(),
                &projectileVisualError)) {
            OutputDebugStringA(
                ("[EnemyProjectileVisuals] Built-in fallback active: " +
                 projectileVisualError + "\n").c_str());
        } else {
            OutputDebugStringA(
                ("[EnemyProjectileVisuals] Loaded " +
                 std::to_string(
                     railEnemyProjectileVfxRenderer_.Assets().size()) +
                 " visual definitions\n").c_str());
        }
    }
    if (railShooterCollisionSystem_.WeaponDefinitions().Directory().empty()) {
        std::string weaponError;
        if (!railShooterCollisionSystem_.LoadWeaponDefinitions(
                WeaponDefinitionRegistry::DefaultDirectory(),
                &weaponError)) {
            OutputDebugStringA(
                ("[WeaponDefinitions] Fallback definitions active: " + weaponError + "\n").c_str());
        } else {
            OutputDebugStringA(
                ("[WeaponDefinitions] " +
                 railShooterCollisionSystem_.WeaponDefinitions().Stats().status + "\n").c_str());
        }
    }
    if (railAimAssistPresetRegistry_.Directory().empty()) {
        std::string presetError;
        if (!railAimAssistPresetRegistry_.LoadDirectory(
                RailAimAssistPresetRegistry::DefaultDirectory(),
                &presetError)) {
            OutputDebugStringA(
                ("[AimAssistPresets] Fallback presets active: " +
                 presetError + "\n").c_str());
        } else {
            OutputDebugStringA(
                ("[AimAssistPresets] " +
                 railAimAssistPresetRegistry_.Stats().status + "\n").c_str());
        }
        railAimAssistAppliedPresetRevision_ = 0;
        railAimAssistAppliedPresetId_.clear();
    }
    railShooterCheckpointSystem_.Reset(&railShooterCourse_, runtimeState_.terrain.previewDistance);
    GameSessionDefinition sessionDefinition = GameSessionDefinition::RailShooterDefaults();
    if (!railShooterCourse_.name.empty()) {
        sessionDefinition.sessionId = railShooterCourse_.name;
    }
    std::string sessionError;
    if (!railShooterGameSession_.Initialize(
            sessionDefinition,
            railPath_.Length(),
            &sessionError)) {
        OutputDebugStringA(("[GameSession] Initialize failed: " + sessionError + "\n").c_str());
    } else if (railShooterInitialized_ &&
               !railShooterGameSession_.Start(
                   railShooterCourseRuntime_.Distance(),
                   &sessionError)) {
        OutputDebugStringA(("[GameSession] Start failed: " + sessionError + "\n").c_str());
    }
    // The course-start retry snapshot must never inherit the previous route's
    // completed Beat or score-chain state.
    railEnemyEncounterPacingDirector_.Reset(&railShooterSpawnRuntime_);
    railEncounterPerformanceScoreSystem_.Reset();
    if (railShooterGameSession_.IsInitialized()) {
        if (!railShooterRetryCoordinator_.Bind(
                GameSessionRetryCoordinatorBinding{
                    &railShooterGameSession_,
                    &railShooterCourseRuntime_,
                    &railShooterGameplayWaveBridge_,
                    &railShooterSpawnRuntime_,
                    &railShooterCollisionSystem_,
                    &railShooterCheckpointSystem_,
                    &railShooterCourse_,
                    &railShooterPlayerMovement_,
                    &railShooterPlayerDodge_,
                    &railShooterVehicleMovement_,
                    &railPath_,
                    &railGrazeScoreSystem_,
                    &railShooterMountedEvasion_,
                    &railShooterVehicleDamageCoordinator_,
                    &railEnemyEncounterPacingDirector_,
                    &railEncounterPerformanceScoreSystem_},
                &sessionError)) {
            OutputDebugStringA(("[GameSessionRetry] Bind failed: " + sessionError + "\n").c_str());
        } else if (railShooterInitialized_) {
            (void)railShooterRetryCoordinator_.CaptureCheckpoint(
                railShooterCourseRuntime_.Distance(),
                "course_start",
                nullptr);
        }
    }
    StopGameSessionPresentation();
    railShooterCombatFeelSystem_.Reset();
    railShooterEncounterDirector_.Reset();
    railEnemyEncounterCameraCompositionBridge_.Reset();
    railShooterCameraDirector_.Reset();
    railShooterSpeedDirector_.Reset();
    railShooterRideDirector_.Reset();
    railShooterTrackFeedbackDirector_.Reset();
    railShooterRideMotionEnvelope_.Reset();
    courseObjectUndoStack_.clear();
    courseObjectRedoStack_.clear();
    courseObjectTransactions_.Clear();
    courseObjectHistoryInitialized_ = false;
    runtimeState_.terrain.courseObjectUndoDepth = 0;
    runtimeState_.terrain.courseObjectRedoDepth = 0;
    std::string railEditorError;
    if (!courseRailEditorController_.Bind(
            editor::CourseRailEditorControllerBinding{
                &railShooterCourse_,
                &railPath_,
                nullptr,
                nullptr,
                railShooterCoursePath_,
                true},
            &railEditorError)) {
        OutputDebugStringA(
            ("[CourseRailEditor] " + railEditorError + "\n").c_str());
    }
    std::string enemyEditorError;
    if (!courseEnemyEditorController_.Bind(
            editor::CourseEnemyEditorControllerBinding{
                &railShooterCourse_,
                nullptr,
                nullptr,
                railShooterCoursePath_,
                true},
            &enemyEditorError)) {
        OutputDebugStringA(
            ("[CourseEnemyEditor] " + enemyEditorError + "\n").c_str());
    }
    std::string waveEditorError;
    if (!courseWaveEditorController_.Bind(
            editor::CourseWaveEditorControllerBinding{
                &railShooterCourse_,
                nullptr,
                nullptr,
                railShooterCoursePath_,
                true},
            &waveEditorError)) {
        OutputDebugStringA(
            ("[CourseWaveEditor] " + waveEditorError + "\n").c_str());
    }
}

bool AppRunLoop::SaveRailShooterCourse(std::string* errorMessage) {
    std::string error;
    railShooterCourse_.SortForRuntime();
    const std::string transactionId = editor::EditorFileTransaction::GenerateTransactionId();
    const std::filesystem::path serializationPath =
        std::filesystem::current_path() / ".editor" / "serialization" /
        (transactionId + ".course.tmp");
    std::error_code filesystemError;
    std::filesystem::create_directories(serializationPath.parent_path(), filesystemError);
    if (filesystemError ||
        !railShooterCourse_.SaveToFile(serializationPath.generic_string(), &error)) {
        if (error.empty()) {
            error = "Failed to create course serialization staging directory: " +
                filesystemError.message();
        }
        railShooterCourseLoadStatus_ = "Save failed. " + error;
        OutputDebugStringA(("[Course] " + railShooterCourseLoadStatus_ + "\n").c_str());
        if (errorMessage != nullptr) {
            *errorMessage = error;
        }
        return false;
    }

    std::ifstream serializedFile(serializationPath, std::ios::binary);
    std::vector<uint8_t> serializedBytes{
        std::istreambuf_iterator<char>(serializedFile),
        std::istreambuf_iterator<char>()};
    const bool serializedRead = serializedFile.good() || serializedFile.eof();
    serializedFile.close();
    std::filesystem::remove(serializationPath, filesystemError);

#if defined(NDEBUG)
    constexpr CourseRuntimeBuildConfiguration saveRuntimeConfiguration =
        CourseRuntimeBuildConfiguration::Release;
#else
    constexpr CourseRuntimeBuildConfiguration saveRuntimeConfiguration =
        CourseRuntimeBuildConfiguration::Debug;
#endif
    editor::CourseRuntimeCookOptions cookOptions{};
    cookOptions.configuration = saveRuntimeConfiguration;
    cookOptions.allowDebugFallbackAssets = true;
    const editor::CourseRuntimeCookResult cooked =
        railShooterRuntimeCookPipeline_.Cook(railShooterCourse_, cookOptions);
    std::string runtimeBytesString;
    if (!serializedRead || !cooked.succeeded ||
        !cooked.program.SaveToString(&runtimeBytesString, &error)) {
        if (error.empty()) error = cooked.message;
        railShooterCourseLoadStatus_ = "Save/cook failed. " + error;
        OutputDebugStringA(("[Course] " + railShooterCourseLoadStatus_ + "\n").c_str());
        if (errorMessage != nullptr) *errorMessage = error;
        return false;
    }
    std::vector<uint8_t> runtimeBytes(
        runtimeBytesString.begin(), runtimeBytesString.end());
    const std::string runtimeProgramPath =
        editor::CourseRuntimeCookPipeline::DefaultOutputPath(railShooterCoursePath_);

    editor::EditorFileTransaction transaction(std::filesystem::current_path(), transactionId);
    if (!serializedRead ||
        !transaction.StageWrite(
            railShooterCoursePath_,
            std::move(serializedBytes),
            [](const std::filesystem::path& stagedPath, std::string* validationError) {
                CourseAsset candidate{};
                return candidate.LoadFromFile(stagedPath.generic_string(), validationError);
            },
            &error) ||
        !transaction.StageWrite(
            runtimeProgramPath,
            std::move(runtimeBytes),
            [](const std::filesystem::path& stagedPath, std::string* validationError) {
                CourseRuntimeProgramAsset candidate{};
                return candidate.LoadFromFile(stagedPath.generic_string(), validationError);
            },
            &error) ||
        !transaction.Execute(nullptr, &error)) {
        railShooterCourseLoadStatus_ = "Save failed. " + error;
        OutputDebugStringA(("[Course] " + railShooterCourseLoadStatus_ + "\n").c_str());
        if (errorMessage != nullptr) {
            *errorMessage = error;
        }
        return false;
    }

    railShooterCourseLoadStatus_ =
        "Saved and cooked course \"" + railShooterCourse_.name + "\" to " +
        railShooterCoursePath_;
    OutputDebugStringA(("[Course] " + railShooterCourseLoadStatus_ + "\n").c_str());
    ApplyRailShooterCourse();
    return true;
}

void AppRunLoop::TeleportRailShooterCourse(float distance) {
    const float railLength = railPath_.Length();
    const float clampedDistance = railLength > 0.0f
        ? (std::clamp)(distance, 0.0f, railLength)
        : (std::max)(0.0f, distance);
    runtimeState_.terrain.previewDistance = clampedDistance;
    railShooterCourseRuntime_.Reset(clampedDistance);
    railShooterDistance_ = railShooterCourseRuntime_.Distance();
    railShooterSpawnRuntime_.Reset();
    railShooterGameplayWaveBridge_.Reset(clampedDistance);
    railEnemyAttackTelegraphSystem_.Reset();
    StopRailEnemyAttackFeedback();
    railShooterCollisionSystem_.Reset();
    railPlayerDamagePresentationBridge_.Reset();
    railShooterPlayerMovement_.Reset();
    railShooterPlayerDodge_.Reset();
    railShooterVehicleMovement_.Reset(clampedDistance, 0.0f, &railPath_);
    railShooterPlayerVehicleMount_.Reset();
    railShooterMountedEvasion_.Reset();
    railShooterMountedDefense_.Reset();
    railEnemyAttackDefenseValidation_.Reset();
    railShooterVehicleBodyCollision_.Reset();
    railShooterVehicleDamageCoordinator_.Reset();
    railShooterVehicleCollisionFeedback_.Reset();
    railShooterOccupantClearance_.Reset();
    railShooterEvasionConstraintResolver_.Reset();
    railShooterCombatMountBridge_.Reset();
    railShooterCameraMountBridge_.Reset();
    railShooterMountedEvasionPresentation_.Reset();
    railShooterOccupantActor_.Reset();
    railShooterEvasionFeedback_.Reset();
    railShooterPlayerLateralOffset_ =
        railShooterVehicleMovement_.Definition().mounts.player.x;
    railShooterPlayerVerticalOffset_ =
        railShooterVehicleMovement_.Definition().bodyVerticalOffset +
        railShooterVehicleMovement_.Definition().mounts.player.y;
    railShooterVehiclePresentation_.Reset();
    railShooterVehicleRideDynamics_.Reset();
    railShooterTrackContactPose_.Reset();
    railShooterCameraInertia_.Reset();
    railShooterVehicleActor_.Reset();
    railShooterVehicleRenderer_.Reset();
    railShooterVehicleAudioBridge_.Reset();
    railEnemyCombatPresentationBridge_.Reset();
    ResetEnemyProjectilePresentation();
    railShooterCheckpointSystem_.NotifyTeleport(&railShooterCourse_, railShooterDistance_);
    std::string sessionError;
    if (railShooterGameSession_.IsInitialized() &&
        !railShooterGameSession_.RestartRun(railShooterDistance_, &sessionError)) {
        OutputDebugStringA(("[GameSession] Teleport restart failed: " + sessionError + "\n").c_str());
    }
    railEnemyEncounterPacingDirector_.Reset(&railShooterSpawnRuntime_);
    railEncounterPerformanceScoreSystem_.Reset();
    if (railShooterRetryCoordinator_.IsBound()) {
        (void)railShooterRetryCoordinator_.CaptureCheckpoint(
            railShooterDistance_,
            "authoring_teleport",
            nullptr);
    }
    railShooterCombatFeelSystem_.Reset();
    railShooterEncounterDirector_.Reset();
    railEnemyEncounterCameraCompositionBridge_.Reset();
    railShooterCameraDirector_.Reset();
    railShooterSpeedDirector_.Reset();
    railShooterRideDirector_.Reset();
    railShooterTrackFeedbackDirector_.Reset();
    railShooterRideMotionEnvelope_.Reset();

    std::ostringstream line;
    line << "[Course] Teleported authoring preview to distance=" << railShooterDistance_ << "\n";
    OutputDebugStringA(line.str().c_str());
}

bool AppRunLoop::BeginEditorGameplaySpawns(std::string* errorMessage) {
    editorGimmickRuntimeEventRouter_.BindEventBindingRegistry(
        &editorGimmickRuntimeEventBindings_);
    editorGimmickRuntimeEventRouter_.BindDelayedEventScheduler(
        &editorGimmickRuntimeDelayedEvents_);
    editorGimmickRuntimeEventRouter_.BindEventSequenceRegistry(
        &editorGimmickRuntimeEventSequences_);
    imguiLayer_.BindEditorGimmickRuntimeDebug(
        &editorGimmickRuntimeWorld_,
        &editorGimmickRuntimeAdapter_,
        &editorGimmickRuntimeEventRouter_,
        &editorGimmickRuntimeInteraction_,
        &editorGimmickRuntimeTriggers_);
    const editor::EditorScene* scene = imguiLayer_.ActiveEditorScene();
    if (scene == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = "Active Editor Scene is unavailable.";
        }
        return false;
    }

    std::string registrationError;
    if (!editor::RegisterBuiltInEditorSceneRuntimeFactories(
            editorSceneRuntimeFactoryRegistry_,
            &registrationError)) {
        if (errorMessage != nullptr) {
            *errorMessage = registrationError;
        }
        return false;
    }
    if (!editor::
            RegisterBuiltInEditorGimmickDefinitionRuntimeFactories(
                editorGimmickDefinitionRuntimeFactories_,
                imguiLayer_.GimmickDefinitionRegistry(),
                &registrationError)) {
        if (errorMessage != nullptr) {
            *errorMessage = registrationError;
        }
        return false;
    }
    std::string bindError;
    if (!editorSceneRuntimeInstantiation_.Bind(
            &imguiLayer_.SceneComponentRegistry(),
            &editorSceneRuntimeFactoryRegistry_,
            &bindError)) {
        if (errorMessage != nullptr) {
            *errorMessage = bindError;
        }
        return false;
    }

    editor::EditorGameplaySpawnRuntimeTarget spawnTarget{
        &railPath_,
        &railShooterEventDispatcher_,
        &railShooterSpawnRuntime_,
        railShooterDistance_,
        &railShooterPlayerLateralOffset_,
        &railShooterPlayerVerticalOffset_,
        [this](float distance) {
            TeleportRailShooterCourse(distance);
        }};
    editor::EditorSceneRuntimeServiceRegistry runtimeServices;
    runtimeServices.Bind(
        std::string(editor::kEditorGameplaySpawnRuntimeTargetServiceId),
        &spawnTarget);
    editor::EditorMeshRendererRuntimeTarget meshTarget{
        &imguiLayer_.AssetRegistry(),
        &imguiLayer_.MeshRendererRuntimeWorld()};
    runtimeServices.Bind(
        std::string(editor::kEditorMeshRendererRuntimeTargetServiceId),
        &meshTarget);
    editor::EditorPatrolRuntimeTarget patrolTarget{
        &editorPatrolRuntimeWorld_,
        &railShooterSpawnRuntime_,
        &railPath_};
    runtimeServices.Bind(
        std::string(editor::kEditorPatrolRuntimeTargetServiceId),
        &patrolTarget);
    editor::EditorGimmickRuntimeTarget gimmickTarget{
        &imguiLayer_.GimmickDefinitionRegistry(),
        &editorGimmickDefinitionRuntimeFactories_,
        &editorGimmickRuntimeWorld_};
    runtimeServices.Bind(
        std::string(editor::kEditorGimmickRuntimeTargetServiceId),
        &gimmickTarget);
    editor::EditorGimmickEventBindingRuntimeTarget
        gimmickEventBindingTarget{
            &editorGimmickRuntimeEventBindings_,
            &editorGimmickRuntimeWorld_};
    runtimeServices.Bind(
        std::string(
            editor::
                kEditorGimmickEventBindingRuntimeTargetServiceId),
        &gimmickEventBindingTarget);
    editor::EditorGimmickEventSequenceRuntimeTarget
        gimmickEventSequenceTarget{
            &editorGimmickRuntimeEventSequences_,
            &editorGimmickRuntimeWorld_};
    runtimeServices.Bind(
        std::string(
            editor::
                kEditorGimmickEventSequenceRuntimeTargetServiceId),
        &gimmickEventSequenceTarget);
    const editor::EditorSceneRuntimeInstantiationResult result =
        editorSceneRuntimeInstantiation_.Begin(*scene, runtimeServices);
    if (!result.succeeded) {
        if (errorMessage != nullptr) {
            *errorMessage = result.message;
        }
        return false;
    }
    editorGimmickRuntimeEventBindings_.FinalizeReconcile();
    editorGimmickRuntimeEventSequences_.FinalizeReconcile();
    std::string adapterError;
    if (!editorGimmickRuntimeAdapter_.Reconcile(
            *scene,
            imguiLayer_.MeshRendererRuntimeWorld(),
            editorGimmickRuntimeWorld_,
            &adapterError)) {
        editorSceneRuntimeInstantiation_.Stop();
        editorGimmickRuntimeWorld_.Clear();
        if (errorMessage != nullptr) {
            *errorMessage =
                "Gimmick Presentation/Physics Adapter rejected "
                "Runtime Scene: " +
                adapterError;
        }
        return false;
    }
    editorGimmickRuntimeEventRouter_.Reconcile(
        editorGimmickRuntimeWorld_);
    editorGimmickRuntimeTriggers_.Reconcile(
        editorGimmickRuntimeWorld_,
        editorGimmickRuntimeAdapter_);
    editorSceneRuntimeLastReconcileAttemptRevision_ = result.sourceRevision;

    std::ostringstream line;
    line << "[RuntimeScene] " << result.message;
    for (const std::string& warning : result.warnings) {
        line << " warning=\"" << warning << "\"";
    }
    line << "\n";
    OutputDebugStringA(line.str().c_str());
    return true;
}

bool AppRunLoop::ReconcileEditorSceneRuntime(std::string* errorMessage) {
    if (!editorSceneRuntimeInstantiation_.Active()) return true;
    const editor::EditorScene* scene = imguiLayer_.ActiveEditorScene();
    if (scene == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = "Active Editor Scene is unavailable during Reconcile.";
        }
        return false;
    }
    if (scene->revision == editorSceneRuntimeInstantiation_.SourceRevision() ||
        scene->revision == editorSceneRuntimeLastReconcileAttemptRevision_) {
        return true;
    }
    editorSceneRuntimeLastReconcileAttemptRevision_ = scene->revision;

    editor::EditorGameplaySpawnRuntimeTarget spawnTarget{
        &railPath_,
        &railShooterEventDispatcher_,
        &railShooterSpawnRuntime_,
        railShooterDistance_,
        &railShooterPlayerLateralOffset_,
        &railShooterPlayerVerticalOffset_,
        [this](float distance) {
            TeleportRailShooterCourse(distance);
        }};
    editor::EditorSceneRuntimeServiceRegistry runtimeServices;
    runtimeServices.Bind(
        std::string(editor::kEditorGameplaySpawnRuntimeTargetServiceId),
        &spawnTarget);
    editor::EditorMeshRendererRuntimeTarget meshTarget{
        &imguiLayer_.AssetRegistry(),
        &imguiLayer_.MeshRendererRuntimeWorld()};
    runtimeServices.Bind(
        std::string(editor::kEditorMeshRendererRuntimeTargetServiceId),
        &meshTarget);
    editor::EditorPatrolRuntimeTarget patrolTarget{
        &editorPatrolRuntimeWorld_,
        &railShooterSpawnRuntime_,
        &railPath_};
    runtimeServices.Bind(
        std::string(editor::kEditorPatrolRuntimeTargetServiceId),
        &patrolTarget);
    editor::EditorGimmickRuntimeTarget gimmickTarget{
        &imguiLayer_.GimmickDefinitionRegistry(),
        &editorGimmickDefinitionRuntimeFactories_,
        &editorGimmickRuntimeWorld_};
    runtimeServices.Bind(
        std::string(editor::kEditorGimmickRuntimeTargetServiceId),
        &gimmickTarget);
    editor::EditorGimmickEventBindingRuntimeTarget
        gimmickEventBindingTarget{
            &editorGimmickRuntimeEventBindings_,
            &editorGimmickRuntimeWorld_};
    runtimeServices.Bind(
        std::string(
            editor::
                kEditorGimmickEventBindingRuntimeTargetServiceId),
        &gimmickEventBindingTarget);
    editor::EditorGimmickEventSequenceRuntimeTarget
        gimmickEventSequenceTarget{
            &editorGimmickRuntimeEventSequences_,
            &editorGimmickRuntimeWorld_};
    runtimeServices.Bind(
        std::string(
            editor::
                kEditorGimmickEventSequenceRuntimeTargetServiceId),
        &gimmickEventSequenceTarget);
    const editor::EditorSceneRuntimeInstantiationResult result =
        editorSceneRuntimeInstantiation_.Reconcile(*scene, runtimeServices);
    if (!result.succeeded) {
        if (errorMessage != nullptr) *errorMessage = result.message;
        return false;
    }
    editorGimmickRuntimeEventBindings_.FinalizeReconcile();
    editorGimmickRuntimeEventSequences_.FinalizeReconcile();
    std::string adapterError;
    if (!editorGimmickRuntimeAdapter_.Reconcile(
            *scene,
            imguiLayer_.MeshRendererRuntimeWorld(),
            editorGimmickRuntimeWorld_,
            &adapterError)) {
        StopEditorGameplaySpawns();
        if (errorMessage != nullptr) {
            *errorMessage =
                "Gimmick Presentation/Physics Adapter failed "
                "during Reconcile: " +
                adapterError;
        }
        return false;
    }
    editorGimmickRuntimeEventRouter_.Reconcile(
        editorGimmickRuntimeWorld_);
    editorGimmickRuntimeTriggers_.Reconcile(
        editorGimmickRuntimeWorld_,
        editorGimmickRuntimeAdapter_);

    std::ostringstream line;
    line << "[RuntimeScene] " << result.message;
    for (const std::string& warning : result.warnings) {
        line << " warning=\"" << warning << "\"";
    }
    line << "\n";
    OutputDebugStringA(line.str().c_str());
    return true;
}

void AppRunLoop::StopEditorGameplaySpawns() {
    editorGimmickRuntimeInteraction_.Reset();
    editorGimmickRuntimeTriggers_.Reset();
    editorGimmickRuntimeEventRouter_.Reset();
    editorGimmickRuntimeEventRouter_.
        BindEventBindingRegistry(nullptr);
    editorGimmickRuntimeEventRouter_.
        BindDelayedEventScheduler(nullptr);
    editorGimmickRuntimeEventRouter_.
        BindEventSequenceRegistry(nullptr);
    editorGimmickRuntimeDelayedEvents_.Reset();
    editorGimmickRuntimeAdapter_.Clear();
    editorSceneRuntimeInstantiation_.Stop();
    editorGimmickRuntimeEventBindings_.Clear();
    editorGimmickRuntimeEventSequences_.Clear();
    editorGimmickRuntimeWorld_.Clear();
    editorSceneRuntimeLastReconcileAttemptRevision_ = 0;
}

void AppRunLoop::LogCourseEvents(const std::vector<CourseEventMarker>& events) {
    if (events.empty()) {
        return;
    }

    std::ofstream log = app::OpenRotatingLog("logs/course_events.log");
    for (const CourseEventMarker& event : events) {
        std::ostringstream line;
        line << "[CourseEvent] distance=" << event.distance
             << " type=" << event.type
             << " id=" << event.id;
        if (!event.payload.empty()) {
            line << " payload=\"" << event.payload << "\"";
        }
        line << "\n";
        OutputDebugStringA(line.str().c_str());
        if (log) {
            log << line.str();
        }
    }
}

void AppRunLoop::StartRailCameraTuningRecording() {
    railCameraTuningRecorder_.samples.clear();
    railCameraTuningRecorder_.recording = true;
    railCameraTuningRecorder_.recordedSamples = 0;
    railCameraTuningRecorder_.droppedSamples = 0;
    railCameraTuningRecorder_.recordingTimeSeconds = 0.0f;
    railCameraTuningRecorder_.status = "recording";
}

void AppRunLoop::StopRailCameraTuningRecording() {
    railCameraTuningRecorder_.recording = false;
    railCameraTuningRecorder_.status = railCameraTuningRecorder_.samples.empty()
        ? "stopped, no samples"
        : "stopped";
}

void AppRunLoop::ClearRailCameraTuningRecording() {
    railCameraTuningRecorder_.samples.clear();
    railCameraTuningRecorder_.recordedSamples = 0;
    railCameraTuningRecorder_.droppedSamples = 0;
    railCameraTuningRecorder_.recordingTimeSeconds = 0.0f;
    railCameraTuningRecorder_.status = railCameraTuningRecorder_.recording ? "recording, cleared" : "cleared";
}

bool AppRunLoop::ExportRailCameraTuningCsv(std::string* outPath) {
    if (railCameraTuningRecorder_.samples.empty()) {
        railCameraTuningRecorder_.status = "export skipped, no samples";
        return false;
    }

    std::error_code ec;
    std::filesystem::create_directories("logs", ec);
    const std::string path = BuildRailTuningCsvPath();
    std::ofstream csv(path, std::ios::out | std::ios::trunc);
    if (!csv) {
        railCameraTuningRecorder_.status = "export failed";
        return false;
    }

    csv << "frame,timeSeconds,distance,sectionName,speedMode,speedReason,"
        << "baseSpeed,requestedSpeed,actualVehicleSpeed,zoneMultiplier,eventMultiplier,"
        << "cameraMode,cameraModeKind,comfortReason,fovYDeg,rollDeg,angularVelocityDeg,"
        << "angularAccelerationDeg,fovChangeRateDeg,linearSpeed,stabilityScore,shakeAmount,"
        << "stableForAiming,hardTransition,allowEnemyFire,aimFocusBlend,lookAtBlend,"
        << "compositionRisk,compositionSafetyBlend,compositionSafe,lineOfSightSafe,"
        << "cameraCollisionSafe,segmentTransitionActive,segmentTransitionBlend,"
        << "encounterFramingActive,encounterFramingBlend,encounterFramingSpread,"
        << "encounterFramingEnemyCount,encounterFramingBossCount,activeEnemies,"
        << "activeBullets,activeObstacles,lockTokenCount,lockHeld,normalShotHeld,"
        << "normalShotsFired,normalShotHits,playerDamage,updateMs,renderMs,presentMs\n";
    csv << std::fixed << std::setprecision(4);
    for (const RailCameraTuningSample& s : railCameraTuningRecorder_.samples) {
        csv << s.frame << ','
            << s.timeSeconds << ','
            << s.distance << ','
            << CsvEscape(s.sectionName) << ','
            << CsvEscape(s.speedMode) << ','
            << CsvEscape(s.speedReason) << ','
            << s.baseSpeed << ','
            << s.requestedSpeed << ','
            << s.actualVehicleSpeed << ','
            << s.zoneMultiplier << ','
            << s.eventMultiplier << ','
            << CsvEscape(s.cameraMode) << ','
            << CsvEscape(s.cameraModeKind) << ','
            << CsvEscape(s.comfortReason) << ','
            << s.fovYDeg << ','
            << s.rollDeg << ','
            << s.angularVelocityDeg << ','
            << s.angularAccelerationDeg << ','
            << s.fovChangeRateDeg << ','
            << s.linearSpeed << ','
            << s.stabilityScore << ','
            << s.shakeAmount << ','
            << BoolCsv(s.stableForAiming) << ','
            << BoolCsv(s.hardTransition) << ','
            << BoolCsv(s.allowEnemyFire) << ','
            << s.aimFocusBlend << ','
            << s.lookAtBlend << ','
            << s.compositionRisk << ','
            << s.compositionSafetyBlend << ','
            << BoolCsv(s.compositionSafe) << ','
            << BoolCsv(s.lineOfSightSafe) << ','
            << BoolCsv(s.cameraCollisionSafe) << ','
            << BoolCsv(s.segmentTransitionActive) << ','
            << s.segmentTransitionBlend << ','
            << BoolCsv(s.encounterFramingActive) << ','
            << s.encounterFramingBlend << ','
            << s.encounterFramingSpread << ','
            << s.encounterFramingEnemyCount << ','
            << s.encounterFramingBossCount << ','
            << s.activeEnemies << ','
            << s.activeBullets << ','
            << s.activeObstacles << ','
            << s.lockTokenCount << ','
            << BoolCsv(s.lockHeld) << ','
            << BoolCsv(s.normalShotHeld) << ','
            << s.normalShotsFired << ','
            << s.normalShotHits << ','
            << s.playerDamage << ','
            << s.updateMs << ','
            << s.renderMs << ','
            << s.presentMs << '\n';
    }

    railCameraTuningRecorder_.lastExportPath = path;
    railCameraTuningRecorder_.status = "exported " + path;
    if (outPath != nullptr) {
        *outPath = path;
    }
    return true;
}

void AppRunLoop::RecordRailCameraTuningSample(
    float deltaTime,
    const RailSpeedDirectorFrame& speedFrame,
    const RailCameraDirectorFrame& cameraFrame,
    const CourseCollisionFrameStats& collisionStats) {
    if (!railCameraTuningRecorder_.recording || !IsRailShooterSceneActive()) {
        return;
    }

    railCameraTuningRecorder_.recordingTimeSeconds += (std::max)(0.0f, deltaTime);
    const uint32_t stride = (std::max)(1u, railCameraTuningRecorder_.sampleStride);
    if ((railShooterFrameIndex_ % stride) != 0u) {
        return;
    }
    if (railCameraTuningRecorder_.samples.size() >= railCameraTuningRecorder_.maxSamples) {
        ++railCameraTuningRecorder_.droppedSamples;
        railCameraTuningRecorder_.status = "recording, buffer full";
        return;
    }

    RailCameraTuningSample sample{};
    sample.frame = railShooterFrameIndex_;
    sample.timeSeconds = railCameraTuningRecorder_.recordingTimeSeconds;
    sample.distance = railShooterDistance_;
    sample.sectionName = speedFrame.sectionName;
    sample.speedMode = speedFrame.modeName;
    sample.speedReason = speedFrame.reason;
    sample.baseSpeed = speedFrame.baseSpeed;
    sample.requestedSpeed = speedFrame.requestedSpeed;
    sample.actualVehicleSpeed = railShooterVehicleMovement_.State().speed;
    sample.zoneMultiplier = speedFrame.zoneMultiplier;
    sample.eventMultiplier = speedFrame.eventMultiplier;
    sample.cameraMode = cameraFrame.mode;
    sample.cameraModeKind = ToRailCameraDirectorModeString(cameraFrame.modeKind);
    sample.comfortReason = cameraFrame.comfortReason;
    sample.fovYDeg = cameraFrame.fovY * 180.0f / 3.14159265358979323846f;
    sample.rollDeg = cameraFrame.rollDeg;
    sample.angularVelocityDeg = cameraFrame.angularVelocityDeg;
    sample.angularAccelerationDeg = cameraFrame.angularAccelerationDeg;
    sample.fovChangeRateDeg = cameraFrame.fovChangeRateDeg;
    sample.linearSpeed = cameraFrame.linearSpeed;
    sample.stabilityScore = cameraFrame.stabilityScore;
    sample.shakeAmount = cameraFrame.shakeAmount;
    sample.stableForAiming = cameraFrame.stableForAiming;
    sample.hardTransition = cameraFrame.hardTransition;
    sample.allowEnemyFire = cameraFrame.allowEnemyFire;
    sample.aimFocusBlend = cameraFrame.aimFocusBlend;
    sample.lookAtBlend = cameraFrame.lookAtBlend;
    sample.compositionRisk = cameraFrame.compositionRisk;
    sample.compositionSafetyBlend = cameraFrame.compositionSafetyBlend;
    sample.compositionSafe = cameraFrame.compositionSafeForAiming;
    sample.lineOfSightSafe = cameraFrame.lineOfSightSafeForAiming;
    sample.cameraCollisionSafe = cameraFrame.cameraCollisionSafe;
    sample.segmentTransitionActive = cameraFrame.segmentTransitionActive;
    sample.segmentTransitionBlend = cameraFrame.segmentTransitionBlend;
    sample.encounterFramingActive = cameraFrame.encounterFramingActive;
    sample.encounterFramingBlend = cameraFrame.encounterFramingBlend;
    sample.encounterFramingSpread = cameraFrame.encounterFramingThreatSpread;
    sample.encounterFramingEnemyCount = cameraFrame.encounterFramingEnemyCount;
    sample.encounterFramingBossCount = cameraFrame.encounterFramingBossCount;
    sample.activeEnemies = static_cast<uint32_t>(railShooterSpawnRuntime_.ActiveEnemyCount());
    sample.activeBullets = static_cast<uint32_t>(railShooterSpawnRuntime_.ActiveBulletCount());
    sample.activeObstacles = static_cast<uint32_t>(railShooterSpawnRuntime_.ActiveObstacleCount());
    sample.lockTokenCount = static_cast<uint32_t>(railShooterLockOnSystem_.Tokens().size());
    sample.lockHeld = railShooterLockOnSystem_.Reticle().lockHeld;
    sample.normalShotHeld = railInputRouteDebug_.normalShotHeld;
    sample.normalShotsFired = collisionStats.playerShotsFired;
    sample.normalShotHits = collisionStats.playerShotWorldHits;
    sample.playerDamage = collisionStats.playerDamage;
    sample.updateMs = gRailPerfFrame.updateMs;
    sample.renderMs = gRailPerfFrame.renderMs;
    sample.presentMs = gRailPerfFrame.presentMs;

    railCameraTuningRecorder_.samples.push_back(std::move(sample));
    ++railCameraTuningRecorder_.recordedSamples;
    railCameraTuningRecorder_.status = "recording";
}

void AppRunLoop::ApplyRailShooterVisualPresets(float distance) {
    const CourseLightingPreset lighting = railShooterCourse_.EvaluateLightingPreset(distance);
    TerrainAuthoringState& terrain = runtimeState_.terrain;

    terrain.useCanyonSunLighting = true;
    terrain.canyonSunColor = lighting.sunColor;
    terrain.canyonSunDirection = NormalizeOr(lighting.sunDirection, {-0.38f, -0.52f, 0.76f});
    terrain.canyonSunIntensity = lighting.sunIntensity;

    runtimeState_.clearColor[0] = lighting.clearColor.x;
    runtimeState_.clearColor[1] = lighting.clearColor.y;
    runtimeState_.clearColor[2] = lighting.clearColor.z;
    runtimeState_.clearColor[3] = lighting.clearColor.w;

    for (PostProcessPass pass : vfxEngine_.PostProcess().Passes()) {
        if (pass.name != "DistanceFog") {
            continue;
        }
        pass.enabled = lighting.fogIntensity > 0.0f;
        pass.intensity = lighting.fogIntensity;
        pass.parameters.fogColorR = lighting.fogColor.x;
        pass.parameters.fogColorG = lighting.fogColor.y;
        pass.parameters.fogColorB = lighting.fogColor.z;
        pass.parameters.fogStart = lighting.fogStart;
        pass.parameters.fogEnd = (std::max)(lighting.fogEnd, lighting.fogStart + 1.0f);
        pass.parameters.fogDensity = lighting.fogDensity;
        pass.parameters.backlitFogLift = lighting.backlitFogLift;
        pass.parameters.openingGlowStrength = lighting.openingGlowStrength;
        pass.parameters.foregroundSilhouetteStrength = lighting.foregroundSilhouetteStrength;
        pass.parameters.lowFogLayerStrength = lighting.lowFogLayerStrength;
        pass.parameters.coolFloorHazeStrength = lighting.coolFloorHazeStrength;

        (void)vfxEngine_.PostProcess().ConfigurePass(pass);
    }

    const CourseTerrainMaterialPreset material =
        railShooterCourse_.EvaluateTerrainMaterialPreset(distance);
    terrain.materialBaseColor = material.baseColor;
    terrain.materialBrightness = material.brightness;
    terrain.materialNoiseStrength = material.noiseStrength;
    terrain.materialStrataStrength = material.strataStrength;
    terrain.materialStrataBreakupStrength = material.strataBreakupStrength;
    terrain.materialSpecularStrength = material.specularStrength;
    terrain.materialRimLightStrength = material.rimLightStrength;
    terrain.materialBacklightRimBoost = material.backlightRimBoost;
    terrain.materialFloorSandShadowStrength = material.floorSandShadowStrength;
    terrain.materialDetailNormalStrength = material.detailNormalStrength;
    terrain.materialMicroDetailStrength = material.microDetailStrength;
    terrain.materialCavityAoStrength = material.cavityAoStrength;
    terrain.materialSkyFillStrength = material.skyFillStrength;

    runtimeState_.materialData.color = {
        material.baseColor.x * material.brightness * 0.50f,
        material.baseColor.y * material.brightness * 0.46f,
        material.baseColor.z * material.brightness * 0.42f,
        1.0f,
    };
    runtimeState_.materialData.enableLighting = true;
    runtimeState_.materialData.shininess = (std::max)(6.0f, material.specularStrength * 72.0f);
    runtimeState_.materialData.environmentCoefficient = 0.025f;
    runtimeState_.materialData.specularMode = 1;
}


void AppRunLoop::BuildRailVisibilityDebugOverlay(
    editor::EditorViewportOverlayService& overlayService) {
#if defined(GE3_ENABLE_IMGUI) && GE3_ENABLE_IMGUI
    const RailVisibilityDebugOverlaySettings& overlay = railVisibilityDebugOverlay_;
    if (!overlay.enabled || !railShooterInitialized_ || railPath_.Length() <= 0.0f) {
        return;
    }

    const editor::EditorViewportOverlayFrameContext& frame = overlayService.FrameContext();
    const uint32_t renderWidth = (std::max)(1u, frame.viewport.renderWidth);
    const uint32_t renderHeight = (std::max)(1u, frame.viewport.renderHeight);
    const float width = static_cast<float>(renderWidth);
    const float height = static_cast<float>(renderHeight);
    const ImVec2 viewportCenter(width * 0.5f, height * 0.5f);
    auto safeFrame = overlayService.Sink(editor::EditorViewportOverlayLayerId::CameraSafeFrame);
    auto navigation = overlayService.Sink(editor::EditorViewportOverlayLayerId::CourseNavigation);
    auto labels = overlayService.Sink(editor::EditorViewportOverlayLayerId::ObjectLabels);
    auto helpers = overlayService.Sink(editor::EditorViewportOverlayLayerId::AuthoringHelpers);

    const auto visible = [width, height](const ImVec2& point, float margin) {
        return point.x >= -margin && point.y >= -margin &&
            point.x <= width + margin && point.y <= height + margin;
    };
    const auto pointInRect = [](const Vector2& point, const ImVec2& minimum, const ImVec2& maximum) {
        return point.x >= minimum.x && point.x <= maximum.x &&
            point.y >= minimum.y && point.y <= maximum.y;
    };
    const auto makeCenteredRect = [width, height](
        float normalizedWidth,
        float normalizedHeight,
        ImVec2& outMin,
        ImVec2& outMax) {
        const float halfWidth = width * (std::clamp)(normalizedWidth, 0.0f, 1.0f) * 0.5f;
        const float halfHeight = height * (std::clamp)(normalizedHeight, 0.0f, 1.0f) * 0.5f;
        outMin = ImVec2(width * 0.5f - halfWidth, height * 0.5f - halfHeight);
        outMax = ImVec2(width * 0.5f + halfWidth, height * 0.5f + halfHeight);
    };

    ImVec2 aimMin{};
    ImVec2 aimMax{};
    ImVec2 warningMin{};
    ImVec2 warningMax{};
    makeCenteredRect(overlay.aimableZoneWidth, overlay.aimableZoneHeight, aimMin, aimMax);
    makeCenteredRect(overlay.warningZoneWidth, overlay.warningZoneHeight, warningMin, warningMax);

    if (overlay.showAimableZone) {
        safeFrame.RectFilled(warningMin.x, warningMin.y, warningMax.x, warningMax.y, IM_COL32(255, 196, 80, 12));
        safeFrame.Rect(warningMin.x, warningMin.y, warningMax.x, warningMax.y, IM_COL32(255, 196, 80, 120), 1.4f);
        safeFrame.RectFilled(aimMin.x, aimMin.y, aimMax.x, aimMax.y, IM_COL32(52, 232, 255, 16));
        safeFrame.Rect(aimMin.x, aimMin.y, aimMax.x, aimMax.y, IM_COL32(80, 238, 255, 190), 2.0f);
        safeFrame.Line(viewportCenter.x - 18.0f, viewportCenter.y, viewportCenter.x + 18.0f, viewportCenter.y, IM_COL32(120, 244, 255, 115), 1.2f);
        safeFrame.Line(viewportCenter.x, viewportCenter.y - 18.0f, viewportCenter.x, viewportCenter.y + 18.0f, IM_COL32(120, 244, 255, 115), 1.2f);
        if (overlay.showLabels) {
            editor::EditorViewportOverlayItemOptions labelOptions{};
            labelOptions.priority = 80;
            labelOptions.iconFallback = false;
            safeFrame.Label(aimMin.x + 8.0f, aimMin.y + 6.0f, "AIMABLE", IM_COL32(140, 248, 255, 210), labelOptions);
            safeFrame.Label(warningMin.x + 8.0f, warningMin.y + 6.0f, "READABILITY", IM_COL32(255, 211, 96, 185), labelOptions);
        }
    }

    const float pulse = 0.5f + 0.5f * std::sin(static_cast<float>(railShooterFrameIndex_) * 0.16f);
    const auto submitMarker = [&navigation, pulse](const ImVec2& position, float radius, ImU32 color, bool filled) {
        if (filled) navigation.CircleFilled(position.x, position.y, radius + 2.0f + pulse * 1.5f, color);
        navigation.Circle(position.x, position.y, radius + 4.0f, color, 1.7f);
        navigation.Line(position.x - radius, position.y, position.x + radius, position.y, color, 1.2f);
        navigation.Line(position.x, position.y - radius, position.x, position.y + radius, color, 1.2f);
    };
    const auto project = [&frame](const Vector3& world) {
        RailOverlayProjectedPoint result{};
        if (frame.coordinates == nullptr) return result;
        const editor::EditorViewportProjectedPoint projected = frame.coordinates->ProjectWorld(world);
        result.screen = Vector2{projected.render.x, projected.render.y};
        result.depth = projected.depth;
        result.behind = projected.behind;
        result.inDepth = projected.inDepth;
        result.onscreen = projected.onscreen;
        return result;
    };

    if (overlay.showActors) {
        for (const CourseEnemyActor& enemy : railShooterSpawnRuntime_.Enemies()) {
            const Vector3 center = RailLocalPoint(
                railPath_, enemy.desc.spawnDistance, enemy.desc.lateralOffset,
                enemy.desc.verticalOffset, enemy.desc.distanceOffset);
            const RailOverlayProjectedPoint projected = project(center);
            if (!projected.inDepth || !visible(ImVec2(projected.screen.x, projected.screen.y), 88.0f)) continue;

            const ImVec2 position(projected.screen.x, projected.screen.y);
            const bool inAimable = projected.onscreen && pointInRect(projected.screen, aimMin, aimMax);
            const bool inWarning = projected.onscreen && pointInRect(projected.screen, warningMin, warningMax);
            const ImU32 color =
                inAimable && enemy.fireSafetyAllowed ? IM_COL32(88, 255, 176, 235) :
                inAimable ? IM_COL32(255, 214, 82, 235) :
                inWarning ? IM_COL32(255, 160, 70, 210) : IM_COL32(255, 78, 72, 185);
            const float radius = inAimable ? 9.0f : (inWarning ? 7.0f : 5.5f);
            submitMarker(position, radius, color, enemy.fireSafetyAllowed);
            if (!inAimable) {
                navigation.Line(position.x, position.y, viewportCenter.x, viewportCenter.y, IM_COL32(255, 170, 70, 72));
            }
            if (overlay.showLabels) {
                const float forwardDistance =
                    enemy.desc.spawnDistance + enemy.desc.distanceOffset - railShooterDistance_;
                char label[192]{};
                std::snprintf(
                    label, sizeof(label), "E%u %s %.0fm %s", enemy.actorId,
                    enemy.desc.role.c_str(), forwardDistance, enemy.fireSafetyReason.c_str());
                editor::EditorViewportOverlayItemOptions options{};
                options.distance = std::fabs(forwardDistance);
                options.minZoom = 0.60f;
                options.priority = inAimable ? 50 : 10;
                labels.Label(position.x + 10.0f, position.y - 8.0f, label, color, options);
            }
        }

        for (const CourseObstacleActor& obstacle : railShooterSpawnRuntime_.Obstacles()) {
            const Vector3 center = RailLocalPoint(
                railPath_, obstacle.desc.spawnDistance, obstacle.desc.lateralOffset,
                obstacle.desc.verticalOffset, obstacle.desc.distanceOffset);
            const RailOverlayProjectedPoint projected = project(center);
            if (!projected.inDepth || !visible(ImVec2(projected.screen.x, projected.screen.y), 88.0f)) continue;

            const ImVec2 position(projected.screen.x, projected.screen.y);
            const bool inAimable = projected.onscreen && pointInRect(projected.screen, aimMin, aimMax);
            const bool inWarning = projected.onscreen && pointInRect(projected.screen, warningMin, warningMax);
            const ImU32 color = inAimable ? IM_COL32(108, 208, 255, 210) :
                inWarning ? IM_COL32(255, 184, 76, 185) : IM_COL32(255, 92, 72, 150);
            const float radius = inAimable ? 8.0f : 6.0f;
            navigation.Rect(
                position.x - radius, position.y - radius,
                position.x + radius, position.y + radius, color, 1.6f);
            if (overlay.showLabels) {
                const float forwardDistance =
                    obstacle.desc.spawnDistance + obstacle.desc.distanceOffset - railShooterDistance_;
                char label[96]{};
                std::snprintf(label, sizeof(label), "O%u %.0fm", obstacle.actorId, forwardDistance);
                editor::EditorViewportOverlayItemOptions options{};
                options.distance = std::fabs(forwardDistance);
                options.minZoom = 0.60f;
                options.priority = inAimable ? 40 : 5;
                labels.Label(position.x + 9.0f, position.y - 7.0f, label, color, options);
            }
        }
    }

    if (overlay.showThreatCenter) {
        const RailCameraDirectorFrame& cameraFrame = railShooterCameraDirector_.LastFrame();
        const auto submitWorldHelper = [&helpers, &project, &visible, viewportCenter](
            const Vector3& world, const char* label, ImU32 color) {
            const RailOverlayProjectedPoint projected = project(world);
            const ImVec2 position(projected.screen.x, projected.screen.y);
            if (!projected.inDepth || !visible(position, 64.0f)) return;
            helpers.Rect(position.x - 6.0f, position.y - 6.0f, position.x + 6.0f, position.y + 6.0f, color, 1.5f);
            helpers.Line(viewportCenter.x, viewportCenter.y, position.x, position.y, IM_COL32(160, 210, 255, 70));
            editor::EditorViewportOverlayItemOptions options{};
            options.priority = 70;
            helpers.Label(position.x + 8.0f, position.y - 7.0f, label, color, options);
        };
        submitWorldHelper(cameraFrame.baseTarget, "BASE", IM_COL32(155, 190, 255, 170));
        submitWorldHelper(cameraFrame.threatCenter, "THREAT", IM_COL32(255, 120, 215, 220));
    }
#else
    (void)overlayService;
#endif
}

bool AppRunLoop::EnsureRailLockOnHudAtlas(ID3D12GraphicsCommandList* commandList) {
    constexpr uint32_t kDescriptorIndex = 18;
    constexpr uint32_t kMaxAtlasVertices = 16384;
    ComPtr<ID3D12Device> device = dev_.GetDevice();

    if (railLockOnHudAtlasVertexResource_ == nullptr) {
        railLockOnHudAtlasVertexResource_ = CreateBufferResource(
            device,
            sizeof(RailHudAtlasVertex) * kMaxAtlasVertices);
        if (railLockOnHudAtlasVertexResource_ == nullptr) {
            return false;
        }
        if (FAILED(railLockOnHudAtlasVertexResource_->Map(
                0,
                nullptr,
                reinterpret_cast<void**>(&railLockOnHudAtlasMappedVertices_))) ||
            railLockOnHudAtlasMappedVertices_ == nullptr) {
            return false;
        }
        railLockOnHudAtlasVertexBufferView_.BufferLocation =
            railLockOnHudAtlasVertexResource_->GetGPUVirtualAddress();
        railLockOnHudAtlasVertexBufferView_.SizeInBytes =
            sizeof(RailHudAtlasVertex) * kMaxAtlasVertices;
        railLockOnHudAtlasVertexBufferView_.StrideInBytes = sizeof(RailHudAtlasVertex);
    }

    if (railLockOnHudAtlasReady_) {
        return true;
    }
    if (commandList == nullptr || srvDescriptorHeap_ == nullptr || device == nullptr) {
        return false;
    }

    DirectX::ScratchImage atlasImage;
    if (FAILED(atlasImage.Initialize2D(
            DXGI_FORMAT_R8G8B8A8_UNORM,
            kRailHudAtlasWidth,
            kRailHudAtlasHeight,
            1,
            1))) {
        return false;
    }
    const DirectX::Image* image = atlasImage.GetImage(0, 0, 0);
    if (image == nullptr || image->pixels == nullptr) {
        return false;
    }
    std::memset(image->pixels, 0, image->slicePitch);

    auto writePixel = [&](int x, int y, uint8_t alpha) {
        if (x < 0 || y < 0 ||
            x >= static_cast<int>(kRailHudAtlasWidth) ||
            y >= static_cast<int>(kRailHudAtlasHeight)) {
            return;
        }
        uint8_t* p = image->pixels + static_cast<size_t>(y) * image->rowPitch + static_cast<size_t>(x) * 4u;
        p[0] = 255;
        p[1] = 255;
        p[2] = 255;
        p[3] = alpha;
    };
    auto fillRect = [&](int x, int y, int w, int h, uint8_t alpha) {
        for (int yy = y; yy < y + h; ++yy) {
            for (int xx = x; xx < x + w; ++xx) {
                writePixel(xx, yy, alpha);
            }
        }
    };
    auto fillCircle = [&](int cx, int cy, int radius) {
        const float r = static_cast<float>((std::max)(radius, 1));
        for (int y = cy - radius; y <= cy + radius; ++y) {
            for (int x = cx - radius; x <= cx + radius; ++x) {
                const float dx = static_cast<float>(x - cx);
                const float dy = static_cast<float>(y - cy);
                const float d = std::sqrt(dx * dx + dy * dy);
                const float edge = (std::clamp)((r - d) / 3.0f, 0.0f, 1.0f);
                const float body = d <= r ? 1.0f : 0.0f;
                writePixel(x, y, static_cast<uint8_t>(255.0f * body * edge));
            }
        }
    };
    auto drawSegmentDigit = [&](int digit, int x, int y) {
        static constexpr bool kSegments[10][7] = {
            {true, true, true, false, true, true, true},
            {false, false, true, false, false, true, false},
            {true, false, true, true, true, false, true},
            {true, false, true, true, false, true, true},
            {false, true, true, true, false, true, false},
            {true, true, false, true, false, true, true},
            {true, true, false, true, true, true, true},
            {true, false, true, false, false, true, false},
            {true, true, true, true, true, true, true},
            {true, true, true, true, false, true, true},
        };
        const bool* s = kSegments[(std::clamp)(digit, 0, 9)];
        if (s[0]) fillRect(x + 2, y + 1, 7, 2, 255);
        if (s[1]) fillRect(x + 1, y + 3, 2, 5, 255);
        if (s[2]) fillRect(x + 8, y + 3, 2, 5, 255);
        if (s[3]) fillRect(x + 2, y + 8, 7, 2, 255);
        if (s[4]) fillRect(x + 1, y + 10, 2, 5, 255);
        if (s[5]) fillRect(x + 8, y + 10, 2, 5, 255);
        if (s[6]) fillRect(x + 2, y + 15, 7, 2, 255);
    };
    fillRect(0, 112, 8, 8, 255);
    fillCircle(32, 104, 15);
    fillCircle(72, 104, 15);
    fillCircle(112, 104, 11);
    for (int digit = 0; digit < 10; ++digit) {
        drawSegmentDigit(digit, digit * 12, 0);
    }
    // Bake the bundled M PLUS Rounded 1c Medium (weight 500 / 中字) directly
    // into the native HUD atlas. This is not an ImGui font atlas: the TTF is
    // rasterized once and rendered by UI.SubmissionLiveVisualizer.
    constexpr int kSubmissionFontFirstCodepoint = 32;
    constexpr int kSubmissionFontGlyphCount = 95;
    constexpr int kSubmissionFontBitmapHeight = 384;
    constexpr int kSubmissionFontAtlasY = 128;
    constexpr float kSubmissionFontPixelHeight = 26.0f;
    static_assert(
        kSubmissionFontAtlasY + kSubmissionFontBitmapHeight <=
        static_cast<int>(kRailHudAtlasHeight),
        "The native HUD font region must fit inside the shared atlas");
    std::vector<int> submissionJapaneseCodepoints{
        // Title: レールであばレール (で is already in the HUD set below).
        0x3042, 0x3070, 0x30EB, 0x30EC, 0x30FC, 0x529B,
        0x304B, 0x3051, 0x3059, 0x3063, 0x3066, 0x3067, 0x3068, 0x306E, 0x3078, 0x307E, 0x308A, 0x308D,
        0x3092, 0x4E00, 0x4E0B, 0x4E0D, 0x4E2D, 0x4E3B, 0x4E45, 0x4E86, 0x4E88, 0x4ECA, 0x4EFB, 0x4F53,
        0x505C, 0x5099, 0x50B7, 0x5168, 0x518D, 0x524D, 0x529F, 0x52D9, 0x533A, 0x5371, 0x53EF, 0x53F3,
        0x544A, 0x56DE, 0x56E0, 0x5730, 0x586B, 0x58CA, 0x5931, 0x5A01, 0x5B89, 0x5B8C, 0x5BB3, 0x5C04,
        0x5DE6, 0x5E2F, 0x5EA6, 0x5F3E, 0x5F62, 0x5F85, 0x5F8C, 0x5F97, 0x5FA1, 0x610F, 0x6210, 0x6212,
        0x6226, 0x6311, 0x63A5, 0x6483, 0x653B, 0x6557, 0x6575, 0x6642, 0x679C, 0x6A5F, 0x6B62, 0x6B63,
        0x6B8B, 0x6CE2, 0x6CE8, 0x6E96, 0x6EC5, 0x70B9, 0x7121, 0x71B1, 0x7269, 0x767A, 0x76F4, 0x7740,
        0x7832, 0x7834, 0x78BA, 0x7A81, 0x7D42, 0x7D50, 0x7D9A, 0x8010, 0x8105, 0x885D, 0x88AB, 0x88C5,
        0x89E6, 0x8A8D, 0x8B66, 0x8ECA, 0x8FCE, 0x8FD1, 0x901A, 0x901F, 0x9023, 0x9032, 0x904E, 0x907F,
        0x958B, 0x9593, 0x95D8, 0x9632, 0x963B, 0x9650, 0x967A, 0x969C, 0x9762,
        0x3057, 0x308B, 0x30A6, 0x30AA, 0x30AF, 0x30B2, 0x30B9, 0x30C3, 0x30DE, 0x30E0, 0x30EA, 0x30ED, 0x30F3, 0x4F5C, 0x52D5, 0x59CB, 0x5B9A, 0x623B, 0x629E, 0x62BC, 0x64CD, 0x6589, 0x660E, 0x6C7A, 0x7167, 0x79FB, 0x8AAC, 0x9078, 0x9577, 0x96E2,
    };
    submissionHudFontReady_ = false;
    submissionHudGlyphs_ = {};
    std::ifstream fontFile(
        "Resources/Editor/Fonts/MPLUSRounded1c-Medium.ttf",
        std::ios::binary | std::ios::ate);
    if (fontFile) {
        const std::streamsize byteCount = fontFile.tellg();
        if (byteCount > 0) {
            fontFile.seekg(0, std::ios::beg);
            std::vector<unsigned char> fontBytes(static_cast<size_t>(byteCount));
            std::vector<unsigned char> fontBitmap(
                static_cast<size_t>(kRailHudAtlasWidth) *
                    kSubmissionFontBitmapHeight,
                0);
            std::array<stbtt_packedchar, kSubmissionFontGlyphCount> ascii{};
            std::vector<stbtt_packedchar> japanese(
                submissionJapaneseCodepoints.size());
            bool packed = false;
            if (fontFile.read(
                    reinterpret_cast<char*>(fontBytes.data()), byteCount)) {
                stbtt_pack_context context{};
                if (stbtt_PackBegin(
                        &context,
                        fontBitmap.data(),
                        static_cast<int>(kRailHudAtlasWidth),
                        kSubmissionFontBitmapHeight,
                        0,
                        1,
                        nullptr) != 0) {
                    stbtt_PackSetOversampling(&context, 1, 1);
                    std::array<stbtt_pack_range, 2> ranges{};
                    ranges[0].font_size = kSubmissionFontPixelHeight;
                    ranges[0].first_unicode_codepoint_in_range =
                        kSubmissionFontFirstCodepoint;
                    ranges[0].num_chars = kSubmissionFontGlyphCount;
                    ranges[0].chardata_for_range = ascii.data();
                    ranges[1].font_size = kSubmissionFontPixelHeight;
                    ranges[1].array_of_unicode_codepoints =
                        submissionJapaneseCodepoints.data();
                    ranges[1].num_chars = static_cast<int>(
                            submissionJapaneseCodepoints.size());
                    ranges[1].chardata_for_range = japanese.data();
                    packed = stbtt_PackFontRanges(
                        &context,
                        fontBytes.data(),
                        0,
                        ranges.data(),
                        static_cast<int>(ranges.size())) != 0;
                    stbtt_PackEnd(&context);
                }
            }
            if (packed) {
                for (int y = 0; y < kSubmissionFontBitmapHeight; ++y) {
                    for (int x = 0;
                         x < static_cast<int>(kRailHudAtlasWidth);
                         ++x) {
                        const uint8_t alpha = fontBitmap[
                            static_cast<size_t>(y) * kRailHudAtlasWidth +
                            static_cast<size_t>(x)];
                        if (alpha != 0) {
                            writePixel(x, kSubmissionFontAtlasY + y, alpha);
                        }
                    }
                }
                const auto addPackedGlyph = [&]
                    (uint32_t codepoint, const stbtt_packedchar& source) {
                    SubmissionHudGlyph destination{};
                    destination.codepoint = codepoint;
                    destination.uv = {
                        static_cast<float>(source.x0) / kRailHudAtlasWidth,
                        static_cast<float>(source.y0 + kSubmissionFontAtlasY) /
                            kRailHudAtlasHeight,
                        static_cast<float>(source.x1) / kRailHudAtlasWidth,
                        static_cast<float>(source.y1 + kSubmissionFontAtlasY) /
                            kRailHudAtlasHeight};
                    destination.size = {
                        static_cast<float>(source.x1 - source.x0),
                        static_cast<float>(source.y1 - source.y0)};
                    destination.offset = {source.xoff, source.yoff};
                    destination.advance = source.xadvance;
                    destination.valid = source.x1 > source.x0 && source.y1 > source.y0;
                    submissionHudGlyphs_.push_back(destination);
                };
                submissionHudGlyphs_.reserve(
                    ascii.size() + japanese.size());
                for (int glyphIndex = 0;
                     glyphIndex < kSubmissionFontGlyphCount;
                     ++glyphIndex) {
                    addPackedGlyph(
                        static_cast<uint32_t>(
                            kSubmissionFontFirstCodepoint + glyphIndex),
                        ascii[glyphIndex]);
                }
                for (size_t glyphIndex = 0;
                     glyphIndex < submissionJapaneseCodepoints.size();
                     ++glyphIndex) {
                    addPackedGlyph(
                        static_cast<uint32_t>(
                            submissionJapaneseCodepoints[glyphIndex]),
                        japanese[glyphIndex]);
                }
                submissionHudFontReady_ = true;
            }
        }
    }

    // Bake the RGBA logo into the same atlas, so it participates in the normal
    // HUD draw ordering, menu fade and start-transition blackout.
    railTitleLogoReady_ = false;
    DirectX::ScratchImage titleSource, titleRgba, titleResized;
    if (SUCCEEDED(DirectX::LoadFromWICFile(
            L"Resources/UI/rail_title_logo.png",
            DirectX::WIC_FLAGS_FORCE_RGB | DirectX::WIC_FLAGS_IGNORE_SRGB,
            nullptr, titleSource))) {
        const DirectX::Image* source = titleSource.GetImage(0,0,0);
        if (source != nullptr && source->format != DXGI_FORMAT_R8G8B8A8_UNORM) {
            if (SUCCEEDED(DirectX::Convert(*source, DXGI_FORMAT_R8G8B8A8_UNORM,
                    DirectX::TEX_FILTER_DEFAULT, DirectX::TEX_THRESHOLD_DEFAULT, titleRgba)))
                source = titleRgba.GetImage(0,0,0);
            else source = nullptr;
        }
        if (source != nullptr && SUCCEEDED(DirectX::Resize(*source,
                kTitleLogoAtlasWidth, kTitleLogoAtlasHeight,
                DirectX::TEX_FILTER_FANT, titleResized))) {
            const DirectX::Image* logo = titleResized.GetImage(0,0,0);
            if (logo != nullptr) {
                for (uint32_t y=0; y<kTitleLogoAtlasHeight; ++y) {
                    std::memcpy(image->pixels+(y+kTitleLogoAtlasY)*image->rowPitch+kTitleLogoAtlasX*4u,
                        logo->pixels+y*logo->rowPitch,kTitleLogoAtlasWidth*4u);
                }
                railTitleLogoReady_ = true;
            }
        }
    }
    OutputDebugStringA(railTitleLogoReady_ ? "[RailTitleLogo] Loaded RGBA logo.\n" :
        "[RailTitleLogo] Image unavailable; using text fallback.\n");

    railLockOnHudAtlasTexture_ =
        AppRenderResources::CreateTextureResource(device, atlasImage.GetMetadata());
    if (railLockOnHudAtlasTexture_ == nullptr) {
        return false;
    }
    AppRenderResources::UploadTextureData(
        device,
        commandList,
        railLockOnHudAtlasTexture_,
        atlasImage,
        railLockOnHudAtlasUploadResources_);

    const uint32_t descriptorSize = device->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;
    const D3D12_CPU_DESCRIPTOR_HANDLE cpu =
        AppRenderResources::GetCPUDescriptorHandle(srvDescriptorHeap_, descriptorSize, kDescriptorIndex);
    railLockOnHudAtlasSrvGpu_ =
        AppRenderResources::GetGPUDescriptorHandle(srvDescriptorHeap_, descriptorSize, kDescriptorIndex);
    device->CreateShaderResourceView(railLockOnHudAtlasTexture_.Get(), &srvDesc, cpu);
    railLockOnHudAtlasReady_ = railLockOnHudAtlasSrvGpu_.ptr != 0;
    return railLockOnHudAtlasReady_;
}

bool AppRunLoop::BuildRailLockOnHudAtlasQuads() {
    constexpr uint32_t kMaxAtlasVertices = 16384;
    railLockOnHudAtlasVertexCount_ = 0;
    railTitleDustVertexCount_ = 0;
    railTitleTracerVertexCount_ = 0;
    const RenderViewportMetrics hudMetrics =
        ResolveRenderViewportMetrics(
            imguiLayer_.EditorViewportRenderTargetState(),
            windowWidth_,
            windowHeight_);
    const uint32_t hudWidth = hudMetrics.width;
    const uint32_t hudHeight = hudMetrics.height;
    if (railLockOnHudAtlasMappedVertices_ == nullptr || hudWidth == 0 || hudHeight == 0) {
        return false;
    }

    const auto uv = [](float x, float y, float w, float h) {
        return Vector4{
            x / static_cast<float>(kRailHudAtlasWidth),
            y / static_cast<float>(kRailHudAtlasHeight),
            (x + w) / static_cast<float>(kRailHudAtlasWidth),
            (y + h) / static_cast<float>(kRailHudAtlasHeight)};
    };
    // The title backdrop must stay fully opaque at the viewport edges. Sample
    // inside the solid texel block instead of filtering its transparent border.
    const Vector4 uvWhite = railTitleScreenVisible_
        ? uv(3.5f, 115.5f, 0.0f, 0.0f)
        : uv(0.0f, 112.0f, 8.0f, 8.0f);
    const Vector4 uvCircle = uv(16.0f, 88.0f, 32.0f, 32.0f);
    const Vector4 uvGlow = uv(56.0f, 88.0f, 32.0f, 32.0f);
    const Vector4 uvPip = uv(101.0f, 93.0f, 22.0f, 22.0f);

    const auto clip = [&](float x, float y) {
        return Vector4{
            x / static_cast<float>(hudWidth) * 2.0f - 1.0f,
            1.0f - y / static_cast<float>(hudHeight) * 2.0f,
            0.0f,
            1.0f};
    };
    auto push = [&](float x, float y, float u, float v, const Vector4& color) {
        if (railLockOnHudAtlasVertexCount_ >= kMaxAtlasVertices) {
            return;
        }
        railLockOnHudAtlasMappedVertices_[railLockOnHudAtlasVertexCount_++] = {
            clip(x, y),
            {u, v},
            color};
    };
    auto addQuad = [&](float x, float y, float w, float h, const Vector4& rect, const Vector4& color) {
        if (w <= 0.0f || h <= 0.0f || railLockOnHudAtlasVertexCount_ + 6 > kMaxAtlasVertices) {
            return;
        }
        const float x0 = x;
        const float y0 = y;
        const float x1 = x + w;
        const float y1 = y + h;
        push(x0, y0, rect.x, rect.y, color);
        push(x1, y0, rect.z, rect.y, color);
        push(x0, y1, rect.x, rect.w, color);
        push(x0, y1, rect.x, rect.w, color);
        push(x1, y0, rect.z, rect.y, color);
        push(x1, y1, rect.z, rect.w, color);
    };
    auto addQuadCorners = [&](const Vector2& a, const Vector2& b, const Vector2& c, const Vector2& d, const Vector4& rect, const Vector4& color) {
        if (railLockOnHudAtlasVertexCount_ + 6 > kMaxAtlasVertices) {
            return;
        }
        push(a.x, a.y, rect.x, rect.y, color);
        push(b.x, b.y, rect.z, rect.y, color);
        push(c.x, c.y, rect.x, rect.w, color);
        push(c.x, c.y, rect.x, rect.w, color);
        push(b.x, b.y, rect.z, rect.y, color);
        push(d.x, d.y, rect.z, rect.w, color);
    };
    auto addCentered = [&](const Vector2& center, float size, const Vector4& rect, const Vector4& color) {
        addQuad(center.x - size * 0.5f, center.y - size * 0.5f, size, size, rect, color);
    };
    auto addCenteredRect = [&](const Vector2& center, float w, float h, const Vector4& rect, const Vector4& color) {
        addQuad(center.x - w * 0.5f, center.y - h * 0.5f, w, h, rect, color);
    };
    auto addGlyph = [&](char c, float x, float y, float scale, const Vector4& color) {
        if (c < '0' || c > '9') {
            return;
        }
        const Vector4 rect = uv(static_cast<float>(c - '0') * 12.0f, 0.0f, 11.0f, 18.0f);
        addQuad(x, y, 11.0f * scale, 18.0f * scale, rect, color);
    };
    auto addNumber = [&](int value, const Vector2& center, float scale, const Vector4& color) {
        const int clamped = (std::clamp)(value, 0, 99);
        char text[3] = {};
        if (clamped >= 10) {
            text[0] = static_cast<char>('0' + clamped / 10);
            text[1] = static_cast<char>('0' + clamped % 10);
        } else {
            text[0] = static_cast<char>('0' + clamped);
        }
        const float width = static_cast<float>(std::strlen(text)) * 12.0f * scale;
        float cursor = center.x - width * 0.5f;
        for (const char* ch = text; *ch != '\0'; ++ch) {
            addGlyph(*ch, cursor, center.y - 9.0f * scale, scale, color);
            cursor += 12.0f * scale;
        }
    };
    const auto nextUtf8Codepoint = [](std::string_view value, size_t& offset) {
        if (offset >= value.size()) return uint32_t{0};
        const auto byte = [&](size_t index) {
            return static_cast<uint32_t>(
                static_cast<unsigned char>(value[index]));
        };
        const uint32_t lead = byte(offset++);
        if (lead < 0x80u) return lead;
        const auto continuation = [&](uint32_t& result) {
            if (offset >= value.size()) return false;
            const uint32_t next = byte(offset);
            if ((next & 0xC0u) != 0x80u) return false;
            ++offset;
            result = (result << 6u) | (next & 0x3Fu);
            return true;
        };
        uint32_t result = 0;
        int remaining = 0;
        if ((lead & 0xE0u) == 0xC0u) {
            result = lead & 0x1Fu;
            remaining = 1;
        } else if ((lead & 0xF0u) == 0xE0u) {
            result = lead & 0x0Fu;
            remaining = 2;
        } else if ((lead & 0xF8u) == 0xF0u) {
            result = lead & 0x07u;
            remaining = 3;
        } else {
            return uint32_t{0xFFFD};
        }
        for (int index = 0; index < remaining; ++index) {
            if (!continuation(result)) return uint32_t{0xFFFD};
        }
        return result;
    };
    const auto findHudGlyph = [&](uint32_t codepoint)
        -> const SubmissionHudGlyph* {
        const auto found = std::find_if(
            submissionHudGlyphs_.begin(), submissionHudGlyphs_.end(),
            [codepoint](const SubmissionHudGlyph& glyph) {
                return glyph.codepoint == codepoint;
            });
        return found != submissionHudGlyphs_.end() ? &*found : nullptr;
    };
    auto measureText = [&](const std::string& text, float scale) {
        float width = 0.0f;
        if (!submissionHudFontReady_) return width;
        size_t offset = 0;
        while (offset < text.size()) {
            const SubmissionHudGlyph* glyph = findHudGlyph(
                nextUtf8Codepoint(text, offset));
            if (glyph != nullptr) width += glyph->advance * scale;
        }
        return width;
    };
    auto addText = [&](const std::string& text, float x, float baseline,
                       float scale, const Vector4& color) {
        if (!submissionHudFontReady_) return;
        float cursor = x;
        size_t offset = 0;
        while (offset < text.size()) {
            const SubmissionHudGlyph* glyph = findHudGlyph(
                nextUtf8Codepoint(text, offset));
            if (glyph == nullptr) continue;
            if (glyph->valid) {
                addQuad(
                    cursor + glyph->offset.x * scale,
                    baseline + glyph->offset.y * scale,
                    glyph->size.x * scale,
                    glyph->size.y * scale,
                    glyph->uv,
                    color);
            }
            cursor += glyph->advance * scale;
        }
    };
    auto addCenteredText = [&](const std::string& text, float centerX,
                               float baseline, float scale, const Vector4& color) {
        addText(text, centerX - measureText(text, scale) * 0.5f,
                baseline, scale, color);
    };
    const auto validPoint = [](const Vector2& p) {
        return std::isfinite(p.x) && std::isfinite(p.y);
    };
    auto addLine = [&](const Vector2& start, const Vector2& end, float thickness, const Vector4& color) {
        if (!validPoint(start) || !validPoint(end) || thickness <= 0.0f) {
            return;
        }
        const float dx = end.x - start.x;
        const float dy = end.y - start.y;
        const float length = std::sqrt(dx * dx + dy * dy);
        if (length <= 0.0001f) {
            return;
        }
        const float nx = -dy / length * thickness * 0.5f;
        const float ny = dx / length * thickness * 0.5f;
        addQuadCorners(
            {start.x + nx, start.y + ny},
            {end.x + nx, end.y + ny},
            {start.x - nx, start.y - ny},
            {end.x - nx, end.y - ny},
            uvWhite,
            color);
    };
    auto addSoftLine = [&](const Vector2& start, const Vector2& end, float thickness, const Vector4& color) {
        addLine(start, end, thickness * 2.4f, Vector4{color.x, color.y, color.z, color.w * 0.16f});
        addLine(start, end, thickness, color);
    };
    auto addCircleLine = [&](const Vector2& center, float radius, float thickness, const Vector4& color, int segments) {
        if (!validPoint(center) || radius <= 0.0f || thickness <= 0.0f) {
            return;
        }
        constexpr float kTau = 6.28318530717958647692f;
        const int safeSegments = (std::clamp)(segments, 8, 48);
        Vector2 previous{center.x + radius, center.y};
        for (int index = 1; index <= safeSegments; ++index) {
            const float t = kTau * static_cast<float>(index) / static_cast<float>(safeSegments);
            Vector2 current{center.x + std::cos(t) * radius, center.y + std::sin(t) * radius};
            addLine(previous, current, thickness, color);
            previous = current;
        }
    };
    auto addCross = [&](const Vector2& center, float radius, float thickness, const Vector4& color) {
        addLine({center.x - radius, center.y}, {center.x + radius, center.y}, thickness, color);
        addLine({center.x, center.y - radius}, {center.x, center.y + radius}, thickness, color);
    };
    auto addBracket = [&](const Vector2& center, float radius, float length, float thickness, const Vector4& color) {
        const float inner = radius;
        const float outer = radius + length;
        addLine({center.x - outer, center.y - inner}, {center.x - inner, center.y - inner}, thickness, color);
        addLine({center.x - inner, center.y - outer}, {center.x - inner, center.y - inner}, thickness, color);
        addLine({center.x + inner, center.y - inner}, {center.x + outer, center.y - inner}, thickness, color);
        addLine({center.x + inner, center.y - outer}, {center.x + inner, center.y - inner}, thickness, color);
        addLine({center.x - outer, center.y + inner}, {center.x - inner, center.y + inner}, thickness, color);
        addLine({center.x - inner, center.y + inner}, {center.x - inner, center.y + outer}, thickness, color);
        addLine({center.x + inner, center.y + inner}, {center.x + outer, center.y + inner}, thickness, color);
        addLine({center.x + inner, center.y + inner}, {center.x + inner, center.y + outer}, thickness, color);
    };
    auto addTickedRing = [&](const Vector2& center, float radius, int ticks, float thickness, const Vector4& color) {
        addCircleLine(center, radius, thickness, color, 36);
        constexpr float kTau = 6.28318530717958647692f;
        const int safeTicks = (std::clamp)(ticks, 1, 8);
        for (int index = 0; index < safeTicks; ++index) {
            const float t = kTau * static_cast<float>(index) / static_cast<float>(safeTicks);
            const float c = std::cos(t);
            const float s = std::sin(t);
            addLine(
                {center.x + c * (radius - 4.0f), center.y + s * (radius - 4.0f)},
                {center.x + c * (radius + 5.0f), center.y + s * (radius + 5.0f)},
                thickness,
                color);
        }
    };

    if (!railShooterInitialized_) {
        return false;
    }

    const RailLockDebugFrame& debug = railShooterLockOnSystem_.DebugFrame();
    const RailReticleState& reticle = debug.reticle;
    const RailLockSettings& settings = railShooterLockOnSystem_.Settings();
    const int maxLocks = (std::max)(1, settings.maxLocks);
    const int tokenCount = static_cast<int>(debug.tokens.size());
    const bool maxLock = tokenCount >= maxLocks;
    const float acquirePulse = debug.acceptedThisFrame > 0 ? 1.0f : 0.0f;
    const float pulse = 0.5f + 0.5f * std::sin(static_cast<float>(railShooterFrameIndex_) * 0.18f);
    const float baseScale = settings.lockHudPolishEnabled
        ? (std::clamp)(settings.lockHudScale, 0.65f, 1.7f)
        : 1.0f;
    const float responsiveScale = (std::clamp)(static_cast<float>(hudHeight) / 900.0f, 0.82f, 1.16f);
    const float hudScale = baseScale * responsiveScale;
    const float opacity = settings.lockHudPolishEnabled
        ? (std::clamp)(settings.lockHudOpacity, 0.15f, 1.0f)
        : 1.0f;
    const float safeArea = settings.lockHudPolishEnabled
        ? (std::max)(12.0f, settings.lockHudSafeArea) * hudScale
        : 34.0f * hudScale;
    const float glowScale = settings.lockHudPolishEnabled
        ? (std::clamp)(settings.lockHudReticleGlowScale, 0.2f, 2.2f)
        : 1.0f;
    const float releaseFlash = (std::clamp)(
        static_cast<float>(debug.releasedThisFrame) * settings.lockHudReleaseFlash,
        0.0f,
        0.42f);
    const Vector4 cyan = maxLock
        ? Vector4{1.0f, 0.78f, 0.26f, opacity}
        : Vector4{0.25f, 0.92f, 1.0f, opacity};
    const Vector4 cyanSoft = maxLock
        ? Vector4{1.0f, 0.66f, 0.20f, opacity * 0.26f}
        : Vector4{0.16f, 0.78f, 1.0f, opacity * 0.24f};
    const Vector4 panel = Vector4{0.015f, 0.025f, 0.030f, opacity * 0.56f};
    const Vector4 panelAccent = maxLock
        ? Vector4{1.0f, 0.72f, 0.22f, opacity * 0.62f}
        : Vector4{0.18f, 0.72f, 0.88f, opacity * 0.48f};
    const Vector4 glass = Vector4{0.72f, 0.96f, 1.0f, opacity * 0.10f};
    const Vector4 lockLine = maxLock
        ? Vector4{1.0f, 0.86f, 0.30f, opacity * 0.86f}
        : Vector4{0.34f, 0.94f, 1.0f, opacity * 0.80f};
    const Vector4 candidateLine = Vector4{0.28f, 0.82f, 1.0f, opacity * 0.62f};
    const Vector4 blockedLine = Vector4{0.90f, 0.48f, 0.20f, opacity * 0.56f};
    const Vector4 reticleLine = reticle.lockHeld ? lockLine : Vector4{0.76f, 0.86f, 0.92f, opacity * 0.76f};

    if (releaseFlash > 0.0f) {
        addQuad(0.0f, 0.0f, static_cast<float>(hudWidth), static_cast<float>(hudHeight), uvWhite, Vector4{0.72f, 0.95f, 1.0f, releaseFlash});
    }
    const GameSessionPresentationFrame& sessionPresentation =
        railShooterSessionPresentation_.Frame();
    const PlayerDamagePresentationFrame& damagePresentation =
        railPlayerDamagePresentationBridge_.Frame();
    float damageFlashIntensity = sessionPresentation.screenFlashIntensity;
    Vector4 damageFlashColor{
        sessionPresentation.screenFlashColor.r,
        sessionPresentation.screenFlashColor.g,
        sessionPresentation.screenFlashColor.b,
        sessionPresentation.screenFlashColor.a};
    if (damagePresentation.screenFlashIntensity > damageFlashIntensity) {
        damageFlashIntensity = damagePresentation.screenFlashIntensity;
        damageFlashColor = damagePresentation.screenFlashColor;
    }
    if (damageFlashIntensity > 0.001f) {
        addQuad(
            0.0f,
            0.0f,
            static_cast<float>(hudWidth),
            static_cast<float>(hudHeight),
            uvWhite,
            Vector4{
                damageFlashColor.x,
                damageFlashColor.y,
                damageFlashColor.z,
                damageFlashColor.w * damageFlashIntensity * 0.22f});
    }
    const ThreatResponseFrame& threatResponse =
        railThreatResponseDirector_.Frame();
    if (threatResponse.vignetteIntensity > 0.001f) {
        const bool critical =
            threatResponse.band == ThreatResponseBand::Critical;
        const Vector4 threatColor = critical
            ? Vector4{1.0f, 0.12f, 0.05f, 1.0f}
            : Vector4{0.10f, 0.72f, 1.0f, 1.0f};
        const float edge = (std::clamp)(
            26.0f + threatResponse.vignetteIntensity * 54.0f,
            26.0f,
            static_cast<float>((std::min)(hudWidth, hudHeight)) * 0.16f);
        const Vector4 edgeColor{
            threatColor.x,
            threatColor.y,
            threatColor.z,
            threatResponse.vignetteIntensity *
                (critical ? 0.16f : 0.075f)};
        addQuad(0.0f, 0.0f, static_cast<float>(hudWidth), edge, uvWhite, edgeColor);
        addQuad(0.0f, static_cast<float>(hudHeight) - edge,
            static_cast<float>(hudWidth), edge, uvWhite, edgeColor);
        addQuad(0.0f, edge, edge,
            static_cast<float>(hudHeight) - edge * 2.0f, uvWhite, edgeColor);
        addQuad(static_cast<float>(hudWidth) - edge, edge, edge,
            static_cast<float>(hudHeight) - edge * 2.0f, uvWhite, edgeColor);
    }

    // Enemy attack warnings are rendered below lock markers and the reticle so
    // combat targeting always remains readable when several threats overlap.
    const EnemyAttackTelegraphFrame& telegraphFrame =
        railEnemyAttackTelegraphSystem_.Frame();
    for (const EnemyAttackTelegraphCue& cue : telegraphFrame.cues) {
        if (!validPoint(cue.screenPosition)) {
            continue;
        }
        const float cuePulse = (std::clamp)(cue.pulse, 0.0f, 1.0f);
        const float severity = (std::clamp)(cue.severity, 0.0f, 1.0f);
        const bool fired = cue.phase == EnemyAttackTelegraphPhase::Fired;
        const bool imminent = cue.phase == EnemyAttackTelegraphPhase::Imminent;
        const EnemyAttackTelegraphReadabilityStyle warningStyle =
            ResolveEnemyAttackTelegraphReadabilityStyle(cue.phase, cuePulse);
        Vector4 warningColor = warningStyle.primaryColor;
        warningColor.w *= opacity;
        // Keep the body/muzzle center completely clear. Launch VFX and the
        // projectile own the fired frame; warning UI must not draw over them.
        if (fired || !cue.onScreen) continue;
        float visibleBodyRadius = cue.bodyRadiusPixels;
        if (const auto* readable = railEnemyEncounterReadabilityDirector_.FindActor(cue.actorId)) {
            visibleBodyRadius = (std::max)(visibleBodyRadius,
                readable->projectedDiameterPixels * 0.5f * readable->presentationScale);
        }
        if (const auto* body = railEnemyCombatPresentationBridge_.FindActor(cue.actorId)) {
            visibleBodyRadius *= body->scaleMultiplier *
                (std::max)(body->bodyScale.x, body->bodyScale.y);
        }
        const float radius = (std::clamp)(
            (std::max)(18.0f * hudScale, visibleBodyRadius * 1.45f + 6.0f * hudScale),
            18.0f * hudScale, 96.0f * hudScale);
        addBracket(cue.screenPosition, radius,
            (4.0f + severity * 2.0f) * hudScale,
            (imminent ? 1.9f : 1.2f) * hudScale,
            Vector4{warningColor.x, warningColor.y, warningColor.z, warningColor.w * 0.82f});
        const float labelScale = (imminent ? 0.72f : 0.62f) * hudScale;
        const float labelBaseline = cue.screenPosition.y -
            radius - 28.0f * hudScale;
        const std::string warningLabel = warningStyle.label;
        if (!warningLabel.empty()) {
            const float labelWidth = measureText(warningLabel, labelScale) +
                14.0f * hudScale;
            addCenteredRect(
                {cue.screenPosition.x, labelBaseline + 8.0f * hudScale},
                labelWidth,
                20.0f * hudScale,
                uvWhite,
                Vector4{0.015f, 0.010f, 0.008f, 0.72f * opacity});
            addCenteredText(
                warningLabel,
                cue.screenPosition.x,
                labelBaseline,
                labelScale,
                warningColor);
        }
        if (warningStyle.showCountdown && cue.timeToFire > 0.0f) {
            const std::string countdown =
                FormatEnemyAttackCountdown(cue.timeToFire);
            const float countdownScale = (imminent ? 0.78f : 0.66f) * hudScale;
            const float countdownBaseline = cue.screenPosition.y +
                radius + 10.0f * hudScale;
            const float countdownWidth = measureText(countdown, countdownScale) +
                12.0f * hudScale;
            addCenteredRect(
                {cue.screenPosition.x, countdownBaseline + 8.0f * hudScale},
                countdownWidth,
                20.0f * hudScale,
                uvWhite,
                Vector4{0.015f, 0.010f, 0.008f, 0.72f * opacity});
            addCenteredText(
                countdown,
                cue.screenPosition.x,
                countdownBaseline,
                countdownScale,
                warningColor);
        }
    }

    for (const RailNormalShotLine& shot : railNormalShotLines_) {
        const float t = shot.lifetime > 0.0f
            ? (std::clamp)(shot.age / shot.lifetime, 0.0f, 1.0f)
            : 1.0f;
        const float alpha = (1.0f - t) * (1.0f - t);
        if (alpha <= 0.001f || !validPoint(shot.start) || !validPoint(shot.end)) {
            continue;
        }
        const Vector4 beamColor = shot.hit
            ? Vector4{0.78f, 0.98f, 1.0f, 0.72f * alpha}
            : Vector4{0.42f, 0.78f, 1.0f, 0.42f * alpha};
        addSoftLine(shot.start, shot.end, shot.thickness, beamColor);
        if (shot.hit) {
            addCentered(shot.end, 10.0f + 8.0f * alpha, uvGlow, Vector4{0.65f, 0.96f, 1.0f, 0.30f * alpha});
            addCentered(shot.end, 3.8f + 2.2f * alpha, uvPip, Vector4{0.86f, 1.0f, 1.0f, 0.86f * alpha});
        }
    }

    const RailLockCandidate* primeCandidate = nullptr;
    for (const RailLockCandidate& candidate : debug.candidates) {
        if (candidate.lockable &&
            (primeCandidate == nullptr || candidate.score > primeCandidate->score)) {
            primeCandidate = &candidate;
        }
    }

    if (reticle.aimFeelActive && validPoint(reticle.aimFeelTargetScreenPosition)) {
        const Vector4 aimFeelColor{0.72f, 0.98f, 1.0f, opacity * 0.50f};
        // No tether across the enemy body; the small target ring is sufficient.
        addCircleLine(
            reticle.aimFeelTargetScreenPosition,
            (11.0f + reticle.aimFeelStrength * 7.0f) * hudScale,
            1.4f * hudScale,
            aimFeelColor,
            24);
    }

    for (const RailLockCandidate& candidate : debug.candidates) {
        const bool visible =
            candidate.lockable ||
            candidate.rejectReason == RailLockRejectReason::AlreadyLocked ||
            candidate.rejectReason == RailLockRejectReason::StackLimit;
        if (!visible || !validPoint(candidate.anchor.screenPosition)) {
            continue;
        }
        const float scoreAlpha = settings.lockHudPolishEnabled
            ? (std::clamp)(0.30f + candidate.score * settings.lockHudTargetScoreAlpha, 0.24f, 1.0f)
            : 1.0f;
        const Vector4 lineColor = candidate.lockable
            ? Vector4{candidateLine.x, candidateLine.y, candidateLine.z, candidateLine.w * scoreAlpha}
            : blockedLine;
        const float radius = (std::clamp)(candidate.anchor.screenRadius, 12.0f, 48.0f) * hudScale;
        if (!reticle.lockHeld || !candidate.lockable) continue;
        addBracket(candidate.anchor.screenPosition, radius, 5.0f*hudScale, 1.3f*hudScale, lineColor);
        if (&candidate == primeCandidate) {
            addBracket(
                candidate.anchor.screenPosition,
                radius + 4.0f * hudScale + pulse * 3.0f,
                8.0f * hudScale,
                1.7f * hudScale,
                Vector4{1.0f, 1.0f, 1.0f, opacity * 0.82f});
        }
    }

    // A small aim point preserves the silhouette behind the reticle.
    addCentered(reticle.currentScreenPosition, 3.0f*hudScale, uvPip,
        Vector4{1.0f,0.98f,0.91f,opacity});

    for (int index = 0; index < tokenCount; ++index) {
        const RailLockToken& token = debug.tokens[static_cast<size_t>(index)];
        const float age = (std::max)(0.0f, debug.elapsedTime - token.acquiredTime);
        const float flash = (std::max)(0.0f, 1.0f - age / 0.22f);
        if (!validPoint(token.acquiredScreenPosition)) continue;
        const float radius=(25.0f+flash*8.0f)*hudScale;
        addBracket(token.acquiredScreenPosition,radius,8.0f*hudScale,2.0f*hudScale,lockLine);
        addNumber(index+1,{token.acquiredScreenPosition.x,token.acquiredScreenPosition.y-radius-9.0f*hudScale},
            0.70f*hudScale,Vector4{0.65f,1.0f,0.82f,opacity});
    }

    for (const RailLockToken& token : debug.acquiredTokens) {
        if (!validPoint(token.acquiredScreenPosition)) {
            continue;
        }
        const float age = (std::max)(0.0f, debug.elapsedTime - token.acquiredTime);
        const float flash = (std::max)(0.0f, 1.0f - age / 0.18f);
        if (flash > 0.0f) {
            addCircleLine(
                token.acquiredScreenPosition,
                (35.0f + flash * 18.0f) * hudScale,
                2.2f * hudScale,
                Vector4{1.0f, 1.0f, 1.0f, opacity * flash},
                40);
        }
    }

    const float reticleRadius = reticle.lockHeld
        ? (27.0f + pulse * 3.0f + acquirePulse * 5.0f) * hudScale
        : 15.0f * hudScale;
    addBracket(reticle.currentScreenPosition,reticleRadius,6.0f*hudScale,1.7f*hudScale,reticleLine);
    if(reticle.lockHeld) addCircleLine(reticle.currentScreenPosition,reticleRadius,1.1f*hudScale,reticleLine,32);
    const WeaponFeedbackSystem& feedbackSystem = railShooterCollisionSystem_.WeaponFeedback();
    if (feedbackSystem.HitMarkerActive()) {
        const WeaponFeedbackEvent& feedback = feedbackSystem.LastAcceptedEvent();
        const float markerTime = feedbackSystem.HitMarkerNormalizedTime();
        const float markerAlpha = (std::min)(1.0f, markerTime * 3.0f) * opacity;
        const float markerRadius = (18.0f + (1.0f - markerTime) * 9.0f) * hudScale;
        Vector4 markerColor{0.74f, 0.98f, 1.0f, markerAlpha};
        if (feedback.feedbackKind == HitFeedbackKind::Blocked ||
            feedback.feedbackKind == HitFeedbackKind::ArmorHit) {
            markerColor = {1.0f, 0.58f, 0.20f, markerAlpha};
        } else if (feedback.feedbackKind == HitFeedbackKind::WeakPointHit ||
                   feedback.feedbackKind == HitFeedbackKind::Destroyed) {
            markerColor = {1.0f, 0.88f, 0.28f, markerAlpha};
        }
        // Broken diagonal X for confirmed hits, larger gold X + ring for kills.
        // Leave the aim point open instead of covering it with a solid pip.
        for (int sx : {-1, 1}) for (int sy : {-1, 1}) {
            const Vector2 a{reticle.currentScreenPosition.x + sx * 7.0f * hudScale,
                            reticle.currentScreenPosition.y + sy * 7.0f * hudScale};
            const Vector2 b{reticle.currentScreenPosition.x + sx * markerRadius,
                            reticle.currentScreenPosition.y + sy * markerRadius};
            addLine(a, b, 5.0f * hudScale, {0.015f, 0.02f, 0.03f, markerAlpha});
            addLine(a, b, (feedback.destroyed ? 3.0f : 2.0f) * hudScale, markerColor);
        }
        if (feedback.destroyed) addCircleLine(reticle.currentScreenPosition,
            markerRadius + 5.0f * hudScale, 2.2f * hudScale, markerColor, 24);
        addBracket(
            reticle.currentScreenPosition,
            markerRadius,
            7.0f * hudScale,
            (feedback.destroyed ? 2.8f : 2.0f) * hudScale,
            markerColor);
        if (feedback.feedbackKind == HitFeedbackKind::Blocked ||
            feedback.feedbackKind == HitFeedbackKind::ArmorHit) {
            addCross(
                reticle.currentScreenPosition,
                markerRadius * 0.50f,
                1.8f * hudScale,
                markerColor);
        }
    }
    if (maxLock) {
        addBracket(
            reticle.currentScreenPosition,
            reticleRadius + 8.0f * hudScale,
            9.0f * hudScale,
            1.9f * hudScale,
            Vector4{1.0f, 0.86f, 0.30f, opacity});
    }

    if (!railShooterHudPresentation_.Frame().visible && !railTitleScreenVisible_) {
    const float meterWidth = (static_cast<float>(maxLocks) * 20.0f + 94.0f) * hudScale;
    const float meterHeight = 54.0f * hudScale;
    const float meterX = static_cast<float>(hudWidth) * 0.5f - meterWidth * 0.5f;
    const float meterY = static_cast<float>(hudHeight) - safeArea - meterHeight;
    addQuad(meterX, meterY, meterWidth, meterHeight, uvWhite, panel);
    addQuad(meterX, meterY, meterWidth, 3.0f * hudScale, uvWhite, panelAccent);
    addQuad(meterX + 8.0f * hudScale, meterY + meterHeight - 7.0f * hudScale, meterWidth - 16.0f * hudScale, 2.0f * hudScale, uvWhite, glass);
    const Vector2 countCenter{meterX + meterWidth - 33.0f * hudScale, meterY + meterHeight * 0.50f};
    addNumber(tokenCount, countCenter, 1.05f * hudScale, maxLock ? Vector4{1.0f, 0.86f, 0.34f, opacity} : Vector4{0.72f, 0.98f, 1.0f, opacity});
    for (int index = 0; index < maxLocks; ++index) {
        const bool filled = index < tokenCount;
        const Vector2 center{
            meterX + 18.0f * hudScale + static_cast<float>(index) * 20.0f * hudScale,
            meterY + meterHeight * 0.50f};
        if (filled) {
            addCenteredRect(
                center,
                15.0f * hudScale,
                28.0f * hudScale,
                uvWhite,
                Vector4{0.18f, 0.70f, 0.90f, opacity * 0.18f});
        }
        addCentered(
            center,
            (filled ? 16.0f + acquirePulse * 3.0f : 14.0f) * hudScale,
            uvPip,
            filled ? cyan : Vector4{0.38f, 0.46f, 0.50f, opacity * 0.42f});
        addCircleLine(
            center,
            (filled ? 9.0f + acquirePulse * 2.2f : 8.0f) * hudScale,
            1.0f * hudScale,
            filled ? lockLine : Vector4{0.38f, 0.46f, 0.50f, opacity * 0.44f},
            18);
    }

    }

    // Legacy session-only layout remains as a safe fallback for malformed or
    // disabled HUD assets. The normal path below is the unified rail HUD.
    const GameSessionHudView& sessionHud = sessionPresentation.hud;
    if (sessionHud.visible && !railShooterHudPresentation_.Frame().visible) {
        const float sessionScale = responsiveScale;
        const float barWidth = 230.0f * sessionScale;
        const float barHeight = 13.0f * sessionScale;
        const float left = safeArea;
        const float top = safeArea;
        const Vector4 sessionPanel{0.012f, 0.022f, 0.030f, opacity * 0.78f};
        const Vector4 sessionCyan{0.24f, 0.90f, 1.0f, opacity};
        const Vector4 healthColor = sessionHud.healthNormalized > 0.30f
            ? Vector4{0.30f, 1.0f, 0.58f, opacity}
            : Vector4{1.0f, 0.22f, 0.12f, opacity};
        addQuad(left, top, barWidth + 22.0f * sessionScale,
                67.0f * sessionScale, uvWhite, sessionPanel);
        addText(HudUtf8(u8"\u8010\u4e45"), left + 10.0f * sessionScale,
                top + 20.0f * sessionScale, 0.72f * sessionScale, sessionCyan);
        addQuad(left + 10.0f * sessionScale, top + 29.0f * sessionScale,
                barWidth, barHeight, uvWhite,
                Vector4{0.06f, 0.10f, 0.12f, opacity * 0.92f});
        addQuad(left + 10.0f * sessionScale, top + 29.0f * sessionScale,
                barWidth * sessionHud.healthNormalized, barHeight,
                uvWhite, healthColor);
        addText(
            HudUtf8(u8"\u6b8b\u6a5f ") + std::to_string(sessionHud.retriesRemaining),
            left + 10.0f * sessionScale,
            top + 58.0f * sessionScale,
            0.62f * sessionScale,
            Vector4{0.72f, 0.90f, 0.96f, opacity});

        const float progressWidth = 310.0f * sessionScale;
        const float progressX = static_cast<float>(hudWidth) * 0.5f - progressWidth * 0.5f;
        addQuad(progressX - 12.0f * sessionScale, top,
                progressWidth + 24.0f * sessionScale, 43.0f * sessionScale,
                uvWhite, sessionPanel);
        addCenteredText(HudUtf8(u8"\u9032\u5ea6"), static_cast<float>(hudWidth) * 0.5f,
                        top + 18.0f * sessionScale, 0.62f * sessionScale, sessionCyan);
        addQuad(progressX, top + 27.0f * sessionScale,
                progressWidth, 7.0f * sessionScale, uvWhite,
                Vector4{0.06f, 0.10f, 0.12f, opacity * 0.92f});
        addQuad(progressX, top + 27.0f * sessionScale,
                progressWidth * sessionHud.courseProgressNormalized,
                7.0f * sessionScale, uvWhite, sessionCyan);

        const std::string scoreText = HudUtf8(u8"\u5f97\u70b9 ") + std::to_string(sessionHud.score);
        const std::string comboText = sessionHud.combo > 1
            ? HudUtf8(u8"\u9023\u7d9a ") + std::to_string(sessionHud.combo)
            : std::string{};
        const float scoreWidth = (std::max)(
            175.0f * sessionScale,
            measureText(scoreText, 0.70f * sessionScale) + 24.0f * sessionScale);
        const float scoreX = static_cast<float>(hudWidth) - safeArea - scoreWidth;
        addQuad(scoreX, top, scoreWidth, comboText.empty()
                ? 42.0f * sessionScale : 66.0f * sessionScale,
                uvWhite, sessionPanel);
        addText(scoreText, scoreX + 12.0f * sessionScale,
                top + 27.0f * sessionScale, 0.70f * sessionScale,
                Vector4{0.92f, 0.98f, 1.0f, opacity});
        if (!comboText.empty()) {
            addText(comboText, scoreX + 12.0f * sessionScale,
                    top + 53.0f * sessionScale, 0.64f * sessionScale,
                    Vector4{1.0f, 0.78f, 0.22f, opacity});
        }

        if (sessionHud.showBanner) {
            const float bannerWidth = (std::min)(
                620.0f * sessionScale,
                static_cast<float>(hudWidth) - safeArea * 2.0f);
            const float bannerHeight = sessionHud.detail.empty()
                ? 82.0f * sessionScale : 112.0f * sessionScale;
            const float bannerX = static_cast<float>(hudWidth) * 0.5f - bannerWidth * 0.5f;
            const float bannerY = static_cast<float>(hudHeight) * 0.28f - bannerHeight * 0.5f;
            const float alpha = (std::clamp)(sessionHud.bannerAlpha, 0.0f, 1.0f) * opacity;
            const GameSessionPresentationColor& bannerColor = sessionHud.bannerColor;
            addQuad(bannerX, bannerY, bannerWidth, bannerHeight, uvWhite,
                    Vector4{0.008f, 0.015f, 0.022f, alpha * 0.88f});
            addQuad(bannerX, bannerY, bannerWidth, 4.0f * sessionScale, uvWhite,
                    Vector4{bannerColor.r, bannerColor.g, bannerColor.b, alpha});
            addCenteredText(
                sessionHud.headline,
                static_cast<float>(hudWidth) * 0.5f,
                bannerY + 49.0f * sessionScale,
                1.24f * sessionScale,
                Vector4{bannerColor.r, bannerColor.g, bannerColor.b, alpha});
            if (!sessionHud.detail.empty()) {
                addCenteredText(
                    sessionHud.detail,
                    static_cast<float>(hudWidth) * 0.5f,
                    bannerY + 84.0f * sessionScale,
                    0.72f * sessionScale,
                    Vector4{0.82f, 0.92f, 0.96f, alpha});
            }
        }
    }

    RailShooterHudRenderInput hudRenderInput{};
    hudRenderInput.definition = &railShooterHudDefinition_;
    hudRenderInput.presentation = &railShooterHudPresentation_.Frame();
    hudRenderInput.viewportWidth = hudWidth;
    hudRenderInput.viewportHeight = hudHeight;
    hudRenderInput.showTitleScreen = railTitleScreenVisible_;
    hudRenderInput.titleMenuSelection = railTitleMenuSelection_;
    hudRenderInput.titleControlsVisible = railTitleControlsVisible_;
    hudRenderInput.titleOpacity = railTitleScene_.MenuOpacity();
    hudRenderInput.titleBlackout = railTitleScene_.Blackout();
    hudRenderInput.titleLogoAvailable = railTitleLogoReady_;
    railShooterHudRenderer_.Update(hudRenderInput);
    const auto emitHudCommands = [&](const auto& commands) {
        for (const RailShooterHudDrawCommand& command : commands) {
            if (command.kind == RailShooterHudDrawCommandKind::Rectangle) {
                addQuad(
                    command.x,
                    command.y,
                    command.width,
                    command.height,
                    uvWhite,
                    command.color);
            } else if (command.kind == RailShooterHudDrawCommandKind::Plate) {
                const float cut = (std::min)(command.height*0.24f,command.width*0.08f);
                addQuadCorners({command.x+cut,command.y},{command.x+command.width,command.y},
                    {command.x,command.y+command.height},{command.x+command.width-cut,command.y+command.height},
                    uvWhite,command.color);
            } else if (command.kind == RailShooterHudDrawCommandKind::TitleLogo) {
                addQuad(command.x, command.y, command.width, command.height,
                    uv(float(kTitleLogoAtlasX),float(kTitleLogoAtlasY),
                       float(kTitleLogoAtlasWidth),float(kTitleLogoAtlasHeight)), command.color);
            } else if (command.textAlignment ==
                       RailShooterHudTextAlignment::Center) {
                addCenteredText(
                    command.text,
                    command.x,
                    command.y,
                    command.fontScale,
                    command.color);
            } else {
                addText(
                    command.text,
                    command.x,
                    command.y,
                    command.fontScale,
                    command.color);
            }
        }
    };
    if (railTitleScreenVisible_) {
        // Suppress every gameplay overlay, including the reticle built above.
        railLockOnHudAtlasVertexCount_ = 0;
        emitHudCommands(railShooterHudRenderer_.Frame().commands);
        // Append world billboards after the UI range. They retain homogeneous
        // depth and are drawn separately into SceneColor with read-only depth.
        const Vector3 right=Normalize(Vector3{frameState_.viewMatrix.m[0][0],frameState_.viewMatrix.m[1][0],frameState_.viewMatrix.m[2][0]});
        const Vector3 up=Normalize(Vector3{frameState_.viewMatrix.m[0][1],frameState_.viewMatrix.m[1][1],frameState_.viewMatrix.m[2][1]});
        auto dustVertex=[&](Vector3 world,Vector2 texcoord,Vector4 color) {
            const uint32_t index=railLockOnHudAtlasVertexCount_+railTitleDustVertexCount_;
            if(index>=kMaxAtlasVertices) return;
            const auto& m=frameState_.viewProjectionMatrix.m;
            const Vector4 projected{world.x*m[0][0]+world.y*m[1][0]+world.z*m[2][0]+m[3][0],
                world.x*m[0][1]+world.y*m[1][1]+world.z*m[2][1]+m[3][1],
                world.x*m[0][2]+world.y*m[1][2]+world.z*m[2][2]+m[3][2],
                world.x*m[0][3]+world.y*m[1][3]+world.z*m[2][3]+m[3][3]};
            railLockOnHudAtlasMappedVertices_[index]={projected,texcoord,color};
            ++railTitleDustVertexCount_;
        };
        struct SandBillboard {
            Vector3 center,horizontal,vertical; Vector4 color; float seed;
            Vector3 impactOrigin{};
            float impactLight=0.0f;
        };
        std::vector<SandBillboard> billboards;
        billboards.reserve(180);
        const float titleAlpha=railTitleScene_.Pursuer().alpha;
        for(const auto& cloud:railTitleScene_.DustClouds()) {
            const float age=cloud.age,t=age/RailTitleDustCloud::Lifetime;
            const float fade=(1.0f-std::exp(-age*38.0f))*std::pow((std::max)(0.0f,1.0f-t),1.15f)*titleAlpha;
            const float impactLight=std::exp(-age*9.0f)*(std::max)(0.0f,1.0f-age/0.34f);
            // Fast, low rolling skirt establishes a contact point on the sand.
            for(int puff=0;puff<8;++puff) {
                const float angle=puff*0.785398f+cloud.seed*0.71f;
                const float spread=0.18f+2.4f*(1.0f-std::exp(-age*3.5f));
                const float radius=0.38f+0.80f*(1.0f-std::exp(-age*4.0f));
                const Vector3 center=Add(cloud.origin,{std::cos(angle)*spread,0.20f+age*0.10f,std::sin(angle)*spread});
                billboards.push_back({center,Scale(right,radius),Scale(up,radius*0.32f),
                    {0.62f,0.43f,0.25f,fade*std::exp(-age*1.7f)*0.70f},float(2*(1+puff+cloud.seed%47)),
                    cloud.origin,impactLight});
            }
            // Unequal rising lobes slow down and disperse instead of expanding as one circle.
            for(int puff=0;puff<10;++puff) {
                const float elapsed=(std::max)(0.0f,age-puff*0.012f);
                const float angle=puff*2.399963f+cloud.seed*0.71f;
                const float spread=(0.25f+0.09f*puff)*(1.0f-std::exp(-elapsed*3.0f));
                const float rise=(0.85f+0.10f*(puff%4))*(1.0f-std::exp(-elapsed*1.6f));
                const Vector3 center=Add(cloud.origin,{std::cos(angle)*spread+elapsed*0.35f,
                    0.38f+rise,std::sin(angle)*spread});
                const float radius=(0.34f+1.1f*(1.0f-std::exp(-elapsed*2.1f)))*(0.82f+0.08f*(puff%4));
                const float rotation=angle*0.7f+elapsed*0.12f;
                const Vector3 horizontal=Scale(Add(Scale(right,std::cos(rotation)),Scale(up,std::sin(rotation))),radius);
                const Vector3 vertical=Scale(Add(Scale(up,std::cos(rotation)),Scale(right,-std::sin(rotation))),radius*0.85f);
                billboards.push_back({center,horizontal,vertical,{0.69f,0.50f,0.31f,fade*0.55f},
                    float(2*(16+puff+cloud.seed%47)),cloud.origin,impactLight});
            }
            // Actual ballistic sand/debris, rendered without the shared GPU pool resets.
            for(uint32_t grainIndex=0;grainIndex<36;++grainIndex) {
                const auto grain=RailTitleScene::EvaluateSandGrain(cloud,grainIndex);
                if(grain.opacity<0.001f) continue;
                const float r=grain.radius*(grainIndex%7==0 ? 1.8f : 1.0f);
                billboards.push_back({grain.position,Scale(right,r),Scale(up,r*1.3f),
                    {0.37f,0.25f,0.14f,grain.opacity*titleAlpha},-2.0f});
            }
        }
        const Vector3 eye=railTitleScene_.CameraPosition();
        std::sort(billboards.begin(),billboards.end(),[&](const auto& a,const auto& b) {
            const auto da=Add(a.center,Scale(eye,-1)),db=Add(b.center,Scale(eye,-1));
            return Dot(da,da)>Dot(db,db);
        });
        for(const auto& puff:billboards) {
            if(railLockOnHudAtlasVertexCount_+railTitleDustVertexCount_+6>kMaxAtlasVertices) break;
            const auto a=Add(puff.center,Add(Scale(puff.horizontal,-1),puff.vertical));
            const auto b=Add(puff.center,Add(puff.horizontal,puff.vertical));
            const auto c=Add(puff.center,Add(Scale(puff.horizontal,-1),Scale(puff.vertical,-1)));
            const auto d=Add(puff.center,Add(puff.horizontal,Scale(puff.vertical,-1)));
            // Scatter light from the fixed contact point through the billow.
            // Leave density/alpha untouched; distance and height dim the upper smoke.
            const auto litColor=[&](Vector3 corner) {
                Vector4 color=puff.color;
                const auto offset=Add(corner,Scale(Add(puff.impactOrigin,{0,0.18f,0}),-1));
                const float light=3.2f*puff.impactLight/
                    (1.0f+2.2f*(offset.x*offset.x+offset.z*offset.z)+3.5f*offset.y*offset.y);
                color.x+=light; color.y+=light*0.30f; color.z+=light*0.045f;
                return color;
            };
            dustVertex(a,{puff.seed,0},litColor(a)); dustVertex(b,{puff.seed+1,0},litColor(b)); dustVertex(c,{puff.seed,1},litColor(c));
            dustVertex(c,{puff.seed,1},litColor(c)); dustVertex(b,{puff.seed+1,0},litColor(b)); dustVertex(d,{puff.seed+1,1},litColor(d));
        }
        // Camera-facing world ribbons aligned to flight, with a tip and tapered tail.
        // Keep their suffix separate so the glow uses additive rather than smoke blending.
        const uint32_t dustCount=railTitleDustVertexCount_;
        const auto glowQuad=[&](Vector3 center,Vector3 h,Vector3 v,float uvBase,Vector4 color) {
            if(railLockOnHudAtlasVertexCount_+railTitleDustVertexCount_+6>kMaxAtlasVertices) return;
            const auto a=Add(center,Add(Scale(h,-1),v)),b=Add(center,Add(h,v));
            const auto c=Add(center,Add(Scale(h,-1),Scale(v,-1))),d=Add(center,Add(h,Scale(v,-1)));
            dustVertex(a,{uvBase,0},color); dustVertex(b,{uvBase+1,0},color); dustVertex(c,{uvBase,1},color);
            dustVertex(c,{uvBase,1},color); dustVertex(b,{uvBase+1,0},color); dustVertex(d,{uvBase+1,1},color);
        };
        for(const auto& cloud:railTitleScene_.DustClouds()) {
            const float age=cloud.age;
            if(age>=0.36f) continue;
            // A brief hot core, followed by a softer irregular patch on the sand.
            if(age<0.10f) {
                glowQuad(Add(cloud.origin,{0,0.18f,0}),Scale(right,0.65f),Scale(up,0.42f),2.0f,
                    {1.4f,0.36f,0.055f,2.0f*(1.0f-age/0.10f)*titleAlpha});
            }
            const float glow=std::exp(-age*10.0f)*(std::max)(0.0f,1.0f-age/0.34f)*titleAlpha;
            const float angle=float(cloud.seed)*2.399963f;
            const float radius=0.70f+0.55f*(1.0f-std::exp(-age*18.0f));
            glowQuad(Add(cloud.origin,{0,0.035f,0}),
                {std::cos(angle)*radius,0,std::sin(angle)*radius},
                {-std::sin(angle)*radius,0,std::cos(angle)*radius},6.0f,{1.5f,0.32f,0.045f,glow*2.2f});
            // Reuse deterministic ballistic motion for a few hot fragments.
            // Their streaks cool before landing, without resetting the GPU dust pool.
            for(uint32_t spark=0;spark<10;++spark) {
                const auto grain=RailTitleScene::EvaluateSandGrain(cloud,72+spark);
                const float lifetime=0.24f+0.012f*float(spark);
                if(age>=lifetime || grain.opacity<0.001f) continue;
                const float brightness=grain.opacity*std::exp(-age*6.0f)*(1.0f-age/lifetime)*titleAlpha;
                const auto direction=NormalizeOr(grain.velocity,up);
                const auto side=NormalizeOr(Cross(direction,NormalizeOr(Add(eye,Scale(grain.position,-1)),right)),right);
                const float length=0.12f+0.012f*float(spark);
                glowQuad(Add(grain.position,Scale(direction,-length*0.5f)),Scale(direction,length*0.5f),
                    Scale(side,0.025f+0.002f*float(spark%3)),8.0f,
                    {1.9f,0.85f-1.1f*age,0.18f,brightness*1.8f});
            }
        }
        for(const auto& shot:railTitleScene_.Shots()) {
            if(railLockOnHudAtlasVertexCount_+railTitleDustVertexCount_+12>kMaxAtlasVertices) break;
            const auto direction=Normalize(Add(shot.target,Scale(shot.origin,-1)));
            const auto side=NormalizeOr(Cross(direction,Normalize(Add(eye,Scale(shot.position,-1)))),right);
            const float flown=std::sqrt(Dot(Add(shot.position,Scale(shot.origin,-1)),Add(shot.position,Scale(shot.origin,-1))));
            const auto tail=Add(shot.position,Scale(direction,-(std::min)(flown,1.8f)));
            const auto width=Scale(side,0.26f);
            const auto a=Add(shot.position,width),b=Add(shot.position,Scale(width,-1));
            const auto c=Add(tail,width),d=Add(tail,Scale(width,-1));
            const Vector4 color{1.0f,0.055f,0.015f,titleAlpha};
            const Vector4 trailColor{color.x,color.y,color.z,titleAlpha*0.72f};
            dustVertex(a,{0,0},trailColor); dustVertex(b,{0,1},trailColor); dustVertex(c,{1,0},trailColor);
            dustVertex(c,{1,0},trailColor); dustVertex(b,{0,1},trailColor); dustVertex(d,{1,1},trailColor);
            // The 1.8 m circular head provides a clear aiming target even near
            // the distant drone. Keep the shorter, dimmer trail directional.
            const auto h=Scale(right,0.90f),v=Scale(up,0.90f);
            const auto ha=Add(shot.position,Add(Scale(h,-1),v)),hb=Add(shot.position,Add(h,v));
            const auto hc=Add(shot.position,Add(Scale(h,-1),Scale(v,-1))),hd=Add(shot.position,Add(h,Scale(v,-1)));
            dustVertex(ha,{4,0},color); dustVertex(hb,{5,0},color); dustVertex(hc,{4,1},color);
            dustVertex(hc,{4,1},color); dustVertex(hb,{5,0},color); dustVertex(hd,{5,1},color);
        }
        railTitleTracerVertexCount_=railTitleDustVertexCount_-dustCount;
        railTitleDustVertexCount_=dustCount;
        return railLockOnHudAtlasVertexCount_ > 0 || railTitleDustVertexCount_ > 0 || railTitleTracerVertexCount_ > 0;
    }
    emitHudCommands(railShooterHudRenderer_.Frame().commands);

    RailShooterDefensePromptRenderInput defensePromptInput{};
    defensePromptInput.presentation = railDefenseUiProofEnabled_
        ? nullptr : &railEnemyAttackDefensePresentationBridge_.Frame();
    defensePromptInput.outcome =
        &railEnemyAttackDefenseOutcomeFeedbackBridge_.Frame();
    defensePromptInput.viewportWidth = hudWidth;
    defensePromptInput.viewportHeight = hudHeight;
    defensePromptInput.settings = railShooterDefensePromptRendererSettings_;
    railShooterDefensePromptRenderer_.Update(defensePromptInput);
    emitHudCommands(railShooterDefensePromptRenderer_.Frame().commands);
    if(railTitleGameplayFade_>0.0f) {
        const float t=(std::clamp)(railTitleGameplayFade_/0.40f,0.0f,1.0f);
        addQuad(0,0,float(hudWidth),float(hudHeight),uv(3.5f,115.5f,0,0),{0.015f,0.025f,0.035f,t*t*(3-2*t)});
    }

    return railLockOnHudAtlasVertexCount_ > 0;
}

void AppRunLoop::RegisterRailLockOnHudPass(
    ID3D12GraphicsCommandList* commandList,
    const std::string& targetResourceName) {
    if (targetResourceName.empty() ||
        !imguiLayer_.EditorViewportOverlay().LayerVisible(
            editor::EditorViewportOverlayLayerId::GameplayHud)) {
        return;
    }

    const bool atlasReady =
        EnsureRailLockOnHudAtlas(commandList) &&
        BuildRailLockOnHudAtlasQuads();
    if (!atlasReady) {
        return;
    }

    if(railTitleScreenVisible_ && railTitleDustVertexCount_>0 && appPipelines_.GetTitleDustPSO()) {
        renderGraph_.AddPass({
            "VFX.TitleGroundDust",
            ge3::graphics::RenderPassLayer::Vfx,
            {{"SceneColor",ge3::graphics::RenderResourceAccessType::WriteRtv},
             {"SceneDepth",ge3::graphics::RenderResourceAccessType::ReadDepth}},
            "SceneDepth",
            [this](ge3::graphics::RenderPassContext& passContext) {
                passContext.commandList->RSSetViewports(1,&runtimeState_.viewport);
                passContext.commandList->RSSetScissorRects(1,&runtimeState_.scissorRect);
                passContext.commandList->SetGraphicsRootSignature(appPipelines_.GetRailHudAtlasRootSignature());
                passContext.commandList->SetPipelineState(appPipelines_.GetTitleDustPSO());
                ID3D12DescriptorHeap* heaps[]={srvDescriptorHeap_.Get()};
                passContext.commandList->SetDescriptorHeaps(1,heaps);
                passContext.commandList->SetGraphicsRootDescriptorTable(0,railLockOnHudAtlasSrvGpu_);
                passContext.commandList->IASetVertexBuffers(0,1,&railLockOnHudAtlasVertexBufferView_);
                passContext.commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
                passContext.commandList->DrawInstanced(railTitleDustVertexCount_,1,railLockOnHudAtlasVertexCount_,0);
            }});
    }
    if(railTitleScreenVisible_ && railTitleTracerVertexCount_>0 && appPipelines_.GetTitleTracerPSO()) {
        renderGraph_.AddPass({"VFX.TitleTracers",ge3::graphics::RenderPassLayer::Vfx,
            {{"SceneColor",ge3::graphics::RenderResourceAccessType::WriteRtv},
             {"SceneDepth",ge3::graphics::RenderResourceAccessType::ReadDepth}},"SceneDepth",
            [this](ge3::graphics::RenderPassContext& passContext) {
                passContext.commandList->RSSetViewports(1,&runtimeState_.viewport);
                passContext.commandList->RSSetScissorRects(1,&runtimeState_.scissorRect);
                passContext.commandList->SetGraphicsRootSignature(appPipelines_.GetRailHudAtlasRootSignature());
                passContext.commandList->SetPipelineState(appPipelines_.GetTitleTracerPSO());
                ID3D12DescriptorHeap* heaps[]={srvDescriptorHeap_.Get()};
                passContext.commandList->SetDescriptorHeaps(1,heaps);
                passContext.commandList->SetGraphicsRootDescriptorTable(0,railLockOnHudAtlasSrvGpu_);
                passContext.commandList->IASetVertexBuffers(0,1,&railLockOnHudAtlasVertexBufferView_);
                passContext.commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
                passContext.commandList->DrawInstanced(railTitleTracerVertexCount_,1,railLockOnHudAtlasVertexCount_+railTitleDustVertexCount_,0);
            }});
    }
    if(railLockOnHudAtlasVertexCount_==0) return;
    renderGraph_.AddPass({
        "UI.RailLockOnHud",
        ge3::graphics::RenderPassLayer::Ui,
        {
            {targetResourceName, ge3::graphics::RenderResourceAccessType::WriteRtv},
        },
        "",
        [this](ge3::graphics::RenderPassContext& passContext) {
            if (railLockOnHudAtlasVertexCount_ == 0 ||
                railLockOnHudAtlasSrvGpu_.ptr == 0 ||
                railLockOnHudAtlasVertexBufferView_.BufferLocation == 0) {
                return;
            }
            passContext.commandList->RSSetViewports(1, &runtimeState_.viewport);
            passContext.commandList->RSSetScissorRects(1, &runtimeState_.scissorRect);
            passContext.commandList->SetGraphicsRootSignature(appPipelines_.GetRailHudAtlasRootSignature());
            passContext.commandList->SetPipelineState(appPipelines_.GetRailHudAtlasPSO());
            ID3D12DescriptorHeap* descriptorHeaps[] = {srvDescriptorHeap_.Get()};
            passContext.commandList->SetDescriptorHeaps(1, descriptorHeaps);
            passContext.commandList->SetGraphicsRootDescriptorTable(0, railLockOnHudAtlasSrvGpu_);
            passContext.commandList->IASetVertexBuffers(0, 1, &railLockOnHudAtlasVertexBufferView_);
            passContext.commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            passContext.commandList->DrawInstanced(railLockOnHudAtlasVertexCount_, 1, 0, 0);
        }});
}

bool AppRunLoop::EnsureSubmissionHudResources(
    ID3D12GraphicsCommandList* commandList) {
    constexpr uint32_t kMaxSubmissionHudVertices = 4096;
    if (!EnsureRailLockOnHudAtlas(commandList)) {
        return false;
    }
    if (submissionHudVertexResource_ != nullptr &&
        submissionHudMappedVertices_ != nullptr) {
        return true;
    }

    ComPtr<ID3D12Device> device = dev_.GetDevice();
    if (device == nullptr) {
        return false;
    }
    submissionHudVertexResource_ = CreateBufferResource(
        device,
        sizeof(RailHudAtlasVertex) * kMaxSubmissionHudVertices);
    if (submissionHudVertexResource_ == nullptr ||
        FAILED(submissionHudVertexResource_->Map(
            0,
            nullptr,
            reinterpret_cast<void**>(&submissionHudMappedVertices_))) ||
        submissionHudMappedVertices_ == nullptr) {
        submissionHudVertexResource_.Reset();
        submissionHudMappedVertices_ = nullptr;
        return false;
    }
    submissionHudVertexBufferView_.BufferLocation =
        submissionHudVertexResource_->GetGPUVirtualAddress();
    submissionHudVertexBufferView_.SizeInBytes =
        sizeof(RailHudAtlasVertex) * kMaxSubmissionHudVertices;
    submissionHudVertexBufferView_.StrideInBytes = sizeof(RailHudAtlasVertex);
    return true;
}

bool AppRunLoop::BuildSubmissionHudQuads() {
    constexpr uint32_t kMaxSubmissionHudVertices = 4096;

    submissionHudVertexCount_ = 0;
    if (!runtimeState_.submissionShowcase.enabled ||
        submissionHudManuallyHidden_ ||
        !submissionHudFontReady_ ||
        submissionHudMappedVertices_ == nullptr) {
        return false;
    }

    const RenderViewportMetrics metrics = ResolveRenderViewportMetrics(
        imguiLayer_.EditorViewportRenderTargetState(),
        windowWidth_,
        windowHeight_);
    if (metrics.width == 0 || metrics.height == 0) {
        return false;
    }

    const float uiScale = (std::clamp)(
        static_cast<float>(metrics.height) / 900.0f,
        0.72f,
        1.18f);
    const auto uv = [](float x, float y, float w, float h) {
        return Vector4{
            x / static_cast<float>(kRailHudAtlasWidth),
            y / static_cast<float>(kRailHudAtlasHeight),
            (x + w) / static_cast<float>(kRailHudAtlasWidth),
            (y + h) / static_cast<float>(kRailHudAtlasHeight)};
    };
    const Vector4 uvWhite = uv(0.0f, 112.0f, 8.0f, 8.0f);
    const auto clip = [&](float x, float y) {
        return Vector4{
            x / static_cast<float>(metrics.width) * 2.0f - 1.0f,
            1.0f - y / static_cast<float>(metrics.height) * 2.0f,
            0.0f,
            1.0f};
    };
    auto push = [&](float x, float y, float u, float v, const Vector4& color) {
        if (submissionHudVertexCount_ >= kMaxSubmissionHudVertices) return;
        submissionHudMappedVertices_[submissionHudVertexCount_++] = {
            clip(x, y),
            {u, v},
            color};
    };
    auto addQuad = [&](float x, float y, float width, float height,
                       const Vector4& rect, const Vector4& color) {
        if (width <= 0.0f || height <= 0.0f ||
            submissionHudVertexCount_ + 6 > kMaxSubmissionHudVertices) {
            return;
        }
        const float x1 = x + width;
        const float y1 = y + height;
        push(x,  y,  rect.x, rect.y, color);
        push(x1, y,  rect.z, rect.y, color);
        push(x,  y1, rect.x, rect.w, color);
        push(x,  y1, rect.x, rect.w, color);
        push(x1, y,  rect.z, rect.y, color);
        push(x1, y1, rect.z, rect.w, color);
    };
    auto addText = [&](const char* text, float x, float baseline, float scale,
                       const Vector4& color) {
        float cursor = x;
        for (const char* ch = text; *ch != '\0'; ++ch) {
            const unsigned char codepoint = static_cast<unsigned char>(*ch);
            if (codepoint < 32 || codepoint > 126) {
                continue;
            }
            const SubmissionHudGlyph& glyph = submissionHudGlyphs_[codepoint - 32];
            if (glyph.valid) {
                addQuad(
                    cursor + glyph.offset.x * scale,
                    baseline + glyph.offset.y * scale,
                    glyph.size.x * scale,
                    glyph.size.y * scale,
                    glyph.uv,
                    color);
            }
            cursor += glyph.advance * scale;
        }
    };

    const float panelX = 22.0f * uiScale;
    const float panelY = 22.0f * uiScale;
    const float panelWidth = 510.0f * uiScale;
    const float panelHeight = 222.0f * uiScale;
    addQuad(panelX, panelY, panelWidth, panelHeight, uvWhite,
            {0.008f, 0.014f, 0.028f, 0.84f});
    addQuad(panelX, panelY, 5.0f * uiScale, panelHeight, uvWhite,
            {0.10f, 0.72f, 1.0f, 0.92f});
    addQuad(panelX, panelY, panelWidth, 2.0f * uiScale, uvWhite,
            {0.25f, 0.86f, 1.0f, 0.75f});

    const Vector4 titleColor{0.92f, 0.98f, 1.0f, 1.0f};
    const Vector4 keyColor{0.28f, 0.84f, 1.0f, 1.0f};
    const Vector4 actionColor{0.78f, 0.86f, 0.92f, 1.0f};
    addText("CONTROLS", panelX + 20.0f * uiScale,
            panelY + 42.0f * uiScale, 1.12f * uiScale, titleColor);

    struct ControlLine { const char* input; const char* action; };
    static constexpr ControlLine kLines[] = {
        {"WASD / LEFT STICK", "MOVE"},
        {"Q E / RIGHT STICK", "ROTATE"},
        {"SPACE / X", "BLEND ANIMATION"},
        {"K / A", "SKELETON DEBUG"},
        {"R / B", "RESET"},
        {"H / Y", "TOGGLE HUD"},
    };
    float lineY = panelY + 78.0f * uiScale;
    for (const ControlLine& line : kLines) {
        addText(line.input, panelX + 20.0f * uiScale, lineY,
                0.78f * uiScale, keyColor);
        addText(line.action, panelX + 270.0f * uiScale, lineY,
                0.78f * uiScale, actionColor);
        lineY += 25.0f * uiScale;
    }

    const float statusPanelWidth = 370.0f * uiScale;
    const float statusPanelHeight = panelHeight;
    const float panelGap = 12.0f * uiScale;
    const bool placeStatusBesideControls =
        panelX + panelWidth + panelGap + statusPanelWidth + 22.0f * uiScale <=
        static_cast<float>(metrics.width);
    const float statusPanelX = placeStatusBesideControls
        ? panelX + panelWidth + panelGap
        : panelX;
    const float statusPanelY = placeStatusBesideControls
        ? panelY
        : panelY + panelHeight + panelGap;

    addQuad(statusPanelX, statusPanelY, statusPanelWidth, statusPanelHeight,
            uvWhite, {0.008f, 0.014f, 0.028f, 0.88f});
    addQuad(statusPanelX, statusPanelY, 5.0f * uiScale, statusPanelHeight,
            uvWhite, {0.36f, 1.0f, 0.54f, 0.94f});
    addQuad(statusPanelX, statusPanelY, statusPanelWidth, 2.0f * uiScale,
            uvWhite, {0.50f, 1.0f, 0.66f, 0.78f});

    const auto animationName = [](uint32_t modelIndex) -> const char* {
        switch (modelIndex) {
        case 1u: return "WALK";
        case 2u: return "SNEAK WALK";
        default: return "BIND POSE";
        }
    };
    const RuntimeSkinnedAnimationBlendState& blend =
        runtimeState_.skinnedAnimationBlend;
    const char* currentAnimation = blend.active
        ? animationName(blend.fromModelIndex)
        : animationName(runtimeState_.selectedSkinnedModelIndex);
    const char* nextAnimation = blend.active
        ? animationName(blend.toModelIndex)
        : "--";
    const float blendProgress = blend.active
        ? (std::clamp)(blend.alpha, 0.0f, 1.0f)
        : 1.0f;
    const std::string blendText = blend.active
        ? std::to_string(static_cast<int>(std::lround(blendProgress * 100.0f))) + "%"
        : "READY";

    const char* skinningPath = "WAITING";
    Vector4 skinningPathColor{0.65f, 0.72f, 0.78f, 1.0f};
    switch (runtimeState_.activeSkinningPath) {
    case RuntimeSkinningPath::ComputeShader:
        skinningPath = "COMPUTE";
        skinningPathColor = {0.36f, 1.0f, 0.54f, 1.0f};
        break;
    case RuntimeSkinningPath::VertexShader:
        skinningPath = "VERTEX SHADER";
        skinningPathColor = {1.0f, 0.72f, 0.24f, 1.0f};
        break;
    case RuntimeSkinningPath::Unavailable:
    default:
        break;
    }

    const SkinnedModelInstance* activeSkinnedModel =
        scene_.GetActiveSkinnedModel();
    const std::string jointCount = activeSkinnedModel != nullptr
        ? std::to_string(activeSkinnedModel->skeleton.joints.size())
        : "0";

    const Vector4 statusLabelColor{0.52f, 0.72f, 0.82f, 1.0f};
    const Vector4 statusValueColor{0.92f, 0.98f, 1.0f, 1.0f};
    const float statusLabelX = statusPanelX + 20.0f * uiScale;
    const float statusValueX = statusPanelX + 155.0f * uiScale;
    float statusLineY = statusPanelY + 78.0f * uiScale;
    addText("ANIMATION LIVE", statusPanelX + 20.0f * uiScale,
            statusPanelY + 42.0f * uiScale, 1.12f * uiScale, titleColor);
    addText("SKINNING", statusLabelX, statusLineY,
            0.78f * uiScale, statusLabelColor);
    addText(skinningPath, statusValueX, statusLineY,
            0.78f * uiScale, skinningPathColor);
    statusLineY += 25.0f * uiScale;
    addText("STATE", statusLabelX, statusLineY,
            0.78f * uiScale, statusLabelColor);
    addText(currentAnimation, statusValueX, statusLineY,
            0.78f * uiScale, statusValueColor);
    statusLineY += 25.0f * uiScale;
    addText("NEXT", statusLabelX, statusLineY,
            0.78f * uiScale, statusLabelColor);
    addText(nextAnimation, statusValueX, statusLineY,
            0.78f * uiScale, blend.active ? keyColor : actionColor);
    statusLineY += 25.0f * uiScale;
    addText("BLEND", statusLabelX, statusLineY,
            0.78f * uiScale, statusLabelColor);
    addText(blendText.c_str(), statusValueX, statusLineY,
            0.78f * uiScale, blend.active ? keyColor : statusValueColor);
    statusLineY += 25.0f * uiScale;
    addText("JOINTS", statusLabelX, statusLineY,
            0.78f * uiScale, statusLabelColor);
    addText(jointCount.c_str(), statusValueX, statusLineY,
            0.78f * uiScale, statusValueColor);

    const float progressX = statusPanelX + 20.0f * uiScale;
    const float progressY = statusPanelY + 199.0f * uiScale;
    const float progressWidth = statusPanelWidth - 40.0f * uiScale;
    const float progressHeight = 8.0f * uiScale;
    addQuad(progressX, progressY, progressWidth, progressHeight, uvWhite,
            {0.08f, 0.14f, 0.18f, 0.94f});
    addQuad(progressX, progressY, progressWidth * blendProgress, progressHeight,
            uvWhite, blend.active
                ? Vector4{0.20f, 0.78f, 1.0f, 1.0f}
                : Vector4{0.28f, 0.88f, 0.46f, 0.88f});
    return submissionHudVertexCount_ > 0;
}

void AppRunLoop::RegisterSubmissionHudPass(
    ID3D12GraphicsCommandList* commandList,
    const std::string& targetResourceName) {
    if (targetResourceName.empty() ||
        !runtimeState_.submissionShowcase.enabled ||
        submissionHudManuallyHidden_) {
        return;
    }
    if (!EnsureSubmissionHudResources(commandList) ||
        !BuildSubmissionHudQuads() ||
        submissionHudVertexCount_ == 0) {
        return;
    }

    renderGraph_.AddPass({
        "UI.SubmissionLiveVisualizer",
        ge3::graphics::RenderPassLayer::Ui,
        {{targetResourceName, ge3::graphics::RenderResourceAccessType::WriteRtv}},
        "",
        [this](ge3::graphics::RenderPassContext& passContext) {
            if (submissionHudVertexCount_ == 0 ||
                railLockOnHudAtlasSrvGpu_.ptr == 0 ||
                submissionHudVertexBufferView_.BufferLocation == 0) {
                return;
            }
            passContext.commandList->RSSetViewports(1, &runtimeState_.viewport);
            passContext.commandList->RSSetScissorRects(1, &runtimeState_.scissorRect);
            passContext.commandList->SetGraphicsRootSignature(
                appPipelines_.GetRailHudAtlasRootSignature());
            passContext.commandList->SetPipelineState(appPipelines_.GetRailHudAtlasPSO());
            ID3D12DescriptorHeap* descriptorHeaps[] = {srvDescriptorHeap_.Get()};
            passContext.commandList->SetDescriptorHeaps(1, descriptorHeaps);
            passContext.commandList->SetGraphicsRootDescriptorTable(
                0, railLockOnHudAtlasSrvGpu_);
            passContext.commandList->IASetVertexBuffers(
                0, 1, &submissionHudVertexBufferView_);
            passContext.commandList->IASetPrimitiveTopology(
                D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            passContext.commandList->DrawInstanced(
                submissionHudVertexCount_, 1, 0, 0);
        },
        true});
}

void AppRunLoop::DrawRailLockOnDebugPanel() {
#if defined(GE3_ENABLE_IMGUI) && GE3_ENABLE_IMGUI
    const auto reasonLabel = [](RailLockRejectReason reason) {
        switch (reason) {
        case RailLockRejectReason::None: return "None";
        case RailLockRejectReason::BehindCamera: return "BehindCamera";
        case RailLockRejectReason::Offscreen: return "Offscreen";
        case RailLockRejectReason::OutOfDepth: return "OutOfDepth";
        case RailLockRejectReason::OutOfForwardRange: return "OutOfForwardRange";
        case RailLockRejectReason::Occluded: return "Occluded";
        case RailLockRejectReason::NotSwept: return "NotSwept";
        case RailLockRejectReason::AlreadyLocked: return "AlreadyLocked";
        case RailLockRejectReason::StackLimit: return "StackLimit";
        case RailLockRejectReason::LockModeInactive: return "LockModeInactive";
        case RailLockRejectReason::LockContainerFull: return "LockContainerFull";
        }
        return "Unknown";
    };

    const RailLockDebugFrame& debug = railShooterLockOnSystem_.DebugFrame();
    const RailAimAssistFrame& aimAssist = railShooterLockOnSystem_.AimAssist();
    const RailReticleState& reticle = debug.reticle;
    const PlayerCombatFeelStats& combatStats = railShooterCombatFeelSystem_.LastStats();
    RailLockSettings settings = railShooterLockOnSystem_.Settings();
    RailAimAssistSettings aimAssistSettings =
        railShooterLockOnSystem_.AimAssistSettings();
    RailSpeedDirectorSettings speedSettings = railShooterSpeedDirector_.Settings();
    const RailSpeedDirectorFrame& speedFrame = railShooterSpeedDirector_.LastFrame();
    RailCameraComfortSettings cameraComfort = railShooterCameraDirector_.ComfortSettings();
    RailCameraAimFocusSettings aimFocusSettings = railShooterCameraDirector_.AimFocusSettings();
    RailCameraLookAtSettings lookAtSettings = railShooterCameraDirector_.LookAtSettings();
    RailCameraCompositionSafetySettings compositionSettings =
        railShooterCameraDirector_.CompositionSafetySettings();
    RailCameraLineOfSightSettings lineOfSightSettings =
        railShooterCameraDirector_.LineOfSightSettings();
    RailCameraCollisionProtectionSettings collisionProtectionSettings =
        railShooterCameraDirector_.CollisionProtectionSettings();
    RailCameraSegmentTransitionSettings segmentTransitionSettings =
        railShooterCameraDirector_.SegmentTransitionSettings();
    RailCameraEncounterFramingSettings encounterFramingSettings =
        railShooterCameraDirector_.EncounterFramingSettings();
    const RailCameraDirectorFrame& cameraFrame = railShooterCameraDirector_.LastFrame();
    CourseEnemyFireSafetySettings fireSafetySettings = railShooterSpawnRuntime_.FireSafetySettings();
    const CourseEnemyFireSafetyStats& fireSafetyStats = railShooterSpawnRuntime_.LastFireSafetyStats();
    EnemyAttackTelegraphSettings& telegraphSettings =
        railEnemyAttackTelegraphSettings_;
    const EnemyAttackTelegraphFrame& telegraphFrame =
        railEnemyAttackTelegraphSystem_.Frame();
    EnemyAttackTelegraphFeedbackSettings& telegraphFeedbackSettings =
        railEnemyAttackTelegraphFeedbackSettings_;
    const EnemyAttackTelegraphFeedbackFrame& telegraphFeedbackFrame =
        railEnemyAttackTelegraphFeedbackBridge_.Frame();
    EnemyProjectileVfxRendererSettings& projectileVisualSettings =
        railEnemyProjectileVfxRendererSettings_;
    const EnemyProjectilePresentationFrame& projectilePresentationFrame =
        railEnemyProjectilePresentationBridge_.Frame();
    const EnemyProjectileVfxRenderFrame& projectileVisualFrame =
        railEnemyProjectileVfxRenderer_.Frame();
    const EnemyAttackLaneTelegraphRenderFrame& laneVisualFrame =
        railEnemyAttackLaneTelegraphRenderer_.Frame();
    const EnemyProjectileShootDownFrame& shootDownFrame =
        railShooterCollisionSystem_.ProjectileShootDown().Frame();
    const EnemyAttackInterruptFrame& interruptFrame =
        railShooterCollisionSystem_.AttackInterrupt().Frame();
    const EnemyAttackDefenseValidationFrame& defenseValidationFrame =
        railEnemyAttackDefenseValidation_.Frame();
    const RailVehicleMountedDefenseFrame& mountedDefenseFrame =
        railShooterMountedDefense_.Frame();
    const IAppSceneState* currentScene = sceneStateManager_.CurrentState();
    const char* currentSceneName = currentScene != nullptr ? currentScene->Name() : "-";
    ImGui::Text(
        "Reticle prev=(%.1f, %.1f) current=(%.1f, %.1f)",
        reticle.previousScreenPosition.x,
        reticle.previousScreenPosition.y,
        reticle.currentScreenPosition.x,
        reticle.currentScreenPosition.y);
    ImGui::Text(
        "World Aim=%s ndc=(%.3f, %.3f) origin=(%.2f, %.2f, %.2f) dir=(%.3f, %.3f, %.3f) range=%.1f",
        reticle.aim.valid ? "valid" : "invalid",
        reticle.aim.normalizedPosition.x,
        reticle.aim.normalizedPosition.y,
        reticle.aim.worldRayOrigin.x,
        reticle.aim.worldRayOrigin.y,
        reticle.aim.worldRayOrigin.z,
        reticle.aim.worldRayDirection.x,
        reticle.aim.worldRayDirection.y,
        reticle.aim.worldRayDirection.z,
        reticle.aim.maxDistance);
    ImGui::Text(
        "World Hit=%s kind=%s distance=%.2f point=(%.2f, %.2f, %.2f) actor=%u source=%u",
        reticle.aim.hasWorldHit ? "true" : "false",
        ToRailAimHitKindString(reticle.aim.hitKind),
        reticle.aim.aimDistance,
        reticle.aim.worldAimPoint.x,
        reticle.aim.worldAimPoint.y,
        reticle.aim.worldAimPoint.z,
        reticle.aim.hitActorId,
        reticle.aim.hitSourceIndex);
    const WeaponHitRequest& lastHitRequest =
        railShooterCollisionSystem_.LastWeaponHitRequest();
    const DamageResult& lastDamage = railShooterCollisionSystem_.LastDamageResult();
    const WeaponFeedbackDispatchResult& lastFeedback =
        railShooterCollisionSystem_.LastWeaponFeedbackResult();
    const WeaponFireResult& lastFire = railShooterCollisionSystem_.LastWeaponFireResult();
    const WeaponDefinitionRegistryStats& weaponRegistry =
        railShooterCollisionSystem_.WeaponDefinitions().Stats();
    const RailAimAssistPresetRegistryStats& aimPresetRegistry =
        railAimAssistPresetRegistry_.Stats();
    const RailVehicleControlRegistryStats& vehicleControlRegistry =
        railVehicleControlPresetRegistry_.Stats();
    ImGui::Text(
        "Damage shot=%llu accepted=%s resolved=%s applied=%.1f/%.1f hp=%.1f destroyed=%s blocked=%s reason=%s",
        static_cast<unsigned long long>(lastHitRequest.shotId),
        lastDamage.requestAccepted ? "true" : "false",
        lastDamage.targetResolved ? "true" : "false",
        lastDamage.appliedDamage,
        lastDamage.requestedDamage,
        lastDamage.remainingHitPoints,
        lastDamage.destroyed ? "true" : "false",
        lastDamage.blocked ? "true" : "false",
        ToDamageRejectReasonString(lastDamage.rejectReason));
    ImGui::Text(
        "Feedback accepted=%s kind=%s intensity=%.2f marker=%.2f vfx=%s reason=%s",
        lastFeedback.accepted ? "true" : "false",
        ToHitFeedbackKindString(lastFeedback.event.feedbackKind),
        lastFeedback.event.intensity,
        railShooterCollisionSystem_.WeaponFeedback().HitMarkerNormalizedTime(),
        lastFeedback.vfxSpawned ? "true" : "false",
        ToWeaponFeedbackRejectReasonString(lastFeedback.rejectReason));
    ImGui::Text(
        "Fire weapon=%s fired=%s projectiles=%u cooldown=%.3f heat=%.2f ammo=%u reserve=%u reason=%s",
        lastFire.weaponId.empty() ? "-" : lastFire.weaponId.c_str(),
        lastFire.fired ? "true" : "false",
        static_cast<unsigned int>(lastFire.shots.size()),
        lastFire.cooldownRemaining,
        lastFire.heat,
        lastFire.ammoInMagazine,
        lastFire.reserveAmmo,
        ToWeaponFireRejectReasonString(lastFire.rejectReason));
    ImGui::Text(
        "Weapon assets revision=%llu loaded=%u fallback=%u reloads=%llu failures=%llu",
        static_cast<unsigned long long>(weaponRegistry.revision),
        weaponRegistry.loadedAssetCount,
        weaponRegistry.fallbackAssetCount,
        static_cast<unsigned long long>(weaponRegistry.successfulReloads),
        static_cast<unsigned long long>(weaponRegistry.failedReloads));
    ImGui::TextWrapped("Weapon assets: %s", weaponRegistry.status.c_str());
    ImGui::Text(
        "Aim presets active=%s revision=%llu loaded=%u fallback=%u reloads=%llu failures=%llu",
        railAimAssistAppliedPresetId_.empty()
            ? "-"
            : railAimAssistAppliedPresetId_.c_str(),
        static_cast<unsigned long long>(aimPresetRegistry.revision),
        aimPresetRegistry.loadedPresetCount,
        aimPresetRegistry.fallbackPresetCount,
        static_cast<unsigned long long>(aimPresetRegistry.successfulReloads),
        static_cast<unsigned long long>(aimPresetRegistry.failedReloads));
    ImGui::TextWrapped("Aim presets: %s", aimPresetRegistry.status.c_str());
    ImGui::Text(
        "Vehicle controls active=%s revision=%llu loaded=%u fallback=%u reloads=%llu failures=%llu",
        railVehicleControlAppliedPresetId_.empty()
            ? "-" : railVehicleControlAppliedPresetId_.c_str(),
        static_cast<unsigned long long>(vehicleControlRegistry.revision),
        vehicleControlRegistry.loadedPresetCount,
        vehicleControlRegistry.fallbackPresetCount,
        static_cast<unsigned long long>(vehicleControlRegistry.successfulReloads),
        static_cast<unsigned long long>(vehicleControlRegistry.failedReloads));
    ImGui::TextWrapped(
        "Vehicle controls: %s", vehicleControlRegistry.status.c_str());
    ImGui::Text(
        "Lock held=%s pressed=%s released=%s accepted=%d fired=%d",
        reticle.lockHeld ? "true" : "false",
        reticle.lockPressed ? "true" : "false",
        reticle.lockReleased ? "true" : "false",
        debug.acceptedThisFrame,
        debug.releasedThisFrame);
    ImGui::Text(
        "Anchors=%d Candidates=%d Tokens=%d",
        debug.anchorCount,
        static_cast<int>(debug.candidates.size()),
        static_cast<int>(debug.tokens.size()));
    ImGui::Text(
        "Combat score=%u combo=%u max=%u lastLockScore=%u tokens=%u hits=%u timing=%s",
        combatStats.score,
        combatStats.combo,
        combatStats.maxCombo,
        combatStats.lastLockScore,
        combatStats.lastLockTokenCount,
        combatStats.lastLockHitCount,
        combatStats.lastLockWasMax ? "MAX" : (combatStats.lastLockWasEarly ? "EARLY" : "-"));
    ImGui::Text(
        "Role split: normalShot=%s lockMax=%d normalDamage=%.1f lockDamage=%.1f",
        reticle.lockHeld ? "suppressed while locking" : "manual rapid fire",
        settings.maxLocks,
        railShooterCollisionSystem_.Weapon().damage,
        settings.releaseDamage);
    ImGui::Text(
        "Aim feel=%s target=(%.1f, %.1f) strength=%.2f pull=%.2f score=%.2f",
        reticle.aimFeelActive ? "active" : "idle",
        reticle.aimFeelTargetScreenPosition.x,
        reticle.aimFeelTargetScreenPosition.y,
        reticle.aimFeelStrength,
        reticle.aimFeelPullPixels,
        reticle.aimFeelTargetScore);
    ImGui::Text(
        "Aim assist=%s device=%s target=%u score=%.3f correction=%.3fdeg strength=%.3f friction=%s next/applied=%.3f/%.3f visibility=%u",
        aimAssist.active ? "active" : "idle",
        ToRailAimAssistInputDeviceString(aimAssist.inputDevice),
        aimAssist.target.actorId,
        aimAssist.targetScore,
        aimAssist.correctionDegrees,
        aimAssist.appliedStrength,
        aimAssist.frictionActive ? "active" : "idle",
        aimAssist.inputFrictionScale,
        reticle.appliedAimFrictionScale,
        aimAssist.visibilityQueries);

    if (ImGui::CollapsingHeader("Rail Aim Assist", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Checkbox("Aim Assist Enabled", &aimAssistSettings.enabled);
        ImGui::Checkbox("Mouse/Keyboard Assist", &aimAssistSettings.mouseKeyboardEnabled);
        ImGui::Checkbox("Gamepad Assist", &aimAssistSettings.gamepadEnabled);
        ImGui::Checkbox("Require World Visibility", &aimAssistSettings.requireWorldVisibility);
        ImGui::DragFloat(
            "Mouse Acquire Angle",
            &aimAssistSettings.mouseAcquireAngleDegrees,
            0.05f,
            0.1f,
            20.0f,
            "%.2f deg");
        ImGui::DragFloat(
            "Gamepad Acquire Angle",
            &aimAssistSettings.gamepadAcquireAngleDegrees,
            0.05f,
            0.1f,
            30.0f,
            "%.2f deg");
        ImGui::DragFloat(
            "Mouse Magnetism",
            &aimAssistSettings.mouseMagnetismStrength,
            0.01f,
            0.0f,
            1.0f,
            "%.2f");
        ImGui::DragFloat(
            "Gamepad Magnetism",
            &aimAssistSettings.gamepadMagnetismStrength,
            0.01f,
            0.0f,
            1.0f,
            "%.2f");
        ImGui::DragFloat(
            "Switch Advantage",
            &aimAssistSettings.targetSwitchAdvantage,
            0.01f,
            0.0f,
            1.0f,
            "%.2f");
        ImGui::DragFloat(
            "Retention Seconds",
            &aimAssistSettings.targetRetentionSeconds,
            0.01f,
            0.0f,
            1.0f,
            "%.2f s");
        ImGui::Text(
            "Raw dir=(%.4f, %.4f, %.4f) assisted dir=(%.4f, %.4f, %.4f) candidates=%zu",
            aimAssist.rawAim.worldRayDirection.x,
            aimAssist.rawAim.worldRayDirection.y,
            aimAssist.rawAim.worldRayDirection.z,
            aimAssist.assistedAim.worldRayDirection.x,
            aimAssist.assistedAim.worldRayDirection.y,
            aimAssist.assistedAim.worldRayDirection.z,
            aimAssist.candidates.size());
    }

    if (ImGui::CollapsingHeader("Camera/Rail Tuning Recorder P1-B-12", ImGuiTreeNodeFlags_DefaultOpen)) {
        bool recording = railCameraTuningRecorder_.recording;
        if (ImGui::Checkbox("Record Camera/Rail CSV", &recording)) {
            if (recording) {
                StartRailCameraTuningRecording();
            } else {
                StopRailCameraTuningRecording();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Export CSV")) {
            std::string exportedPath;
            ExportRailCameraTuningCsv(&exportedPath);
        }
        ImGui::SameLine();
        if (ImGui::Button("Clear Buffer")) {
            ClearRailCameraTuningRecording();
        }

        int sampleStride = static_cast<int>(railCameraTuningRecorder_.sampleStride);
        int maxSamples = static_cast<int>(railCameraTuningRecorder_.maxSamples);
        ImGui::DragInt("Sample Stride", &sampleStride, 1.0f, 1, 60);
        ImGui::DragInt("Max Samples", &maxSamples, 100.0f, 300, 60000);
        railCameraTuningRecorder_.sampleStride = static_cast<uint32_t>((std::clamp)(sampleStride, 1, 60));
        railCameraTuningRecorder_.maxSamples = static_cast<uint32_t>((std::clamp)(maxSamples, 300, 60000));

        ImGui::Text(
            "status=%s samples=%zu recorded=%u dropped=%u time=%.2f sec",
            railCameraTuningRecorder_.status.c_str(),
            railCameraTuningRecorder_.samples.size(),
            railCameraTuningRecorder_.recordedSamples,
            railCameraTuningRecorder_.droppedSamples,
            railCameraTuningRecorder_.recordingTimeSeconds);
        ImGui::Text(
            "lastExport=%s",
            railCameraTuningRecorder_.lastExportPath.empty()
                ? "-"
                : railCameraTuningRecorder_.lastExportPath.c_str());
        ImGui::Text(
            "captures speed/camera/comfort/composition/encounter/input/collision/frame timing into logs/rail_camera_tuning_*.csv");
    }

    if (ImGui::CollapsingHeader("Rail Speed Director P1-B-1", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Checkbox("Speed Director Enabled", &speedSettings.enabled);
        ImGui::Text(
            "Mode=%s section=%s reason=%s",
            speedFrame.modeName.c_str(),
            speedFrame.sectionName.c_str(),
            speedFrame.reason.c_str());
        ImGui::Text(
            "distance=%.1f base=%.2f requested=%.2f actual=%.2f zone=%.2f event=%.2f",
            speedFrame.distance,
            speedFrame.baseSpeed,
            speedFrame.requestedSpeed,
            railShooterVehicleMovement_.State().speed,
            speedFrame.zoneMultiplier,
            speedFrame.eventMultiplier);
        ImGui::DragFloat("Min Speed", &speedSettings.minSpeed, 0.25f, 1.0f, 80.0f, "%.2f");
        ImGui::DragFloat("Max Speed", &speedSettings.maxSpeed, 0.25f, 8.0f, 160.0f, "%.2f");
        ImGui::DragFloat("Cruise Mult", &speedSettings.cruiseMultiplier, 0.01f, 0.25f, 2.0f, "%.2f");
        ImGui::DragFloat("Combat Mult", &speedSettings.combatMultiplier, 0.01f, 0.25f, 2.0f, "%.2f");
        ImGui::DragFloat("Tunnel Mult", &speedSettings.tunnelMultiplier, 0.01f, 0.25f, 2.0f, "%.2f");
        ImGui::DragFloat("Boss Mult", &speedSettings.bossMultiplier, 0.01f, 0.25f, 2.0f, "%.2f");
        ImGui::DragFloat("Setpiece Mult", &speedSettings.setpieceMultiplier, 0.01f, 0.25f, 2.0f, "%.2f");
        ImGui::DragFloat("High Speed Mult", &speedSettings.highSpeedMultiplier, 0.01f, 0.25f, 2.0f, "%.2f");
        ImGui::DragFloat("Cinematic Mult", &speedSettings.cinematicMultiplier, 0.01f, 0.25f, 2.0f, "%.2f");
        ImGui::DragFloat("Recovery Mult", &speedSettings.recoveryMultiplier, 0.01f, 0.25f, 2.0f, "%.2f");
        ImGui::DragFloat("Event Slow Mult", &speedSettings.eventSlowMultiplier, 0.01f, 0.25f, 1.0f, "%.2f");
        ImGui::DragFloat("Event Boost Mult", &speedSettings.eventBoostMultiplier, 0.01f, 1.0f, 2.0f, "%.2f");
        ImGui::DragFloat("Event Blend Duration", &speedSettings.eventBlendDuration, 0.01f, 0.05f, 5.0f, "%.2f");

        speedSettings.minSpeed = (std::max)(1.0f, speedSettings.minSpeed);
        speedSettings.maxSpeed = (std::max)(speedSettings.minSpeed, speedSettings.maxSpeed);
        speedSettings.cruiseMultiplier = (std::max)(0.25f, speedSettings.cruiseMultiplier);
        speedSettings.combatMultiplier = (std::max)(0.25f, speedSettings.combatMultiplier);
        speedSettings.tunnelMultiplier = (std::max)(0.25f, speedSettings.tunnelMultiplier);
        speedSettings.bossMultiplier = (std::max)(0.25f, speedSettings.bossMultiplier);
        speedSettings.setpieceMultiplier = (std::max)(0.25f, speedSettings.setpieceMultiplier);
        speedSettings.highSpeedMultiplier = (std::max)(0.25f, speedSettings.highSpeedMultiplier);
        speedSettings.cinematicMultiplier = (std::max)(0.25f, speedSettings.cinematicMultiplier);
        speedSettings.recoveryMultiplier = (std::max)(0.25f, speedSettings.recoveryMultiplier);
        speedSettings.eventSlowMultiplier = (std::clamp)(speedSettings.eventSlowMultiplier, 0.25f, 1.0f);
        speedSettings.eventBoostMultiplier = (std::max)(1.0f, speedSettings.eventBoostMultiplier);
        speedSettings.eventBlendDuration = (std::max)(0.05f, speedSettings.eventBlendDuration);
        ImGui::Text(
            "Movement authority: accel %.2f / service brake %.2f / emergency brake %.2f",
            railShooterVehicleMovement_.Definition().acceleration,
            railShooterVehicleMovement_.Definition().serviceBrakeDeceleration,
            railShooterVehicleMovement_.Definition().emergencyBrakeDeceleration);
        ImGui::TextUnformatted(
            "Director emits policy requests only; RailVehicleMovementSystem integrates speed and distance.");
    }

    if (ImGui::CollapsingHeader("Camera Director State P1-B-2", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Checkbox("Comfort Metrics Enabled", &cameraComfort.enabled);
        ImGui::Text(
            "Mode=%s shot=%s stable=%s hard=%s enemyFire=%s reason=%s",
            ToRailCameraDirectorModeString(cameraFrame.modeKind),
            cameraFrame.mode.c_str(),
            cameraFrame.stableForAiming ? "true" : "false",
            cameraFrame.hardTransition ? "true" : "false",
            cameraFrame.allowEnemyFire ? "true" : "false",
            cameraFrame.comfortReason.c_str());
        ImGui::Text(
            "speed rail=%.2f cameraLinear=%.2f stability=%.2f shake=%.2f",
            cameraFrame.railSpeed,
            cameraFrame.linearSpeed,
            cameraFrame.stabilityScore,
            cameraFrame.shakeAmount);
        ImGui::Text(
            "shot=%s preset=%s blend=%s curve=%s weight=%.2f",
            cameraFrame.cinematicShotId.c_str(),
            cameraFrame.cinematicShotPresetId.c_str(),
            cameraFrame.cinematicShotBlendAssetId.c_str(),
            cameraFrame.cinematicShotBlendCurve.c_str(),
            cameraFrame.cinematicShotWeight);
        ImGui::Text(
            "angularVel=%.1f deg/s angularAccel=%.1f deg/s2 fovRate=%.1f deg/s roll=%.1f deg",
            cameraFrame.angularVelocityDeg,
            cameraFrame.angularAccelerationDeg,
            cameraFrame.fovChangeRateDeg,
            cameraFrame.rollDeg);
        ImGui::DragFloat(
            "Stable Angular Velocity",
            &cameraComfort.stableAngularVelocityDeg,
            1.0f,
            1.0f,
            180.0f,
            "%.1f deg/s");
        ImGui::DragFloat(
            "Stable Angular Accel",
            &cameraComfort.stableAngularAccelerationDeg,
            5.0f,
            10.0f,
            3000.0f,
            "%.1f deg/s2");
        ImGui::DragFloat(
            "Stable FOV Rate",
            &cameraComfort.stableFovChangeRateDeg,
            1.0f,
            1.0f,
            180.0f,
            "%.1f deg/s");
        ImGui::DragFloat("Stable Roll", &cameraComfort.stableRollDeg, 0.5f, 0.0f, 45.0f, "%.1f deg");
        ImGui::DragFloat("Stable Shake", &cameraComfort.stableShakeAmount, 0.01f, 0.0f, 3.0f, "%.2f");
        ImGui::DragFloat(
            "Hard Angular Velocity",
            &cameraComfort.hardTransitionAngularVelocityDeg,
            1.0f,
            1.0f,
            360.0f,
            "%.1f deg/s");
        ImGui::DragFloat(
            "Hard FOV Rate",
            &cameraComfort.hardTransitionFovChangeRateDeg,
            1.0f,
            1.0f,
            360.0f,
            "%.1f deg/s");
        ImGui::DragFloat(
            "Hard Roll",
            &cameraComfort.hardTransitionRollDeg,
            0.5f,
            0.0f,
            90.0f,
            "%.1f deg");

        cameraComfort.stableAngularVelocityDeg =
            (std::max)(1.0f, cameraComfort.stableAngularVelocityDeg);
        cameraComfort.stableAngularAccelerationDeg =
            (std::max)(10.0f, cameraComfort.stableAngularAccelerationDeg);
        cameraComfort.stableFovChangeRateDeg =
            (std::max)(1.0f, cameraComfort.stableFovChangeRateDeg);
        cameraComfort.stableRollDeg = (std::max)(0.0f, cameraComfort.stableRollDeg);
        cameraComfort.stableShakeAmount = (std::max)(0.0f, cameraComfort.stableShakeAmount);
        cameraComfort.hardTransitionAngularVelocityDeg =
            (std::max)(cameraComfort.stableAngularVelocityDeg, cameraComfort.hardTransitionAngularVelocityDeg);
        cameraComfort.hardTransitionFovChangeRateDeg =
            (std::max)(cameraComfort.stableFovChangeRateDeg, cameraComfort.hardTransitionFovChangeRateDeg);
        cameraComfort.hardTransitionRollDeg =
            (std::max)(cameraComfort.stableRollDeg, cameraComfort.hardTransitionRollDeg);
        ImGui::TextUnformatted("These flags are the bridge for aim stabilization and enemy fire gating in later P1-B steps.");
    }

    if (ImGui::CollapsingHeader("Look-At Target / Threat Center P1-B-4", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Checkbox("Look-At Director Enabled", &lookAtSettings.enabled);
        ImGui::Text(
            "Policy=%s reason=%s candidates=%d blend=%.2f weight=%.2f",
            ToRailCameraLookAtPolicyString(cameraFrame.lookAtPolicy),
            cameraFrame.lookAtReason.c_str(),
            cameraFrame.lookAtCandidateCount,
            cameraFrame.lookAtBlend,
            cameraFrame.lookAtWeight);
        ImGui::Text(
            "base=(%.1f, %.1f, %.1f) threat=(%.1f, %.1f, %.1f) target=(%.1f, %.1f, %.1f)",
            cameraFrame.baseTarget.x,
            cameraFrame.baseTarget.y,
            cameraFrame.baseTarget.z,
            cameraFrame.threatCenter.x,
            cameraFrame.threatCenter.y,
            cameraFrame.threatCenter.z,
            cameraFrame.target.x,
            cameraFrame.target.y,
            cameraFrame.target.z);
        ImGui::Text(
            "runtime enemies=%zu obstacles=%zu lockTokens=%zu",
            railShooterSpawnRuntime_.ActiveEnemyCount(),
            railShooterSpawnRuntime_.ActiveObstacleCount(),
            railShooterLockOnSystem_.Tokens().size());
        ImGui::DragFloat("Blend Rate", &lookAtSettings.blendRate, 0.1f, 0.0f, 30.0f, "%.2f");
        ImGui::DragFloat("Release Blend Rate", &lookAtSettings.releaseBlendRate, 0.1f, 0.0f, 30.0f, "%.2f");
        ImGui::DragFloat("Min Forward", &lookAtSettings.minForwardDistance, 1.0f, -120.0f, 80.0f, "%.1f");
        ImGui::DragFloat("Max Forward", &lookAtSettings.maxForwardDistance, 1.0f, 20.0f, 420.0f, "%.1f");
        ImGui::DragFloat("Lock Token Weight", &lookAtSettings.lockTokenWeight, 0.05f, 0.0f, 12.0f, "%.2f");
        ImGui::DragFloat("Enemy Weight", &lookAtSettings.enemyWeight, 0.05f, 0.0f, 8.0f, "%.2f");
        ImGui::DragFloat("Boss Weight", &lookAtSettings.bossWeight, 0.05f, 0.0f, 12.0f, "%.2f");
        ImGui::DragFloat("Obstacle Weight", &lookAtSettings.obstacleWeight, 0.05f, 0.0f, 8.0f, "%.2f");
        ImGui::DragFloat("Center Retention", &lookAtSettings.centerRetention, 0.01f, 0.0f, 1.0f, "%.2f");
        ImGui::DragFloat("Max Target Offset", &lookAtSettings.maxTargetOffset, 1.0f, 0.0f, 160.0f, "%.1f");

        lookAtSettings.blendRate = (std::max)(0.0f, lookAtSettings.blendRate);
        lookAtSettings.releaseBlendRate = (std::max)(0.0f, lookAtSettings.releaseBlendRate);
        lookAtSettings.maxForwardDistance =
            (std::max)(lookAtSettings.minForwardDistance + 1.0f, lookAtSettings.maxForwardDistance);
        lookAtSettings.lockTokenWeight = (std::max)(0.0f, lookAtSettings.lockTokenWeight);
        lookAtSettings.enemyWeight = (std::max)(0.0f, lookAtSettings.enemyWeight);
        lookAtSettings.bossWeight = (std::max)(0.0f, lookAtSettings.bossWeight);
        lookAtSettings.obstacleWeight = (std::max)(0.0f, lookAtSettings.obstacleWeight);
        lookAtSettings.centerRetention = (std::clamp)(lookAtSettings.centerRetention, 0.0f, 1.0f);
        lookAtSettings.maxTargetOffset = (std::max)(0.0f, lookAtSettings.maxTargetOffset);
        ImGui::TextUnformatted("Lock tokens override ambient threat center; otherwise active enemies steer the camera target.");
    }

    if (ImGui::CollapsingHeader("Gameplay Safety / Enemy Fire P1-B-5", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Checkbox("Enemy Fire Safety Enabled", &fireSafetySettings.enabled);
        ImGui::Checkbox("Require Camera Allows Fire", &fireSafetySettings.requireCameraAllowsFire);
        ImGui::Text(
            "Camera allow=%s stable=%s hard=%s reason=%s",
            cameraFrame.allowEnemyFire ? "true" : "false",
            cameraFrame.stableForAiming ? "true" : "false",
            cameraFrame.hardTransition ? "true" : "false",
            cameraFrame.comfortReason.c_str());
        ImGui::Text(
            "Enemies active=%u allowed=%u blocked camera=%u range=%u visibleTime=%u bullets=%u",
            fireSafetyStats.activeEnemies,
            fireSafetyStats.allowedEnemies,
            fireSafetyStats.blockedByCamera,
            fireSafetyStats.blockedByRange,
            fireSafetyStats.blockedByVisibilityTime,
            fireSafetyStats.bulletsEmitted);
        ImGui::Text(
            "Last allowed=%s  last blocked=%s",
            fireSafetyStats.lastAllowedReason.c_str(),
            fireSafetyStats.lastBlockedReason.c_str());
        ImGui::DragFloat("Fire Min Forward", &fireSafetySettings.minForwardDistance, 1.0f, 0.0f, 80.0f, "%.1f");
        ImGui::DragFloat("Fire Max Forward", &fireSafetySettings.maxForwardDistance, 1.0f, 20.0f, 360.0f, "%.1f");
        ImGui::DragFloat("Min Visible Before Fire", &fireSafetySettings.minVisibleBeforeFire, 0.01f, 0.0f, 2.0f, "%.2f");
        ImGui::DragFloat("Blocked Retry Delay", &fireSafetySettings.blockedRetryDelay, 0.005f, 0.01f, 0.5f, "%.3f");

        fireSafetySettings.maxForwardDistance =
            (std::max)(fireSafetySettings.minForwardDistance + 1.0f, fireSafetySettings.maxForwardDistance);
        fireSafetySettings.minVisibleBeforeFire = (std::max)(0.0f, fireSafetySettings.minVisibleBeforeFire);
        fireSafetySettings.blockedRetryDelay = (std::max)(0.01f, fireSafetySettings.blockedRetryDelay);
        std::string fireSafetyError;
        if (!railShooterSpawnRuntime_.ConfigureFireSafety(fireSafetySettings, &fireSafetyError)) {
            ImGui::TextUnformatted(fireSafetyError.c_str());
        }

        int shown = 0;
        for (const CourseEnemyActor& enemy : railShooterSpawnRuntime_.Enemies()) {
            if (shown++ >= 12) {
                ImGui::TextUnformatted("...");
                break;
            }
            const float actorDistance = enemy.desc.spawnDistance + enemy.desc.distanceOffset;
            ImGui::BulletText(
                "actor=%u role=%s fire=%s reason=%s forward=%.1f visible=%.2f timer=%.2f",
                enemy.actorId,
                enemy.desc.role.c_str(),
                enemy.fireSafetyAllowed ? "allowed" : "blocked",
                enemy.fireSafetyReason.c_str(),
                actorDistance - railShooterDistance_,
                enemy.fireVisibleTime,
                enemy.fireTimer);
        }
        ImGui::TextUnformatted("This uses the previous camera frame so enemy shots never occur during hard camera transitions.");
    }

    if (ImGui::CollapsingHeader("Enemy Attack Telegraph", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Checkbox("Telegraph Enabled", &telegraphSettings.enabled);
        ImGui::Checkbox("Telegraph World Visibility", &telegraphSettings.requireWorldVisibility);
        ImGui::Checkbox("Suppress Occluded Telegraphs", &telegraphSettings.suppressOccluded);
        ImGui::DragFloat("Telegraph Lead Seconds", &telegraphSettings.leadSeconds, 0.01f, 0.10f, 3.0f, "%.2f");
        ImGui::DragFloat("Telegraph Imminent Seconds", &telegraphSettings.imminentSeconds, 0.01f, 0.03f, 1.0f, "%.2f");
        ImGui::DragFloat("Telegraph Fired Flash", &telegraphSettings.firedFlashSeconds, 0.01f, 0.03f, 0.8f, "%.2f");
        ImGui::DragFloat("Telegraph Safe Area", &telegraphSettings.safeAreaPixels, 1.0f, 12.0f, 240.0f, "%.0f px");
        int maximumVisibleCues = static_cast<int>(telegraphSettings.maximumVisibleCues);
        if (ImGui::DragInt("Telegraph Visible Cue Limit", &maximumVisibleCues, 1.0f, 1, 24)) {
            telegraphSettings.maximumVisibleCues = static_cast<uint32_t>(
                (std::clamp)(maximumVisibleCues, 1, 24));
        }
        telegraphSettings.leadSeconds = (std::max)(0.10f, telegraphSettings.leadSeconds);
        telegraphSettings.imminentSeconds = (std::clamp)(
            telegraphSettings.imminentSeconds, 0.03f, telegraphSettings.leadSeconds);
        telegraphSettings.firedFlashSeconds = (std::max)(0.03f, telegraphSettings.firedFlashSeconds);
        ImGui::Text(
            "Cues=%u on=%u off=%u occluded=%u suppressed=%u LOS=%u budgetMiss=%u priority=%.2f revision=%llu",
            telegraphFrame.stats.visibleCues,
            telegraphFrame.stats.onScreenCues,
            telegraphFrame.stats.offscreenCues,
            telegraphFrame.stats.occludedCues,
            telegraphFrame.stats.prioritySuppressed,
            telegraphFrame.stats.visibilityQueries,
            telegraphFrame.stats.visibilityBudgetExhausted,
            telegraphFrame.highestPriority,
            static_cast<unsigned long long>(telegraphFrame.revision));
        ImGui::SeparatorText("Production Threat Visual Pipeline");
        ImGui::Checkbox(
            "Production Projectile Readability Guard",
            &projectileVisualSettings.productionPrimitivesEnabled);
        ImGui::Text(
            "Runtime=%u presentation=%zu submitted=%u effects=%u fallback=%u unavailable=%u culled=%u budget=%u",
            railShooterSpawnRuntime_.EnemyProjectiles().Frame().activeProjectiles,
            projectilePresentationFrame.projectiles.size(),
            projectileVisualFrame.productionSubmittedProjectiles,
            projectileVisualFrame.effectBackedProjectiles,
            projectileVisualFrame.fallbackProjectiles,
            projectileVisualFrame.unavailableProjectiles,
            projectileVisualFrame.culledByDistance,
            projectileVisualFrame.droppedByBudget);
        ImGui::Text(
            "Telegraph cues=%zu production lanes=%u marker effects=%u lane budget=%u",
            telegraphFrame.cues.size(),
            laneVisualFrame.productionSubmittedLanes,
            laneVisualFrame.effectBackedMarkers,
            laneVisualFrame.droppedByBudget);
        ImGui::Text(
            "Defense=%s strength=%.2f hitbox=%.2f invulnerable=%s | shotDown=%u interrupted=%u",
            ToString(mountedDefenseFrame.state.action),
            mountedDefenseFrame.state.actionStrength,
            mountedDefenseFrame.state.hitboxRadiusScale,
            mountedDefenseFrame.invulnerable ? "true" : "false",
            shootDownFrame.projectilesDestroyed,
            interruptFrame.interruptedAttacks);
        ImGui::Text(
            "Defense validation actors=%u valid=%u warnings=%u errors=%u revision=%llu",
            defenseValidationFrame.validatedActors,
            defenseValidationFrame.validAttacks,
            defenseValidationFrame.warnings,
            defenseValidationFrame.errors,
            static_cast<unsigned long long>(defenseValidationFrame.revision));
        for (const EnemyAttackTelegraphEvent& event : telegraphFrame.events) {
            ImGui::BulletText(
                "event=%s actor=%u sequence=%llu severity=%.2f offscreen=%s",
                ToEnemyAttackTelegraphEventKindString(event.kind),
                event.actorId,
                static_cast<unsigned long long>(event.fireSequence),
                event.severity,
                event.offscreen ? "true" : "false");
        }
        ImGui::SeparatorText("Feedback Bridge");
        ImGui::Checkbox("Telegraph Feedback Enabled", &telegraphFeedbackSettings.enabled);
        ImGui::Checkbox("Telegraph Warning Audio", &telegraphFeedbackSettings.audioEnabled);
        ImGui::Checkbox("Telegraph Warning Haptics", &telegraphFeedbackSettings.hapticsEnabled);
        ImGui::DragFloat("Telegraph Master Volume", &telegraphFeedbackSettings.masterVolume, 0.01f, 0.0f, 1.0f, "%.2f");
        ImGui::DragFloat("Telegraph Stereo Pan", &telegraphFeedbackSettings.stereoPanStrength, 0.01f, 0.0f, 1.0f, "%.2f");
        ImGui::DragFloat("Offscreen Warning Boost", &telegraphFeedbackSettings.offscreenVolumeBoost, 0.01f, 0.0f, 0.5f, "%.2f");
        telegraphFeedbackSettings.masterVolume = (std::clamp)(
            telegraphFeedbackSettings.masterVolume, 0.0f, 1.0f);
        telegraphFeedbackSettings.stereoPanStrength = (std::clamp)(
            telegraphFeedbackSettings.stereoPanStrength, 0.0f, 1.0f);
        telegraphFeedbackSettings.offscreenVolumeBoost = (std::clamp)(
            telegraphFeedbackSettings.offscreenVolumeBoost, 0.0f, 0.5f);
        ImGui::Text(
            "Feedback events=%u audio=%u haptic=%u cooldown=%u budget=%u inactive=%u motors=%.2f/%.2f revision=%llu",
            telegraphFeedbackFrame.stats.inputEvents,
            telegraphFeedbackFrame.stats.audioCommands,
            telegraphFeedbackFrame.stats.hapticEvents,
            telegraphFeedbackFrame.stats.suppressedCooldown,
            telegraphFeedbackFrame.stats.suppressedBudget,
            telegraphFeedbackFrame.stats.suppressedInactive,
            telegraphFeedbackFrame.haptics.lowFrequencyMotor,
            telegraphFeedbackFrame.haptics.highFrequencyMotor,
            static_cast<unsigned long long>(telegraphFeedbackFrame.revision));
        for (const EnemyAttackTelegraphAudioCommand& command :
             telegraphFeedbackFrame.audioCommands) {
            ImGui::BulletText(
                "audio=%s actor=%u volume=%.2f pan=%.2f pitch=%.2f priority=%.2f",
                ToEnemyAttackTelegraphEventKindString(command.kind),
                command.actorId,
                command.volume,
                command.pan,
                command.pitch,
                command.priority);
        }
        ImGui::TextUnformatted("HUD, positional warning audio, and XInput haptics consume the same ordered event frame.");
    }

    if (ImGui::CollapsingHeader("Aimable Zone / Visibility Overlay P1-B-6", ImGuiTreeNodeFlags_DefaultOpen)) {
        RailVisibilityDebugOverlaySettings& overlay = railVisibilityDebugOverlay_;
        ImGui::Checkbox("Overlay Enabled", &overlay.enabled);
        ImGui::Checkbox("Show Aimable Zone", &overlay.showAimableZone);
        ImGui::Checkbox("Show Actors", &overlay.showActors);
        ImGui::Checkbox("Show Labels", &overlay.showLabels);
        ImGui::Checkbox("Show Threat Center", &overlay.showThreatCenter);
        ImGui::Text(
            "Enemies=%zu Obstacles=%zu CameraFire=%s ThreatPolicy=%s",
            railShooterSpawnRuntime_.ActiveEnemyCount(),
            railShooterSpawnRuntime_.ActiveObstacleCount(),
            cameraFrame.allowEnemyFire ? "allowed" : "blocked",
            ToRailCameraLookAtPolicyString(cameraFrame.lookAtPolicy));
        ImGui::DragFloat("Aimable Zone Width", &overlay.aimableZoneWidth, 0.01f, 0.10f, 1.00f, "%.2f");
        ImGui::DragFloat("Aimable Zone Height", &overlay.aimableZoneHeight, 0.01f, 0.10f, 1.00f, "%.2f");
        ImGui::DragFloat("Readability Zone Width", &overlay.warningZoneWidth, 0.01f, 0.10f, 1.00f, "%.2f");
        ImGui::DragFloat("Readability Zone Height", &overlay.warningZoneHeight, 0.01f, 0.10f, 1.00f, "%.2f");

        overlay.aimableZoneWidth = (std::clamp)(overlay.aimableZoneWidth, 0.10f, 1.00f);
        overlay.aimableZoneHeight = (std::clamp)(overlay.aimableZoneHeight, 0.10f, 1.00f);
        overlay.warningZoneWidth = (std::clamp)(
            overlay.warningZoneWidth,
            (std::min)(1.0f, overlay.aimableZoneWidth + 0.02f),
            1.00f);
        overlay.warningZoneHeight = (std::clamp)(
            overlay.warningZoneHeight,
            (std::min)(1.0f, overlay.aimableZoneHeight + 0.02f),
            1.00f);
        ImGui::TextUnformatted("Cyan=aimable, amber=readability edge, green=fire-safe enemy, magenta=threat center.");
    }

    if (ImGui::CollapsingHeader("Composition Safety Director P1-B-7", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Checkbox("Composition Safety Enabled", &compositionSettings.enabled);
        ImGui::Text(
            "blend=%.2f risk=%.2f safe=%s reason=%s",
            cameraFrame.compositionSafetyBlend,
            cameraFrame.compositionRisk,
            cameraFrame.compositionSafeForAiming ? "true" : "false",
            cameraFrame.compositionReason.c_str());
        ImGui::Text(
            "candidates=%d outAimable=%d outReadability=%d correction=(%.2f, %.2f) fov+=%.2f deg",
            cameraFrame.compositionCandidateCount,
            cameraFrame.compositionOutOfAimableCount,
            cameraFrame.compositionOutOfReadabilityCount,
            cameraFrame.compositionCorrection.x,
            cameraFrame.compositionCorrection.y,
            cameraFrame.compositionFovOffsetDeg);
        ImGui::DragFloat("Comp Aimable Width", &compositionSettings.aimableZoneWidth, 0.01f, 0.10f, 1.00f, "%.2f");
        ImGui::DragFloat("Comp Aimable Height", &compositionSettings.aimableZoneHeight, 0.01f, 0.10f, 1.00f, "%.2f");
        ImGui::DragFloat(
            "Comp Readability Width",
            &compositionSettings.readabilityZoneWidth,
            0.01f,
            0.10f,
            1.00f,
            "%.2f");
        ImGui::DragFloat(
            "Comp Readability Height",
            &compositionSettings.readabilityZoneHeight,
            0.01f,
            0.10f,
            1.00f,
            "%.2f");
        ImGui::DragFloat("Comp Min Forward", &compositionSettings.minForwardDistance, 1.0f, 0.0f, 80.0f, "%.1f");
        ImGui::DragFloat("Comp Max Forward", &compositionSettings.maxForwardDistance, 1.0f, 20.0f, 360.0f, "%.1f");
        ImGui::DragFloat("Comp Blend In", &compositionSettings.blendInRate, 0.1f, 0.0f, 30.0f, "%.2f");
        ImGui::DragFloat("Comp Blend Out", &compositionSettings.blendOutRate, 0.1f, 0.0f, 30.0f, "%.2f");
        ImGui::DragFloat("Max Target Correction", &compositionSettings.maxTargetCorrection, 0.25f, 0.0f, 80.0f, "%.2f");
        ImGui::DragFloat("Correction Gain", &compositionSettings.correctionGain, 0.01f, 0.0f, 2.0f, "%.2f");
        ImGui::DragFloat("FOV Expand Deg", &compositionSettings.fovExpandDeg, 0.1f, 0.0f, 14.0f, "%.1f");
        ImGui::DragFloat("Max FOV Deg", &compositionSettings.maxFovDeg, 0.5f, 40.0f, 90.0f, "%.1f");
        ImGui::DragFloat("Fire Block Risk", &compositionSettings.fireBlockRisk, 0.01f, 0.0f, 1.50f, "%.2f");
        ImGui::DragFloat("Comp Lock Weight", &compositionSettings.lockTokenWeight, 0.05f, 0.0f, 12.0f, "%.2f");
        ImGui::DragFloat("Comp Boss Weight", &compositionSettings.bossWeight, 0.05f, 0.0f, 12.0f, "%.2f");
        ImGui::DragFloat("Comp Enemy Weight", &compositionSettings.enemyWeight, 0.05f, 0.0f, 8.0f, "%.2f");
        ImGui::DragFloat("Comp Obstacle Weight", &compositionSettings.obstacleWeight, 0.05f, 0.0f, 8.0f, "%.2f");

        compositionSettings.aimableZoneWidth = (std::clamp)(compositionSettings.aimableZoneWidth, 0.10f, 1.00f);
        compositionSettings.aimableZoneHeight = (std::clamp)(compositionSettings.aimableZoneHeight, 0.10f, 1.00f);
        compositionSettings.readabilityZoneWidth = (std::clamp)(
            compositionSettings.readabilityZoneWidth,
            (std::min)(1.0f, compositionSettings.aimableZoneWidth + 0.02f),
            1.00f);
        compositionSettings.readabilityZoneHeight = (std::clamp)(
            compositionSettings.readabilityZoneHeight,
            (std::min)(1.0f, compositionSettings.aimableZoneHeight + 0.02f),
            1.00f);
        compositionSettings.maxForwardDistance =
            (std::max)(compositionSettings.minForwardDistance + 1.0f, compositionSettings.maxForwardDistance);
        compositionSettings.blendInRate = (std::max)(0.0f, compositionSettings.blendInRate);
        compositionSettings.blendOutRate = (std::max)(0.0f, compositionSettings.blendOutRate);
        compositionSettings.maxTargetCorrection = (std::max)(0.0f, compositionSettings.maxTargetCorrection);
        compositionSettings.correctionGain = (std::max)(0.0f, compositionSettings.correctionGain);
        compositionSettings.fovExpandDeg = (std::max)(0.0f, compositionSettings.fovExpandDeg);
        compositionSettings.maxFovDeg = (std::max)(40.0f, compositionSettings.maxFovDeg);
        compositionSettings.fireBlockRisk = (std::clamp)(compositionSettings.fireBlockRisk, 0.0f, 1.50f);
        compositionSettings.lockTokenWeight = (std::max)(0.0f, compositionSettings.lockTokenWeight);
        compositionSettings.bossWeight = (std::max)(0.0f, compositionSettings.bossWeight);
        compositionSettings.enemyWeight = (std::max)(0.0f, compositionSettings.enemyWeight);
        compositionSettings.obstacleWeight = (std::max)(0.0f, compositionSettings.obstacleWeight);
        ImGui::TextUnformatted("Keeps important targets inside the aimable/readability zones by nudging target and FOV.");
    }

    if (ImGui::CollapsingHeader("Camera Occlusion / Line-of-Sight Safety P1-B-8", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Checkbox("Camera Line-of-Sight Enabled", &lineOfSightSettings.enabled);
        ImGui::Checkbox("Block Enemy Fire When Occluded", &lineOfSightSettings.blockEnemyFireWhenOccluded);
        ImGui::Checkbox("Prefer Base Target When Occluded", &lineOfSightSettings.preferBaseTargetWhenOccluded);
        ImGui::Checkbox("Lock Line-of-Sight Reject", &settings.lockLineOfSightEnabled);
        ImGui::Text(
            "safe=%s candidates=%d blocked=%d occluder=%u reason=%s",
            cameraFrame.lineOfSightSafeForAiming ? "true" : "false",
            cameraFrame.lineOfSightCandidateCount,
            cameraFrame.lineOfSightBlockedCount,
            cameraFrame.lineOfSightOccluderActorId,
            cameraFrame.lineOfSightReason.c_str());
        ImGui::Text(
            "cameraFire=%s comfort=%s fov+=%.2f deg",
            cameraFrame.allowEnemyFire ? "allowed" : "blocked",
            cameraFrame.comfortReason.c_str(),
            cameraFrame.lineOfSightFovOffsetDeg);
        ImGui::DragFloat("LOS Min Forward", &lineOfSightSettings.minForwardDistance, 1.0f, 0.0f, 80.0f, "%.1f");
        ImGui::DragFloat("LOS Max Forward", &lineOfSightSettings.maxForwardDistance, 1.0f, 20.0f, 380.0f, "%.1f");
        ImGui::DragFloat("LOS Obstacle Padding", &lineOfSightSettings.obstaclePadding, 0.05f, 0.0f, 8.0f, "%.2f");
        ImGui::DragFloat("LOS Target Release", &lineOfSightSettings.targetReleaseStrength, 0.01f, 0.0f, 1.0f, "%.2f");
        ImGui::DragFloat("LOS FOV Expand Deg", &lineOfSightSettings.fovExpandDeg, 0.1f, 0.0f, 12.0f, "%.1f");
        ImGui::DragFloat("LOS Max FOV Deg", &lineOfSightSettings.maxFovDeg, 0.5f, 40.0f, 92.0f, "%.1f");
        ImGui::DragFloat("Lock LOS Padding", &settings.lockLineOfSightObstaclePadding, 0.05f, 0.0f, 8.0f, "%.2f");

        lineOfSightSettings.maxForwardDistance =
            (std::max)(lineOfSightSettings.minForwardDistance + 1.0f, lineOfSightSettings.maxForwardDistance);
        lineOfSightSettings.obstaclePadding = (std::max)(0.0f, lineOfSightSettings.obstaclePadding);
        lineOfSightSettings.targetReleaseStrength =
            (std::clamp)(lineOfSightSettings.targetReleaseStrength, 0.0f, 1.0f);
        lineOfSightSettings.fovExpandDeg = (std::max)(0.0f, lineOfSightSettings.fovExpandDeg);
        lineOfSightSettings.maxFovDeg = (std::max)(40.0f, lineOfSightSettings.maxFovDeg);
        settings.lockLineOfSightObstaclePadding = (std::max)(0.0f, settings.lockLineOfSightObstaclePadding);
        ImGui::TextUnformatted("Active obstacles act as inflated AABB occluders for camera safety and lock rejection.");
    }

    if (ImGui::CollapsingHeader("Camera Collision / Near-Clip Protection P1-B-9", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Checkbox("Camera Collision Protection Enabled", &collisionProtectionSettings.enabled);
        ImGui::Checkbox("Block Enemy Fire When Camera Unsafe", &collisionProtectionSettings.blockEnemyFireWhenUnsafe);
        ImGui::Text(
            "safe=%s obstacles=%d closest=%.2f occluder=%u reason=%s",
            cameraFrame.cameraCollisionSafe ? "true" : "false",
            cameraFrame.cameraCollisionObstacleCount,
            cameraFrame.cameraCollisionClosestDistance,
            cameraFrame.cameraCollisionObstacleActorId,
            cameraFrame.cameraCollisionReason.c_str());
        ImGui::Text(
            "push=%.2f fov+=%.2f deg cameraFire=%s comfort=%s",
            cameraFrame.cameraCollisionPushDistance,
            cameraFrame.cameraCollisionFovOffsetDeg,
            cameraFrame.allowEnemyFire ? "allowed" : "blocked",
            cameraFrame.comfortReason.c_str());
        ImGui::DragFloat("Collision Obstacle Padding", &collisionProtectionSettings.obstaclePadding, 0.05f, 0.0f, 8.0f, "%.2f");
        ImGui::DragFloat("Min Clearance", &collisionProtectionSettings.minClearance, 0.05f, 0.10f, 8.0f, "%.2f");
        ImGui::DragFloat(
            "Near Clip Clearance Mult",
            &collisionProtectionSettings.nearClipClearanceMultiplier,
            0.1f,
            1.0f,
            24.0f,
            "%.1f");
        ImGui::DragFloat("Max Push Distance", &collisionProtectionSettings.maxPushDistance, 0.1f, 0.0f, 24.0f, "%.2f");
        ImGui::DragFloat("Target Compensation", &collisionProtectionSettings.targetCompensation, 0.01f, 0.0f, 1.0f, "%.2f");
        ImGui::DragFloat("Collision FOV Expand Deg", &collisionProtectionSettings.fovExpandDeg, 0.1f, 0.0f, 12.0f, "%.1f");
        ImGui::DragFloat("Collision Max FOV Deg", &collisionProtectionSettings.maxFovDeg, 0.5f, 40.0f, 92.0f, "%.1f");

        collisionProtectionSettings.obstaclePadding = (std::max)(0.0f, collisionProtectionSettings.obstaclePadding);
        collisionProtectionSettings.minClearance = (std::max)(0.10f, collisionProtectionSettings.minClearance);
        collisionProtectionSettings.nearClipClearanceMultiplier =
            (std::max)(1.0f, collisionProtectionSettings.nearClipClearanceMultiplier);
        collisionProtectionSettings.maxPushDistance = (std::max)(0.0f, collisionProtectionSettings.maxPushDistance);
        collisionProtectionSettings.targetCompensation =
            (std::clamp)(collisionProtectionSettings.targetCompensation, 0.0f, 1.0f);
        collisionProtectionSettings.fovExpandDeg = (std::max)(0.0f, collisionProtectionSettings.fovExpandDeg);
        collisionProtectionSettings.maxFovDeg = (std::max)(40.0f, collisionProtectionSettings.maxFovDeg);
        ImGui::TextUnformatted("Pushes the camera out of inflated obstacle AABBs before line-of-sight and comfort metrics run.");
    }

    if (ImGui::CollapsingHeader("Rail Segment Transition Polish P1-B-10", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Checkbox("Segment Transition Polish Enabled", &segmentTransitionSettings.enabled);
        ImGui::Text(
            "active=%s blend=%.2f remaining=%.2f reason=%s",
            cameraFrame.segmentTransitionActive ? "true" : "false",
            cameraFrame.segmentTransitionBlend,
            cameraFrame.segmentTransitionRemaining,
            cameraFrame.segmentTransitionReason.c_str());
        ImGui::Text(
            "section prev=%s current=%s enemyFire=%s comfort=%s",
            cameraFrame.previousSectionName.c_str(),
            cameraFrame.currentSectionName.c_str(),
            cameraFrame.allowEnemyFire ? "allowed" : "blocked",
            cameraFrame.comfortReason.c_str());
        ImGui::DragFloat("Transition Duration", &segmentTransitionSettings.duration, 0.01f, 0.10f, 3.0f, "%.2f");
        ImGui::DragFloat("Min Transition Duration", &segmentTransitionSettings.minDuration, 0.01f, 0.05f, 1.0f, "%.2f");
        ImGui::DragFloat("High Speed Duration", &segmentTransitionSettings.highSpeedDuration, 0.01f, 0.05f, 2.0f, "%.2f");
        ImGui::DragFloat("Boss Duration", &segmentTransitionSettings.bossDuration, 0.01f, 0.05f, 3.0f, "%.2f");
        ImGui::DragFloat("Tunnel Duration", &segmentTransitionSettings.tunnelDuration, 0.01f, 0.05f, 2.0f, "%.2f");
        ImGui::DragFloat("Max Position Blend Dist", &segmentTransitionSettings.maxPositionBlendDistance, 0.5f, 0.0f, 120.0f, "%.1f");
        ImGui::DragFloat("Max Target Blend Dist", &segmentTransitionSettings.maxTargetBlendDistance, 0.5f, 0.0f, 160.0f, "%.1f");
        ImGui::DragFloat("Roll Blend Strength", &segmentTransitionSettings.rollBlendStrength, 0.01f, 0.0f, 1.0f, "%.2f");
        ImGui::DragFloat("FOV Blend Strength", &segmentTransitionSettings.fovBlendStrength, 0.01f, 0.0f, 1.0f, "%.2f");
        ImGui::DragFloat("Shake Dampen", &segmentTransitionSettings.shakeDampen, 0.01f, 0.0f, 1.0f, "%.2f");
        ImGui::DragFloat("Enemy Fire Hold", &segmentTransitionSettings.enemyFireHold, 0.01f, 0.0f, 1.0f, "%.2f");
        ImGui::DragFloat("Comfort Grace Mult", &segmentTransitionSettings.comfortGraceMultiplier, 0.01f, 1.0f, 3.0f, "%.2f");

        segmentTransitionSettings.duration = (std::max)(0.10f, segmentTransitionSettings.duration);
        segmentTransitionSettings.minDuration = (std::max)(0.05f, segmentTransitionSettings.minDuration);
        segmentTransitionSettings.highSpeedDuration =
            (std::max)(segmentTransitionSettings.minDuration, segmentTransitionSettings.highSpeedDuration);
        segmentTransitionSettings.bossDuration =
            (std::max)(segmentTransitionSettings.minDuration, segmentTransitionSettings.bossDuration);
        segmentTransitionSettings.tunnelDuration =
            (std::max)(segmentTransitionSettings.minDuration, segmentTransitionSettings.tunnelDuration);
        segmentTransitionSettings.maxPositionBlendDistance =
            (std::max)(0.0f, segmentTransitionSettings.maxPositionBlendDistance);
        segmentTransitionSettings.maxTargetBlendDistance =
            (std::max)(0.0f, segmentTransitionSettings.maxTargetBlendDistance);
        segmentTransitionSettings.rollBlendStrength =
            (std::clamp)(segmentTransitionSettings.rollBlendStrength, 0.0f, 1.0f);
        segmentTransitionSettings.fovBlendStrength =
            (std::clamp)(segmentTransitionSettings.fovBlendStrength, 0.0f, 1.0f);
        segmentTransitionSettings.shakeDampen =
            (std::clamp)(segmentTransitionSettings.shakeDampen, 0.0f, 1.0f);
        segmentTransitionSettings.enemyFireHold = (std::max)(0.0f, segmentTransitionSettings.enemyFireHold);
        segmentTransitionSettings.comfortGraceMultiplier =
            (std::max)(1.0f, segmentTransitionSettings.comfortGraceMultiplier);
        ImGui::TextUnformatted("Blends camera output across section changes and briefly gates enemy fire at transition entry.");
    }

    if (ImGui::CollapsingHeader("Encounter Framing Rules P1-B-11", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Checkbox("Encounter Framing Enabled", &encounterFramingSettings.enabled);
        ImGui::Text(
            "active=%s blend=%.2f remain=%.2f reason=%s",
            cameraFrame.encounterFramingActive ? "true" : "false",
            cameraFrame.encounterFramingBlend,
            cameraFrame.encounterFramingRemaining,
            cameraFrame.encounterFramingReason.c_str());
        ImGui::Text(
            "enemies=%d bosses=%d spread=%.2f fov+=%.2f deg enemyFire=%s comfort=%s",
            cameraFrame.encounterFramingEnemyCount,
            cameraFrame.encounterFramingBossCount,
            cameraFrame.encounterFramingThreatSpread,
            cameraFrame.encounterFramingFovOffsetDeg,
            cameraFrame.allowEnemyFire ? "allowed" : "blocked",
            cameraFrame.comfortReason.c_str());
        ImGui::DragFloat("Encounter Blend In", &encounterFramingSettings.blendInRate, 0.1f, 0.0f, 30.0f, "%.2f");
        ImGui::DragFloat("Encounter Blend Out", &encounterFramingSettings.blendOutRate, 0.1f, 0.0f, 30.0f, "%.2f");
        ImGui::DragFloat("Wave Hold", &encounterFramingSettings.waveHoldDuration, 0.01f, 0.0f, 5.0f, "%.2f");
        ImGui::DragFloat("Boss Hold", &encounterFramingSettings.bossHoldDuration, 0.01f, 0.0f, 6.0f, "%.2f");
        ImGui::DragFloat("Obstacle Hold", &encounterFramingSettings.obstacleHoldDuration, 0.01f, 0.0f, 3.0f, "%.2f");
        ImGui::DragFloat("Min Forward", &encounterFramingSettings.minForwardDistance, 1.0f, -80.0f, 80.0f, "%.1f");
        ImGui::DragFloat("Max Forward", &encounterFramingSettings.maxForwardDistance, 1.0f, 20.0f, 420.0f, "%.1f");
        ImGui::DragFloat("Min Active Enemy Focus", &encounterFramingSettings.minActiveEnemyFocus, 0.1f, 0.0f, 12.0f, "%.1f");
        ImGui::DragFloat("Enemies For Full Wide", &encounterFramingSettings.enemyCountForFullWide, 0.1f, 1.0f, 20.0f, "%.1f");
        ImGui::DragFloat("Spread For Full Wide", &encounterFramingSettings.enemySpreadForFullWide, 0.25f, 1.0f, 80.0f, "%.1f");
        ImGui::DragFloat("Boss Focus Boost", &encounterFramingSettings.bossFocusBoost, 0.01f, 0.0f, 1.0f, "%.2f");
        ImGui::DragFloat("Encounter FOV Expand", &encounterFramingSettings.fovExpandDeg, 0.1f, 0.0f, 18.0f, "%.1f");
        ImGui::DragFloat("Boss FOV Expand", &encounterFramingSettings.bossFovExpandDeg, 0.1f, 0.0f, 18.0f, "%.1f");
        ImGui::DragFloat("Encounter Max FOV", &encounterFramingSettings.maxFovDeg, 0.5f, 40.0f, 92.0f, "%.1f");
        ImGui::DragFloat("Encounter Look Ahead", &encounterFramingSettings.lookAheadBoost, 0.25f, 0.0f, 30.0f, "%.1f");
        ImGui::DragFloat("Encounter Back Distance", &encounterFramingSettings.backDistanceBoost, 0.25f, 0.0f, 20.0f, "%.1f");
        ImGui::DragFloat("Encounter Lateral Dampen", &encounterFramingSettings.lateralDampen, 0.01f, 0.0f, 1.0f, "%.2f");
        ImGui::DragFloat("Encounter Roll Dampen", &encounterFramingSettings.rollDampen, 0.01f, 0.0f, 1.0f, "%.2f");
        ImGui::DragFloat("Encounter Fire Hold", &encounterFramingSettings.fireHoldDuration, 0.01f, 0.0f, 1.5f, "%.2f");

        encounterFramingSettings.blendInRate = (std::max)(0.0f, encounterFramingSettings.blendInRate);
        encounterFramingSettings.blendOutRate = (std::max)(0.0f, encounterFramingSettings.blendOutRate);
        encounterFramingSettings.waveHoldDuration = (std::max)(0.0f, encounterFramingSettings.waveHoldDuration);
        encounterFramingSettings.bossHoldDuration = (std::max)(0.0f, encounterFramingSettings.bossHoldDuration);
        encounterFramingSettings.obstacleHoldDuration =
            (std::max)(0.0f, encounterFramingSettings.obstacleHoldDuration);
        encounterFramingSettings.maxForwardDistance =
            (std::max)(encounterFramingSettings.minForwardDistance + 1.0f, encounterFramingSettings.maxForwardDistance);
        encounterFramingSettings.minActiveEnemyFocus =
            (std::max)(0.0f, encounterFramingSettings.minActiveEnemyFocus);
        encounterFramingSettings.enemyCountForFullWide =
            (std::max)(encounterFramingSettings.minActiveEnemyFocus + 1.0f, encounterFramingSettings.enemyCountForFullWide);
        encounterFramingSettings.enemySpreadForFullWide =
            (std::max)(1.0f, encounterFramingSettings.enemySpreadForFullWide);
        encounterFramingSettings.bossFocusBoost =
            (std::clamp)(encounterFramingSettings.bossFocusBoost, 0.0f, 1.0f);
        encounterFramingSettings.fovExpandDeg = (std::max)(0.0f, encounterFramingSettings.fovExpandDeg);
        encounterFramingSettings.bossFovExpandDeg = (std::max)(0.0f, encounterFramingSettings.bossFovExpandDeg);
        encounterFramingSettings.maxFovDeg = (std::max)(40.0f, encounterFramingSettings.maxFovDeg);
        encounterFramingSettings.lookAheadBoost = (std::max)(0.0f, encounterFramingSettings.lookAheadBoost);
        encounterFramingSettings.backDistanceBoost = (std::max)(0.0f, encounterFramingSettings.backDistanceBoost);
        encounterFramingSettings.lateralDampen =
            (std::clamp)(encounterFramingSettings.lateralDampen, 0.0f, 1.0f);
        encounterFramingSettings.rollDampen =
            (std::clamp)(encounterFramingSettings.rollDampen, 0.0f, 1.0f);
        encounterFramingSettings.fireHoldDuration = (std::max)(0.0f, encounterFramingSettings.fireHoldDuration);
        ImGui::TextUnformatted("Frames wave/boss entries before aim focus, composition safety, collision, and LOS polish run.");
    }

    if (ImGui::CollapsingHeader("AimFocus Camera Stabilization P1-B-3", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Checkbox("AimFocus Enabled", &aimFocusSettings.enabled);
        ImGui::Text(
            "active=%s blend=%.2f strength=%.2f lockHeld=%s tokens=%d/%d reticleVel=(%.1f, %.1f)",
            cameraFrame.lockCameraStabilized ? "true" : "false",
            cameraFrame.aimFocusBlend,
            cameraFrame.aimFocusStrength,
            reticle.lockHeld ? "true" : "false",
            static_cast<int>(debug.tokens.size()),
            settings.maxLocks,
            reticle.velocity.x,
            reticle.velocity.y);
        ImGui::Text(
            "fov=%.1f deg roll=%.1f deg shake=%.2f stable=%s",
            cameraFrame.fovY * 180.0f / 3.14159265358979323846f,
            cameraFrame.rollDeg,
            cameraFrame.shakeAmount,
            cameraFrame.stableForAiming ? "true" : "false");
        ImGui::DragFloat("Blend In Rate", &aimFocusSettings.blendInRate, 0.1f, 0.0f, 30.0f, "%.2f");
        ImGui::DragFloat("Blend Out Rate", &aimFocusSettings.blendOutRate, 0.1f, 0.0f, 30.0f, "%.2f");
        ImGui::DragFloat(
            "Full Focus Reticle Velocity",
            &aimFocusSettings.maxReticleVelocityForFullFocus,
            10.0f,
            60.0f,
            2400.0f,
            "%.1f px/s");
        ImGui::DragFloat("FOV Offset", &aimFocusSettings.fovOffsetDeg, 0.1f, -12.0f, 8.0f, "%.1f deg");
        ImGui::DragFloat(
            "Max Lock FOV Offset",
            &aimFocusSettings.maxLockFovOffsetDeg,
            0.1f,
            -10.0f,
            8.0f,
            "%.1f deg");
        ImGui::DragFloat("Roll Suppression", &aimFocusSettings.rollSuppression, 0.01f, 0.0f, 1.0f, "%.2f");
        ImGui::DragFloat("Shake Suppression", &aimFocusSettings.shakeSuppression, 0.01f, 0.0f, 1.0f, "%.2f");
        ImGui::DragFloat("Lateral Suppression", &aimFocusSettings.lateralSuppression, 0.01f, 0.0f, 1.0f, "%.2f");
        ImGui::DragFloat("LookAhead Boost", &aimFocusSettings.lookAheadBoost, 0.1f, 0.0f, 40.0f, "%.1f");
        ImGui::DragFloat("Back Distance Boost", &aimFocusSettings.backDistanceBoost, 0.1f, -8.0f, 16.0f, "%.1f");

        aimFocusSettings.blendInRate = (std::max)(0.0f, aimFocusSettings.blendInRate);
        aimFocusSettings.blendOutRate = (std::max)(0.0f, aimFocusSettings.blendOutRate);
        aimFocusSettings.maxReticleVelocityForFullFocus =
            (std::max)(60.0f, aimFocusSettings.maxReticleVelocityForFullFocus);
        aimFocusSettings.rollSuppression = (std::clamp)(aimFocusSettings.rollSuppression, 0.0f, 1.0f);
        aimFocusSettings.shakeSuppression = (std::clamp)(aimFocusSettings.shakeSuppression, 0.0f, 1.0f);
        aimFocusSettings.lateralSuppression = (std::clamp)(aimFocusSettings.lateralSuppression, 0.0f, 1.0f);
        aimFocusSettings.lookAheadBoost = (std::max)(0.0f, aimFocusSettings.lookAheadBoost);
        ImGui::TextUnformatted("Uses previous-frame lock state so camera stabilization blends instead of snapping.");
    }

    if (ImGui::CollapsingHeader("Input Routes P0-D-4", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Text(
            "Scene=%s railInput=%s",
            currentSceneName,
            railInputRouteDebug_.railSceneActive ? "active" : "inactive");
        ImGui::Text(
            "HUD Renderer=RenderGraph UI pass atlasVertices=%u imguiOverlay=%s",
            railLockOnHudAtlasVertexCount_,
            "debug panel only");
        const char* normalShotState =
            !railInputRouteDebug_.normalShotEnabled ? "suppressed by lock hold" :
            railInputRouteDebug_.normalShotBlockedByUi ? "blocked by UI" :
            railInputRouteDebug_.normalShotHeld ? "firing" :
            "ready";
        ImGui::Text(
            "Normal Shot=%s held=%s pressed=%s shots=%u hits=%u aim=(%.1f, %.1f)",
            normalShotState,
            railInputRouteDebug_.normalShotHeld ? "true" : "false",
            railInputRouteDebug_.normalShotPressed ? "true" : "false",
            railInputRouteDebug_.normalShotsFired,
            railInputRouteDebug_.normalShotHits,
            railInputRouteDebug_.normalAimLateral,
            railInputRouteDebug_.normalAimVertical);
        ImGui::Text(
            "Aim Assist=%s",
            railInputRouteDebug_.aimAssistEnabled ? "active for normal shot" : "disabled by lock hold");
        ImGui::Text(
            "Lock Input held=%s pressed=%s released=%s",
            railInputRouteDebug_.lockHeld ? "true" : "false",
            railInputRouteDebug_.lockPressed ? "true" : "false",
            railInputRouteDebug_.lockReleased ? "true" : "false");
        ImGui::Text(
            "Release Fire=%s tokens=%u hits=%d",
            railInputRouteDebug_.releaseFireTriggered ? "fired" : "idle",
            railInputRouteDebug_.releaseTokenCount,
            railInputRouteDebug_.releaseHitCount);
        const char* showcaseRoute =
            railInputRouteDebug_.showcaseClickBlockedInRail
                ? "blocked in RailShooter"
                : (railInputRouteDebug_.showcaseClickToFireEnabled ? "allowed in VFX preview" : "disabled");
        ImGui::Text(
            "Showcase Ice LeftClick=%s leftMouse=%s fired=%s imguiCapture=%s",
            showcaseRoute,
            railInputRouteDebug_.leftMouseDown ? "down" : "up",
            railInputRouteDebug_.showcaseClickFired ? "true" : "false",
            railInputRouteDebug_.showcaseClickIgnoredByImgui ? "true" : "false");
        if (railInputRouteDebug_.showcaseClickBlockedInRail) {
            ImGui::TextColored(
                ImVec4(1.0f, 0.78f, 0.25f, 1.0f),
                "RailShooter validation uses lock release VFX only; showcase left-click is isolated.");
        }
    }

    if (ImGui::CollapsingHeader("Lock-On VFX Tuning", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::DragFloat("Travel Min", &settings.lockVfxTravelDurationMin, 0.01f, 0.03f, 2.0f, "%.2f");
        ImGui::DragFloat("Travel Max", &settings.lockVfxTravelDurationMax, 0.01f, 0.03f, 2.0f, "%.2f");
        ImGui::DragFloat("Travel Distance Divisor", &settings.lockVfxTravelDistanceDivisor, 1.0f, 1.0f, 600.0f, "%.1f");
        ImGui::DragFloat("Visual Scale Min", &settings.lockVfxVisualScaleMin, 0.05f, 0.1f, 30.0f, "%.2f");
        ImGui::DragFloat("Visual Scale Max", &settings.lockVfxVisualScaleMax, 0.05f, 0.1f, 30.0f, "%.2f");
        ImGui::DragFloat("Visual Scale Per Distance", &settings.lockVfxVisualScalePerDistance, 0.001f, 0.0f, 0.25f, "%.3f");
        ImGui::DragFloat("Impact Scale Min", &settings.lockVfxImpactScaleMin, 0.05f, 0.1f, 20.0f, "%.2f");
        ImGui::DragFloat("Impact Scale Max", &settings.lockVfxImpactScaleMax, 0.05f, 0.1f, 20.0f, "%.2f");
        ImGui::DragFloat("Impact Scale Per Distance", &settings.lockVfxImpactScalePerDistance, 0.001f, 0.0f, 0.25f, "%.3f");
        ImGui::DragFloat("Release Shot Interval", &settings.lockVfxReleaseShotInterval, 0.005f, 0.0f, 0.35f, "%.3f");
        ImGui::DragFloat("Muzzle Forward Offset", &settings.lockVfxMuzzleForwardOffset, 0.05f, -8.0f, 18.0f, "%.2f");
        ImGui::InputInt("Max Concurrent Shots", &settings.lockVfxMaxConcurrentShots);

        settings.lockVfxTravelDurationMin = (std::max)(0.03f, settings.lockVfxTravelDurationMin);
        settings.lockVfxTravelDurationMax =
            (std::max)(settings.lockVfxTravelDurationMin, settings.lockVfxTravelDurationMax);
        settings.lockVfxTravelDistanceDivisor = (std::max)(1.0f, settings.lockVfxTravelDistanceDivisor);
        settings.lockVfxVisualScaleMin = (std::max)(0.1f, settings.lockVfxVisualScaleMin);
        settings.lockVfxVisualScaleMax = (std::max)(settings.lockVfxVisualScaleMin, settings.lockVfxVisualScaleMax);
        settings.lockVfxVisualScalePerDistance = (std::max)(0.0f, settings.lockVfxVisualScalePerDistance);
        settings.lockVfxImpactScaleMin = (std::max)(0.1f, settings.lockVfxImpactScaleMin);
        settings.lockVfxImpactScaleMax = (std::max)(settings.lockVfxImpactScaleMin, settings.lockVfxImpactScaleMax);
        settings.lockVfxImpactScalePerDistance = (std::max)(0.0f, settings.lockVfxImpactScalePerDistance);
        settings.lockVfxReleaseShotInterval = (std::max)(0.0f, settings.lockVfxReleaseShotInterval);
        settings.lockVfxMaxConcurrentShots = (std::clamp)(
            settings.lockVfxMaxConcurrentShots,
            1,
            static_cast<int>(runtimeState_.vfx.iceProjectileShots.size()));
    }

    if (ImGui::CollapsingHeader("Lock Priority Scoring P0-E", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::DragFloat("Reticle Weight", &settings.lockPriorityReticleWeight, 0.01f, 0.0f, 3.0f, "%.2f");
        ImGui::DragFloat("Screen Center Weight", &settings.lockPriorityCenterWeight, 0.01f, 0.0f, 3.0f, "%.2f");
        ImGui::DragFloat("Forward Threat Weight", &settings.lockPriorityForwardThreatWeight, 0.01f, 0.0f, 3.0f, "%.2f");
        ImGui::DragFloat("Anchor Value Weight", &settings.lockPriorityAnchorWeight, 0.01f, 0.0f, 3.0f, "%.2f");
        ImGui::DragFloat("Enemy Bonus", &settings.lockPriorityEnemyBonus, 0.01f, -1.0f, 2.0f, "%.2f");
        ImGui::DragFloat("Obstacle Bonus", &settings.lockPriorityObstacleBonus, 0.01f, -1.0f, 2.0f, "%.2f");
        ImGui::DragFloat("Distance Tie Break", &settings.lockPriorityDistanceTieBreak, 0.0005f, 0.0f, 0.05f, "%.4f");

        settings.lockPriorityReticleWeight = (std::max)(0.0f, settings.lockPriorityReticleWeight);
        settings.lockPriorityCenterWeight = (std::max)(0.0f, settings.lockPriorityCenterWeight);
        settings.lockPriorityForwardThreatWeight = (std::max)(0.0f, settings.lockPriorityForwardThreatWeight);
        settings.lockPriorityAnchorWeight = (std::max)(0.0f, settings.lockPriorityAnchorWeight);
        settings.lockPriorityDistanceTieBreak = (std::max)(0.0f, settings.lockPriorityDistanceTieBreak);
        ImGui::TextUnformatted("Higher score wins. Reticle remains dominant; center/threat/value create commercial target intent.");
    }

    if (ImGui::CollapsingHeader("Aim Feel Assist P0-F", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Checkbox("Enabled", &settings.lockAimFeelEnabled);
        ImGui::DragFloat("Magnet Radius", &settings.lockAimMagnetRadius, 1.0f, 0.0f, 260.0f, "%.1f");
        ImGui::DragFloat("Magnet Strength", &settings.lockAimMagnetStrength, 0.01f, 0.0f, 1.5f, "%.2f");
        ImGui::DragFloat("Max Pull Speed", &settings.lockAimMaxPullSpeed, 5.0f, 0.0f, 2400.0f, "%.1f");
        ImGui::DragFloat("Dead Zone", &settings.lockAimDeadZone, 0.25f, 0.0f, 48.0f, "%.1f");
        ImGui::DragFloat("Target Blend", &settings.lockAimTargetBlend, 0.01f, 0.0f, 1.0f, "%.2f");
        ImGui::DragFloat("Reticle Intent", &settings.lockAimReticleIntentWeight, 0.01f, 0.0f, 3.0f, "%.2f");
        ImGui::DragFloat("Center Intent", &settings.lockAimCenterIntentWeight, 0.01f, 0.0f, 3.0f, "%.2f");
        ImGui::DragFloat("Forward Intent", &settings.lockAimForwardIntentWeight, 0.01f, 0.0f, 3.0f, "%.2f");

        settings.lockAimMagnetRadius = (std::max)(0.0f, settings.lockAimMagnetRadius);
        settings.lockAimMagnetStrength = (std::max)(0.0f, settings.lockAimMagnetStrength);
        settings.lockAimMaxPullSpeed = (std::max)(0.0f, settings.lockAimMaxPullSpeed);
        settings.lockAimDeadZone = (std::max)(0.0f, settings.lockAimDeadZone);
        settings.lockAimTargetBlend = (std::clamp)(settings.lockAimTargetBlend, 0.0f, 1.0f);
        settings.lockAimReticleIntentWeight = (std::max)(0.0f, settings.lockAimReticleIntentWeight);
        settings.lockAimCenterIntentWeight = (std::max)(0.0f, settings.lockAimCenterIntentWeight);
        settings.lockAimForwardIntentWeight = (std::max)(0.0f, settings.lockAimForwardIntentWeight);
        ImGui::TextUnformatted("Applies only while lock input is held, before lock resolution.");
    }

    if (ImGui::CollapsingHeader("HUD Commercial Polish P1-A", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Checkbox("Polish Enabled", &settings.lockHudPolishEnabled);
        ImGui::DragFloat("HUD Scale", &settings.lockHudScale, 0.01f, 0.65f, 1.70f, "%.2f");
        ImGui::DragFloat("HUD Opacity", &settings.lockHudOpacity, 0.01f, 0.15f, 1.00f, "%.2f");
        ImGui::DragFloat("HUD Safe Area", &settings.lockHudSafeArea, 1.0f, 12.0f, 96.0f, "%.1f");
        ImGui::DragFloat("Target Score Alpha", &settings.lockHudTargetScoreAlpha, 0.01f, 0.0f, 1.25f, "%.2f");
        ImGui::DragFloat("Reticle Glow Scale", &settings.lockHudReticleGlowScale, 0.01f, 0.20f, 2.20f, "%.2f");
        ImGui::DragFloat("Release Flash", &settings.lockHudReleaseFlash, 0.01f, 0.0f, 0.55f, "%.2f");

        settings.lockHudScale = (std::clamp)(settings.lockHudScale, 0.65f, 1.70f);
        settings.lockHudOpacity = (std::clamp)(settings.lockHudOpacity, 0.15f, 1.0f);
        settings.lockHudSafeArea = (std::max)(12.0f, settings.lockHudSafeArea);
        settings.lockHudTargetScoreAlpha = (std::max)(0.0f, settings.lockHudTargetScoreAlpha);
        settings.lockHudReticleGlowScale = (std::clamp)(settings.lockHudReticleGlowScale, 0.20f, 2.20f);
        settings.lockHudReleaseFlash = (std::max)(0.0f, settings.lockHudReleaseFlash);
        ImGui::Text(
            "Atlas vertices=%u renderer=%s releasedFlash=%d",
            railLockOnHudAtlasVertexCount_,
            "single UI.RailLockOnHud pass",
            debug.releasedThisFrame);
    }

    if (ImGui::CollapsingHeader("Tokens", ImGuiTreeNodeFlags_DefaultOpen)) {
        for (const RailLockToken& token : debug.tokens) {
            ImGui::BulletText(
                "%s actor=%u anchor=%u stack=%d screen=(%.1f, %.1f) dist=%.1f",
                token.label.c_str(),
                token.target.actorId,
                token.anchorId,
                token.stackIndex,
                token.acquiredScreenPosition.x,
                token.acquiredScreenPosition.y,
                token.acquiredScreenDistance);
        }
    }

    if (ImGui::CollapsingHeader("Released This Frame")) {
        for (const RailLockToken& token : debug.releasedTokens) {
            ImGui::BulletText(
                "%s actor=%u anchor=%u screen=(%.1f, %.1f)",
                token.label.c_str(),
                token.target.actorId,
                token.anchorId,
                token.acquiredScreenPosition.x,
                token.acquiredScreenPosition.y);
        }
    }

    if (ImGui::CollapsingHeader("Acquired This Frame")) {
        for (const RailLockToken& token : debug.acquiredTokens) {
            ImGui::BulletText(
                "%s actor=%u anchor=%u age=%.2f screen=(%.1f, %.1f)",
                token.label.c_str(),
                token.target.actorId,
                token.anchorId,
                debug.elapsedTime - token.acquiredTime,
                token.acquiredScreenPosition.x,
                token.acquiredScreenPosition.y);
        }
    }

    if (ImGui::CollapsingHeader("Candidates")) {
        int shown = 0;
        for (const RailLockCandidate& candidate : debug.candidates) {
            if (shown++ >= 32) {
                ImGui::TextUnformatted("...");
                break;
            }
            ImGui::BulletText(
                "%s actor=%u lockable=%s reason=%s score=%.3f r=%.2f c=%.2f f=%.2f kind=%.2f val=%.2f forward=%.1f screen=(%.1f, %.1f) dist=%.1f radius=%.1f",
                candidate.anchor.label.c_str(),
                candidate.anchor.target.actorId,
                candidate.lockable ? "true" : "false",
                reasonLabel(candidate.rejectReason),
                candidate.score,
                candidate.reticlePriorityScore,
                candidate.centerPriorityScore,
                candidate.forwardPriorityScore,
                candidate.kindPriorityScore,
                candidate.anchorPriorityScore,
                candidate.anchor.forwardDistance,
                candidate.anchor.screenPosition.x,
                candidate.anchor.screenPosition.y,
                candidate.distanceToReticle,
                candidate.anchor.screenRadius);
        }
    }

    const auto showSettingsError = [](bool accepted, const std::string& error) {
        if (!accepted) ImGui::TextWrapped("Settings rejected: %s", error.c_str());
    };
    std::string settingsError;
    // コピーを編集し、各所有者に検証と反映を依頼する。速度の進行状態は保持する。
    const auto configureSettings = [&showSettingsError](auto& owner, auto configure, const auto& candidate) {
        std::string error;
        const bool accepted = (owner.*configure)(candidate, &error);
        showSettingsError(accepted, error);
    };
    configureSettings(railShooterLockOnSystem_, &RailLockOnSystem::Configure, settings);
    configureSettings(railShooterLockOnSystem_, &RailLockOnSystem::ConfigureAimAssist, aimAssistSettings);
    const bool speedAccepted = railShooterSpeedDirector_.Configure(speedSettings, true, &settingsError);
    showSettingsError(speedAccepted, settingsError);
    configureSettings(railShooterCameraDirector_, &RailCameraDirector::ConfigureComfort, cameraComfort);
    configureSettings(railShooterCameraDirector_, &RailCameraDirector::ConfigureAimFocus, aimFocusSettings);
    configureSettings(railShooterCameraDirector_, &RailCameraDirector::ConfigureLookAt, lookAtSettings);
    configureSettings(railShooterCameraDirector_, &RailCameraDirector::ConfigureCompositionSafety, compositionSettings);
    configureSettings(railShooterCameraDirector_, &RailCameraDirector::ConfigureLineOfSight, lineOfSightSettings);
    configureSettings(railShooterCameraDirector_, &RailCameraDirector::ConfigureCollisionProtection, collisionProtectionSettings);
    configureSettings(railShooterCameraDirector_, &RailCameraDirector::ConfigureSegmentTransition, segmentTransitionSettings);
    configureSettings(railShooterCameraDirector_, &RailCameraDirector::ConfigureEncounterFraming, encounterFramingSettings);

#endif
}

void AppRunLoop::QueueRailLockIceProjectile(const Vector3& start, const Vector3& target, int shotIndex) {
    const RailLockSettings& settings = railShooterLockOnSystem_.Settings();
    const size_t shotLimit = static_cast<size_t>((std::clamp)(
        settings.lockVfxMaxConcurrentShots,
        1,
        static_cast<int>(runtimeState_.vfx.iceProjectileShots.size())));
    AppVfxRuntimeState::IceProjectileShotState* slot = nullptr;
    for (size_t index = 0; index < shotLimit; ++index) {
        AppVfxRuntimeState::IceProjectileShotState& shot = runtimeState_.vfx.iceProjectileShots[index];
        if (!shot.active) {
            slot = &shot;
            break;
        }
    }
    if (slot == nullptr) {
        slot = &runtimeState_.vfx.iceProjectileShots.front();
        for (size_t index = 0; index < shotLimit; ++index) {
            AppVfxRuntimeState::IceProjectileShotState& shot = runtimeState_.vfx.iceProjectileShots[index];
            if (shot.timer > slot->timer) {
                slot = &shot;
            }
        }
        if (slot->instanceId != 0) {
            vfxEngine_.Runtime().StopEffect(slot->instanceId);
        }
    }

    *slot = {};
    slot->active = true;
    slot->useWorldSpace = true;
    slot->start = start;
    slot->target = target;
    slot->launchDelay = (std::max)(0.0f, settings.lockVfxReleaseShotInterval) *
        static_cast<float>((std::max)(0, shotIndex));

    const Vector3 delta = Subtract(target, start);
    const float distance = std::sqrt((std::max)(0.0f, Dot(delta, delta)));
    const float durationDivisor = (std::max)(1.0f, settings.lockVfxTravelDistanceDivisor);
    const float durationMin = (std::max)(0.03f, settings.lockVfxTravelDurationMin);
    const float durationMax = (std::max)(durationMin, settings.lockVfxTravelDurationMax);
    const float visualMin = (std::max)(0.1f, settings.lockVfxVisualScaleMin);
    const float visualMax = (std::max)(visualMin, settings.lockVfxVisualScaleMax);
    const float impactMin = (std::max)(0.1f, settings.lockVfxImpactScaleMin);
    const float impactMax = (std::max)(impactMin, settings.lockVfxImpactScaleMax);
    slot->travelDuration = (std::clamp)(distance / durationDivisor, durationMin, durationMax);
    slot->cleanupDelay = slot->travelDuration + 0.72f;
    slot->visualScale = (std::clamp)(
        distance * (std::max)(0.0f, settings.lockVfxVisualScalePerDistance),
        visualMin,
        visualMax);
    slot->impactScale = (std::clamp)(
        distance * (std::max)(0.0f, settings.lockVfxImpactScalePerDistance),
        impactMin,
        impactMax);
}

bool AppRunLoop::IsRailShooterSceneActive() const {
    const IAppSceneState* currentState = sceneStateManager_.CurrentState();
    const char* sceneName = currentState != nullptr ? currentState->Name() : nullptr;
    return sceneName != nullptr && std::strcmp(sceneName, "RailShooter") == 0;
}

int AppRunLoop::ProcessRailLockOnRelease(const Vector3& muzzlePosition, float deltaTime) {
    const RailLockRelease& release = railShooterLockOnSystem_.LastRelease();
    if (railPath_.Length() <= 0.0f) {
        return 0;
    }

    const RailLockSettings& settings = railShooterLockOnSystem_.Settings();
    const bool maxLockRelease =
        static_cast<int>(release.tokens.size()) >= settings.maxLocks;

    const WeaponDefinitionAsset* lockWeapon =
        railShooterCollisionSystem_.FindWeaponDefinition(RailWeaponIds::LockOnIce);
    if (lockWeapon == nullptr || !lockWeapon->definition.lockOnCompatible) {
        return 0;
    }

    WeaponFireInput fireInput{};
    fireInput.weaponId = RailWeaponIds::LockOnIce;
    fireInput.deltaTime = deltaTime;
    fireInput.enabled = true;
    fireInput.triggerHeld = railShooterLockOnSystem_.Reticle().lockHeld;
    fireInput.triggerPressed = railShooterLockOnSystem_.Reticle().lockPressed;
    fireInput.triggerReleased = railShooterLockOnSystem_.Reticle().lockReleased;
    fireInput.requestedProjectileCount = static_cast<uint32_t>(release.tokens.size());
    fireInput.damageMultiplier = maxLockRelease ? 1.25f : 1.0f;
    const WeaponFireResult fireResult =
        railShooterCollisionSystem_.UpdateWeaponFire(fireInput);
    if (!fireResult.fired) {
        return 0;
    }

    int hitCount = 0;
    const size_t projectileCount = (std::min)(fireResult.shots.size(), release.tokens.size());
    for (size_t index = 0; index < projectileCount; ++index) {
        const RailLockToken& token = release.tokens[index];
        const WeaponShot& shot = fireResult.shots[index];
        bool targetLocated = false;
        Vector3 targetPosition{};
        if (token.target.kind == RailLockTargetKind::Enemy) {
            for (const CourseEnemyActor& enemy : railShooterSpawnRuntime_.Enemies()) {
                if (enemy.actorId != token.target.actorId) {
                    continue;
                }
                targetPosition = RailLocalPoint(
                    railPath_,
                    enemy.desc.spawnDistance + enemy.desc.distanceOffset,
                    enemy.desc.lateralOffset,
                    enemy.desc.verticalOffset,
                    0.0f);
                targetLocated = true;
                break;
            }
        } else {
            for (const CourseObstacleActor& obstacle : railShooterSpawnRuntime_.Obstacles()) {
                if (obstacle.actorId != token.target.actorId || !obstacle.desc.breakable) {
                    continue;
                }
                targetPosition = RailLocalPoint(
                    railPath_,
                    obstacle.desc.spawnDistance + obstacle.desc.distanceOffset,
                    obstacle.desc.lateralOffset,
                    obstacle.desc.verticalOffset,
                    0.0f);
                targetLocated = true;
                break;
            }
        }

        if (!targetLocated) {
            continue;
        }

        const Vector3 shotDelta = Subtract(targetPosition, muzzlePosition);
        const float shotDistance = std::sqrt(LengthSquared(shotDelta));
        if (!std::isfinite(shotDistance) || shotDistance > shot.range + 0.001f) {
            continue;
        }
        const Vector3 shotDirection = NormalizeOr(shotDelta, {0.0f, 0.0f, 1.0f});
        WeaponHitRequest request{};
        request.shotId = shot.shotId;
        request.targetActorId = token.target.actorId;
        request.hitKind = token.target.kind == RailLockTargetKind::Enemy
            ? RailAimHitKind::Enemy
            : RailAimHitKind::Obstacle;
        request.damageType = shot.damageType;
        request.rayOrigin = muzzlePosition;
        request.rayDirection = shotDirection;
        request.hitPoint = targetPosition;
        request.hitNormal = Scale(shotDirection, -1.0f);
        request.hitDistance = shotDistance;
        request.baseDamage = shot.damage;
        const DamageResult damageResult = railShooterCollisionSystem_.ApplyWeaponHit(
            railShooterSpawnRuntime_,
            &railShooterCourse_,
            request);
        if (!damageResult.damageApplied) {
            continue;
        }
        QueueRailLockIceProjectile(muzzlePosition, targetPosition, hitCount);
        ++hitCount;
    }

    if (hitCount > 0) {
        railShooterSpawnRuntime_.PruneDestroyedActors();
    }
    return hitCount;
}

void AppRunLoop::LogRailShooterRuntimeDiagnostics(const char* reason) {
    const CourseSection* section = railShooterCourse_.FindSection(railShooterDistance_);
    const CourseCinematicShotSet* shotSet = railShooterCourse_.FindCinematicShotSet(railShooterDistance_);
    const EffectRuntimeFrame vfxFrame = vfxEngine_.Runtime().BuildFrame();
    const TerrainDebrisCullingStats& debrisStats = terrainChunkManager_.LastDebrisCullingStats();
    std::ofstream log = app::OpenRotatingLog("logs/course_runtime_heartbeat.log");
    std::ostringstream line;
    line << "[RailShooterRuntime] reason=" << (reason != nullptr ? reason : "unknown")
         << " frame=" << railShooterFrameIndex_
         << " distance=" << railShooterDistance_
         << " railLength=" << railPath_.Length()
         << " section=\"" << (section != nullptr ? section->name : std::string("-")) << "\""
         << " cinematicShot=\"" << (shotSet != nullptr ? shotSet->id : std::string("-")) << "\""
         << " enemies=" << railShooterSpawnRuntime_.ActiveEnemyCount()
         << " bullets=" << railShooterSpawnRuntime_.ActiveBulletCount()
         << " obstacles=" << railShooterSpawnRuntime_.ActiveObstacleCount()
         << " vfx=" << railShooterSpawnRuntime_.ActiveVfxCueCount()
         << " effectInstances=" << vfxFrame.activeInstanceCount
         << " effectComponents=" << vfxFrame.activeComponentCount
         << " effectParticles=" << vfxFrame.particleQueue.size()
         << " effectTrails=" << vfxFrame.trailQueue.size()
         << " effectRings=" << vfxFrame.ringQueue.size()
         << " effectCylinders=" << vfxFrame.cylinderQueue.size()
         << " courseMeshVisible=" << scene_.CourseMeshes().VisibleCount()
         << " terrainChunks=" << terrainChunkManager_.RenderChunks().size()
         << " terrainDebris=" << debrisStats.debrisInstanceCount
         << " terrainDebrisCullDispatches=" << debrisStats.debrisCullDispatchCount
         << " cascadeShadow=" << (runtimeState_.terrain.cascadeShadowEnabled ? 1 : 0)
         << " visibleMeshes=\"";
    bool firstVisibleCourseMesh = true;
    for (const CourseMeshRenderItem& item : scene_.CourseMeshes().Items()) {
        if (!item.visible) {
            continue;
        }
        if (!firstVisibleCourseMesh) {
            line << ",";
        }
        firstVisibleCourseMesh = false;
        line << item.name << ":" << item.meshId << "@" << item.sortDistance;
    }
    line << "\"\n";
    OutputDebugStringA(line.str().c_str());
    if (log) {
        log << line.str();
    }
}

void AppRunLoop::LogRailShooterPerfSpike() {
    const double cpuNoPresentMs =
        gRailPerfFrame.updateMs +
        (std::max)(0.0, gRailPerfFrame.renderMs - gRailPerfFrame.presentMs);
    const bool shouldLog =
        (drone_perf::Enabled() && gRailPerfFrame.frame % 30 == 0) ||
        cpuNoPresentMs >= 18.0 ||
        gRailPerfFrame.updateMs >= 8.0 ||
        gRailPerfFrame.terrainUpdateMs >= 4.0 ||
        gRailPerfFrame.syncCourseMeshMs >= 3.0 ||
        gRailPerfFrame.imguiMs >= 4.0 ||
        gRailPerfFrame.prepareGraphResourcesMs >= 3.0 ||
        gRailPerfFrame.renderGraphExecuteMs >= 6.0 ||
        gRailPerfFrame.endAndExecuteMs >= 4.0 ||
        gRailPerfFrame.waitFrameSlotMs >= 8.0 ||
        gRailPerfFrame.presentMs >= 8.0;
    if (!shouldLog) {
        return;
    }

    const CourseSection* section = railShooterCourse_.FindSection(railShooterDistance_);
    const CourseCinematicShotSet* shotSet = railShooterCourse_.FindCinematicShotSet(railShooterDistance_);
    const EffectRuntimeFrame vfxFrame = vfxEngine_.Runtime().BuildFrame();
    const TerrainDebrisCullingStats& debrisStats = terrainChunkManager_.LastDebrisCullingStats();
    std::ostringstream line;
    line << "[RailPerfSpike]"
         << " frame=" << gRailPerfFrame.frame
         << " distance=" << railShooterDistance_
         << " section=\"" << (section != nullptr ? section->name : std::string("-")) << "\""
         << " cinematicShot=\"" << (shotSet != nullptr ? shotSet->id : std::string("-")) << "\""
         << " cpuNoPresentMs=" << cpuNoPresentMs
         << " updateMs=" << gRailPerfFrame.updateMs
         << " renderMs=" << gRailPerfFrame.renderMs
         << " presentMs=" << gRailPerfFrame.presentMs
         << " presentSyncInterval=" << frameCoordinator_.PresentSyncInterval()
         << " presentTearingAllowed=" << (frameCoordinator_.PresentTearingAllowed() ? 1 : 0)
         << " presentMaxFrameLatency=" << frameCoordinator_.PresentMaxFrameLatency()
         << " swapBufferCount=" << swapChain_.BufferCount()
         << " collisionMs=" << gRailPerfFrame.collisionMs
         << " visualPresetMs=" << gRailPerfFrame.visualPresetMs
         << " vfxUpdateMs=" << gRailPerfFrame.vfxUpdateMs
         << " terrainUpdateMs=" << gRailPerfFrame.terrainUpdateMs
         << " particleUpdateMs=" << gRailPerfFrame.particleUpdateMs
         << " waitFrameSlotMs=" << gRailPerfFrame.waitFrameSlotMs
         << " gpuParticleInitMs=" << gRailPerfFrame.gpuParticleInitMs
         << " sceneTransformsMs=" << gRailPerfFrame.sceneTransformsMs
         << " syncCourseMeshMs=" << gRailPerfFrame.syncCourseMeshMs
         << " imguiMs=" << gRailPerfFrame.imguiMs
         << " imguiBuildUiMs=" << gRailPerfFrame.imguiBuildUiMs
         << " imguiEndFrameMs=" << gRailPerfFrame.imguiEndFrameMs
         << " sceneRuntimeSyncMs=" << gRailPerfFrame.sceneRuntimeSyncMs
         << " registerPassesMs=" << gRailPerfFrame.registerPassesMs
         << " prepareGraphResourcesMs=" << gRailPerfFrame.prepareGraphResourcesMs
         << " renderGraphDebugMs=" << gRailPerfFrame.renderGraphDebugMs
         << " cascadeShadowMs=" << gRailPerfFrame.cascadeShadowMs
         << " renderGraphExecuteMs=" << gRailPerfFrame.renderGraphExecuteMs
         << " telemetryMs=" << gRailPerfFrame.telemetryMs
         << " endFrameMs=" << gRailPerfFrame.endFrameMs
         << " endAndExecuteMs=" << gRailPerfFrame.endAndExecuteMs
         << " signalMs=" << gRailPerfFrame.signalMs
         << " enemies=" << railShooterSpawnRuntime_.ActiveEnemyCount()
         << " bullets=" << railShooterSpawnRuntime_.ActiveBulletCount()
         << " obstacles=" << railShooterSpawnRuntime_.ActiveObstacleCount()
         << " vfxCues=" << railShooterSpawnRuntime_.ActiveVfxCueCount()
         << " effectInstances=" << vfxFrame.activeInstanceCount
         << " effectComponents=" << vfxFrame.activeComponentCount
         << " effectParticles=" << vfxFrame.particleQueue.size()
         << " effectRings=" << vfxFrame.ringQueue.size()
         << " courseMeshVisible=" << scene_.CourseMeshes().VisibleCount()
         << " terrainChunks=" << terrainChunkManager_.RenderChunks().size()
         << " terrainDebris=" << debrisStats.debrisInstanceCount
         << " terrainDebrisCullDispatches=" << debrisStats.debrisCullDispatchCount;
    drone_perf::AppendToLog(line);
    line << "\n";
    OutputDebugStringA(line.str().c_str());
    std::ofstream log = app::OpenRotatingLog("logs/rail_perf_spikes.log");
    if (log) {
        log << line.str();
    }
}

void AppRunLoop::Shutdown() {
    StopRailEnemyAttackFeedback();
    ResetEnemyProjectilePresentation();
    frameCoordinator_.FlushGpu();
    sceneStateManager_.Shutdown(*this);
    handParticleAttachment_.Stop(vfxEngine_.Runtime());
    leftHandParticleAttachment_.Stop(vfxEngine_.Runtime());
    weaponAttachment_.Reset();
    vfxEngine_.Shutdown();
}

void AppRunLoop::UpdateHandParticleAttachment() {
    const SkinnedModelInstance* model = scene_.GetActiveSkinnedModel();
    const Skeleton* skeleton = model != nullptr && model->loaded
        ? &model->skeleton
        : nullptr;
    Matrix4x4 ownerWorld = MakeIdentity4x4();
    if (model != nullptr && model->loaded) {
        ownerWorld = MakeAffineMatrix(
            model->transform.scale,
            model->transform.rotate,
            model->transform.translate);
    }

    handParticleAttachment_.Update(
        runtimeState_.handParticleAttachment,
        skeleton,
        ownerWorld,
        vfxEngine_.Runtime());
    runtimeState_.handParticleAttachmentTelemetry =
        handParticleAttachment_.Telemetry();

    leftHandParticleAttachment_.Update(
        runtimeState_.leftHandParticleAttachment,
        skeleton,
        ownerWorld,
        vfxEngine_.Runtime());
    runtimeState_.leftHandParticleAttachmentTelemetry =
        leftHandParticleAttachment_.Telemetry();
}

void AppRunLoop::UpdateWeaponAttachment() {
    const SkinnedModelInstance* model = scene_.GetActiveSkinnedModel();
    const Skeleton* skeleton = model != nullptr && model->loaded
        ? &model->skeleton
        : nullptr;
    Matrix4x4 ownerWorld = MakeIdentity4x4();
    if (model != nullptr && model->loaded) {
        ownerWorld = MakeAffineMatrix(
            model->transform.scale,
            model->transform.rotate,
            model->transform.translate);
    }

    const uint32_t modelIndex = scene_.TrainingSwordModelIndex();
    weaponAttachment_.Update(
        runtimeState_.weaponAttachment,
        skeleton,
        ownerWorld,
        modelIndex,
        scene_.FindManagedModel(modelIndex) != nullptr);
    runtimeState_.weaponAttachmentTelemetry = weaponAttachment_.Telemetry();
    scene_.UpdateWeaponAttachment(
        runtimeState_.weaponAttachmentTelemetry,
        frameState_.viewMatrix,
        frameState_.projMatrix);
}

void AppRunLoop::EnterVfxPreviewScene() {
    StopRailEnemyAttackFeedback();
    StopGameSessionPresentation();
    railShooterVehicleActor_.Reset();
    railShooterVehicleRenderer_.Reset();
    railShooterVehicleAudioBridge_.Reset();
    railShooterMountedEvasion_.Reset();
    railShooterMountedDefense_.Reset();
    railEnemyAttackDefenseValidation_.Reset();
    railShooterVehicleBodyCollision_.Reset();
    railShooterVehicleDamageCoordinator_.Reset();
    railShooterVehicleCollisionFeedback_.Reset();
    railShooterOccupantClearance_.Reset();
    railShooterEvasionConstraintResolver_.Reset();
    railShooterCombatMountBridge_.Reset();
    railShooterCameraMountBridge_.Reset();
    railShooterMountedEvasionPresentation_.Reset();
    railShooterOccupantActor_.Reset();
    railShooterEvasionFeedback_.Reset();
    railEnemyCombatPresentationBridge_.Reset();
    ResetEnemyProjectilePresentation();
    railShooterInitialized_ = false;
    runtimeState_.terrain.enabled = false;
    runtimeState_.terrain.autoAdvancePreview = false;
    runtimeState_.camera.enableDebugInput = true;
    runtimeState_.showSprite = true;
    runtimeState_.showVfxModelObjects = true;
    runtimeState_.submissionShowcase = {};
}

void AppRunLoop::EnterMultiMaterialShowcaseScene() {
    StopRailEnemyAttackFeedback();
    StopGameSessionPresentation();
    railShooterVehicleActor_.Reset();
    railShooterVehicleRenderer_.Reset();
    railShooterVehicleAudioBridge_.Reset();
    railShooterMountedEvasion_.Reset();
    railShooterMountedDefense_.Reset();
    railEnemyAttackDefenseValidation_.Reset();
    railShooterVehicleBodyCollision_.Reset();
    railShooterVehicleDamageCoordinator_.Reset();
    railShooterVehicleCollisionFeedback_.Reset();
    railShooterOccupantClearance_.Reset();
    railShooterEvasionConstraintResolver_.Reset();
    railShooterCombatMountBridge_.Reset();
    railShooterCameraMountBridge_.Reset();
    railShooterMountedEvasionPresentation_.Reset();
    railShooterOccupantActor_.Reset();
    railShooterEvasionFeedback_.Reset();
    railEnemyCombatPresentationBridge_.Reset();
    ResetEnemyProjectilePresentation();
    railShooterInitialized_ = false;
    runtimeState_.terrain.enabled = false;
    runtimeState_.terrain.autoAdvancePreview = false;
    ApplyMultiMaterialShowcasePresentationDefaults(runtimeState_);
    submissionHudManuallyHidden_ = false;

    uint32_t humanoidModelIndex = 1;
    if (!scene_.skinnedModels[humanoidModelIndex].loaded) {
        humanoidModelIndex = scene_.skinnedModels[2].loaded ? 2u : 0u;
    }
    runtimeState_.selectedSkinnedModelIndex = humanoidModelIndex;

    ClearShowcaseEffects();
    runtimeState_.vfx.showcaseMode = false;
    runtimeState_.vfx.autoPlayVfxDemo = false;
    runtimeState_.vfx.iceProjectileClickToFire = false;
    // Presentation state must be configured by the scene, not by BuildUi().
    // Release intentionally skips ImGui while retaining the same VFX and
    // lighting pipeline as the development executable.
    ConfigureShowcasePostProcess();

    uint32_t multiMaterialModelIndex = UINT32_MAX;
    for (uint32_t modelIndex = 0;
         modelIndex < scene_.ManagedModelLibrary().size();
         ++modelIndex) {
        if (scene_.ManagedModelLibrary()[modelIndex].name == "multi_material_demo") {
            multiMaterialModelIndex = modelIndex;
            break;
        }
    }
    for (RuntimeVfxModelObjectState& object : runtimeState_.vfxModelObjects) {
        object.visible = false;
    }
    if (multiMaterialModelIndex != UINT32_MAX) {
        RuntimeVfxModelObjectState& showcase = runtimeState_.vfxModelObjects[0];
        showcase.visible = true;
        showcase.modelIndex = multiMaterialModelIndex;
        showcase.transform.scale = {0.68f, 0.68f, 0.68f};
        showcase.transform.rotate = {0.30f, 0.55f, 0.0f};
        showcase.transform.translate = {1.55f, -0.42f, -1.0f};
    }
}

void AppRunLoop::EnterRailShooterScene() {
    if (railPath_.Length() <= 0.0f) {
        ApplyRailShooterCourse();
    }

    // Submission-only attachments are opt-in with the isolated showcase scene.
    // Stop their persistent runtime instances before the rail course starts so
    // an invisible humanoid cannot keep spawning hand particles in gameplay.
    runtimeState_.handParticleAttachment.enabled = false;
    runtimeState_.leftHandParticleAttachment.enabled = false;
    runtimeState_.weaponAttachment.enabled = false;
    runtimeState_.skinnedAnimationBlend = {};
    runtimeState_.activeSkinningPath = RuntimeSkinningPath::Unavailable;
    handParticleAttachment_.Stop(vfxEngine_.Runtime());
    leftHandParticleAttachment_.Stop(vfxEngine_.Runtime());
    weaponAttachment_.Reset();
    runtimeState_.handParticleAttachmentTelemetry =
        handParticleAttachment_.Telemetry();
    runtimeState_.leftHandParticleAttachmentTelemetry =
        leftHandParticleAttachment_.Telemetry();
    runtimeState_.weaponAttachmentTelemetry = weaponAttachment_.Telemetry();
    runtimeState_.weaponDrawTelemetry = {};

    ClearShowcaseEffects();
    railShooterCourseRuntime_.Reset(runtimeState_.terrain.previewDistance);
    railShooterDistance_ = railShooterCourseRuntime_.Distance();
    railShooterPlayerMovement_.Reset();
    railShooterPlayerDodge_.Reset();
    railShooterVehicleMovement_.Reset(
        railShooterDistance_,
        0.0f,
        &railPath_);
    railShooterPlayerVehicleMount_.Reset();
    railShooterMountedEvasion_.Reset();
    railShooterMountedDefense_.Reset();
    railEnemyAttackDefenseValidation_.Reset();
    railShooterVehicleBodyCollision_.Reset();
    railShooterVehicleDamageCoordinator_.Reset();
    railShooterVehicleCollisionFeedback_.Reset();
    railShooterOccupantClearance_.Reset();
    railShooterEvasionConstraintResolver_.Reset();
    railShooterCombatMountBridge_.Reset();
    railShooterCameraMountBridge_.Reset();
    railShooterMountedEvasionPresentation_.Reset();
    railShooterOccupantActor_.Reset();
    railShooterEvasionFeedback_.Reset();
    railShooterPlayerLateralOffset_ =
        railShooterVehicleMovement_.Definition().mounts.player.x;
    railShooterPlayerVerticalOffset_ =
        railShooterVehicleMovement_.Definition().bodyVerticalOffset +
        railShooterVehicleMovement_.Definition().mounts.player.y;
    railShooterVehiclePresentation_.Reset();
    railShooterVehicleRideDynamics_.Reset();
    railShooterTrackContactPose_.Reset();
    railShooterCameraInertia_.Reset();
    railShooterVehicleActor_.Reset();
    railShooterVehicleRenderer_.Reset();
    railShooterVehicleAudioBridge_.Reset();
    railEnemyCombatPresentationBridge_.Reset();
    ResetEnemyProjectilePresentation();
    railShooterFrameIndex_ = 0;
    railShooterInitialized_ = true;
    if (railShooterGameSession_.IsInitialized()) {
        std::string sessionError;
        const bool sessionStarted =
            railShooterGameSession_.State().phase == GameSessionPhase::Ready
            ? railShooterGameSession_.Start(railShooterDistance_, &sessionError)
            : railShooterGameSession_.RestartRun(railShooterDistance_, &sessionError);
        if (!sessionStarted) {
            OutputDebugStringA(("[GameSession] Scene entry failed: " + sessionError + "\n").c_str());
        }
    }
    railShooterSessionPresentation_.Reset();
    if (railShooterRetryCoordinator_.IsBound()) {
        (void)railShooterRetryCoordinator_.CaptureCheckpoint(
            railShooterDistance_,
            "course_start",
            nullptr);
    }
    railEnemyAttackTelegraphSystem_.Reset();
    StopRailEnemyAttackFeedback();
    railShooterLockOnSystem_.Reset();
    railShooterSpeedDirector_.Reset();
    railShooterRideDirector_.Reset();
    railShooterTrackFeedbackDirector_.Reset();
    railShooterRideMotionEnvelope_.Reset();

    runtimeState_.terrain.enabled = true;
    runtimeState_.terrain.autoAdvancePreview = false;
    ApplyRailShooterTerrainBudget(runtimeState_.terrain);
    runtimeState_.terrain.cascadeShadowSplit0 =
        (std::min)(runtimeState_.terrain.cascadeShadowSplit0, 96.0f);
    runtimeState_.terrain.cascadeShadowSplit1 =
        (std::min)(runtimeState_.terrain.cascadeShadowSplit1, 220.0f);
    runtimeState_.terrain.cascadeShadowSplit2 =
        (std::min)(runtimeState_.terrain.cascadeShadowSplit2, 420.0f);
    runtimeState_.terrain.cascadeShadowSplit3 =
        (std::min)(runtimeState_.terrain.cascadeShadowSplit3, 760.0f);
    runtimeState_.camera.enableDebugInput = false;
    runtimeState_.camera.fovY = 0.30f * 3.14159265358979323846f;
    runtimeState_.camera.nearZ = 0.1f;
    runtimeState_.camera.farZ = 5000.0f;

    runtimeState_.useMonsterBall = false;
    runtimeState_.showAnimatedCube = false;
    runtimeState_.showSkinnedModel = false;
    runtimeState_.showSkeletonDebug = false;
    runtimeState_.showSkybox = false;
    runtimeState_.showProceduralBackdrop = true;
    runtimeState_.showSprite = true;
    runtimeState_.showVfxModelObjects = false;
    runtimeState_.submissionShowcase = {};

    runtimeState_.vfx.showcaseMode = false;
    runtimeState_.vfx.autoPlayVfxDemo = false;
    runtimeState_.vfx.enableParticles = true;
    runtimeState_.vfx.enableTrails = true;
    runtimeState_.vfx.enableBeams = false;
    runtimeState_.vfx.enableDistortions = false;
    runtimeState_.vfx.enableRings = true;
    runtimeState_.vfx.enableCylinders = true;
    runtimeState_.vfx.enableElectricOrbStrike = false;
    runtimeState_.vfx.enableSkinnedSurfaceVfx = false;
    runtimeState_.vfx.enableParticleDedicatedResources = false;
    runtimeState_.vfx.enableTrailMeshStream = false;
    runtimeState_.vfx.enableTrailMeshStreamAutoFallback = false;
    runtimeState_.vfx.enableTrailMeshStreamStartupTelemetry = false;
    runtimeState_.vfx.trailMeshStreamFallbackActive = false;
    runtimeState_.vfx.enableParticleDedicatedResourceProbe = false;
    runtimeState_.vfx.enableParticleDedicatedProbeTelemetry = false;
    runtimeState_.vfx.particleDedicatedResourceFallbackActive = false;
    runtimeState_.vfx.enableDistortionDedicatedResources = false;
    runtimeState_.vfx.distortionDedicatedResourceFallbackActive = false;
    runtimeState_.vfx.enableBeamDedicatedTelemetry = false;
    runtimeState_.vfx.beamDedicatedResourceFallbackActive = false;
}

bool AppRunLoop::HandleTitleScreenMessage(
    UINT message, WPARAM wParam, LPARAM lParam) {
    if (!railTitleScreenVisible_) return false;
#if defined(GE3_ENABLE_IMGUI) && GE3_ENABLE_IMGUI
    if (imguiLayer_.IsVisible()) {
        const ImGuiIO& io = ImGui::GetIO();
        const bool mouseMessage = message >= WM_MOUSEFIRST && message <= WM_MOUSELAST;
        const bool keyboardMessage = message >= WM_KEYFIRST && message <= WM_KEYLAST;
        // Let the normal window callback deliver UI input instead of using it
        // to activate a title-menu item underneath the tools switch/editor.
        if ((mouseMessage && io.WantCaptureMouse) ||
            (keyboardMessage && io.WantCaptureKeyboard)) return false;
    }
#endif
    // Ignore repeated confirm/menu input while the start camera owns the view.
    if (railTitleScene_.Starting()) return (message >= WM_KEYFIRST && message <= WM_KEYLAST) ||
        (message >= WM_MOUSEFIRST && message <= WM_MOUSELAST);
    bool activate = false;
    if (message == WM_MOUSEMOVE || message == WM_LBUTTONUP) {
        const int item = RailTitleScene::HitTest(static_cast<short>(LOWORD(lParam)),
            static_cast<short>(HIWORD(lParam)),float(windowWidth_),float(windowHeight_));
        if (!railTitleControlsVisible_ && item >= 0) railTitleMenuSelection_ = item;
        activate = message == WM_LBUTTONUP && (railTitleControlsVisible_ || item >= 0);
    } else if (message == WM_KEYDOWN) {
        if ((lParam & (LPARAM{1} << 30)) != 0) return true;
        if (wParam == VK_ESCAPE) {
            if (railTitleControlsVisible_) railTitleControlsVisible_ = false;
            else railTitleMenuSelection_ = 2;
            return true;
        }
        if (!railTitleControlsVisible_) {
            if (wParam == VK_UP || wParam == 'W') railTitleMenuSelection_ = (railTitleMenuSelection_ + 2) % 3;
            if (wParam == VK_DOWN || wParam == 'S') railTitleMenuSelection_ = (railTitleMenuSelection_ + 1) % 3;
        }
        activate = wParam == VK_RETURN || wParam == VK_SPACE;
    } else if (message == WM_LBUTTONDOWN || message == WM_RBUTTONDOWN || message == WM_RBUTTONUP) {
        return true;
    } else return false;
    if (activate) {
        if (railTitleControlsVisible_) railTitleControlsVisible_ = false;
        else if (railTitleMenuSelection_ == 1) railTitleControlsVisible_ = true;
        else if (railTitleMenuSelection_ == 2) PostMessageW(hwnd_, WM_CLOSE, 0, 0);
        else {
            railTitleScene_.BeginStart();
            railTitleControlsVisible_ = false;
            (void)audio_.Play(railSessionStartSound_,0.12f);
            OutputDebugStringA("[RailTitle] Start camera transition.\n");
        }
    }
    // Mouse movement also reaches ImGui so it can detect hover on the switch.
    return message != WM_MOUSEMOVE;
}

void AppRunLoop::UpdateRailShooterFrame() {
    const auto updateStart = RailPerfClock::now();
    railInputRouteDebug_.railSceneActive = true;
    railInputRouteDebug_.releaseFireTriggered = false;
    railInputRouteDebug_.releaseTokenCount = 0;
    railInputRouteDebug_.releaseHitCount = 0;
    railInputRouteDebug_.showcaseClickToFireEnabled = runtimeState_.vfx.iceProjectileClickToFire;
    if (RailShaderHotReloadEnabled() || imguiLayer_.WantsDeveloperDiagnostics()) {
        appPipelines_.HotReloadIfNeeded(dev_.GetDevice());
    }
    const RenderViewportMetrics metrics =
        ResolveRenderViewportMetrics(
            imguiLayer_.EditorViewportRenderTargetState(),
            windowWidth_,
            windowHeight_);
    ConfigureViewportAndScissor(runtimeState_, metrics.width, metrics.height);
    if (railTitleScreenVisible_ && railTitleScene_.ReadyForGameplay()) {
        railTitleScreenVisible_ = false;
        railTitleGameplayFade_ = 0.40f;
        runtimeState_.terrain.enabled = railTitleSavedTerrainEnabled_;
        runtimeState_.directionalLightData = railTitleSavedLight_;
        runtimeState_.pointLightData = railTitleSavedPointLight_;
        runtimeState_.spotLight = railTitleSavedSpotLight_;
        for(int i=0;i<4;++i) runtimeState_.clearColor[i]=railTitleSavedClearColor_[i];
        runtimeState_.showSkybox = railTitleSavedSkybox_;
        runtimeState_.showProceduralBackdrop = railTitleSavedBackdrop_;
        for (const auto& pass : railTitleSavedPostProcess_)
            (void)vfxEngine_.PostProcess().ConfigurePass(pass);
        railTitleSavedPostProcess_.clear();
        audio_.Stop(railTitleAmbience_);
        railTitleAmbiencePlaying_ = false;
        for(uint32_t id:railTitleDustIds_) vfxEngine_.Runtime().StopEffect(id);
        railTitleDustIds_.clear();
        railShooterHasLastUpdateTime_ = false;
        for(int key=0;key<256;++key) previousKeyDown_[key]=(GetAsyncKeyState(key)&0x8000)!=0;
        previousLeftMouseDown_=(GetAsyncKeyState(VK_LBUTTON)&0x8000)!=0;
    }
    if (railTitleScreenVisible_) {
        if (!railTitleHasLastUpdate_) {
            railTitleSavedTerrainEnabled_ = runtimeState_.terrain.enabled;
            // Gameplay fog/AO use a different depth range. Save and restore
            // only the passes this presentation overrides.
            railTitleSavedPostProcess_.clear();
            for (auto pass : vfxEngine_.PostProcess().Passes()) {
                if (pass.name != "ContactAO" && pass.name != "DistanceFog" && pass.name != "ToneMapping") continue;
                railTitleSavedPostProcess_.push_back(pass);
                if (pass.name == "DistanceFog") {
                    pass.enabled = false; // Title objects own their world-space haze.
                } else if (pass.name == "ToneMapping") {
                    pass.enabled = true;
                    pass.intensity = 1.0f;
                    pass.parameters.toneExposure = 1.55f;
                } else {
                    pass.enabled = true;
                    pass.intensity = 0.30f;
                    pass.parameters.contactAoRadiusPixels = 2.5f;
                    pass.parameters.contactAoBias = 0.08f;
                    pass.parameters.contactAoFalloff = 1.8f;
                    pass.parameters.contactAoNearPlane = 0.5f;
                    pass.parameters.contactAoFarPlane = 800.0f;
                }
                (void)vfxEngine_.PostProcess().ConfigurePass(pass);
            }
            const bool startRequested = railTitleScene_.Starting();
            const auto openingLight = railShooterCourse_.EvaluateLightingPreset(railShooterDistance_);
            scene_.titleVehicleReferenceLight = {openingLight.sunColor,
                Normalize(openingLight.sunDirection),openingLight.sunIntensity};
            const auto openingPose = railPath_.Evaluate(railShooterDistance_);
            scene_.titleVehicleReferenceNormal = Scale(openingPose.tangent,-1.0f);
            scene_.titleVehicleReferenceView = Normalize(Add(
                Scale(openingPose.tangent,-42.0f),Vector3{0,14.0f,0}));
            if (!railTitleScene_.Initialize()) OutputDebugStringA("[RailTitle] Track bake failed.\n");
            if (startRequested) railTitleScene_.BeginStart();
        }
        const float dt = railTitleHasLastUpdate_ ? std::chrono::duration<float>(updateStart-railTitleLastUpdate_).count() : 0.0f;
        railTitleLastUpdate_ = updateStart;
        railTitleHasLastUpdate_ = true;
        const bool focused = hwnd_ == nullptr || GetForegroundWindow() == hwnd_;
        const float titleDt = focused ? (std::clamp)(dt,0.0f,0.05f) : 0.0f;
        railTitleScene_.Update(titleDt);
        const auto titleSample = railTitleScene_.Path().Evaluate(railTitleScene_.Distance());
        // Stable warm key light follows the title composition; restore the
        // authored gameplay light only under the fully opaque transition.
        const auto titleColors = railTitleScene_.Colors(
            railShooterCourse_.EvaluateLightingPreset(railShooterDistance_).sunColor);
        runtimeState_.directionalLightData.color = titleColors.light;
        runtimeState_.directionalLightData.direction = Normalize(Add(
            Scale(titleSample.tangent,-0.85f),Add(Scale(titleSample.right,-0.35f),Vector3{0,-0.75f,0})));
        runtimeState_.directionalLightData.intensity = 2.15f*(1.0f-0.35f*railTitleScene_.TunnelShade());
        runtimeState_.pointLightData.intensity = 0.0f;
        runtimeState_.spotLight.intensity = 0.0f;
        // Match the far landscape haze and remove the gameplay sky's bright sun.
        runtimeState_.showSkybox = false;
        runtimeState_.showProceduralBackdrop = false;
        runtimeState_.clearColor[0]=titleColors.background.x;
        runtimeState_.clearColor[1]=titleColors.background.y;
        runtimeState_.clearColor[2]=titleColors.background.z;
        runtimeState_.clearColor[3]=1.0f;
        const float targetGain = focused ? railTitleScene_.AmbienceGain()*0.34f : 0.0f;
        railTitleAudioGain_ += (targetGain-railTitleAudioGain_)*(1.0f-std::exp(-8.0f*(std::clamp)(dt,0.0f,0.05f)));
        if(focused && !railTitleAmbiencePlaying_ && railTitleAmbience_.IsValid())
            railTitleAmbiencePlaying_ = audio_.PlaySpatial(railTitleAmbience_,0.0f,0.15f,1.0f,true);
        audio_.SetPlayback(railTitleAmbience_,railTitleAudioGain_*(1.0f-railTitleScene_.Blackout()),
            1.0f);
        if(!focused && railTitleAudioGain_<0.001f && railTitleAmbiencePlaying_) {
            audio_.Stop(railTitleAmbience_); railTitleAmbiencePlaying_=false;
        }
        railTitleDustTimer_ += titleDt;
        // Creating a new effect resets the shared GPU particle pool. Let impact
        // billows evolve without repeatedly resetting them with wheel puffs.
        const bool attackParticlesActive = !railTitleScene_.Shots().empty() || !railTitleScene_.DustClouds().empty() || !railTitleScene_.AttackCues().empty() ||
            std::any_of(vfxEngine_.Runtime().Instances().begin(),vfxEngine_.Runtime().Instances().end(),
                [](const auto& instance){return instance.assetName=="title_ground_impact" || instance.assetName=="title_drone_muzzle";});
        if(railTitleDustTimer_ >= 0.12f && !attackParticlesActive && railTitleScene_.Blackout()<0.2f) {
            railTitleDustTimer_ = std::fmod(railTitleDustTimer_,0.12f);
            for(int wheel=0;wheel<2;++wheel) {
                Vector3 origin=railTitleScene_.Wheels().wheels[wheel].railContact;
                origin=Add(origin,Add(Scale(titleSample.tangent,-0.7f),Vector3{0,0.12f,0}));
                const uint32_t id=vfxEngine_.Runtime().PlayEffectWithParams("title_wheel_dust",origin,
                    {1,1,1,1},{1,1,1});
                if(id!=0) railTitleDustIds_.push_back(id);
            }
        }
        // Sand clouds and tracers are deterministic, depth-tested world draws.
        // Only the brief muzzle flash uses the shared GPU particle pool.
        for(const auto& cue:railTitleScene_.AttackCues()) {
            if(cue.kind!=RailTitleAttackCueKind::Muzzle) continue;
            const uint32_t id=vfxEngine_.Runtime().PlayEffectWithParams(
                "title_drone_muzzle",cue.position,{1,1,1,1},{1,1,1});
            if(id) railTitleDustIds_.push_back(id);
        }
        vfxEngine_.Update(runtimeState_.vfx,titleDt);
        std::erase_if(railTitleDustIds_,[this](uint32_t id){return vfxEngine_.Runtime().FindInstance(id)==nullptr;});
        runtimeState_.terrain.enabled = false;
        const Vector3 eye = railTitleScene_.CameraPosition();
        frameState_.viewMatrix = MakeLookAtMatrix(eye,railTitleScene_.CameraTarget(),{0,1,0});
        // The orbit never approaches geometry closer than several metres.
        // Tight title-only clipping improves depth precision for ground/rail separation.
        frameState_.projMatrix = MakePerspectiveFovMatrix(railTitleScene_.CameraFov(),metrics.AspectRatio(),0.5f,800.0f);
        frameState_.viewProjectionMatrix = Multiply(frameState_.viewMatrix,frameState_.projMatrix);
        frameState_.cameraWorldPosition = eye;
        frameState_.deltaTime = titleDt;
        runtimeState_.cameraWorldPosition = eye;
        scene_.UpdateCameraWorldPosition(eye);
        if (scene_.mappedCamera != nullptr) {
            auto& camera = *scene_.mappedCamera;
            camera.titleSubject = {titleSample.position.x,titleSample.position.y,titleSample.position.z,1.0f};
            camera.titleForward = {titleSample.tangent.x,titleSample.tangent.y,titleSample.tangent.z,railTitleScene_.TunnelShade()};
            camera.titleHaze = {titleColors.background.x,titleColors.background.y,titleColors.background.z,1.0f};
            for (size_t i=0;i<camera.titleWheelContacts.size();++i) {
                const auto& wheel = railTitleScene_.Wheels().wheels[i];
                camera.titleWheelContacts[i] = {wheel.railContact.x,wheel.railContact.y,wheel.railContact.z,wheel.visible?1.0f:0.0f};
            }
        }
        railShooterHasLastUpdateTime_ = false;
        return;
    }
    ++railShooterFrameIndex_;
    ResetRailPerfFrame(railShooterFrameIndex_, railShooterDistance_);
    const RailWorldRaycast::FrameCacheScope raycastFrameCache;
    LogRailFrameStage(railShooterFrameIndex_, railShooterDistance_, "update.begin");

    // The review capture ran at 30 fps, so advancing a fixed 16 ms only once
    // per rendered frame stretched the authored 41-second run to about 86
    // seconds. Use measured wall time, bounded so a hitch cannot tunnel play.
    constexpr float kFallbackGameplayDeltaTime = 0.016f;
    float realGameplayDeltaTime = kFallbackGameplayDeltaTime;
    if (railShooterHasLastUpdateTime_) {
        const float measured = std::chrono::duration<float>(
            updateStart - railShooterLastUpdateTime_).count();
        if (std::isfinite(measured) && measured > 0.0f) {
            realGameplayDeltaTime = (std::clamp)(measured, 0.001f, 0.050f);
        }
    }
    railShooterLastUpdateTime_ = updateStart;
    railShooterHasLastUpdateTime_ = true;
    const bool titleEntryBlocked = railTitleGameplayFade_ > 0.0f;
    railTitleGameplayFade_ = (std::max)(0.0f,railTitleGameplayFade_-realGameplayDeltaTime);
    const bool editorRuntimeAdvance = imguiLayer_.ShouldAdvanceEditorRuntimeFrame();
    const bool coursePreviewOwnsRail = coursePreviewSimulationSystem_.IsActive();
    const bool coursePreviewAdvancing =
        coursePreviewOwnsRail && coursePreviewSimulationSystem_.IsPlaying() &&
        editorRuntimeAdvance && !runtimeState_.terrain.freezeCourseRuntime;
    if (coursePreviewAdvancing) {
        coursePreviewSimulationSystem_.Tick(kFallbackGameplayDeltaTime);
    }
    const bool coursePreviewFrozen =
        runtimeState_.terrain.freezeCourseRuntime || !editorRuntimeAdvance ||
        coursePreviewOwnsRail;
    const float sessionDeltaTime = (coursePreviewFrozen || titleEntryBlocked)
        ? 0.0f : realGameplayDeltaTime;
    float gameplayDeltaTime = sessionDeltaTime;
    if (!coursePreviewOwnsRail && railShooterGameSession_.IsInitialized() &&
        !railShooterGameSession_.AllowsGameplaySimulation()) {
        gameplayDeltaTime = 0.0f;
    }
    railWeaponHotReloadPollTimer_ += realGameplayDeltaTime;
    if (drone_perf::Enabled()) drone_perf::frame.simulationDeltaTime = gameplayDeltaTime;
    if (railWeaponHotReloadPollTimer_ >= 0.25f &&
        (!railShooterCollisionSystem_.WeaponDefinitions().Directory().empty() ||
         !railAimAssistPresetRegistry_.Directory().empty() ||
         !railVehicleControlPresetRegistry_.Directory().empty())) {
        railWeaponHotReloadPollTimer_ = 0.0f;
        if (!railShooterCollisionSystem_.WeaponDefinitions().Directory().empty()) {
            const WeaponDefinitionReloadReport weaponReload =
                railShooterCollisionSystem_.ReloadChangedWeaponDefinitions();
            if (weaponReload.status == WeaponDefinitionReloadStatus::Reloaded) {
                OutputDebugStringA(
                    ("[WeaponDefinitions] " + weaponReload.message + "\n").c_str());
            }
        }
        if (!railAimAssistPresetRegistry_.Directory().empty()) {
            const RailAimAssistPresetReloadReport presetReload =
                railAimAssistPresetRegistry_.ReloadChangedPresets();
            if (presetReload.status == RailAimAssistPresetReloadStatus::Reloaded) {
                OutputDebugStringA(
                    ("[AimAssistPresets] " + presetReload.message + "\n").c_str());
            }
        }
        if (!railVehicleControlPresetRegistry_.Directory().empty()) {
            const RailVehicleControlReloadReport controlReload =
                railVehicleControlPresetRegistry_.ReloadChangedPresets();
            if (controlReload.status ==
                RailVehicleControlReloadStatus::Reloaded) {
                OutputDebugStringA(
                    ("[RailVehicleControls] " +
                     controlReload.message + "\n").c_str());
            }
        }
    }
    for (RailNormalShotLine& line : railNormalShotLines_) {
        line.age += gameplayDeltaTime;
    }
    railNormalShotLines_.erase(
        std::remove_if(
            railNormalShotLines_.begin(),
            railNormalShotLines_.end(),
            [](const RailNormalShotLine& line) {
                return line.age >= line.lifetime;
            }),
        railNormalShotLines_.end());

    if (!railShooterInitialized_) {
        EnterRailShooterScene();
    }
    ProcessPostProcessShowcaseShortcuts();
    if (railPath_.Length() <= 0.0f) {
        ApplyRailShooterCourse();
    }
    if (coursePreviewSimulationSystem_.IsActive()) {
        railShooterCourseRuntime_.Reset(
            coursePreviewSimulationSystem_.Frame().distance);
        railShooterDistance_ = railShooterCourseRuntime_.Distance();
    }
    std::string previewActorError;
    if (!coursePreviewActorRuntimeBridge_.Synchronize(
            coursePreviewSimulationSystem_,
            coursePreviewAdvancing ? kFallbackGameplayDeltaTime : 0.0f,
            railShooterDistance_,
            &previewActorError) &&
        !previewActorError.empty()) {
        OutputDebugStringA(
            ("[CoursePreviewActors] " + previewActorError + "\n").c_str());
    }

    // Vehicle mounting owns whether rail-local player movement is ticked. Poll
    // once and share this immutable device frame with dodge and lock-on so
    // button edges cannot be consumed twice.
    const AppGamepadFrame railAimGamepad = railAimGamepad_.Poll();
    bool playerKeyboardAllowed =
        hwnd_ == nullptr || GetForegroundWindow() == hwnd_;
#if defined(GE3_ENABLE_IMGUI) && GE3_ENABLE_IMGUI
    if (playerKeyboardAllowed && imguiLayer_.IsVisible() &&
        (ImGui::GetIO().WantTextInput || ImGui::IsAnyItemActive())) {
        playerKeyboardAllowed = false;
    }
#endif
    const auto playerKeyDown = [playerKeyboardAllowed](int virtualKey) {
        return playerKeyboardAllowed &&
            (GetAsyncKeyState(virtualKey) & 0x8000) != 0;
    };
    const float keyboardMoveX =
        ((playerKeyDown('D') || playerKeyDown(VK_RIGHT)) ? 1.0f : 0.0f) -
        ((playerKeyDown('A') || playerKeyDown(VK_LEFT)) ? 1.0f : 0.0f);
    const float keyboardMoveY =
        ((playerKeyDown('W') || playerKeyDown(VK_UP)) ? 1.0f : 0.0f) -
        ((playerKeyDown('S') || playerKeyDown(VK_DOWN)) ? 1.0f : 0.0f);
    const AppGamepadStick playerMove = AppGamepadInput::ClampUnitCircle(
        railAimGamepad.leftStick.x + keyboardMoveX,
        railAimGamepad.leftStick.y + keyboardMoveY);
    const bool playerInputEnabled =
        !coursePreviewOwnsRail && gameplayDeltaTime > 0.0f &&
        (!railShooterGameSession_.IsInitialized() ||
         railShooterGameSession_.State().playerInputEnabled);
    const bool keyboardDodgePressed =
        playerKeyboardAllowed &&
        (WasKeyPressed(VK_SPACE) || WasKeyPressed(VK_SHIFT));
    const bool evasionPressed =
        keyboardDodgePressed || railAimGamepad.resetPressed ||
        railAimGamepad.leftShoulderPressed ||
        railAimGamepad.rightShoulderPressed;
    const bool mountedMovementActive =
        railShooterVehicleMovement_.IsInitialized() &&
        railShooterVehicleMovement_.Definition().mountedMovementMode ==
            RailVehicleMountedMovementMode::VehicleMounted;
    RailDodgeInput dodgeInput{};
    dodgeInput.deltaTime = gameplayDeltaTime;
    dodgeInput.inputEnabled = playerInputEnabled && !mountedMovementActive;
    dodgeInput.dodgePressed = evasionPressed && !mountedMovementActive;
    dodgeInput.directionX = playerMove.x;
    dodgeInput.directionY = playerMove.y;
    if (railAimGamepad.leftShoulderPressed !=
        railAimGamepad.rightShoulderPressed) {
        dodgeInput.directionX = railAimGamepad.leftShoulderPressed ? -1.0f : 1.0f;
        dodgeInput.directionY = 0.0f;
    }
    dodgeInput.movementVelocityX =
        railShooterPlayerMovement_.State().lateralVelocity;
    dodgeInput.movementVelocityY =
        railShooterPlayerMovement_.State().verticalVelocity;
    const RailDodgeFrame& dodgeFrame = railShooterPlayerDodge_.Update(dodgeInput);

    RailPlayerMovementInput movementInput{};
    movementInput.deltaTime = gameplayDeltaTime;
    movementInput.moveX = playerMove.x;
    movementInput.moveY = playerMove.y;
    movementInput.inputEnabled = playerInputEnabled;
    movementInput.movementScale = dodgeFrame.movementScale;
    movementInput.externalLateralDisplacement = dodgeFrame.lateralDisplacement;
    movementInput.externalVerticalDisplacement = dodgeFrame.verticalDisplacement;
    movementInput.externalBankNormalized = dodgeFrame.bankNormalized;
    if (dodgeFrame.startedThisFrame) {
        railShooterCameraDirector_.AddFeedbackImpulse(
            0.32f,
            -0.003f,
            -0.018f * dodgeFrame.state.directionX);
    }

    RailSpeedDirectorFrameInput speedInput{};
    speedInput.course = &railShooterCourse_;
    speedInput.railPath = &railPath_;
    speedInput.section = railShooterCourseRuntime_.CurrentSection();
    speedInput.rideProfile = railShooterCourse_.FindRideProfile(speedInput.distance);
    speedInput.distance = railShooterCourseRuntime_.Distance();
    speedInput.deltaTime = gameplayDeltaTime;
    const RailSpeedDirectorFrame speedFrame = railShooterSpeedDirector_.Evaluate(speedInput);
    RailRideDirectorInput rideInput{};
    rideInput.course = &railShooterCourse_;
    rideInput.railPath = &railPath_;
    rideInput.distance = speedInput.distance;
    rideInput.baseRequestedSpeed = speedFrame.requestedSpeed;
    const RailRideDirectorFrame& rideFrame = railShooterRideDirector_.Evaluate(rideInput);
    RailRideMotionEnvelopeInput envelopeInput{};
    envelopeInput.vehicleDefinition = &railShooterVehicleMovement_.Definition();
    envelopeInput.vehicleState = &railShooterVehicleMovement_.State();
    envelopeInput.ride = &rideFrame;
    envelopeInput.railPath = &railPath_;
    const RailRideMotionEnvelopeFrame& motionEnvelope =
        railShooterRideMotionEnvelope_.Evaluate(envelopeInput);
    RailVehicleMovementInput vehicleInput{};
    vehicleInput.deltaTime = gameplayDeltaTime;
    vehicleInput.requestedSpeed = motionEnvelope.requestedSpeed;
    vehicleInput.movementEnabled = gameplayDeltaTime > 0.0f;
    vehicleInput.motionEnvelopeActive = motionEnvelope.active;
    vehicleInput.accelerationLimit = motionEnvelope.accelerationLimit;
    vehicleInput.brakingLimit = motionEnvelope.brakingLimit;
    vehicleInput.jerkLimit = motionEnvelope.jerkLimit;
    vehicleInput.courseRuntime = &railShooterCourseRuntime_;
    vehicleInput.railPath = &railPath_;
    const RailVehicleMovementFrame& vehicleFrame =
        railShooterVehicleMovement_.Update(vehicleInput);
    RailTrackFeedbackInput trackFeedbackInput{};
    trackFeedbackInput.course = &railShooterCourse_;
    trackFeedbackInput.previousDistance = vehicleFrame.state.previousDistance;
    trackFeedbackInput.distance = vehicleFrame.state.distance;
    trackFeedbackInput.speed = vehicleFrame.state.speed;
    trackFeedbackInput.maximumSpeed =
        railShooterVehicleMovement_.Definition().maximumSpeed;
    trackFeedbackInput.deltaTime = gameplayDeltaTime;
    trackFeedbackInput.gameplayActive =
        !coursePreviewOwnsRail && gameplayDeltaTime > 0.0f;
    trackFeedbackInput.settings = railShooterTrackFeedbackSettings_;
    const RailTrackFeedbackFrame& trackFeedbackFrame =
        railShooterTrackFeedbackDirector_.Update(trackFeedbackInput);
    DispatchRailTrackFeedback();
    RailVehicleTrackContactPoseInput trackContactInput{};
    trackContactInput.definition = &railShooterVehicleMovement_.Definition();
    trackContactInput.state = &railShooterVehicleMovement_.State();
    trackContactInput.railPath = &railPath_;
    trackContactInput.settings = railShooterTrackContactPoseSettings_;
    const RailVehicleTrackContactPoseFrame& trackContactFrame =
        railShooterTrackContactPose_.Solve(trackContactInput);
    std::vector<CourseEventMarker> triggeredEvents = vehicleFrame.triggeredEvents;
    railShooterDistance_ = vehicleFrame.state.distance;

    RailVehicleMountedEvasionInput mountedEvasionInput{};
    mountedEvasionInput.deltaTime = gameplayDeltaTime;
    mountedEvasionInput.vehicleDefinition =
        &railShooterVehicleMovement_.Definition();
    mountedEvasionInput.vehicleState = &railShooterVehicleMovement_.State();
    mountedEvasionInput.occupantMounted = true;
    mountedEvasionInput.inputEnabled = playerInputEnabled;
    mountedEvasionInput.evadePressed = evasionPressed;
    mountedEvasionInput.directionX = dodgeInput.directionX;
    mountedEvasionInput.directionY = dodgeInput.directionY;
    float clearanceDirectionX = mountedEvasionInput.directionX;
    float clearanceDirectionY = mountedEvasionInput.directionY;
    const RailVehicleMountedEvasionRuntimeState& previousEvasionState =
        railShooterMountedEvasion_.State();
    if (previousEvasionState.phase == RailVehicleMountedEvasionPhase::Evading ||
        previousEvasionState.phase == RailVehicleMountedEvasionPhase::Recovering) {
        clearanceDirectionX = previousEvasionState.directionX;
        clearanceDirectionY = previousEvasionState.directionY;
    }
    float clearanceDirectionLength = std::sqrt(
        clearanceDirectionX * clearanceDirectionX +
        clearanceDirectionY * clearanceDirectionY);
    if (!std::isfinite(clearanceDirectionLength) || clearanceDirectionLength <
        railShooterMountedEvasion_.Definition().minimumDirectionalInput) {
        clearanceDirectionX =
            (previousEvasionState.evasionCount % 2u) == 0u ? 1.0f : -1.0f;
        clearanceDirectionY = 0.0f;
        clearanceDirectionLength = 1.0f;
    }
    clearanceDirectionX /= clearanceDirectionLength;
    clearanceDirectionY /= clearanceDirectionLength;

    const RailVehicleDefinition& activeVehicleDefinition =
        railShooterVehicleMovement_.Definition();
    const RailVehicleRuntimeState& activeVehicleState =
        railShooterVehicleMovement_.State();
    RailVehicleOccupantClearanceInput clearanceInput{};
    clearanceInput.spawnRuntime = &railShooterSpawnRuntime_;
    clearanceInput.course = &railShooterCourse_;
    clearanceInput.railPath = &railPath_;
    clearanceInput.terrainSettings = &runtimeState_.terrain.settings;
    clearanceInput.terrainEdits = &railShooterCourse_.terrainEditLayer;
    clearanceInput.terrainPreview = &runtimeState_.terrain.previewEditLayer;
    clearanceInput.startDistance = activeVehicleState.distance;
    clearanceInput.startLateralOffset = activeVehicleDefinition.mounts.player.x;
    clearanceInput.startVerticalOffset =
        activeVehicleDefinition.bodyVerticalOffset +
        activeVehicleDefinition.mounts.player.y;
    clearanceInput.targetDistance = activeVehicleState.distance;
    clearanceInput.targetLateralOffset =
        clearanceInput.startLateralOffset + clearanceDirectionX *
        railShooterMountedEvasion_.Definition().lateralDistance;
    clearanceInput.targetVerticalOffset =
        clearanceInput.startVerticalOffset + clearanceDirectionY *
        railShooterMountedEvasion_.Definition().verticalDistance;
    const bool clearanceRequired =
        (mountedEvasionInput.evadePressed &&
         railShooterMountedEvasion_.CanEvade()) ||
        previousEvasionState.phase == RailVehicleMountedEvasionPhase::Evading ||
        previousEvasionState.phase == RailVehicleMountedEvasionPhase::Recovering;
    clearanceInput.gameplayActive = mountedMovementActive &&
        !coursePreviewOwnsRail && clearanceRequired;
    const RailVehicleOccupantClearanceFrame& clearanceFrame =
        railShooterOccupantClearance_.Update(clearanceInput);

    RailVehicleEvasionConstraintInput constraintInput{};
    constraintInput.requestedInput = mountedEvasionInput;
    constraintInput.clearance = &clearanceFrame;
    constraintInput.canStartEvasion = railShooterMountedEvasion_.CanEvade();
    const RailVehicleEvasionConstraintFrame& constraintFrame =
        railShooterEvasionConstraintResolver_.Update(constraintInput);
    const RailVehicleMountedEvasionFrame& mountedEvasionFrame =
        railShooterMountedEvasion_.Update(constraintFrame.constrainedInput);
    RailVehicleMountedDefenseInput mountedDefenseInput{};
    mountedDefenseInput.vehicleDefinition =
        &railShooterVehicleMovement_.Definition();
    mountedDefenseInput.vehicleState = &railShooterVehicleMovement_.State();
    mountedDefenseInput.occupantMotion = &mountedEvasionFrame;
    mountedDefenseInput.occupantMounted = true;
    const RailVehicleMountedDefenseFrame& mountedDefenseFrame =
        railShooterMountedDefense_.Update(mountedDefenseInput);
    if (mountedEvasionFrame.startedThisFrame) {
        railShooterCameraDirector_.AddFeedbackImpulse(
            0.24f,
            -0.002f,
            -0.012f * mountedEvasionFrame.state.directionX);
    }

    RailPlayerVehicleMountInput mountInput{};
    mountInput.vehicleDefinition = &railShooterVehicleMovement_.Definition();
    mountInput.vehicleState = &railShooterVehicleMovement_.State();
    mountInput.playerMovement = &railShooterPlayerMovement_;
    mountInput.movementInput = movementInput;
    mountInput.occupantMounted = true;
    const RailPlayerVehicleMountFrame& mountFrame =
        railShooterPlayerVehicleMount_.Update(mountInput);
    RailVehicleCombatMountInput combatMountInput{};
    combatMountInput.vehicleDefinition =
        &railShooterVehicleMovement_.Definition();
    combatMountInput.vehicleState = &railShooterVehicleMovement_.State();
    combatMountInput.evasion = &mountedEvasionFrame;
    railShooterCombatMountBridge_.Update(combatMountInput);
    const RailVehicleCombatMountFrame& combatMountFrame =
        railShooterCombatMountBridge_.Frame();
    railShooterPlayerLateralOffset_ = combatMountFrame.valid
        ? combatMountFrame.playerLateralOffset
        : mountFrame.railLateralOffset;
    railShooterPlayerVerticalOffset_ = combatMountFrame.valid
        ? combatMountFrame.playerVerticalOffset
        : mountFrame.railVerticalOffset;

    RailVehicleRideDynamicsInput rideDynamicsInput{};
    rideDynamicsInput.definition = &railShooterVehicleMovement_.Definition();
    rideDynamicsInput.state = &railShooterVehicleMovement_.State();
    rideDynamicsInput.ride = &rideFrame;
    rideDynamicsInput.trackFeedback = &trackFeedbackFrame;
    rideDynamicsInput.deltaTime = gameplayDeltaTime;
    rideDynamicsInput.settings = railShooterVehicleRideDynamicsSettings_;
    const RailVehicleRideDynamicsFrame& rideDynamicsFrame =
        railShooterVehicleRideDynamics_.Update(rideDynamicsInput);

    RailVehiclePresentationInput vehiclePresentationInput{};
    vehiclePresentationInput.definition = &railShooterVehicleMovement_.Definition();
    vehiclePresentationInput.state = &railShooterVehicleMovement_.State();
    vehiclePresentationInput.deltaTime = gameplayDeltaTime;
    vehiclePresentationInput.settings =
        railShooterVehiclePresentationSettings_;
    vehiclePresentationInput.rideProfileActive = rideFrame.active;
    vehiclePresentationInput.rideProfileBlend = rideFrame.profileBlend;
    vehiclePresentationInput.rideAnticipatedSignedCurvature =
        rideFrame.anticipatedSignedCurvature;
    vehiclePresentationInput.rideVisualBankScale = rideFrame.visualBankScale;
    vehiclePresentationInput.rideMaximumVisualBankDegrees =
        rideFrame.maximumVisualBankDegrees;
    vehiclePresentationInput.rideDynamics = &rideDynamicsFrame;
    vehiclePresentationInput.trackContactPose = &trackContactFrame;
    railShooterVehiclePresentation_.Update(vehiclePresentationInput);

    RailVehicleWheelContactPresentationInput wheelPresentationInput{};
    wheelPresentationInput.contacts = &trackContactFrame;
    wheelPresentationInput.vehiclePresentation =
        &railShooterVehiclePresentation_.Frame();
    wheelPresentationInput.settings.wheelRadius =
        railShooterVehiclePresentationSettings_.wheelRadius;
    wheelPresentationInput.settings.wheelWidth =
        railShooterTrackDefinition_.wheelWidth;
    wheelPresentationInput.settings.wheelMeshId =
        railShooterTrackDefinition_.wheelProxyMeshId;
    wheelPresentationInput.settings.color =
        railShooterTrackDefinition_.wheelColor;
    railShooterWheelContactPresentation_.Update(wheelPresentationInput);

    RailVehicleMountedEvasionPresentationInput evasionPresentationInput{};
    evasionPresentationInput.vehicleState =
        &railShooterVehicleMovement_.State();
    evasionPresentationInput.vehiclePresentation =
        &railShooterVehiclePresentation_.Frame();
    evasionPresentationInput.evasion = &mountedEvasionFrame;
    evasionPresentationInput.combatMount = &combatMountFrame;
    evasionPresentationInput.deltaTime = gameplayDeltaTime;
    evasionPresentationInput.gameplayVisible = !coursePreviewOwnsRail;
    evasionPresentationInput.settings =
        railShooterMountedEvasionPresentationSettings_;
    railShooterMountedEvasionPresentation_.Update(
        evasionPresentationInput);

    RailVehicleOccupantActorInput occupantActorInput{};
    occupantActorInput.presentation =
        &railShooterMountedEvasionPresentation_.Frame();
    occupantActorInput.cameraWorldPosition = frameState_.cameraWorldPosition;
    railShooterOccupantActor_.Update(occupantActorInput);

    RailVehicleEvasionFeedbackInput evasionFeedbackInput{};
    evasionFeedbackInput.evasion = &mountedEvasionFrame;
    evasionFeedbackInput.combatMount = &combatMountFrame;
    evasionFeedbackInput.deltaTime = gameplayDeltaTime;
    evasionFeedbackInput.gameplayActive =
        playerInputEnabled && !coursePreviewOwnsRail;
    evasionFeedbackInput.settings = railShooterEvasionFeedbackSettings_;
    railShooterEvasionFeedback_.Update(evasionFeedbackInput);
    DispatchRailVehicleEvasionFeedback();

    RailVehicleActorInput vehicleActorInput{};
    vehicleActorInput.vehicleDefinition = &railShooterVehicleMovement_.Definition();
    vehicleActorInput.vehicleState = &railShooterVehicleMovement_.State();
    vehicleActorInput.presentation = &railShooterVehiclePresentation_.Frame();
    // Build the stable ride pose for camera framing first. Damage offsets are
    // applied only to the final render pose, after this frame's hit resolution.
    vehicleActorInput.collisionFeedback = nullptr;
    railShooterVehicleActor_.Update(vehicleActorInput);
    RailVehicleRenderInput vehicleRenderInput{};
    vehicleRenderInput.actor = &railShooterVehicleActor_.Frame();
    vehicleRenderInput.cameraWorldPosition = frameState_.cameraWorldPosition;
    railShooterVehicleRenderer_.Update(vehicleRenderInput);
    DispatchRailVehicleAudio(gameplayDeltaTime);
    if (railShooterVehiclePresentation_.Frame().jointImpactThisFrame) {
        railShooterCameraDirector_.AddFeedbackImpulse(0.055f, 0.0f, 0.0015f);
    }
    RailVehicleCameraMountInput cameraMountInput{};
    cameraMountInput.vehicleDefinition =
        &railShooterVehicleMovement_.Definition();
    cameraMountInput.vehicleState = &railShooterVehicleMovement_.State();
    cameraMountInput.evasion = &mountedEvasionFrame;
    cameraMountInput.gameplayActive = !coursePreviewOwnsRail;
    railShooterCameraMountBridge_.Update(cameraMountInput);
    RailVehicleCameraInertiaInput cameraInertiaInput{};
    cameraInertiaInput.definition = &railShooterVehicleMovement_.Definition();
    cameraInertiaInput.state = &railShooterVehicleMovement_.State();
    cameraInertiaInput.trackContact = &trackContactFrame;
    cameraInertiaInput.rideDynamics = &rideDynamicsFrame;
    cameraInertiaInput.ride = &rideFrame;
    cameraInertiaInput.deltaTime = gameplayDeltaTime;
    cameraInertiaInput.gameplayActive = !coursePreviewOwnsRail;
    cameraInertiaInput.aimFocusActive =
        railShooterLockOnSystem_.Reticle().aimFeelActive;
    cameraInertiaInput.settings = railShooterCameraInertiaSettings_;
    const RailVehicleCameraInertiaFrame& cameraInertiaFrame =
        railShooterCameraInertia_.Update(cameraInertiaInput);
    if (!coursePreviewOwnsRail && railShooterGameplayWaveBridge_.IsBound()) {
        CourseGameplayWaveFrameInput waveInput{};
        waveInput.deltaTime = gameplayDeltaTime;
        waveInput.playerDistance = railShooterDistance_;
        waveInput.signaledEventIds.reserve(triggeredEvents.size());
        for (const CourseEventMarker& event : triggeredEvents) {
            if (!event.id.empty()) waveInput.signaledEventIds.push_back(event.id);
        }
        railShooterGameplayWaveBridge_.Update(waveInput);
        for (const CourseGameplayWaveEvent& event :
             railShooterGameplayWaveBridge_.Events()) {
            OutputDebugStringA(
                ("[CourseGameplayWaves] " + std::string(ToString(event.type)) +
                 " " + event.subjectGuid + ": " + event.message + "\n").c_str());
        }
    }
    EncounterDirectorFrameInput encounterInput{};
    encounterInput.deltaTime = gameplayDeltaTime;
    encounterInput.currentDistance = railShooterDistance_;
    encounterInput.triggeredEvents = triggeredEvents;
    if (railShooterGameplayWaveBridge_.IsBound() &&
        !railShooterRuntimeProgram_.waves.empty()) {
        // First-class WaveDefinitions replace only the legacy EnemyWaveAsset
        // authored at the same rail distance. This permits incremental Course
        // migration: adding one Editor Wave must not silence every remaining
        // legacy encounter in the production course.
        encounterInput.triggeredEvents.erase(
            std::remove_if(
                encounterInput.triggeredEvents.begin(),
                encounterInput.triggeredEvents.end(),
                [this](const CourseEventMarker& event) {
                    if (event.type != "enemy_wave") return false;
                    constexpr float kSameEncounterDistanceTolerance = 0.5f;
                    return std::any_of(
                        railShooterRuntimeProgram_.waves.begin(),
                        railShooterRuntimeProgram_.waves.end(),
                        [&event, kSameEncounterDistanceTolerance](
                            const CourseRuntimeWaveNode& wave) {
                            return wave.enabled &&
                                std::abs(
                                    wave.triggerRailDistance - event.distance) <=
                                    kSameEncounterDistanceTolerance;
                        });
                }),
            encounterInput.triggeredEvents.end());
    }
    encounterInput.spawnRuntime = &railShooterSpawnRuntime_;
    const EncounterDirectorFrameOutput encounterOutput =
        railShooterEncounterDirector_.Update(std::move(encounterInput));
    LogCourseEvents(encounterOutput.dispatchEvents);
    railShooterCameraDirector_.NotifyCourseEvents(encounterOutput.dispatchEvents);
    railShooterSpeedDirector_.NotifyCourseEvents(encounterOutput.dispatchEvents);
    railShooterCheckpointSystem_.Update(&railShooterCourse_, railShooterDistance_);
    const SectionCheckpointStats& checkpointStats = railShooterCheckpointSystem_.LastStats();
    if (!coursePreviewOwnsRail && checkpointStats.enteredSectionThisFrame &&
        railShooterRetryCoordinator_.IsBound()) {
        std::string retryCheckpointError;
        if (!railShooterRetryCoordinator_.CaptureCheckpoint(
            checkpointStats.checkpointDistance,
            checkpointStats.currentSectionName,
            &retryCheckpointError)) {
            OutputDebugStringA(
                ("[GameSessionRetry] Checkpoint capture skipped: " +
                 retryCheckpointError + "\n").c_str());
        }
    }
    railShooterEventDispatcher_.Dispatch(
        encounterOutput.dispatchEvents,
        railShooterSpawnRuntime_,
        railShooterDistance_);

    EnemyEncounterPacingInput pacingInput{};
    pacingInput.course = &railShooterCourse_;
    pacingInput.runtime = &railShooterSpawnRuntime_;
    // Readability is intentionally the completed previous-frame contract.
    // This avoids ordering cycles with projection/telegraph presentation.
    pacingInput.readability = &railEnemyEncounterReadabilityDirector_.Frame();
    pacingInput.currentDistance = railShooterDistance_;
    pacingInput.deltaTime = gameplayDeltaTime;
    pacingInput.gameplayActive = !coursePreviewOwnsRail &&
        gameplayDeltaTime > 0.0f &&
        (!railShooterGameSession_.IsInitialized() ||
         railShooterGameSession_.State().gameplaySimulationEnabled);
    const EnemyEncounterPacingFrame& encounterPacingFrame =
        railEnemyEncounterPacingDirector_.Update(pacingInput);
    for (const EnemyEncounterPacingEvent& event :
         encounterPacingFrame.events) {
        OutputDebugStringA(
            ("[EnemyEncounterPacing] " +
             std::string(ToString(event.kind)) + " " +
             event.encounterId + ": " + event.message + "\n").c_str());
    }
    EnemyEncounterCameraCompositionInput compositionInput{};
    compositionInput.pacing = &encounterPacingFrame;
    compositionInput.runtime = &railShooterSpawnRuntime_;
    compositionInput.railPath = &railPath_;
    compositionInput.playerDistance = railShooterDistance_;
    compositionInput.deltaTime = gameplayDeltaTime;
    compositionInput.gameplayActive = pacingInput.gameplayActive;
    const EnemyEncounterCameraCompositionFrame& encounterCompositionFrame =
        railEnemyEncounterCameraCompositionBridge_.Update(compositionInput);

    RailVehicleBodyCollisionInput vehicleBodyCollisionInput{};
    vehicleBodyCollisionInput.vehicleDefinition =
        &railShooterVehicleMovement_.Definition();
    vehicleBodyCollisionInput.vehicleState =
        &railShooterVehicleMovement_.State();
    vehicleBodyCollisionInput.spawnRuntime = &railShooterSpawnRuntime_;
    vehicleBodyCollisionInput.course = &railShooterCourse_;
    vehicleBodyCollisionInput.railPath = &railPath_;
    vehicleBodyCollisionInput.terrainSettings =
        &runtimeState_.terrain.settings;
    vehicleBodyCollisionInput.terrainEdits =
        &railShooterCourse_.terrainEditLayer;
    vehicleBodyCollisionInput.terrainPreview =
        &runtimeState_.terrain.previewEditLayer;
    vehicleBodyCollisionInput.gameplayActive =
        mountedMovementActive && !coursePreviewOwnsRail &&
        gameplayDeltaTime > 0.0f;
    const RailVehicleBodyCollisionFrame& vehicleBodyCollisionFrame =
        railShooterVehicleBodyCollision_.Update(
            vehicleBodyCollisionInput);

    const RailCameraDirectorFrame& previousCameraSafetyFrame = railShooterCameraDirector_.LastFrame();
    CourseEnemyFireSafetyFrameInput fireSafetyInput{};
    fireSafetyInput.cameraAllowsEnemyFire = previousCameraSafetyFrame.allowEnemyFire;
    fireSafetyInput.cameraStableForAiming = previousCameraSafetyFrame.stableForAiming;
    fireSafetyInput.cameraHardTransition = previousCameraSafetyFrame.hardTransition;
    fireSafetyInput.playerDistance = railShooterDistance_;
    fireSafetyInput.playerLateralOffset = railShooterPlayerLateralOffset_;
    fireSafetyInput.playerVerticalOffset = railShooterPlayerVerticalOffset_;
    fireSafetyInput.deltaTime = gameplayDeltaTime;
    fireSafetyInput.cameraReason = previousCameraSafetyFrame.comfortReason;
    fireSafetyInput.railPath = &railPath_;
    fireSafetyInput.cameraPosition = frameState_.cameraWorldPosition;
    fireSafetyInput.hasCameraPosition = true;
    fireSafetyInput.viewProjection = &frameState_.viewProjectionMatrix;
    fireSafetyInput.course = &railShooterCourse_;
    fireSafetyInput.terrainSettings = &runtimeState_.terrain.settings;
    fireSafetyInput.terrainEdits = &railShooterCourse_.terrainEditLayer;
    fireSafetyInput.terrainPreview = &runtimeState_.terrain.previewEditLayer;
    railShooterSpawnRuntime_.Update(gameplayDeltaTime, fireSafetyInput);
    railEnemyAttackDefenseValidation_.Update(railShooterSpawnRuntime_);
    editorPatrolRuntimeWorld_.Update(gameplayDeltaTime);
    CourseCollisionFrameInput collisionInput{};
    collisionInput.deltaTime = gameplayDeltaTime;
    collisionInput.course = &railShooterCourse_;
    collisionInput.railPath = &railPath_;
    collisionInput.player.distance = combatMountFrame.valid
        ? combatMountFrame.playerDistance
        : railShooterDistance_;
    collisionInput.player.lateralOffset = railShooterPlayerLateralOffset_;
    collisionInput.player.verticalOffset = railShooterPlayerVerticalOffset_;
    collisionInput.player.radius = 1.6f;
    collisionInput.player.invulnerabilityTime =
        combatMountFrame.valid
        ? 0.0f
        : (dodgeFrame.invulnerable
            ? dodgeFrame.state.invulnerabilityRemainingSeconds
            : 0.0f);
    collisionInput.player.dodgeActive =
        combatMountFrame.valid
        ? mountedDefenseFrame.state.active
        : dodgeFrame.state.phase == RailDodgePhase::Active;
    collisionInput.player.hurtRadiusScale = combatMountFrame.valid
        ? mountedDefenseFrame.state.hitboxRadiusScale
        : 1.0f;
    collisionInput.player.hitPoints = railShooterGameSession_.IsInitialized()
        ? railShooterGameSession_.State().playerHealth
        : 100.0f;
    collisionInput.player.maximumHitPoints = railShooterGameSession_.IsInitialized()
        ? railShooterGameSession_.State().maximumPlayerHealth
        : 100.0f;
    collisionInput.externalBodyCollisionAuthority = mountedMovementActive;
    railShooterCollisionSystem_.SynchronizePlayerHitPoints(
        collisionInput.player.hitPoints,
        collisionInput.player.maximumHitPoints);
    CourseCollisionWeaponState baseWeapon{};
    baseWeapon.enabled = true;
    if (const WeaponDefinitionAsset* pulseCannon =
            railShooterCollisionSystem_.FindWeaponDefinition(RailWeaponIds::PulseCannon)) {
        baseWeapon.shotInterval = pulseCannon->definition.shotInterval;
        baseWeapon.range = pulseCannon->definition.range;
        baseWeapon.radius = pulseCannon->projectileRadius;
        baseWeapon.damage = pulseCannon->definition.baseDamage;
        baseWeapon.muzzleForwardOffset = pulseCannon->muzzleForwardOffset;
        baseWeapon.tracerForwardDistance = pulseCannon->tracerForwardDistance;
        baseWeapon.muzzleRadius = pulseCannon->muzzleRadius;
        baseWeapon.tracerRadius = pulseCannon->tracerRadius;
        const std::string presetId = pulseCannon->aimAssistPresetId.empty()
            ? std::string{"gamepad_standard"}
            : pulseCannon->aimAssistPresetId;
        const uint64_t presetRevision = railAimAssistPresetRegistry_.Stats().revision;
        if (presetId != railAimAssistAppliedPresetId_ ||
            presetRevision != railAimAssistAppliedPresetRevision_) {
            if (const RailAimAssistPreset* preset =
                    railAimAssistPresetRegistry_.Find(presetId)) {
                (void)railShooterLockOnSystem_.ConfigureAimAssist(preset->settings);
                railAimAssistAppliedPresetId_ = presetId;
                railAimAssistAppliedPresetRevision_ = presetRevision;
            } else {
                OutputDebugStringA(
                    ("[AimAssistPresets] Unknown preset '" + presetId +
                     "'; retaining last-known-good runtime settings.\n").c_str());
                // Record the unresolved revision to avoid logging every frame.
                railAimAssistAppliedPresetId_ = presetId;
                railAimAssistAppliedPresetRevision_ = presetRevision;
            }
        }
        auto aimSettings = railShooterLockOnSystem_.AimAssistSettings();
        aimSettings.maximumDistance = pulseCannon->definition.range;
        // A short-range weapon may be nearer than the preset's acquisition minimum.
        aimSettings.minimumDistance = (std::min)(aimSettings.minimumDistance,
            aimSettings.maximumDistance * 0.5f);
        (void)railShooterLockOnSystem_.ConfigureAimAssist(aimSettings);
    }
    if (const WeaponDefinitionAsset* lockOn =
            railShooterCollisionSystem_.FindWeaponDefinition(RailWeaponIds::LockOnIce)) {
        auto lockSettings = railShooterLockOnSystem_.Settings();
        lockSettings.maxLocks = static_cast<int>((std::clamp)(
            lockOn->definition.maxProjectilesPerTrigger,
            1u,
            64u));
        lockSettings.maxForwardDistance = lockOn->definition.range;
        lockSettings.releaseDamage = lockOn->definition.baseDamage;
        lockSettings.lockVfxMuzzleForwardOffset = lockOn->muzzleForwardOffset;
        lockSettings.minForwardDistance = (std::min)(lockSettings.minForwardDistance,
            lockSettings.maxForwardDistance * 0.5f);
        (void)railShooterLockOnSystem_.Configure(lockSettings);
    }
    runtimeState_.terrain.previewDistance = railShooterDistance_;
    const auto visualPresetStart = RailPerfClock::now();
    ApplyRailShooterVisualPresets(railShooterDistance_);
    gRailPerfFrame.visualPresetMs = ElapsedMs(visualPresetStart, RailPerfClock::now());
    LogRailFrameStage(railShooterFrameIndex_, railShooterDistance_, "update.afterVisualPresets");

    RailCameraDirectorFrameInput cameraInput{};
    cameraInput.course = &railShooterCourse_;
    cameraInput.railPath = &railPath_;
    cameraInput.section = railShooterCourseRuntime_.CurrentSection();
    cameraInput.distance = railShooterDistance_;
    cameraInput.deltaTime = coursePreviewAdvancing
        ? kFallbackGameplayDeltaTime : gameplayDeltaTime;
    cameraInput.railSpeed = coursePreviewSimulationSystem_.IsActive()
        ? coursePreviewAdvancing
            ? coursePreviewSimulationSystem_.Frame().currentSpeed
            : 0.0f
        : coursePreviewFrozen ? 0.0f : vehicleFrame.state.speed;
    cameraInput.lockHeld = railShooterLockOnSystem_.Reticle().lockHeld;
    cameraInput.lockPressed = railShooterLockOnSystem_.Reticle().lockPressed;
    cameraInput.lockReleased = railShooterLockOnSystem_.Reticle().lockReleased;
    cameraInput.lockAimFeelActive = railShooterLockOnSystem_.Reticle().aimFeelActive;
    cameraInput.lockTokenCount = static_cast<int>(railShooterLockOnSystem_.Tokens().size());
    cameraInput.maxLockCount = railShooterLockOnSystem_.Settings().maxLocks;
    cameraInput.reticleVelocity = railShooterLockOnSystem_.Reticle().velocity;
    cameraInput.spawnRuntime = coursePreviewActorRuntimeBridge_.Active()
        ? &coursePreviewActorRuntimeBridge_.Runtime()
        : &railShooterSpawnRuntime_;
    cameraInput.lockTokens = &railShooterLockOnSystem_.Tokens();
    cameraInput.viewportWidth = metrics.width;
    cameraInput.viewportHeight = metrics.height;
    cameraInput.nearClipDistance = runtimeState_.camera.nearZ;
    const RailVehicleRenderFrame& vehicleCameraBounds = railShooterVehicleRenderer_.Frame();
    cameraInput.vehicleFramingActive = vehicleCameraBounds.visible && !coursePreviewOwnsRail;
    if (cameraInput.vehicleFramingActive) {
        // Include the raised safety trim above the collision body. Transform
        // the envelope with the rendered pose so banking/suspension count too.
        Vector3 half = railShooterVehicleMovement_.Definition().collisionHalfExtents;
        half.y += 0.6f;
        const Matrix4x4& world = vehicleCameraBounds.worldMatrix;
        for (size_t index = 0; index < cameraInput.vehicleBoundsCorners.size(); ++index) {
            const Vector3 local{
                (index & 1) ? half.x : -half.x,
                (index & 2) ? half.y : -half.y,
                (index & 4) ? half.z : -half.z};
            cameraInput.vehicleBoundsCorners[index] = {
                local.x * world.m[0][0] + local.y * world.m[1][0] + local.z * world.m[2][0] + world.m[3][0],
                local.x * world.m[0][1] + local.y * world.m[1][1] + local.z * world.m[2][1] + world.m[3][1],
                local.x * world.m[0][2] + local.y * world.m[1][2] + local.z * world.m[2][2] + world.m[3][2]};
        }
    }
    const RailVehicleCameraMountFrame& cameraMountFrame =
        railShooterCameraMountBridge_.Frame();
    cameraInput.mountedCameraActive = cameraMountFrame.active;
    cameraInput.mountedCameraAnchor = cameraMountFrame.anchorWorldPosition;
    cameraInput.mountedCameraOffset = cameraMountFrame.cameraOffset;
    cameraInput.mountedTargetOffset = cameraMountFrame.targetOffset;
    cameraInput.mountedCameraBlend = cameraMountFrame.anchorBlend;
    cameraInput.mountedMaximumCorrection =
        cameraMountFrame.maximumAnchorCorrection;
    cameraInput.mountedRollOffsetRadians = cameraMountFrame.rollOffsetRadians;
    cameraInput.cameraInertiaActive = cameraInertiaFrame.active;
    cameraInput.cameraInertiaGameplayPositionOffset =
        cameraInertiaFrame.gameplayPositionOffset;
    cameraInput.cameraInertiaGameplayTargetOffset =
        cameraInertiaFrame.gameplayTargetOffset;
    cameraInput.cameraInertiaPresentationPositionOffset =
        cameraInertiaFrame.presentationPositionOffset;
    cameraInput.cameraInertiaPresentationTargetOffset =
        cameraInertiaFrame.presentationTargetOffset;
    cameraInput.cameraInertiaRollOffsetRadians = cameraInertiaFrame.rollOffsetRadians;
    cameraInput.cameraInertiaFovOffsetRadians = cameraInertiaFrame.fovOffsetRadians;
    cameraInput.preferredCinematicShotId = rideFrame.cameraShotId;
    cameraInput.preferredCinematicShotWeight = rideFrame.cameraShotWeight;
    if (encounterCompositionFrame.active &&
        !encounterCompositionFrame.preferredCameraShotId.empty() &&
        encounterCompositionFrame.preferredCameraShotWeight >=
            cameraInput.preferredCinematicShotWeight) {
        cameraInput.preferredCinematicShotId =
            encounterCompositionFrame.preferredCameraShotId;
        cameraInput.preferredCinematicShotWeight =
            encounterCompositionFrame.preferredCameraShotWeight;
    }
    cameraInput.authoredEncounterCompositionActive =
        encounterCompositionFrame.active;
    cameraInput.authoredEncounterHasFocusWorld =
        encounterCompositionFrame.hasFocusWorld;
    cameraInput.authoredEncounterFocusWorld =
        encounterCompositionFrame.focusWorld;
    cameraInput.authoredEncounterCompositionWeight =
        encounterCompositionFrame.blend;
    cameraInput.authoredEncounterFocusWeight =
        encounterCompositionFrame.focusWeight;
    cameraInput.authoredEncounterFovOffsetRadians =
        encounterCompositionFrame.fovOffsetRadians;
    cameraInput.authoredEncounterBackDistanceOffset =
        encounterCompositionFrame.backDistanceOffset;
    const RailCameraDirectorFrame directedCamera =
        railShooterCameraDirector_.Evaluate(cameraInput);
    if (gameplayDeltaTime > 0.0f) {
        railShooterRideTuningTelemetry_.Record(
            rideFrame,
            motionEnvelope,
            vehicleFrame.state,
            rideDynamicsFrame,
            directedCamera);
    }
    const Vector3& cameraPosition = directedCamera.position;
    // The current rig may have moved since the pre-fire check (hard cuts,
    // banking, encounter framing). Protect its actual pose before presentation.
    fireSafetyInput.cameraPosition = cameraPosition;
    railShooterSpawnRuntime_.EnforceEnemyEngagementClearance(fireSafetyInput);
    const Vector3& lookTarget = directedCamera.target;
    const Vector3& forward = directedCamera.forward;
    const Vector3& cameraUp = directedCamera.up;

    const float aspectRatio = metrics.AspectRatio();
    runtimeState_.camera.fovY = directedCamera.fovY;
    frameState_.viewMatrix = MakeLookAtMatrix(cameraPosition, lookTarget, cameraUp);
    frameState_.projMatrix = MakePerspectiveFovMatrix(
        runtimeState_.camera.fovY,
        aspectRatio,
        runtimeState_.camera.nearZ,
        runtimeState_.camera.farZ);
    frameState_.viewProjectionMatrix = Multiply(frameState_.viewMatrix, frameState_.projMatrix);
    const Matrix4x4 gameplayViewMatrix = MakeLookAtMatrix(
        directedCamera.gameplayPosition,
        directedCamera.gameplayTarget,
        directedCamera.gameplayUp);
    const Matrix4x4 gameplayViewProjectionMatrix =
        Multiply(gameplayViewMatrix, frameState_.projMatrix);
    frameState_.cameraWorldPosition = cameraPosition;
    frameState_.deltaTime = gameplayDeltaTime;

    EnemyAttackTelegraphFrameInput telegraphInput{};
    telegraphInput.spawnRuntime = &railShooterSpawnRuntime_;
    telegraphInput.railPath = &railPath_;
    telegraphInput.viewProjection = &frameState_.viewProjectionMatrix;
    telegraphInput.course = &railShooterCourse_;
    telegraphInput.terrainSettings = &runtimeState_.terrain.settings;
    telegraphInput.terrainEdits = &railShooterCourse_.terrainEditLayer;
    telegraphInput.terrainPreview = &runtimeState_.terrain.previewEditLayer;
    telegraphInput.cameraPosition = cameraPosition;
    telegraphInput.playerDistance = railShooterDistance_;
    telegraphInput.deltaTime = gameplayDeltaTime;
    telegraphInput.viewportWidth = metrics.width;
    telegraphInput.viewportHeight = metrics.height;
    telegraphInput.settings = railEnemyAttackTelegraphSettings_;
    telegraphInput.cameraAllowsAttack = directedCamera.allowEnemyFire &&
        directedCamera.stableForAiming && !directedCamera.hardTransition;
    railEnemyAttackTelegraphSystem_.Update(telegraphInput);
    EnemyAttackLaneTelegraphRenderInput laneTelegraphInput{};
    laneTelegraphInput.telegraph = &railEnemyAttackTelegraphSystem_.Frame();
    laneTelegraphInput.railPath = &railPath_;
    laneTelegraphInput.effectRuntime = &vfxEngine_.Runtime();
    laneTelegraphInput.gameplayActive = gameplayDeltaTime > 0.0f &&
        (!railShooterGameSession_.IsInitialized() ||
         railShooterGameSession_.State().gameplaySimulationEnabled);
    laneTelegraphInput.openingPresentation =
        railShooterCoursePath_ == "Resources/courses/CanyonAssaultRoute01.course" &&
        railShooterDistance_ < 180.0f;
    laneTelegraphInput.elapsedTime = railShooterGameSession_.IsInitialized()
        ? railShooterGameSession_.State().gameplayElapsedSeconds
        : railEnemyProjectilePresentationTime_;
    laneTelegraphInput.settings =
        railEnemyAttackLaneTelegraphRendererSettings_;
    railEnemyAttackLaneTelegraphRenderer_.Update(laneTelegraphInput);
    // Fire authorization follows an actually submitted production warning,
    // not merely the existence of a simulation cue.
    for (const EnemyAttackLaneTelegraphProxy& lane :
         railEnemyAttackLaneTelegraphRenderer_.Frame().lanes) {
        if (lane.attackIntentSequence != 0 &&
            railEnemyAttackLaneTelegraphRenderer_.WasSubmitted(
                lane.actorId,
                lane.attackIntentSequence)) {
            (void)railShooterSpawnRuntime_.MarkEnemyAttackTelegraphPresented(
                lane.actorId,
                lane.attackIntentSequence);
        }
    }

    bool gimmickInteractionAllowed =
        gameplayDeltaTime > 0.0f &&
        hwnd_ != nullptr &&
        GetForegroundWindow() == hwnd_;
#if defined(GE3_ENABLE_IMGUI) && GE3_ENABLE_IMGUI
    gimmickInteractionAllowed =
        gimmickInteractionAllowed &&
        (!imguiLayer_.IsVisible() ||
         (!ImGui::GetIO().WantTextInput &&
          !ImGui::IsAnyItemActive()));
#endif
    const bool gimmickInteractionDown =
        (GetAsyncKeyState('E') & 0x8000) != 0;
    editorGimmickRuntimeInteraction_.Update(
        editor::EditorGimmickRuntimeInteractionInput{
            &editorGimmickRuntimeWorld_,
            &editorGimmickRuntimeEventRouter_,
            &editorGimmickRuntimeAdapter_,
            cameraPosition,
            forward,
            "runtime.player",
            gimmickInteractionDown,
            gimmickInteractionAllowed});

    if (gameplayDeltaTime > 0.0f) {
        const Vector3 playerWorld = RailLocalPoint(
            railPath_,
            railShooterDistance_,
            collisionInput.player.lateralOffset,
            collisionInput.player.verticalOffset,
            0.0f);
        const float playerRadius =
            (std::max)(0.01f, collisionInput.player.radius);
        editor::EditorGimmickRuntimeTriggerSubject playerSubject{};
        playerSubject.entityGuid = "runtime.player";
        playerSubject.boundsMin = {
            playerWorld.x - playerRadius,
            playerWorld.y - playerRadius,
            playerWorld.z - playerRadius};
        playerSubject.boundsMax = {
            playerWorld.x + playerRadius,
            playerWorld.y + playerRadius,
            playerWorld.z + playerRadius};
        editor::EditorGimmickRuntimeTriggerInput triggerInput{};
        triggerInput.world = &editorGimmickRuntimeWorld_;
        triggerInput.eventRouter =
            &editorGimmickRuntimeEventRouter_;
        triggerInput.physics = &editorGimmickRuntimeAdapter_;
        triggerInput.subjects.push_back(
            std::move(playerSubject));
        editorGimmickRuntimeTriggers_.Update(triggerInput);
    }

    if (gameplayDeltaTime > 0.0f) {
        std::string delayedEventError;
        if (!editorGimmickRuntimeDelayedEvents_.Update(
                gameplayDeltaTime,
                editorGimmickRuntimeWorld_,
                editorGimmickRuntimeEventRouter_,
                &delayedEventError) &&
            !delayedEventError.empty()) {
            OutputDebugStringA(
                ("[RuntimeScene] Delayed Gimmick event: " +
                 delayedEventError + "\n")
                    .c_str());
        }
    }

    editorGimmickRuntimeWorld_.Update(gameplayDeltaTime);
    std::string gimmickAdapterError;
    if (!editorGimmickRuntimeAdapter_.Sync(
            editorGimmickRuntimeWorld_,
            &gimmickAdapterError)) {
        OutputDebugStringA(
            ("[RuntimeScene] Gimmick Presentation/Physics sync "
             "failed: " +
             gimmickAdapterError + "\n")
                .c_str());
        editorGimmickRuntimeAdapter_.Clear();
        editorGimmickRuntimeInteraction_.Reset();
        editorGimmickRuntimeTriggers_.Reset();
        editorGimmickRuntimeDelayedEvents_.Reset();
        editorGimmickRuntimeEventRouter_.Reset();
    }

    RailLockOnFrameInput lockOnInput{};
    lockOnInput.hwnd = hwnd_;
    lockOnInput.deltaTime = gameplayDeltaTime;
    lockOnInput.playerDistance = collisionInput.player.distance;
    lockOnInput.viewportWidth = metrics.width;
    lockOnInput.viewportHeight = metrics.height;
    const editor::EditorViewportRenderTargetState& editorViewportTarget =
        imguiLayer_.EditorViewportRenderTargetState();
    if (editorViewportTarget.enabled) {
        POINT cursor{};
        POINT viewportCursor{};
        uint32_t viewportCursorWidth = 0;
        uint32_t viewportCursorHeight = 0;
        lockOnInput.hasCursorPosition = true;
        lockOnInput.cursorPosition = railShooterLockOnSystem_.Reticle().currentScreenPosition;
        if (GetCursorPos(&cursor) &&
            ScreenToClient(hwnd_, &cursor) &&
            ResolveEditorViewportClientPoint(
                cursor,
                viewportCursor,
                viewportCursorWidth,
                viewportCursorHeight)) {
            lockOnInput.cursorPosition = {
                static_cast<float>(viewportCursor.x),
                static_cast<float>(viewportCursor.y)};
        }
    }
    lockOnInput.gameplayViewProjection = &gameplayViewProjectionMatrix;
    lockOnInput.railPath = &railPath_;
    lockOnInput.spawnRuntime = &railShooterSpawnRuntime_;
    lockOnInput.course = &railShooterCourse_;
    lockOnInput.terrainSettings = &runtimeState_.terrain.settings;
    lockOnInput.terrainEdits = &railShooterCourse_.terrainEditLayer;
    lockOnInput.terrainPreview = &runtimeState_.terrain.previewEditLayer;
    lockOnInput.gameplayCameraPosition = directedCamera.gameplayPosition;
    lockOnInput.aimRayMaxDistance = (std::max)(
        baseWeapon.range,
        railShooterLockOnSystem_.Settings().maxForwardDistance);
    DispatchRailEnemyAttackFeedback(
        railAimGamepad,
        gameplayDeltaTime,
        gameplayDeltaTime > 0.0f &&
            (hwnd_ == nullptr || GetForegroundWindow() == hwnd_));
    lockOnInput.gamepadConnected = railAimGamepad.connected;
    lockOnInput.gamepadAim = {
        railAimGamepad.rightStick.x,
        railAimGamepad.rightStick.y};
    // Flight owns WASD/arrows/left stick and Shift. Aim owns mouse/right
    // stick, while lock-on owns RMB. Never dispatch one physical input to two
    // gameplay actions in the same context.
    lockOnInput.keyboardDirectionalAimEnabled = false;
    lockOnInput.shiftLockEnabled = false;
    if (!titleEntryBlocked) railShooterLockOnSystem_.Update(lockOnInput);
    if (railShooterLockOnSystem_.DebugFrame().acceptedThisFrame > 0) {
        railShooterCameraDirector_.AddFeedbackImpulse(0.075f, -0.0012f, 0.0007f);
    }
    const Vector3 railLockMuzzle = combatMountFrame.valid
        ? Add(
            combatMountFrame.weaponWorldPosition,
            Scale(
                combatMountFrame.forward,
                railShooterLockOnSystem_.Settings().lockVfxMuzzleForwardOffset))
        : RailLocalPoint(
            railPath_,
            railShooterDistance_,
            collisionInput.player.lateralOffset,
            collisionInput.player.verticalOffset,
            railShooterLockOnSystem_.Settings().lockVfxMuzzleForwardOffset);
    const int lockReleaseHits = ProcessRailLockOnRelease(railLockMuzzle, gameplayDeltaTime);
    const uint32_t lockReleaseTokenCount =
        static_cast<uint32_t>(railShooterLockOnSystem_.LastRelease().tokens.size());
    railInputRouteDebug_.releaseFireTriggered = lockReleaseTokenCount > 0;
    railInputRouteDebug_.releaseTokenCount = lockReleaseTokenCount;
    railInputRouteDebug_.releaseHitCount = lockReleaseHits;
    if (lockReleaseTokenCount > 0) {
        railShooterCombatFeelSystem_.ApplyLockOnRelease(
            lockReleaseTokenCount,
            static_cast<uint32_t>((std::max)(lockReleaseHits, 0)),
            static_cast<uint32_t>((std::max)(railShooterLockOnSystem_.Settings().maxLocks, 0)));
    }
    if (lockReleaseHits > 0) {
        const WeaponFeedbackEvent& feedback =
            railShooterCollisionSystem_.WeaponFeedback().LastAcceptedEvent();
        railShooterCameraDirector_.AddFeedbackImpulse(
            feedback.cameraShake,
            -0.003f * feedback.intensity,
            0.0015f * feedback.intensity);
    }

    const bool lockModeActive = railShooterLockOnSystem_.Reticle().lockHeld;
    const bool leftMouseDown = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
    const bool wasLeftMouseDown = previousLeftMouseDown_;
    previousLeftMouseDown_ = leftMouseDown;
    bool normalShotBlockedByUi = false;
#if defined(GE3_ENABLE_IMGUI) && GE3_ENABLE_IMGUI
    normalShotBlockedByUi =
        imguiLayer_.IsVisible() && ImGui::GetIO().WantCaptureMouse;
#endif
    const bool normalShotPressed = leftMouseDown && !wasLeftMouseDown;
    const bool normalShotReleased = !leftMouseDown && wasLeftMouseDown;
    const bool normalShotTriggerHeld = !titleEntryBlocked && leftMouseDown && !normalShotBlockedByUi && !lockModeActive;
    const bool normalShotTriggerPressed = !titleEntryBlocked && normalShotPressed && !normalShotBlockedByUi && !lockModeActive;

    railInputRouteDebug_.lockHeld = lockModeActive;
    railInputRouteDebug_.lockPressed = railShooterLockOnSystem_.Reticle().lockPressed;
    railInputRouteDebug_.lockReleased = railShooterLockOnSystem_.Reticle().lockReleased;
    baseWeapon.enabled = !lockModeActive;
    baseWeapon.triggerHeld = normalShotTriggerHeld;
    baseWeapon.triggerPressed = normalShotTriggerPressed;
    baseWeapon.triggerReleased = normalShotReleased;
    railInputRouteDebug_.normalShotEnabled = baseWeapon.enabled;
    railInputRouteDebug_.normalShotHeld = normalShotTriggerHeld;
    railInputRouteDebug_.normalShotPressed = normalShotTriggerPressed;
    railInputRouteDebug_.normalShotBlockedByUi = normalShotBlockedByUi;
    railInputRouteDebug_.leftMouseDown = leftMouseDown;

    const RailReticleState& normalReticle = railShooterLockOnSystem_.Reticle();
    const RailAimState& worldAim = railShooterLockOnSystem_.Aim();
    const float reticleNormX = (std::clamp)(
        worldAim.valid ? worldAim.normalizedPosition.x : 0.0f,
        -1.15f,
        1.15f);
    const float reticleNormY = (std::clamp)(
        worldAim.valid ? worldAim.normalizedPosition.y : 0.0f,
        -1.15f,
        1.15f);
    const float aimForwardDistance = 38.0f;
    const float tanHalfY = std::tan(runtimeState_.camera.fovY * 0.5f);
    const float tanHalfX = tanHalfY * aspectRatio;
    const float normalAimLateral = (std::clamp)(
        reticleNormX * tanHalfX * aimForwardDistance * 0.74f,
        -15.0f,
        15.0f);
    const float normalAimVertical = (std::clamp)(
        collisionInput.player.verticalOffset + reticleNormY * tanHalfY * aimForwardDistance * 0.74f,
        -2.0f,
        17.0f);
    railInputRouteDebug_.normalAimLateral = normalAimLateral;
    railInputRouteDebug_.normalAimVertical = normalAimVertical;

    PlayerCombatFeelFrameInput combatFeelInput{};
    combatFeelInput.deltaTime = gameplayDeltaTime;
    combatFeelInput.playerDistance = collisionInput.player.distance;
    combatFeelInput.playerLateralOffset = collisionInput.player.lateralOffset;
    combatFeelInput.playerVerticalOffset = collisionInput.player.verticalOffset;
    combatFeelInput.baseWeapon = baseWeapon;
    combatFeelInput.spawnRuntime = &railShooterSpawnRuntime_;
    combatFeelInput.allowAimAssist = !lockModeActive;
    combatFeelInput.hasReticleAim = true;
    combatFeelInput.reticleAimLateralOffset = normalAimLateral;
    combatFeelInput.reticleAimVerticalOffset = normalAimVertical;
    railInputRouteDebug_.aimAssistEnabled = combatFeelInput.allowAimAssist;
    collisionInput.weapon = railShooterCombatFeelSystem_.BuildWeaponState(combatFeelInput);
    collisionInput.worldAim = &worldAim;
    const auto collisionStart = RailPerfClock::now();
    CourseCollisionFrameStats collisionStats =
        railShooterCollisionSystem_.Update(railShooterSpawnRuntime_, collisionInput);
    RailVehicleDamageCoordinatorInput vehicleDamageInput{};
    vehicleDamageInput.deltaTime = gameplayDeltaTime;
    vehicleDamageInput.bodyCollision = &vehicleBodyCollisionFrame;
    vehicleDamageInput.collisionSystem = &railShooterCollisionSystem_;
    vehicleDamageInput.vehicleMovement = &railShooterVehicleMovement_;
    vehicleDamageInput.gameplayActive =
        mountedMovementActive && !coursePreviewOwnsRail &&
        gameplayDeltaTime > 0.0f;
    railShooterVehicleDamageCoordinator_.Update(vehicleDamageInput);
    collisionStats = railShooterCollisionSystem_.LastFrameStats();
    RailVehicleCollisionFeedbackInput collisionFeedbackInput{};
    collisionFeedbackInput.damageResults =
        railShooterCollisionSystem_.PlayerDamageResults();
    collisionFeedbackInput.vehicleState = &railShooterVehicleMovement_.State();
    collisionFeedbackInput.deltaTime = gameplayDeltaTime;
    collisionFeedbackInput.listenerRight = {frameState_.viewMatrix.m[0][0],frameState_.viewMatrix.m[1][0],frameState_.viewMatrix.m[2][0]};
    collisionFeedbackInput.listenerForward = {frameState_.viewMatrix.m[0][2],frameState_.viewMatrix.m[1][2],frameState_.viewMatrix.m[2][2]};
    collisionFeedbackInput.gameplayActive = vehicleDamageInput.gameplayActive;
    collisionFeedbackInput.settings =
        railShooterVehicleCollisionFeedbackSettings_;
    collisionFeedbackInput.settings.cameraShake = 0.0f;
    railShooterVehicleCollisionFeedback_.Update(collisionFeedbackInput);
    vehicleActorInput.collisionFeedback = &railShooterVehicleCollisionFeedback_.Frame();
    railShooterVehicleActor_.Update(vehicleActorInput);
    vehicleRenderInput.cameraWorldPosition = frameState_.cameraWorldPosition;
    railShooterVehicleRenderer_.Update(vehicleRenderInput);
    wheelPresentationInput.collisionFeedback = &railShooterVehicleCollisionFeedback_.Frame();
    railShooterWheelContactPresentation_.Update(wheelPresentationInput);
    const RailVehicleCollisionFeedbackFrame& collisionFeedback =
        railShooterVehicleCollisionFeedback_.Frame();
    if (collisionFeedback.slowdown.valid) {
        (void)railShooterVehicleMovement_.ApplyImpactSlowdown(
            collisionFeedback.slowdown.speedMultiplier,
            collisionFeedback.slowdown.durationSeconds,
            collisionFeedback.slowdown.resultSequence);
    }
    DispatchRailVehicleCollisionFeedback();
    if (!mountedMovementActive && (collisionFeedback.cameraShake > 0.0f ||
        std::abs(collisionFeedback.cameraPitchImpulse) > 0.00001f ||
        std::abs(collisionFeedback.cameraYawImpulse) > 0.00001f)) {
        railShooterCameraDirector_.AddFeedbackImpulse(
            collisionFeedback.cameraShake,
            collisionFeedback.cameraPitchImpulse,
            collisionFeedback.cameraYawImpulse);
    }
    PlayerDamagePresentationInput playerDamagePresentationInput{};
    playerDamagePresentationInput.results =
        railShooterCollisionSystem_.PlayerDamageResults();
    playerDamagePresentationInput.deltaTime = gameplayDeltaTime;
    playerDamagePresentationInput.suppressCameraFeedback = mountedMovementActive;
    playerDamagePresentationInput.vehicleOwnsImpactPresentation = mountedMovementActive &&
        railShooterVehicleCollisionFeedbackSettings_.enabled;
    playerDamagePresentationInput.gameplayActive =
        gameplayDeltaTime > 0.0f &&
        (!railShooterGameSession_.IsInitialized() ||
         railShooterGameSession_.State().gameplaySimulationEnabled);
    railPlayerDamagePresentationBridge_.Update(
        playerDamagePresentationInput);
    DispatchPlayerDamagePresentation();
    GrazeScoreFrameInput grazeInput{};
    grazeInput.deltaTime = gameplayDeltaTime;
    grazeInput.nearMissResults =
        railShooterCollisionSystem_.PlayerNearMissResults();
    grazeInput.gameplayActive =
        playerDamagePresentationInput.gameplayActive && !coursePreviewOwnsRail;
    railGrazeScoreSystem_.Update(grazeInput);
    EnemyAttackDefenseResolutionInput defenseResolutionInput{};
    defenseResolutionInput.runtime = &railShooterSpawnRuntime_;
    defenseResolutionInput.shootDown =
        &railShooterCollisionSystem_.ProjectileShootDown().Frame();
    defenseResolutionInput.interrupt =
        &railShooterCollisionSystem_.AttackInterrupt().Frame();
    defenseResolutionInput.nearMissResults =
        railShooterCollisionSystem_.PlayerNearMissResults();
    defenseResolutionInput.damageResults =
        railShooterCollisionSystem_.PlayerDamageResults();
    defenseResolutionInput.mountedDefense =
        &railShooterMountedDefense_.Frame();
    defenseResolutionInput.playerDistance = railShooterDistance_;
    defenseResolutionInput.deltaTime = gameplayDeltaTime;
    defenseResolutionInput.gameplayActive = grazeInput.gameplayActive;
    railEnemyAttackDefenseResolutionSystem_.Update(
        defenseResolutionInput,
        railEnemyAttackDefenseResolutionSettings_);
    EnemyAttackDefenseOutcomeFeedbackInput defenseFeedbackInput{};
    std::array<EnemyAttackDefenseResult, 1> defenseProofResults{};
    bool emitDefenseProof = false;
    if (railDefenseUiProofEnabled_) {
        // The proof playlist must keep advancing after the short encounter
        // reaches Mission Complete, where gameplayDeltaTime intentionally stops.
        railDefenseUiProofElapsedSeconds_ += (std::max)(0.0f, sessionDeltaTime);
        // Leave a short capture lead-in so the proof can be recorded without
        // window-management frames obscuring the three result treatments.
        static constexpr std::array<float, 3> kProofTimes{
            1.40f, 4.10f, 6.80f};
        const bool individualProof = railDefenseUiProofVariant_ !=
            CombatLoopDefenseUiProofVariant::Sequence;
        const uint32_t proofCount = individualProof ? 1u :
            static_cast<uint32_t>(kProofTimes.size());
        const float proofTime = individualProof
            ? 1.40f
            : kProofTimes[(std::min)(
                  railDefenseUiProofStage_, proofCount - 1u)];
        if (railDefenseUiProofStage_ < proofCount &&
            railDefenseUiProofElapsedSeconds_ >=
                proofTime) {
            const uint32_t proofIndex = individualProof
                ? static_cast<uint32_t>(railDefenseUiProofVariant_) -
                      static_cast<uint32_t>(
                          CombatLoopDefenseUiProofVariant::Interrupt)
                : railDefenseUiProofStage_;
            EnemyAttackDefenseResult& proof = defenseProofResults.front();
            proof.sequence = 100000u + proofIndex;
            proof.outcome = EnemyAttackDefenseOutcome::Success;
            proof.grade = proofIndex == 1
                ? EnemyAttackDefenseGrade::Perfect
                : EnemyAttackDefenseGrade::Good;
            proof.scoreAwarded = proofIndex == 0
                ? 216u : (proofIndex == 1 ? 210u : 160u);
            proof.chainAfter = proofIndex + 1;
            proof.accepted = true;
            proof.method = proofIndex == 0
                ? EnemyAttackDefenseMethod::Interrupt
                : (proofIndex == 1
                    ? EnemyAttackDefenseMethod::ShootDown
                    : EnemyAttackDefenseMethod::LeanLeft);
            emitDefenseProof = true;
            ++railDefenseUiProofStage_;
        }
    }
    defenseFeedbackInput.results = emitDefenseProof
        ? std::span<const EnemyAttackDefenseResult>(defenseProofResults)
        : std::span<const EnemyAttackDefenseResult>(
            railEnemyAttackDefenseResolutionSystem_.Frame().results);
    defenseFeedbackInput.deltaTime = gameplayDeltaTime;
    defenseFeedbackInput.gameplayActive = grazeInput.gameplayActive;
    defenseFeedbackInput.settings =
        railEnemyAttackDefenseOutcomeFeedbackSettings_;
    railEnemyAttackDefenseOutcomeFeedbackBridge_.Update(
        defenseFeedbackInput);
    DispatchEnemyAttackDefenseOutcomeFeedback();
    const Vector3 projectileCameraRight = NormalizeOr(
        Cross(cameraUp, forward),
        {1.0f, 0.0f, 0.0f});
    DispatchEnemyProjectilePresentation(
        gameplayDeltaTime,
        playerDamagePresentationInput.gameplayActive,
        cameraPosition,
        projectileCameraRight,
        cameraUp);
    ThreatResponseFrameInput threatInput{};
    threatInput.deltaTime = gameplayDeltaTime;
    threatInput.projectileFrame =
        &railEnemyProjectilePresentationBridge_.Frame();
    threatInput.grazeResults = railGrazeScoreSystem_.ResultsThisFrame();
    threatInput.damageResults =
        railShooterCollisionSystem_.PlayerDamageResults();
    threatInput.currentGrazeChain = railGrazeScoreSystem_.State().chain;
    threatInput.gameplayActive = grazeInput.gameplayActive;
    railThreatResponseDirector_.Update(
        threatInput,
        railThreatResponseSettings_);
    DispatchThreatResponse();
    const std::vector<EnemyCombatEvent> enemyCombatEvents =
        railShooterSpawnRuntime_.EnemyCombat().ConsumeEvents();
    for (const EnemyCombatEvent& event : enemyCombatEvents) {
        if (event.kind == EnemyCombatEventKind::Defeated &&
            !event.placementGuid.empty() &&
            railShooterGameplayWaveBridge_.IsBound()) {
            railShooterGameplayWaveBridge_.NotifyEnemyDefeated(
                event.placementGuid);
        }
        if (event.kind == EnemyCombatEventKind::Defeated ||
            event.kind == EnemyCombatEventKind::Retired) {
            OutputDebugStringA(
                ("[EnemyCombat] " + std::string(ToString(event.kind)) +
                 " actor=" + std::to_string(event.actorId) +
                 " asset=" + event.actorAssetId +
                 " placement=" + event.placementGuid + "\n").c_str());
        }
    }
    EncounterPerformanceScoreInput encounterScoreInput{};
    encounterScoreInput.pacing = &encounterPacingFrame;
    encounterScoreInput.combatEvents = enemyCombatEvents;
    encounterScoreInput.defenseResults =
        railEnemyAttackDefenseResolutionSystem_.Frame().results;
    encounterScoreInput.grazeResults =
        railGrazeScoreSystem_.ResultsThisFrame();
    encounterScoreInput.damageResults =
        railShooterCollisionSystem_.PlayerDamageResults();
    encounterScoreInput.gameplayActive = grazeInput.gameplayActive;
    railEncounterPerformanceScoreSystem_.Update(
        encounterScoreInput,
        railEncounterPerformanceScoreSettings_);
    DispatchEnemyCombatPresentation(
        enemyCombatEvents,
        gameplayDeltaTime,
        gameplayDeltaTime > 0.0f &&
            (hwnd_ == nullptr || GetForegroundWindow() == hwnd_));
    EnemyEncounterReadabilityInput encounterReadabilityInput{};
    encounterReadabilityInput.runtime = &railShooterSpawnRuntime_;
    encounterReadabilityInput.railPath = &railPath_;
    encounterReadabilityInput.viewProjection =
        &frameState_.viewProjectionMatrix;
    encounterReadabilityInput.telegraph =
        &railEnemyAttackTelegraphSystem_.Frame();
    encounterReadabilityInput.projectiles =
        &railEnemyProjectilePresentationBridge_.Frame();
    encounterReadabilityInput.defense =
        &railEnemyAttackDefensePresentationBridge_.Frame();
    encounterReadabilityInput.damageResults =
        railShooterCollisionSystem_.PlayerDamageResults();
    encounterReadabilityInput.activeWaves =
        railShooterGameplayWaveBridge_.IsBound()
        ? railShooterGameplayWaveBridge_.Stats().activeWaves
        : 0;
    encounterReadabilityInput.viewportWidth = metrics.width;
    encounterReadabilityInput.viewportHeight = metrics.height;
    encounterReadabilityInput.playerDistance = railShooterDistance_;
    encounterReadabilityInput.deltaTime = gameplayDeltaTime;
    encounterReadabilityInput.gameplayActive = grazeInput.gameplayActive;
    encounterReadabilityInput.settings =
        railEnemyEncounterReadabilitySettings_;
    railEnemyEncounterReadabilityDirector_.Update(
        encounterReadabilityInput);
    railInputRouteDebug_.normalShotsFired = collisionStats.playerShotsFired;
    railInputRouteDebug_.normalShotHits = collisionStats.playerShotWorldHits;
    if (collisionStats.playerShotsFired > 0 &&
        railShooterCollisionSystem_.LastShotVisible() &&
        railPath_.Length() > 0.0f) {
        const CourseCollisionWeaponState& visualWeapon = railShooterCollisionSystem_.Weapon();
        const Vector3 muzzleWorld = combatMountFrame.valid
            ? Add(
                combatMountFrame.weaponWorldPosition,
                Scale(combatMountFrame.forward, visualWeapon.muzzleForwardOffset))
            : RailLocalPoint(
                railPath_,
                railShooterDistance_,
                collisionInput.player.lateralOffset,
                collisionInput.player.verticalOffset,
                visualWeapon.muzzleForwardOffset);
        const Vector3 hitWorld = railShooterCollisionSystem_.LastShotHasWorldPoint()
            ? railShooterCollisionSystem_.LastShotWorldPoint()
            : RailLocalPoint(
                railPath_,
                railShooterCollisionSystem_.LastShotDistance(),
                railShooterCollisionSystem_.LastShotLateralOffset(),
                railShooterCollisionSystem_.LastShotVerticalOffset(),
                0.0f);
        RailOverlayProjectedPoint muzzleScreen =
            ProjectRailOverlayPoint(muzzleWorld, frameState_.viewProjectionMatrix, metrics.width, metrics.height);
        RailOverlayProjectedPoint hitScreen =
            ProjectRailOverlayPoint(hitWorld, frameState_.viewProjectionMatrix, metrics.width, metrics.height);
        if (!muzzleScreen.inDepth) {
            muzzleScreen.screen = {
                static_cast<float>(metrics.width) * 0.5f,
                static_cast<float>(metrics.height) * 0.78f};
            muzzleScreen.inDepth = true;
        }
        if (hitScreen.inDepth) {
            RailNormalShotLine line{};
            line.start = muzzleScreen.screen;
            line.end = hitScreen.screen;
            line.lifetime = 0.070f;
            line.thickness = railInputRouteDebug_.normalShotHits > 0 ? 2.4f : 1.5f;
            line.hit = railInputRouteDebug_.normalShotHits > 0;
            railNormalShotLines_.push_back(line);
            constexpr size_t kMaxNormalShotLines = 18;
            if (railNormalShotLines_.size() > kMaxNormalShotLines) {
                railNormalShotLines_.erase(
                    railNormalShotLines_.begin(),
                    railNormalShotLines_.begin() + static_cast<std::ptrdiff_t>(
                        railNormalShotLines_.size() - kMaxNormalShotLines));
            }
        }
    }
    railShooterCombatFeelSystem_.ApplyCollisionStats(collisionStats);
    railShooterCombatFeelSystem_.ApplyPlayerDamageResults(
        railShooterCollisionSystem_.PlayerDamageResults());
    railShooterCombatFeelSystem_.ApplyGrazeScoreResults(
        railGrazeScoreSystem_.ResultsThisFrame());
    railShooterCombatFeelSystem_.Update(gameplayDeltaTime);
    if (!coursePreviewOwnsRail && railShooterGameSession_.IsInitialized()) {
        uint64_t grazeScoreThisFrame = 0;
        for (const GrazeScoreResult& result :
             railGrazeScoreSystem_.ResultsThisFrame()) {
            if (!result.accepted || result.scoreAwarded == 0) continue;
            grazeScoreThisFrame += result.scoreAwarded;
        }
        const EnemyAttackDefenseResolutionFrame& defenseResults =
            railEnemyAttackDefenseResolutionSystem_.Frame();
        railShooterGameSession_.SetCombo(
            railEncounterPerformanceScoreSystem_.State().chain,
            !railEncounterPerformanceScoreSystem_.ResultsThisFrame().empty()
                ? "encounter_performance_chain"
                : (defenseResults.successes > 0
                    ? "enemy_attack_defense_chain" : "graze_chain"));
        if (grazeScoreThisFrame > 0) {
            railShooterGameSession_.AddScore(
                grazeScoreThisFrame,
                "near_miss_graze");
        }
        if (defenseResults.scoreAwarded > 0) {
            railShooterGameSession_.AddScore(
                defenseResults.scoreAwarded,
                "enemy_attack_defense");
        }
        for (const EncounterPerformanceScoreResult& result :
             railEncounterPerformanceScoreSystem_.ResultsThisFrame()) {
            if (!result.accepted || result.scoreAwarded == 0) continue;
            const char* source = "encounter_enemy_defeat";
            if (result.kind == EncounterPerformanceScoreKind::CleanClear) {
                source = result.fullSweep
                    ? "encounter_clean_clear_full_sweep"
                    : "encounter_clean_clear";
            } else if (result.kind ==
                       EncounterPerformanceScoreKind::EncounterClear) {
                source = result.fullSweep
                    ? "encounter_clear_full_sweep"
                    : "encounter_clear";
            }
            railShooterGameSession_.AddScore(result.scoreAwarded, source);
        }
        for (const PlayerDamageResult& result :
             railShooterCollisionSystem_.PlayerDamageResults()) {
            if (!result.accepted || result.appliedDamage <= 0.0f) continue;
            const std::string source = result.request.sourceId.empty()
                ? std::string(ToString(result.request.kind))
                : result.request.sourceId;
            (void)railShooterGameSession_.ApplyPlayerDamage(
                result.appliedDamage,
                source);
        }
        GameSessionFrameInput sessionInput{};
        sessionInput.deltaTime = sessionDeltaTime;
        sessionInput.courseDistance = railShooterDistance_;
        sessionInput.combatClearConfirmed =
            railEnemyEncounterReadabilityDirector_.Frame()
                .truth.safeToResolveSession;
        if (railShooterGameplayWaveBridge_.IsBound()) {
            sessionInput.completedMandatoryWaves =
                railShooterGameplayWaveBridge_.Stats().completedWaves;
            sessionInput.totalMandatoryWaves = static_cast<uint32_t>(std::count_if(
                railShooterRuntimeProgram_.waves.begin(),
                railShooterRuntimeProgram_.waves.end(),
                [](const CourseRuntimeWaveNode& wave) { return wave.enabled; }));
        }
        railShooterGameSession_.Update(sessionInput);
        bool sessionKeyboardAllowed =
            hwnd_ == nullptr || GetForegroundWindow() == hwnd_;
#if defined(GE3_ENABLE_IMGUI) && GE3_ENABLE_IMGUI
        if (sessionKeyboardAllowed && imguiLayer_.IsVisible() &&
            (ImGui::GetIO().WantTextInput || ImGui::IsAnyItemActive())) {
            sessionKeyboardAllowed = false;
        }
#endif
        const bool pauseRequested =
            !titleEntryBlocked && sessionKeyboardAllowed && WasKeyPressed('P');
        const bool retryRequested =
            (sessionKeyboardAllowed &&
             (WasKeyPressed('R') || WasKeyPressed(VK_RETURN))) ||
            railAimGamepad.toggleSkeletonPressed;
        if (pauseRequested) {
            if (railShooterGameSession_.State().phase == GameSessionPhase::Paused) {
                (void)railShooterGameSession_.Resume();
            } else {
                (void)railShooterGameSession_.Pause();
            }
        }
        if (retryRequested && railShooterRetryCoordinator_.CanRetry()) {
            std::string retryError;
            const GameSessionRetryResult retry =
                railShooterRetryCoordinator_.Retry(&retryError);
            if (retry.succeeded) {
                railShooterDistance_ = retry.restoredDistance;
                runtimeState_.terrain.previewDistance = retry.restoredDistance;
                railShooterPlayerVehicleMount_.Reset();
                railShooterVehicleBodyCollision_.Reset();
                railShooterVehicleCollisionFeedback_.Reset();
                railShooterCombatMountBridge_.Reset();
                railShooterCameraMountBridge_.Reset();
                railShooterOccupantClearance_.Reset();
                railShooterEvasionConstraintResolver_.Reset();
                railShooterMountedEvasionPresentation_.Reset();
                railShooterOccupantActor_.Reset();
                railShooterEvasionFeedback_.Reset();
                railShooterPlayerLateralOffset_ =
                    railShooterVehicleMovement_.Definition().mounts.player.x;
                railShooterPlayerVerticalOffset_ =
                    railShooterVehicleMovement_.Definition().bodyVerticalOffset +
                    railShooterVehicleMovement_.Definition().mounts.player.y;
                railShooterVehiclePresentation_.Reset();
                railShooterVehicleRideDynamics_.Reset();
                railShooterTrackContactPose_.Reset();
                railShooterCameraInertia_.Reset();
                railShooterVehicleActor_.Reset();
                railShooterVehicleRenderer_.Reset();
                railShooterVehicleAudioBridge_.Reset();
                railEnemyCombatPresentationBridge_.Reset();
                ResetEnemyProjectilePresentation(false);
                railPlayerDamagePresentationBridge_.Reset();
                railEnemyAttackTelegraphSystem_.Reset();
                StopRailEnemyAttackFeedback();
                railShooterCombatFeelSystem_.Reset();
                railShooterEncounterDirector_.Reset();
                railEnemyEncounterCameraCompositionBridge_.Reset();
                railShooterCameraDirector_.Reset();
                railShooterLockOnSystem_.Reset();
                railShooterSpeedDirector_.Reset();
                railShooterRideDirector_.Reset();
                railShooterTrackFeedbackDirector_.Reset();
                railShooterRideMotionEnvelope_.Reset();
                railNormalShotLines_.clear();
                OutputDebugStringA(("[GameSessionRetry] " + retry.message + "\n").c_str());
            } else {
                OutputDebugStringA(("[GameSessionRetry] " + retryError + "\n").c_str());
            }
        }
        GameSessionPresentationInput presentationInput{};
        presentationInput.definition = &railShooterGameSession_.Definition();
        presentationInput.state = &railShooterGameSession_.State();
        presentationInput.eventHistory = &railShooterGameSession_.EventHistory();
        presentationInput.deltaTime = coursePreviewFrozen
            ? 0.0f
            : realGameplayDeltaTime;
        presentationInput.settings = railShooterSessionPresentationSettings_;
        railShooterSessionPresentation_.Update(presentationInput);
        DispatchGameSessionPresentation(railAimGamepad);
        for (const GameSessionEvent& event : railShooterGameSession_.EventsThisFrame()) {
            if (event.type == GameSessionEventType::VictoryConfirmed ||
                event.type == GameSessionEventType::DefeatConfirmed ||
                event.type == GameSessionEventType::ResultEntered) {
                std::ostringstream line;
                line << "[GameSession] " << ToString(event.type)
                     << " phase=" << ToString(event.phaseAfter)
                     << " reason=" << ToString(event.reason)
                     << " distance=" << event.courseDistance << "\n";
                OutputDebugStringA(line.str().c_str());
            }
        }
    }
    RailShooterHudRuntimeInput hudRuntimeInput{};
    hudRuntimeInput.gameplayVisible =
        railShooterInitialized_ && !coursePreviewOwnsRail;
    if (railShooterGameSession_.IsInitialized()) {
        hudRuntimeInput.session = &railShooterGameSession_.State();
    }
    if (railShooterVehicleMovement_.IsInitialized()) {
        hudRuntimeInput.vehicleDefinition =
            &railShooterVehicleMovement_.Definition();
        hudRuntimeInput.vehicle = &railShooterVehicleMovement_.State();
    }
    const WeaponFireSystem& hudWeaponFire =
        railShooterCollisionSystem_.WeaponFire();
    hudRuntimeInput.primaryWeaponDefinition =
        hudWeaponFire.FindDefinition(RailWeaponIds::PulseCannon);
    hudRuntimeInput.primaryWeapon =
        hudWeaponFire.FindRuntimeState(RailWeaponIds::PulseCannon);
    hudRuntimeInput.lockWeaponDefinition =
        hudWeaponFire.FindDefinition(RailWeaponIds::LockOnIce);
    hudRuntimeInput.lockWeapon =
        hudWeaponFire.FindRuntimeState(RailWeaponIds::LockOnIce);
    if (railShooterGameplayWaveBridge_.IsBound()) {
        hudRuntimeInput.waves = &railShooterGameplayWaveBridge_.Stats();
    }
    hudRuntimeInput.graze = &railGrazeScoreSystem_.State();
    hudRuntimeInput.spawnRuntime = &railShooterSpawnRuntime_;
    hudRuntimeInput.threat = &railThreatResponseDirector_.Frame();
    hudRuntimeInput.encounterReadability =
        &railEnemyEncounterReadabilityDirector_.Frame();
    hudRuntimeInput.lockCount = static_cast<uint32_t>(
        railShooterLockOnSystem_.DebugFrame().tokens.size());
    hudRuntimeInput.maximumLocks = static_cast<uint32_t>((std::max)(
        0, railShooterLockOnSystem_.Settings().maxLocks));
    railShooterHudRuntimeModel_.Update(hudRuntimeInput);

    RailShooterHudPresentationInput hudPresentationInput{};
    hudPresentationInput.definition = &railShooterHudDefinition_;
    hudPresentationInput.runtime = &railShooterHudRuntimeModel_.Frame();
    hudPresentationInput.playerDamage = &railPlayerDamagePresentationBridge_.Frame();
    hudPresentationInput.vehicleDamage = &railShooterVehicleCollisionFeedback_.Frame();
    hudPresentationInput.sessionPresentation =
        &railShooterSessionPresentation_.Frame();
    hudPresentationInput.deltaTime = coursePreviewFrozen
        ? 0.0f
        : realGameplayDeltaTime;
    railShooterHudPresentation_.Update(hudPresentationInput);
    const bool vehicleControlPending =
        railVehicleControlAppliedPresetId_ != railVehicleControlPresetId_ ||
        railVehicleControlAppliedRevision_ !=
            railVehicleControlPresetRegistry_.Stats().revision;
    if (vehicleControlPending && railShooterInitialized_ &&
        railShooterMountedEvasion_.CanEvade()) {
        std::string applyError;
        if (!ApplyRailVehicleControlPreset(true, &applyError)) {
            OutputDebugStringA(
                ("[RailVehicleControls] Hot apply failed; active controls retained: " +
                 applyError + "\n").c_str());
        } else {
            OutputDebugStringA(
                ("[RailVehicleControls] Hot applied " +
                 railVehicleControlPresetId_ + " revision=" +
                 std::to_string(railVehicleControlAppliedRevision_) +
                 "\n").c_str());
        }
    }
    gRailPerfFrame.collisionMs = ElapsedMs(collisionStart, RailPerfClock::now());
    LogRailFrameStage(railShooterFrameIndex_, railShooterDistance_, "update.afterCollision");
    if (collisionStats.playerShotWorldHits > 0) {
        const WeaponFeedbackEvent& feedback =
            railShooterCollisionSystem_.WeaponFeedback().LastAcceptedEvent();
        railShooterCameraDirector_.AddFeedbackImpulse(
            feedback.cameraShake,
            -0.004f * feedback.intensity,
            0.002f * feedback.intensity);
    }
    railShooterSpawnRuntime_.SubmitPendingVfx(vfxEngine_.Runtime(), railPath_);

    runtimeState_.camera.transform.scale = {1.0f, 1.0f, 1.0f};
    runtimeState_.camera.transform.translate = cameraPosition;
    runtimeState_.camera.transform.rotate = {
        std::asin((std::clamp)(-forward.y, -1.0f, 1.0f)),
        std::atan2(forward.x, forward.z),
        directedCamera.rig.roll,
    };
    runtimeState_.cameraWorldPosition = cameraPosition;
    scene_.UpdateCameraWorldPosition(cameraPosition);

    // Rail Shooter owns its gameplay camera independently from the generic
    // VFX-preview camera path. When the editor Ejects (or freezes a course
    // preview without a Play session), replace only the viewport presentation
    // camera. The RailCameraDirector transform above remains the authoritative
    // gameplay camera and is restored immediately by Possess.
    if (imguiLayer_.IsEnabled()) {
        const editor::EditorPlaySessionState& playSession =
            imguiLayer_.EditorPlaySession();
        const editor::EditorPlaySessionViewportMode viewportMode =
            playSession.ViewportMode();
        const bool activeSession = playSession.IsActive();
        const bool useEditorInspectionCamera =
            (activeSession && playSession.UsesEditorFreeCamera()) ||
            (!activeSession && runtimeState_.terrain.freezeCourseRuntime);
        const bool viewportModeChanged =
            viewportMode != lastEditorViewportMode_ ||
            playSession.SessionSerial() != lastEditorViewportSessionSerial_;

        if (useEditorInspectionCamera) {
            editor::EditorViewportCameraSettings cameraSettings{};
            cameraSettings.moveSpeed =
                (std::max)(0.0f, runtimeState_.camera.debugMoveSpeed) * 60.0f;
            cameraSettings.rotationSensitivity =
                (std::max)(0.0f, runtimeState_.camera.debugRotateSpeed) * 0.15f;
            cameraSettings.fastMoveMultiplier =
                runtimeState_.camera.debugFastMoveMultiplier;
            cameraSettings.slowMoveMultiplier =
                runtimeState_.camera.debugSlowMoveMultiplier;
            editorViewportCamera_.SetSettings(cameraSettings);

            if (!editorViewportCamera_.Initialized()) {
                editorViewportCamera_.Initialize(
                    runtimeState_.camera.transform,
                    runtimeState_.camera.fovY,
                    aspectRatio,
                    runtimeState_.camera.nearZ,
                    runtimeState_.camera.farZ);
            } else {
                if (viewportModeChanged) {
                    editorViewportCamera_.SetTransform(
                        runtimeState_.camera.transform);
                }
                editorViewportCamera_.SetLens(
                    runtimeState_.camera.fovY,
                    aspectRatio,
                    runtimeState_.camera.nearZ,
                    runtimeState_.camera.farZ);
            }

            // Editor inspection input must continue while gameplay simulation
            // is frozen and while the Rail Shooter rejects debug-camera input.
            editorViewportCamera_.Update(
                imguiLayer_.EditorViewportCameraFrameInput());
            runtimeState_.cameraWorldPosition =
                editorViewportCamera_.WorldPosition();
            frameState_.viewMatrix =
                editorViewportCamera_.ViewMatrix();
            frameState_.projMatrix =
                editorViewportCamera_.ProjectionMatrix();
            frameState_.viewProjectionMatrix =
                editorViewportCamera_.ViewProjectionMatrix();
            frameState_.cameraWorldPosition =
                editorViewportCamera_.WorldPosition();
            scene_.UpdateCameraWorldPosition(
                editorViewportCamera_.WorldPosition());
        }

        lastEditorViewportMode_ = viewportMode;
        lastEditorViewportSessionSerial_ =
            playSession.SessionSerial();
    }

    const auto vfxUpdateStart = RailPerfClock::now();
    vfxEngine_.Update(runtimeState_.vfx, gameplayDeltaTime);
    gRailPerfFrame.vfxUpdateMs = ElapsedMs(vfxUpdateStart, RailPerfClock::now());
    LogRailFrameStage(railShooterFrameIndex_, railShooterDistance_, "update.afterVfx");
    const auto terrainUpdateStart = RailPerfClock::now();
    UpdateTerrainAuthoring(gameplayDeltaTime);
    gRailPerfFrame.terrainUpdateMs = ElapsedMs(terrainUpdateStart, RailPerfClock::now());
    LogRailFrameStage(railShooterFrameIndex_, railShooterDistance_, "update.afterTerrain");
    if ((railShooterFrameIndex_ % 120u) == 0u) {
        LogRailShooterRuntimeDiagnostics("heartbeat");
    }
    const auto particleUpdateStart = RailPerfClock::now();
    frameState_.drawCount = particleSystem_.UpdateInstances(
        frameState_.viewProjectionMatrix,
        frameState_.deltaTime);
    gRailPerfFrame.particleUpdateMs = ElapsedMs(particleUpdateStart, RailPerfClock::now());
    gRailPerfFrame.updateMs = ElapsedMs(updateStart, RailPerfClock::now());
    imguiLayer_.CompleteEditorRuntimeFrameAdvance(!coursePreviewFrozen);
    LogRailFrameStage(railShooterFrameIndex_, railShooterDistance_, "update.end");
}

void AppRunLoop::RenderRailShooterFrame() {
    RenderVfxPreviewFrame();
}

void AppRunLoop::UpdateMultiMaterialShowcaseFrame() {
    UpdateSubmissionShowcaseInput(
        (std::max)(0.0f, frameState_.deltaTime));
    RuntimeVfxModelObjectState& showcase = runtimeState_.vfxModelObjects[0];
    if (showcase.visible) {
        showcase.transform.rotate.y = std::fmod(
            showcase.transform.rotate.y +
                (std::max)(0.0f, frameState_.deltaTime) * 0.24f,
            2.0f * 3.14159265358979323846f);
    }
    UpdateVfxPreviewFrame();
}

void AppRunLoop::UpdateSubmissionShowcaseInput(float deltaTime) {
    const AppGamepadFrame gamepad = submissionGamepad_.Poll();
    RuntimeSubmissionShowcaseState& telemetry =
        runtimeState_.submissionShowcase;
    telemetry.gamepadConnected = gamepad.connected;
    telemetry.controllerIndex = gamepad.controllerIndex;

    if (gamepad.justConnected) {
        OutputDebugStringA(
            "[MultiMaterialShowcase] XInput gamepad connected.\n");
    } else if (gamepad.justDisconnected) {
        OutputDebugStringA(
            "[MultiMaterialShowcase] XInput gamepad disconnected.\n");
    }

    bool keyboardInputAllowed =
        hwnd_ != nullptr && GetForegroundWindow() == hwnd_;
#if defined(GE3_ENABLE_IMGUI) && GE3_ENABLE_IMGUI
    if (keyboardInputAllowed &&
        imguiLayer_.IsVisible() &&
        (ImGui::GetIO().WantTextInput || ImGui::IsAnyItemActive())) {
        keyboardInputAllowed = false;
    }
#endif

    const auto keyDown = [keyboardInputAllowed](int virtualKey) {
        return keyboardInputAllowed &&
            (GetAsyncKeyState(virtualKey) & 0x8000) != 0;
    };
    const float keyboardMoveX =
        (keyDown('D') ? 1.0f : 0.0f) - (keyDown('A') ? 1.0f : 0.0f);
    const float keyboardMoveY =
        (keyDown('W') ? 1.0f : 0.0f) - (keyDown('S') ? 1.0f : 0.0f);
    const float keyboardTurn =
        ((keyDown('E') || keyDown(VK_RIGHT)) ? 1.0f : 0.0f) -
        ((keyDown('Q') || keyDown(VK_LEFT)) ? 1.0f : 0.0f);
    const bool keyboardToggleSkeleton =
        keyboardInputAllowed && WasKeyPressed('K');
    const bool keyboardNextAnimation =
        keyboardInputAllowed && WasKeyPressed(VK_SPACE);
    const bool keyboardReset =
        keyboardInputAllowed && WasKeyPressed('R');
    const bool keyboardToggleHelp =
        keyboardInputAllowed && WasKeyPressed('H');

    if (gamepad.toggleHelpPressed || keyboardToggleHelp) {
        submissionHudManuallyHidden_ = !submissionHudManuallyHidden_;
    }

    const AppGamepadStick combinedMove = AppGamepadInput::ClampUnitCircle(
        gamepad.leftStick.x + keyboardMoveX,
        gamepad.leftStick.y + keyboardMoveY);
    const float moveX = combinedMove.x;
    const float moveY = combinedMove.y;
    const float moveMagnitude = combinedMove.magnitude;
    const bool moving = moveMagnitude > 0.08f;
    const float turnInput =
        (std::clamp)(gamepad.rightStick.x + keyboardTurn, -1.0f, 1.0f);
    const bool keyboardActive =
        std::abs(keyboardMoveX) > 0.0f ||
        std::abs(keyboardMoveY) > 0.0f ||
        std::abs(keyboardTurn) > 0.0f ||
        keyboardToggleSkeleton || keyboardNextAnimation || keyboardReset ||
        keyboardToggleHelp;

    telemetry.keyboardInputEnabled = keyboardInputAllowed;
    telemetry.keyboardActive = keyboardActive;
    telemetry.moveX = moveX;
    telemetry.moveY = moveY;
    telemetry.moveMagnitude = moveMagnitude;

    constexpr float kMoveSpeed = 2.25f;
    constexpr float kTurnSpeed = 2.35f;
    constexpr float kMovementFacingSpeed = 9.0f;
    constexpr float kPi = 3.14159265358979323846f;
    Transform& transform = runtimeState_.skinnedModelTransform;
    transform.translate.x +=
        moveX * kMoveSpeed * deltaTime;
    transform.translate.z +=
        moveY * kMoveSpeed * deltaTime;
    transform.translate.x =
        (std::clamp)(transform.translate.x, -2.25f, 0.55f);
    transform.translate.z =
        (std::clamp)(transform.translate.z, -2.0f, 0.35f);

    const bool manualTurning = std::abs(turnInput) > 0.05f;
    if (moving && !manualTurning) {
        transform.rotate.y = AdvanceHumanoidMovementYaw(
            transform.rotate.y,
            ResolveHumanoidMovementYaw(moveX, moveY),
            kMovementFacingSpeed * (std::max)(0.0f, deltaTime));
    }
    if (manualTurning) {
        transform.rotate.y = std::fmod(
            transform.rotate.y +
                turnInput * kTurnSpeed * deltaTime +
                2.0f * kPi,
            2.0f * kPi);
    }

    // Animation playback is presentation state, not device-connection state.
    // Keep its clock advancing after either keyboard or gamepad movement ends
    // so releasing a stick cannot freeze the humanoid at an arbitrary walk
    // pose. Movement magnitude only controls playback speed.
    runtimeState_.playAnimatedCube = true;
    runtimeState_.animatedCubeSpeed =
        0.65f + (moving ? moveMagnitude * 0.85f : 0.0f);

    if (gamepad.toggleSkeletonPressed || keyboardToggleSkeleton) {
        runtimeState_.showSkeletonDebug =
            !runtimeState_.showSkeletonDebug;
    }
    if ((gamepad.nextAnimationPressed || keyboardNextAnimation) &&
        !runtimeState_.skinnedAnimationBlend.active) {
        const uint32_t nextModel =
            runtimeState_.selectedSkinnedModelIndex == 1u ? 2u : 1u;
        if (nextModel < scene_.skinnedModels.size() &&
            scene_.skinnedModels[nextModel].loaded) {
            BeginSkinnedAnimationBlend(runtimeState_, nextModel, 0.25f);
        }
    }
    if (gamepad.resetPressed || keyboardReset) {
        CancelSkinnedAnimationBlend(runtimeState_);
        ResetMultiMaterialShowcaseHumanoidPose(runtimeState_);
        runtimeState_.animatedCubeTime = 0.0f;
        telemetry.moveX = 0.0f;
        telemetry.moveY = 0.0f;
        telemetry.moveMagnitude = 0.0f;
    }
}

void AppRunLoop::UpdateVfxPreviewFrame() {
    appPipelines_.HotReloadIfNeeded(dev_.GetDevice());
    const RenderViewportMetrics metrics =
        ResolveRenderViewportMetrics(
            imguiLayer_.EditorViewportRenderTargetState(),
            windowWidth_,
            windowHeight_);
    ConfigureViewportAndScissor(runtimeState_, metrics.width, metrics.height);

    const float aspectRatio = metrics.AspectRatio();
    const bool useEditorViewportCamera = imguiLayer_.IsEnabled();
    if (useEditorViewportCamera) {
        const editor::EditorPlaySessionState& playSession =
            imguiLayer_.EditorPlaySession();
        const editor::EditorPlaySessionViewportMode viewportMode =
            playSession.ViewportMode();
        const bool activeSession = playSession.IsActive();
        const bool usePossessedGameCamera =
            activeSession &&
            viewportMode ==
                editor::EditorPlaySessionViewportMode::GameCamera;
        const bool viewportModeChanged =
            viewportMode != lastEditorViewportMode_ ||
            playSession.SessionSerial() != lastEditorViewportSessionSerial_;
        editor::EditorViewportCameraSettings cameraSettings{};
        // Preserve the previous debug-camera tuning while converting its
        // per-frame values to frame-rate independent editor navigation.
        cameraSettings.moveSpeed =
            (std::max)(0.0f, runtimeState_.camera.debugMoveSpeed) * 60.0f;
        cameraSettings.rotationSensitivity =
            (std::max)(0.0f, runtimeState_.camera.debugRotateSpeed) * 0.15f;
        cameraSettings.fastMoveMultiplier =
            runtimeState_.camera.debugFastMoveMultiplier;
        cameraSettings.slowMoveMultiplier =
            runtimeState_.camera.debugSlowMoveMultiplier;
        editorViewportCamera_.SetSettings(cameraSettings);
        editorGameViewportCamera_.SetSettings(cameraSettings);

        if (usePossessedGameCamera) {
            if (!editorGameViewportCamera_.Initialized()) {
                editorGameViewportCamera_.Initialize(
                    runtimeState_.camera.transform,
                    runtimeState_.camera.fovY,
                    aspectRatio,
                    runtimeState_.camera.nearZ,
                    runtimeState_.camera.farZ);
            } else {
                editorGameViewportCamera_.SetTransform(
                    runtimeState_.camera.transform);
                editorGameViewportCamera_.SetLens(
                    runtimeState_.camera.fovY,
                    aspectRatio,
                    runtimeState_.camera.nearZ,
                    runtimeState_.camera.farZ);
            }
            runtimeState_.cameraWorldPosition =
                editorGameViewportCamera_.WorldPosition();
            frameState_.viewMatrix =
                editorGameViewportCamera_.ViewMatrix();
            frameState_.projMatrix =
                editorGameViewportCamera_.ProjectionMatrix();
            frameState_.viewProjectionMatrix =
                editorGameViewportCamera_.ViewProjectionMatrix();
        } else {
            // Entering an ejected session starts from the exact gameplay
            // camera pose. Subsequent free-camera motion remains editor-only.
            const bool seedFromGameCamera =
                activeSession && viewportModeChanged &&
                viewportMode ==
                    editor::EditorPlaySessionViewportMode::EjectedFree;
            if (!editorViewportCamera_.Initialized()) {
                editorViewportCamera_.Initialize(
                    runtimeState_.camera.transform,
                    runtimeState_.camera.fovY,
                    aspectRatio,
                    runtimeState_.camera.nearZ,
                    runtimeState_.camera.farZ);
            } else {
                if (seedFromGameCamera) {
                    editorViewportCamera_.SetTransform(
                        runtimeState_.camera.transform);
                }
                editorViewportCamera_.SetLens(
                    runtimeState_.camera.fovY,
                    aspectRatio,
                    runtimeState_.camera.nearZ,
                    runtimeState_.camera.farZ);
            }
            editor::EditorViewportCameraInput cameraInput =
                imguiLayer_.EditorViewportCameraFrameInput();
            const bool allowEjectedEditorCameraInput =
                activeSession &&
                viewportMode ==
                    editor::EditorPlaySessionViewportMode::EjectedFree;
            // Runtime camera debug input is a gameplay policy. It must not
            // disable the editor-owned inspection camera after Eject.
            if (!runtimeState_.camera.enableDebugInput &&
                !allowEjectedEditorCameraInput) {
                cameraInput = {};
            }
            editorViewportCamera_.Update(cameraInput);
            // Preserve legacy authoring-camera behavior while stopped. During
            // Play/Sim the Runtime Game Camera remains untouched by Eject.
            if (!activeSession) {
                runtimeState_.camera.transform =
                    editorViewportCamera_.CameraTransform();
            }
            runtimeState_.cameraWorldPosition =
                editorViewportCamera_.WorldPosition();
            frameState_.viewMatrix = editorViewportCamera_.ViewMatrix();
            frameState_.projMatrix =
                editorViewportCamera_.ProjectionMatrix();
            frameState_.viewProjectionMatrix =
                editorViewportCamera_.ViewProjectionMatrix();
        }
        lastEditorViewportMode_ = viewportMode;
        lastEditorViewportSessionSerial_ =
            playSession.SessionSerial();
    } else {
        debugCamera_.SetInputEnabled(runtimeState_.camera.enableDebugInput);
        debugCamera_.SetMoveSpeed(runtimeState_.camera.debugMoveSpeed);
        debugCamera_.SetRotateSpeed(runtimeState_.camera.debugRotateSpeed);
        debugCamera_.SetSpeedMultipliers(
            runtimeState_.camera.debugSlowMoveMultiplier,
            runtimeState_.camera.debugFastMoveMultiplier);
        debugCamera_.SetTransform(runtimeState_.camera.transform);
        debugCamera_.SetLens(
            runtimeState_.camera.fovY,
            aspectRatio,
            runtimeState_.camera.nearZ,
            runtimeState_.camera.farZ);
        debugCamera_.Update();
        runtimeState_.camera.transform = debugCamera_.GetTransform();
        runtimeState_.cameraWorldPosition = debugCamera_.GetWorldPosition();
        frameState_.viewMatrix = debugCamera_.GetViewMatrix();
        frameState_.projMatrix = debugCamera_.GetProjectionMatrix();
        frameState_.viewProjectionMatrix = debugCamera_.GetViewProjectionMatrix();
    }
    frameState_.cameraWorldPosition = runtimeState_.cameraWorldPosition;
    scene_.UpdateCameraWorldPosition(runtimeState_.cameraWorldPosition);

    constexpr float kFixedPreviewDeltaTime = 0.016f;
    const bool editorRuntimeAdvance = imguiLayer_.ShouldAdvanceEditorRuntimeFrame();
    const bool previewRuntimeAdvance = ShouldAdvancePreviewRuntime(
        editorRuntimeAdvance,
        runtimeState_.submissionShowcase.enabled);
    const float previewDeltaTime =
        previewRuntimeAdvance ? kFixedPreviewDeltaTime : 0.0f;
    ProcessReleaseShowcaseControls(previewDeltaTime);
    vfxEngine_.Update(runtimeState_.vfx, previewDeltaTime);
    UpdateTerrainAuthoring(previewDeltaTime);

    BYTE key[256] = {};
    (void)key;

    frameState_.deltaTime = previewDeltaTime;
    frameState_.drawCount = particleSystem_.UpdateInstances(
        frameState_.viewProjectionMatrix,
        frameState_.deltaTime);
    // Only acknowledge an editor-owned Play/Step request. Submission auto-run
    // must not mutate the editor play-session state machine.
    imguiLayer_.CompleteEditorRuntimeFrameAdvance(editorRuntimeAdvance);
}

void AppRunLoop::BeginFrameSystems() {
    imguiLayer_.BeginFrame();
    frameTransientAllocator_.BeginFrame();
    resourceRegistry_.Clear();
    vfxEngine_.BeginFrame();
    renderGraph_.Clear();
    renderGraph_.ClearResources();
}

void AppRunLoop::ApplyEditorViewportRenderTargetForRender() {
    const RenderViewportMetrics metrics =
        ResolveRenderViewportMetrics(
            imguiLayer_.EditorViewportRenderTargetState(),
            windowWidth_,
            windowHeight_);
    ConfigureViewportAndScissor(runtimeState_, metrics.width, metrics.height);
    frameState_.projMatrix = MakePerspectiveFovMatrix(
        runtimeState_.camera.fovY,
        metrics.AspectRatio(),
        runtimeState_.camera.nearZ,
        runtimeState_.camera.farZ);
    frameState_.viewProjectionMatrix =
        Multiply(frameState_.viewMatrix, frameState_.projMatrix);
    frameState_.drawCount = particleSystem_.UpdateInstances(
        frameState_.viewProjectionMatrix,
        frameState_.deltaTime);
}

bool AppRunLoop::ResolveEditorViewportClientPoint(
    POINT clientPoint,
    POINT& outViewportPoint,
    uint32_t& outViewportWidth,
    uint32_t& outViewportHeight) const {
    const editor::EditorViewportRenderTargetState& editorViewport =
        imguiLayer_.EditorViewportRenderTargetState();
    if (editorViewport.enabled &&
        editorViewport.Valid() &&
        editorViewport.displayRect.Valid()) {
        const editor::EditorPanelRect& rect = editorViewport.displayRect;
        if (static_cast<float>(clientPoint.x) < rect.x ||
            static_cast<float>(clientPoint.y) < rect.y ||
            static_cast<float>(clientPoint.x) >= rect.x + rect.width ||
            static_cast<float>(clientPoint.y) >= rect.y + rect.height) {
            return false;
        }

        editor::EditorViewportCoordinateService coordinates;
        coordinates.Update(editor::EditorViewportCoordinateContext{
            rect,
            editorViewport.renderWidth,
            editorViewport.renderHeight,
            frameState_.viewProjectionMatrix});
        const editor::EditorViewportCoordinatePoint renderPoint =
            coordinates.DisplayToRender(static_cast<float>(clientPoint.x), static_cast<float>(clientPoint.y));
        if (!renderPoint.valid) {
            return false;
        }
        outViewportPoint.x = static_cast<LONG>((std::clamp)(
            std::lround(renderPoint.x),
            0l,
            static_cast<long>((std::max)(1u, editorViewport.renderWidth) - 1u)));
        outViewportPoint.y = static_cast<LONG>((std::clamp)(
            std::lround(renderPoint.y),
            0l,
            static_cast<long>((std::max)(1u, editorViewport.renderHeight) - 1u)));
        outViewportWidth = editorViewport.renderWidth;
        outViewportHeight = editorViewport.renderHeight;
        return true;
    }

    if (clientPoint.x < 0 ||
        clientPoint.y < 0 ||
        clientPoint.x >= static_cast<LONG>(windowWidth_) ||
        clientPoint.y >= static_cast<LONG>(windowHeight_)) {
        return false;
    }

    outViewportPoint = clientPoint;
    outViewportWidth = windowWidth_;
    outViewportHeight = windowHeight_;
    return outViewportWidth != 0 && outViewportHeight != 0;
}

bool AppRunLoop::EnsureRailGpuTimingResources() {
    if (railGpuTimingReady_) {
        return true;
    }
    ID3D12Device* device = dev_.GetDevice();
    if (device == nullptr || commandQueue_ == nullptr) {
        return false;
    }
    const uint32_t slotCount = (std::max)(1u, swapChain_.BufferCount());
    railGpuTimingSlots_.assign(slotCount, {});
    if (FAILED(commandQueue_->GetTimestampFrequency(&railGpuTimestampFrequency_)) ||
        railGpuTimestampFrequency_ == 0) {
        if (!railGpuTimingUnsupportedLogged_) {
            railGpuTimingUnsupportedLogged_ = true;
            WriteRailGpuTimingLine("[RailGpuTiming] timestamp frequency unavailable\n");
        }
        return false;
    }

    D3D12_QUERY_HEAP_DESC queryHeapDesc{};
    queryHeapDesc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
    queryHeapDesc.Count = slotCount * 2u;
    if (FAILED(device->CreateQueryHeap(&queryHeapDesc, IID_PPV_ARGS(&railGpuTimingQueryHeap_)))) {
        if (!railGpuTimingUnsupportedLogged_) {
            railGpuTimingUnsupportedLogged_ = true;
            WriteRailGpuTimingLine("[RailGpuTiming] CreateQueryHeap failed\n");
        }
        return false;
    }

    D3D12_HEAP_PROPERTIES heapProps{};
    heapProps.Type = D3D12_HEAP_TYPE_READBACK;

    D3D12_RESOURCE_DESC readbackDesc{};
    readbackDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    readbackDesc.Width = sizeof(uint64_t) * queryHeapDesc.Count;
    readbackDesc.Height = 1;
    readbackDesc.DepthOrArraySize = 1;
    readbackDesc.MipLevels = 1;
    readbackDesc.Format = DXGI_FORMAT_UNKNOWN;
    readbackDesc.SampleDesc.Count = 1;
    readbackDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    if (FAILED(device->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &readbackDesc,
            D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr,
            IID_PPV_ARGS(&railGpuTimingReadback_)))) {
        if (!railGpuTimingUnsupportedLogged_) {
            railGpuTimingUnsupportedLogged_ = true;
            WriteRailGpuTimingLine("[RailGpuTiming] CreateCommittedResource(readback) failed\n");
        }
        railGpuTimingQueryHeap_.Reset();
        return false;
    }

    railGpuTimingReady_ = true;
    std::ostringstream line;
    line << "[RailGpuTiming] initialized slots=" << slotCount
         << " timestampFrequency=" << railGpuTimestampFrequency_
         << "\n";
    WriteRailGpuTimingLine(line.str());
    return true;
}

void AppRunLoop::ResolveCompletedRailGpuTiming(uint32_t backBufferIndex) {
    if (!railGpuTimingReady_ || railGpuTimingReadback_ == nullptr || railGpuTimingSlots_.empty()) {
        return;
    }
    const uint32_t slot = backBufferIndex % static_cast<uint32_t>(railGpuTimingSlots_.size());
    RailGpuTimingSlot& timingSlot = railGpuTimingSlots_[slot];
    if (!timingSlot.pending) {
        return;
    }

    const uint64_t offsetBytes = sizeof(uint64_t) * slot * 2ull;
    D3D12_RANGE readRange{static_cast<SIZE_T>(offsetBytes), static_cast<SIZE_T>(offsetBytes + sizeof(uint64_t) * 2ull)};
    uint8_t* mapped = nullptr;
    if (FAILED(railGpuTimingReadback_->Map(0, &readRange, reinterpret_cast<void**>(&mapped))) ||
        mapped == nullptr) {
        return;
    }

    const uint64_t* timestamps = reinterpret_cast<const uint64_t*>(mapped + offsetBytes);
    const uint64_t begin = timestamps[0];
    const uint64_t end = timestamps[1];
    D3D12_RANGE writeRange{0, 0};
    railGpuTimingReadback_->Unmap(0, &writeRange);

    const uint64_t ticks = end >= begin ? end - begin : 0;
    const double gpuMs =
        railGpuTimestampFrequency_ > 0
            ? (static_cast<double>(ticks) * 1000.0 / static_cast<double>(railGpuTimestampFrequency_))
            : 0.0;
    const double pacingWaitMs = timingSlot.waitFrameSlotMs + timingSlot.presentMs;
    const bool shouldLog =
        gpuMs >= kRailGpuTimingLogThresholdMs ||
        timingSlot.cpuNoPresentMs >= 18.0 ||
        pacingWaitMs >= kRailFramePacingLogThresholdMs ||
        timingSlot.endAndExecuteMs >= 4.0;

    if (shouldLog) {
        std::ostringstream line;
        line << "[RailGpuTiming]"
             << " frame=" << timingSlot.frame
             << " distance=" << timingSlot.distance
             << " section=\"" << (timingSlot.section.empty() ? std::string("-") : timingSlot.section) << "\""
             << " gpuMs=" << gpuMs
             << " cpuNoPresentMs=" << timingSlot.cpuNoPresentMs
             << " pacingWaitMs=" << pacingWaitMs
             << " waitFrameSlotMs=" << timingSlot.waitFrameSlotMs
             << " presentMs=" << timingSlot.presentMs
             << " presentSyncInterval=" << frameCoordinator_.PresentSyncInterval()
             << " presentTearingAllowed=" << (frameCoordinator_.PresentTearingAllowed() ? 1 : 0)
             << " presentMaxFrameLatency=" << frameCoordinator_.PresentMaxFrameLatency()
             << " swapBufferCount=" << swapChain_.BufferCount()
             << " renderGraphExecuteMs=" << timingSlot.renderGraphExecuteMs
             << " endAndExecuteMs=" << timingSlot.endAndExecuteMs
             << " timestampTicks=" << ticks
             << "\n";
        WriteRailGpuTimingLine(line.str());
    }
    timingSlot.pending = false;
}

void AppRunLoop::BeginRailGpuTiming(ID3D12GraphicsCommandList* commandList, uint32_t backBufferIndex) {
    if (commandList == nullptr || !EnsureRailGpuTimingResources()) {
        return;
    }
    const uint32_t slot = backBufferIndex % static_cast<uint32_t>(railGpuTimingSlots_.size());
    const uint32_t queryIndex = slot * 2u;
    RailGpuTimingSlot& timingSlot = railGpuTimingSlots_[slot];
    timingSlot = {};
    timingSlot.pending = true;
    timingSlot.frame = railShooterFrameIndex_;
    timingSlot.distance = railShooterDistance_;
    if (const CourseSection* section = railShooterCourse_.FindSection(railShooterDistance_)) {
        timingSlot.section = section->name;
    }
    commandList->EndQuery(railGpuTimingQueryHeap_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, queryIndex);
}

void AppRunLoop::EndRailGpuTiming(ID3D12GraphicsCommandList* commandList, uint32_t backBufferIndex) {
    if (!railGpuTimingReady_ || commandList == nullptr || railGpuTimingQueryHeap_ == nullptr ||
        railGpuTimingReadback_ == nullptr || railGpuTimingSlots_.empty()) {
        return;
    }
    const uint32_t slot = backBufferIndex % static_cast<uint32_t>(railGpuTimingSlots_.size());
    const uint32_t queryIndex = slot * 2u;
    commandList->EndQuery(railGpuTimingQueryHeap_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, queryIndex + 1u);
    commandList->ResolveQueryData(
        railGpuTimingQueryHeap_.Get(),
        D3D12_QUERY_TYPE_TIMESTAMP,
        queryIndex,
        2,
        railGpuTimingReadback_.Get(),
        sizeof(uint64_t) * queryIndex);
}

void AppRunLoop::CaptureRailGpuTimingCpuMetadata(uint32_t backBufferIndex) {
    if (!railGpuTimingReady_ || railGpuTimingSlots_.empty()) {
        return;
    }
    const uint32_t slot = backBufferIndex % static_cast<uint32_t>(railGpuTimingSlots_.size());
    RailGpuTimingSlot& timingSlot = railGpuTimingSlots_[slot];
    if (!timingSlot.pending) {
        return;
    }
    timingSlot.cpuNoPresentMs =
        gRailPerfFrame.updateMs +
        (std::max)(0.0, gRailPerfFrame.renderMs - gRailPerfFrame.presentMs);
    timingSlot.waitFrameSlotMs = gRailPerfFrame.waitFrameSlotMs;
    timingSlot.renderGraphExecuteMs = gRailPerfFrame.renderGraphExecuteMs;
    timingSlot.endAndExecuteMs = gRailPerfFrame.endAndExecuteMs;
    timingSlot.presentMs = gRailPerfFrame.presentMs;
}

void AppRunLoop::RenderFrame() {
    if (gpuDeviceLost_) {
        return;
    }
    editor::EditorViewportRealtimePolicy& realtimePolicy =
        imguiLayer_.ViewportRealtimePolicy();
    const editor::EditorViewportRealtimeSnapshot realtime =
        realtimePolicy.Evaluate(
            editor::EditorViewportRealtimeInput{
                imguiLayer_.EditorPlaySessionActive(),
                imguiLayer_.EditorViewportInteractionActive(),
                imguiLayer_.EditorInteractiveToolActive()});
    imguiLayer_.FramePacingService().Pace(
        editor::EditorFramePacingInput{
            editorFramePacingProfilingMode_,
            hwnd_ != nullptr && IsIconic(hwnd_) != FALSE,
            hwnd_ == nullptr || GetForegroundWindow() == hwnd_,
            !imguiLayer_.IsVisible() ||
                imguiLayer_.EditorPlaySessionActive(),
            imguiLayer_.EditorViewportInteractionActive() ||
                imguiLayer_.EditorInteractiveToolActive(),
            realtime.continuous});
    realtimePolicy.AcknowledgeViewportRendered();
    // Scene updates already write mapped camera/particle buffers. Waiting later
    // in RenderVfxPreviewFrame only protects the command allocator's slot.
    if (!frameCoordinator_.WaitBeforeSharedResourceWrites()) {
        gpuDeviceLost_ = true;
        if (hwnd_ != nullptr) PostMessage(hwnd_, WM_CLOSE, 0, 0);
        return;
    }
    sceneStateManager_.Update(*this);
    sceneStateManager_.Render(*this);
}

void AppRunLoop::UpdateTerrainAuthoring(float deltaTime) {
    TerrainAuthoringState& terrain = runtimeState_.terrain;
    scene_.enemyThreatVisualDraw.BeginFrame();
    railEnemyCombatPresentationBridge_.AppendDestructionPrimitives(scene_.enemyThreatVisualDraw);
    railShooterVehicleCollisionFeedback_.AppendImpactPrimitives(scene_.enemyThreatVisualDraw);
    railEnemyProjectileVfxRenderer_.AppendProductionWorldPrimitives(
        scene_.enemyThreatVisualDraw);
    railEnemyAttackLaneTelegraphRenderer_.AppendProductionWorldPrimitives(
        scene_.enemyThreatVisualDraw);
    scene_.enemyThreatVisualDraw.Upload(frameState_.viewProjectionMatrix);
    if (!terrain.enabled) {
        scene_.debugDraw.BeginFrame();
        scene_.debugDraw.Upload(frameState_.viewProjectionMatrix);
        return;
    }

    std::string presetError;
    bool settingsChanged = false;
    if (terrain.requestSavePreset) {
        terrainPresetStore_.Save(terrain, &presetError);
        terrain.requestSavePreset = false;
    }
    if (terrain.requestLoadPreset || terrain.requestReloadPreset) {
        settingsChanged = terrainPresetStore_.Load(terrain, &presetError);
        terrain.requestLoadPreset = false;
        terrain.requestReloadPreset = false;
    }
    if (terrain.autoReloadPreset) {
        settingsChanged =
            terrainPresetStore_.ReloadIfChanged(terrain, &presetError) || settingsChanged;
    }
    if (settingsChanged) {
        ApplyRailShooterCourse();
    }
    if (railShooterInitialized_) {
        ApplyRailShooterTerrainBudget(terrain);
    }

    if (terrain.useCanyonSunLighting) {
        terrain.canyonSunDirection = NormalizeOr(terrain.canyonSunDirection, {-0.38f, -0.52f, 0.76f});
        runtimeState_.directionalLightData.color = terrain.canyonSunColor;
        runtimeState_.directionalLightData.direction = terrain.canyonSunDirection;
        runtimeState_.directionalLightData.intensity = terrain.canyonSunIntensity;
        runtimeState_.pointLightData.intensity = 0.0f;
    }

    if (terrain.autoAdvancePreview) {
        terrain.previewDistance += terrain.previewSpeed * deltaTime;
        if (railPath_.Length() > 0.0f && terrain.previewDistance > railPath_.Length()) {
            terrain.previewDistance = std::fmod(terrain.previewDistance, railPath_.Length());
        }
    }

    terrainChunkManager_.Update(
        dev_.GetDevice(),
        &heaps_.srv,
        railPath_,
        terrain.settings,
        &railShooterCourse_.terrainEditLayer,
        &terrain.previewEditLayer,
        terrain.previewDistance,
        frameState_.viewProjectionMatrix);

    scene_.debugDraw.BeginFrame();
    if constexpr (!app::kRuntimeAuthoringEnabled) {
        // Release presentation must not expose collision boxes, lock debug
        // spheres, rail guides or stale editor selections. Gameplay telegraphs
        // and the reticle/HUD have their own renderers and remain active.
        scene_.debugDraw.Upload(frameState_.viewProjectionMatrix);
        return;
    }
    if constexpr (app::kRuntimeAuthoringEnabled) {
        AppendCourseObjectSelectionDebugDraw(
            scene_.debugDraw,
            railShooterCourse_,
            railPath_,
            terrain);
    }
    const CourseSpawnRuntime& previewPresentationRuntime =
        coursePreviewActorRuntimeBridge_.Active()
        ? coursePreviewActorRuntimeBridge_.Runtime()
        : railShooterSpawnRuntime_;
    previewPresentationRuntime.AppendDebugDraw(scene_.debugDraw, railPath_);
    railShooterCollisionSystem_.AppendDebugDraw(scene_.debugDraw, railPath_);
    railShooterLockOnSystem_.AppendDebugDraw(scene_.debugDraw);
    const bool debugDrawEnabled =
        terrain.showDebugDraw ||
        terrain.displayMode == TerrainDisplayMode::Debug ||
        terrain.showCascadeBounds;
    if (debugDrawEnabled) {
        if (terrain.showRailPath) {
            const float start = (std::max)(0.0f, terrain.previewDistance - terrain.settings.chunkLength);
            const float end = terrain.previewDistance +
                terrain.settings.chunkLength * static_cast<float>(terrain.settings.visibleAheadChunks);
            scene_.debugDraw.AddPolyline(
                railPath_.SamplePolyline(start, end, 8.0f),
                {0.15f, 0.75f, 1.0f, 1.0f},
                false);

            const RailPathSample preview = railPath_.Evaluate(terrain.previewDistance);
            scene_.debugDraw.AddPoint(preview.position, 2.0f, {1.0f, 1.0f, 0.15f, 1.0f});
            scene_.debugDraw.AddLine(
                preview.position,
                {
                    preview.position.x + preview.tangent.x * 10.0f,
                    preview.position.y + preview.tangent.y * 10.0f,
                    preview.position.z + preview.tangent.z * 10.0f,
                },
                {0.2f, 1.0f, 0.2f, 1.0f});
        }

        terrainChunkManager_.AppendDebugDraw(scene_.debugDraw, railPath_, terrain);

        if (terrain.showCascadeBounds) {
            const std::array<float, AppSceneResources::kCascadeShadowCount> splits =
                GetCascadeShadowSplits(terrain);
            const Vector4 colors[AppSceneResources::kCascadeShadowCount] = {
                {0.20f, 0.85f, 1.0f, 1.0f},
                {0.30f, 1.0f, 0.40f, 1.0f},
                {1.0f, 0.86f, 0.22f, 1.0f},
                {1.0f, 0.32f, 0.20f, 1.0f},
            };
            Vector3 previous = railPath_.Evaluate(terrain.previewDistance).position;
            for (uint32_t cascade = 0; cascade < AppSceneResources::kCascadeShadowCount; ++cascade) {
                const RailPathSample sample =
                    railPath_.Evaluate(terrain.previewDistance + splits[cascade]);
                const float radius = (std::max)(
                    sample.corridorRadius,
                    terrain.settings.canyonHalfWidth * 0.55f);
                scene_.debugDraw.AddCircle(
                    sample.position,
                    sample.right,
                    sample.up,
                    radius,
                    colors[cascade],
                    48);
                scene_.debugDraw.AddPoint(sample.position, 1.4f, colors[cascade]);
                scene_.debugDraw.AddLine(previous, sample.position, colors[cascade]);
                previous = sample.position;
            }
        }
    }
    scene_.debugDraw.Upload(frameState_.viewProjectionMatrix);
}

void AppRunLoop::RenderCascadeShadowMaps(ID3D12GraphicsCommandList* commandList) {
    if (commandList == nullptr ||
        !runtimeState_.terrain.enabled ||
        appPipelines_.GetMainRootSignature() == nullptr ||
        appPipelines_.GetTerrainShadowPSO() == nullptr ||
        scene_.cascadeShadowResource == nullptr ||
        scene_.mappedCascadeShadow == nullptr ||
        scene_.cascadeShadowMaps[0] == nullptr) {
        return;
    }

    TerrainAuthoringState& terrain = runtimeState_.terrain;
    const std::vector<TerrainRenderChunk>& chunks = terrainChunkManager_.RenderChunks();
    if (!terrain.cascadeShadowEnabled || chunks.empty()) {
        scene_.mappedCascadeShadow->parameters.z = 0.0f;
        return;
    }

    const std::array<float, AppSceneResources::kCascadeShadowCount> kSplits =
        GetCascadeShadowSplits(terrain);
    terrain.cascadeShadowSplit0 = kSplits[0];
    terrain.cascadeShadowSplit1 = kSplits[1];
    terrain.cascadeShadowSplit2 = kSplits[2];
    terrain.cascadeShadowSplit3 = kSplits[3];
    terrain.cascadeShadowBias = (std::clamp)(terrain.cascadeShadowBias, 0.0001f, 0.0120f);
    terrain.cascadeShadowStrength = (std::clamp)(terrain.cascadeShadowStrength, 0.0f, 1.0f);
    terrain.shadowDebugCascade = (std::clamp)(terrain.shadowDebugCascade, 0, 3);

    scene_.mappedCascadeShadow->cascadeSplits = Vector4(kSplits[0], kSplits[1], kSplits[2], kSplits[3]);
    scene_.mappedCascadeShadow->parameters = Vector4(
        terrain.cascadeShadowBias,
        terrain.cascadeShadowStrength,
        1.0f,
        1.0f / static_cast<float>(AppSceneResources::kCascadeShadowMapSize));

    Vector3 lightDirection =
        NormalizeOr(runtimeState_.directionalLightData.direction, {-0.38f, -0.52f, 0.76f});
    XMVECTOR lightForward = XMVector3Normalize(XMVectorSet(
        lightDirection.x,
        lightDirection.y,
        lightDirection.z,
        0.0f));
    XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
    const float upDot = std::abs(XMVectorGetX(XMVector3Dot(lightForward, up)));
    if (upDot > 0.92f) {
        up = XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f);
    }

    for (uint32_t cascade = 0; cascade < AppSceneResources::kCascadeShadowCount; ++cascade) {
        const float previousSplit = cascade == 0 ? 0.0f : kSplits[cascade - 1];
        const float split = kSplits[cascade];
        const float midpoint = (previousSplit + split) * 0.5f;
        RailPathSample sample = railPath_.Evaluate(terrain.previewDistance + midpoint);
        sample.position.y += sample.corridorRadius * 0.18f;

        const float cascadeLength = split - previousSplit;
        const float radius = (std::max)(
            sample.corridorRadius * 3.0f,
            cascadeLength * 0.72f + sample.corridorRadius * 2.0f);
        const float depth = radius * 3.2f + sample.corridorRadius * 4.0f;

        XMVECTOR center = XMVectorSet(sample.position.x, sample.position.y, sample.position.z, 1.0f);
        XMVECTOR eye = center - lightForward * (depth * 0.5f);
        XMMATRIX lightView = XMMatrixLookAtLH(eye, center, up);
        XMMATRIX lightProjection = XMMatrixOrthographicLH(radius * 2.0f, radius * 2.0f, 0.0f, depth);
        scene_.mappedCascadeShadow->lightViewProjection[cascade] =
            ToMatrix4x4(lightView * lightProjection);
    }

    std::array<float, AppSceneResources::kCascadeShadowCount> cascadeRangeStart{};
    std::array<float, AppSceneResources::kCascadeShadowCount> cascadeRangeEnd{};
    for (uint32_t cascade = 0; cascade < AppSceneResources::kCascadeShadowCount; ++cascade) {
        const float previousSplit = cascade == 0 ? 0.0f : kSplits[cascade - 1];
        const float split = kSplits[cascade];
        const float guardBand =
            (std::max)(terrain.settings.chunkLength * 1.20f, (split - previousSplit) * 0.30f);
        cascadeRangeStart[cascade] = terrain.previewDistance + previousSplit - guardBand;
        cascadeRangeEnd[cascade] = terrain.previewDistance + split + guardBand;
    }
    const auto chunkAffectsCascade = [&](const TerrainRenderChunk& chunk, uint32_t cascade) {
        return chunk.endDistance >= cascadeRangeStart[cascade] &&
            chunk.startDistance <= cascadeRangeEnd[cascade];
    };

    for (uint32_t cascade = 0; cascade < AppSceneResources::kCascadeShadowCount; ++cascade) {
        if (scene_.mappedCascadeShadowDraw[cascade] == nullptr) {
            continue;
        }
        *scene_.mappedCascadeShadowDraw[cascade] = *scene_.mappedCascadeShadow;
        scene_.mappedCascadeShadowDraw[cascade]->parameters.w = static_cast<float>(cascade);
    }

    D3D12_RESOURCE_BARRIER toDepth[AppSceneResources::kCascadeShadowCount]{};
    uint32_t transitionCount = 0;
    for (uint32_t cascade = 0; cascade < AppSceneResources::kCascadeShadowCount; ++cascade) {
        if (scene_.cascadeShadowStates[cascade] == D3D12_RESOURCE_STATE_DEPTH_WRITE) {
            continue;
        }
        D3D12_RESOURCE_BARRIER& barrier = toDepth[transitionCount++];
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = scene_.cascadeShadowMaps[cascade].Get();
        barrier.Transition.StateBefore = scene_.cascadeShadowStates[cascade];
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_DEPTH_WRITE;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        scene_.cascadeShadowStates[cascade] = D3D12_RESOURCE_STATE_DEPTH_WRITE;
    }
    if (transitionCount > 0) {
        commandList->ResourceBarrier(transitionCount, toDepth);
    }

    D3D12_VIEWPORT shadowViewport{};
    shadowViewport.Width = static_cast<float>(AppSceneResources::kCascadeShadowMapSize);
    shadowViewport.Height = static_cast<float>(AppSceneResources::kCascadeShadowMapSize);
    shadowViewport.MinDepth = 0.0f;
    shadowViewport.MaxDepth = 1.0f;

    D3D12_RECT shadowScissor{};
    shadowScissor.left = 0;
    shadowScissor.top = 0;
    shadowScissor.right = static_cast<LONG>(AppSceneResources::kCascadeShadowMapSize);
    shadowScissor.bottom = static_cast<LONG>(AppSceneResources::kCascadeShadowMapSize);

    commandList->SetGraphicsRootSignature(appPipelines_.GetMainRootSignature());
    commandList->SetPipelineState(appPipelines_.GetTerrainShadowPSO());
    commandList->RSSetViewports(1, &shadowViewport);
    commandList->RSSetScissorRects(1, &shadowScissor);
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    for (uint32_t cascade = 0; cascade < AppSceneResources::kCascadeShadowCount; ++cascade) {
        commandList->OMSetRenderTargets(0, nullptr, FALSE, &scene_.cascadeShadowDsvCpu[cascade]);
        commandList->ClearDepthStencilView(
            scene_.cascadeShadowDsvCpu[cascade],
            D3D12_CLEAR_FLAG_DEPTH,
            1.0f,
            0,
            0,
            nullptr);

        if (scene_.cascadeShadowDrawResources[cascade] != nullptr) {
            commandList->SetGraphicsRootConstantBufferView(
                10,
                scene_.cascadeShadowDrawResources[cascade]->GetGPUVirtualAddress());
        }

        for (const TerrainRenderChunk& chunk : chunks) {
            if (chunk.indexCount == 0 ||
                chunk.transformResource == nullptr ||
                chunk.transformGpuAddress == 0 ||
                !chunkAffectsCascade(chunk, cascade)) {
                continue;
            }
            commandList->SetGraphicsRootConstantBufferView(
                1,
                chunk.transformGpuAddress);
            commandList->IASetVertexBuffers(0, 1, &chunk.vbv);
            commandList->IASetIndexBuffer(&chunk.ibv);
            commandList->DrawIndexedInstanced(chunk.indexCount, 1, 0, 0, 0);
        }

        if (appPipelines_.GetTerrainDebrisShadowPSO() != nullptr) {
            commandList->SetPipelineState(appPipelines_.GetTerrainDebrisShadowPSO());
            for (const TerrainRenderChunk& chunk : chunks) {
                if (chunk.debrisIndexCount == 0 ||
                    chunk.debrisInstanceCount == 0 ||
                    chunk.transformResource == nullptr ||
                    chunk.transformGpuAddress == 0 ||
                    chunk.debrisVbv.BufferLocation == 0 ||
                    chunk.debrisInstanceVbv.BufferLocation == 0 ||
                    chunk.debrisIbv.BufferLocation == 0 ||
                    !chunkAffectsCascade(chunk, cascade)) {
                    continue;
                }
                D3D12_VERTEX_BUFFER_VIEW debrisVertexBuffers[2] = {
                    chunk.debrisVbv,
                    chunk.debrisInstanceVbv,
                };
                commandList->SetGraphicsRootConstantBufferView(
                    1,
                    chunk.transformGpuAddress);
                commandList->IASetVertexBuffers(0, 2, debrisVertexBuffers);
                commandList->IASetIndexBuffer(&chunk.debrisIbv);
                commandList->DrawIndexedInstanced(
                    chunk.debrisIndexCount,
                    chunk.debrisInstanceCount,
                    0,
                    0,
                    0);
            }
            commandList->SetPipelineState(appPipelines_.GetTerrainShadowPSO());
        }
    }

    D3D12_RESOURCE_BARRIER toSample[AppSceneResources::kCascadeShadowCount]{};
    transitionCount = 0;
    for (uint32_t cascade = 0; cascade < AppSceneResources::kCascadeShadowCount; ++cascade) {
        D3D12_RESOURCE_BARRIER& barrier = toSample[transitionCount++];
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = scene_.cascadeShadowMaps[cascade].Get();
        barrier.Transition.StateBefore = scene_.cascadeShadowStates[cascade];
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        scene_.cascadeShadowStates[cascade] = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    }
    if (transitionCount > 0) {
        commandList->ResourceBarrier(transitionCount, toSample);
    }

    if (srvDescriptorHeap_ != nullptr) {
        ID3D12DescriptorHeap* descriptorHeaps[] = { srvDescriptorHeap_.Get() };
        commandList->SetDescriptorHeaps(1, descriptorHeaps);
    }
}

bool AppRunLoop::WasKeyPressed(int virtualKey) {
    if (virtualKey < 0 || virtualKey >= static_cast<int>(previousKeyDown_.size())) {
        return false;
    }
    const bool down = (GetAsyncKeyState(virtualKey) & 0x8000) != 0;
    const bool pressed = down && !previousKeyDown_[static_cast<size_t>(virtualKey)];
    previousKeyDown_[static_cast<size_t>(virtualKey)] = down;
    return pressed;
}

void AppRunLoop::DispatchRailEnemyAttackFeedback(
    const AppGamepadFrame& gamepad,
    float deltaTime,
    bool gameplayActive) {
    EnemyAttackTelegraphFeedbackInput input{};
    input.telegraphFrame = &railEnemyAttackTelegraphSystem_.Frame();
    input.deltaTime = deltaTime;
    input.gameplayActive = gameplayActive;
    input.settings = railEnemyAttackTelegraphFeedbackSettings_;
    railEnemyAttackTelegraphFeedbackBridge_.Update(input);

    const EnemyAttackTelegraphFeedbackFrame& feedback =
        railEnemyAttackTelegraphFeedbackBridge_.Frame();
    for (const EnemyAttackTelegraphAudioCommand& command :
         feedback.audioCommands) {
        audio::SoundHandle sound{};
        switch (command.kind) {
        case EnemyAttackTelegraphEventKind::Acquired:
            sound = railTelegraphAcquiredSound_;
            break;
        case EnemyAttackTelegraphEventKind::Imminent:
            sound = railTelegraphImminentSound_;
            break;
        case EnemyAttackTelegraphEventKind::Fired:
            sound = railTelegraphFiredSound_;
            break;
        }
        if (sound.IsValid()) {
            audio_.PlaySpatial(
                sound,
                command.volume,
                command.pan,
                command.pitch);
        }
    }

    if (!gamepad.connected || gamepad.controllerIndex == UINT32_MAX) {
        if (railTelegraphVibrationController_ != UINT32_MAX) {
            (void)AppGamepadInput::SetVibration(
                railTelegraphVibrationController_, 0.0f, 0.0f);
            railTelegraphVibrationController_ = UINT32_MAX;
        }
        return;
    }
    if (railTelegraphVibrationController_ != UINT32_MAX &&
        railTelegraphVibrationController_ != gamepad.controllerIndex) {
        (void)AppGamepadInput::SetVibration(
            railTelegraphVibrationController_, 0.0f, 0.0f);
    }
    railTelegraphVibrationController_ = gamepad.controllerIndex;
    (void)AppGamepadInput::SetVibration(
        railTelegraphVibrationController_,
        feedback.haptics.lowFrequencyMotor,
        feedback.haptics.highFrequencyMotor);
}

void AppRunLoop::StopRailEnemyAttackFeedback() {
    if (railTelegraphVibrationController_ != UINT32_MAX) {
        (void)AppGamepadInput::SetVibration(
            railTelegraphVibrationController_, 0.0f, 0.0f);
        railTelegraphVibrationController_ = UINT32_MAX;
    }
    railEnemyAttackTelegraphFeedbackBridge_.Reset();
    railEnemyAttackLaneTelegraphRenderer_.Reset(&vfxEngine_.Runtime());
}

void AppRunLoop::DispatchRailVehicleAudio(float deltaTime) {
    RailVehicleAudioInput input{};
    input.actor = &railShooterVehicleActor_.Frame();
    input.presentation = &railShooterVehiclePresentation_.Frame();
    input.deltaTime = deltaTime;
    input.listenerPosition = frameState_.cameraWorldPosition;
    input.listenerRight = railShooterVehicleMovement_.State().right;
    input.settings = railShooterVehicleAudioSettings_;
    railShooterVehicleAudioBridge_.Update(input);

    const RailVehicleAudioFrame& audioFrame =
        railShooterVehicleAudioBridge_.Frame();
    for (size_t index = 0; index < audioFrame.cueCount; ++index) {
        const RailVehicleAudioCue& cue = audioFrame.cues[index];
        audio::SoundHandle sound{};
        switch (cue.kind) {
        case RailVehicleAudioCueKind::RollingPulse:
            sound = railVehicleRollingSound_;
            break;
        case RailVehicleAudioCueKind::RailJointImpact:
            sound = railVehicleJointSound_;
            break;
        case RailVehicleAudioCueKind::BrakeScrape:
            sound = railVehicleBrakeSound_;
            break;
        case RailVehicleAudioCueKind::StopImpact:
            sound = railVehicleStopSound_;
            break;
        }
        if (sound.IsValid()) {
            (void)audio_.PlaySpatial(
                sound,
                cue.volume,
                cue.pan,
                cue.pitch);
        }
    }
}

void AppRunLoop::DispatchRailTrackFeedback() {
    const RailTrackFeedbackFrame& feedback =
        railShooterTrackFeedbackDirector_.Frame();
    if (feedback.cameraShake > 0.0f ||
        std::abs(feedback.cameraFovKick) > 0.00001f ||
        std::abs(feedback.cameraRollKickRadians) > 0.00001f) {
        railShooterCameraDirector_.AddFeedbackImpulse(
            feedback.cameraShake,
            feedback.cameraFovKick,
            feedback.cameraRollKickRadians);
    }
    for (size_t index = 0; index < feedback.cueCount; ++index) {
        const RailTrackFeedbackCue& cue = feedback.cues[index];
        audio::SoundHandle sound{};
        if (cue.audioCueId == "landing") {
            sound = railVehicleStopSound_;
        } else if (cue.audioCueId == "drop") {
            sound = railVehicleBrakeSound_;
        } else if (!cue.audioCueId.empty()) {
            sound = railVehicleJointSound_;
        }
        if (sound.IsValid()) {
            (void)audio_.PlaySpatial(
                sound,
                (std::clamp)(0.32f + cue.intensity * 0.30f, 0.0f, 0.9f),
                0.0f,
                (std::clamp)(0.88f + cue.intensity * 0.12f, 0.65f, 1.4f));
        }
        if (!cue.vfxCueId.empty()) {
            CourseVfxCueDesc vfx{};
            vfx.id = "rail_ride_event_" + std::to_string(cue.sequence);
            vfx.effectName = cue.vfxCueId;
            vfx.spawnDistance = cue.railDistance;
            vfx.radius = 0.5f + cue.intensity * 0.55f;
            vfx.lifetime = 0.45f;
            vfx.color = {0.35f, 0.86f, 1.0f, 0.88f};
            railShooterSpawnRuntime_.SpawnVfxCue(std::move(vfx));
        }
    }
}

void AppRunLoop::DispatchRailVehicleEvasionFeedback() {
    const RailVehicleEvasionFeedbackFrame& feedback =
        railShooterEvasionFeedback_.Frame();
    for (size_t index = 0; index < feedback.audioCueCount; ++index) {
        const RailVehicleEvasionAudioCue& cue = feedback.audioCues[index];
        audio::SoundHandle sound{};
        switch (cue.kind) {
        case RailVehicleEvasionAudioCueKind::Start:
            sound = railVehicleEvasionStartSound_;
            break;
        case RailVehicleEvasionAudioCueKind::Recover:
            sound = railVehicleEvasionRecoverSound_;
            break;
        case RailVehicleEvasionAudioCueKind::Ready:
            sound = railVehicleEvasionReadySound_;
            break;
        }
        if (sound.IsValid()) {
            (void)audio_.PlaySpatial(
                sound,
                cue.volume,
                cue.pan,
                cue.pitch);
        }
    }

    for (size_t index = 0; index < feedback.vfxCommandCount; ++index) {
        const RailVehicleEvasionVfxCommand& command =
            feedback.vfxCommands[index];
        CourseVfxCueDesc cue{};
        cue.id = "vehicle_evasion_" +
            std::to_string(command.eventSequence) + "_" +
            std::to_string(feedback.revision) + "_" +
            std::to_string(index);
        cue.effectName = command.effectId;
        cue.spawnDistance = command.railDistance;
        cue.lateralOffset = command.lateralOffset;
        cue.verticalOffset = command.verticalOffset;
        cue.radius = command.radius;
        cue.lifetime = command.lifetime;
        cue.color = command.color;
        railShooterSpawnRuntime_.SpawnVfxCue(std::move(cue));
    }
}

void AppRunLoop::DispatchRailVehicleCollisionFeedback() {
    const RailVehicleCollisionFeedbackFrame& feedback =
        railShooterVehicleCollisionFeedback_.Frame();
    for (size_t index = 0; index < feedback.audioCueCount; ++index) {
        const RailVehicleCollisionAudioCue& cue = feedback.audioCues[index];
        if (railVehicleCollisionSound_.IsValid()) {
            (void)audio_.PlaySpatial(
                railVehicleCollisionSound_, cue.volume, cue.pan, cue.pitch);
        }
    }
    // Sparks and metal chips are retained by the vehicle feedback bridge and
    // submitted as bounded production primitives in UpdateTerrainAuthoring.

}

void AppRunLoop::DispatchEnemyCombatPresentation(
    std::span<const EnemyCombatEvent> events,
    float deltaTime,
    bool gameplayActive) {
    // Damage is resolved after warning submission. Remove a defeated source
    // from retained HUD/world warnings before this same frame is rendered.
    for (const auto& event : events) {
        if (event.kind == EnemyCombatEventKind::Defeated) {
            railEnemyAttackTelegraphSystem_.CancelActor(event.actorId);
            railEnemyAttackLaneTelegraphRenderer_.CancelActor(event.actorId,&vfxEngine_.Runtime());
        }
    }
    EnemyCombatPresentationInput input{};
    input.runtime = &railShooterSpawnRuntime_;
    input.railPath = &railPath_;
    input.events = events;
    input.listenerPosition = frameState_.cameraWorldPosition;
    input.listenerRight = railPath_.Length() > 0.0f
        ? railPath_.Evaluate(railShooterDistance_).right
        : Vector3{1.0f, 0.0f, 0.0f};
    input.deltaTime = deltaTime;
    input.gameplayActive = gameplayActive;
    input.settings = railEnemyCombatPresentationSettings_;
    railEnemyCombatPresentationBridge_.Update(input);

    const EnemyCombatPresentationFrame& presentation =
        railEnemyCombatPresentationBridge_.Frame();
    if (gameplayActive &&
        (presentation.cameraShakeImpulse > 0.0f ||
         presentation.cameraFovImpulseRadians != 0.0f ||
         presentation.cameraRollImpulseRadians != 0.0f)) {
        // Gameplay aiming has already captured the stable camera for this
        // frame. The impulse is consumed by the next presentation frame only.
        railShooterCameraDirector_.AddFeedbackImpulse(
            presentation.cameraShakeImpulse,
            presentation.cameraFovImpulseRadians,
            presentation.cameraRollImpulseRadians);
    }
    for (const EnemyCombatPresentationVfxCommand& command :
         presentation.vfxCommands) {
        CourseVfxCueDesc cue{};
        cue.id = command.cueId;
        cue.effectName = command.effectName;
        cue.payload = "actorId=" + std::to_string(command.actorId) +
            ";presentation=enemy_combat";
        cue.worldPosition = command.worldPosition;
        cue.hasWorldPosition = true;
        cue.color = command.color;
        cue.radius = command.radius;
        cue.lifetime = command.lifetime;
        railShooterSpawnRuntime_.SpawnVfxCue(std::move(cue));
    }
    for (const EnemyCombatPresentationAudioCue& cue :
         presentation.audioCues) {
        audio::SoundHandle sound{};
        switch (cue.kind) {
        case EnemyCombatPresentationAudioCueKind::Spawn:
            sound = railEnemySpawnSound_;
            break;
        case EnemyCombatPresentationAudioCueKind::Engage:
            sound = railEnemyEngageSound_;
            break;
        case EnemyCombatPresentationAudioCueKind::Attack:
            sound = railEnemyAttackSound_;
            break;
        case EnemyCombatPresentationAudioCueKind::HitReact:
            sound = railEnemyHitReactSound_;
            break;
        case EnemyCombatPresentationAudioCueKind::Death:
            sound = railEnemyDeathSound_;
            break;
        }
        if (sound.IsValid()) {
            (void)audio_.PlaySpatial(sound, cue.volume, cue.pan, cue.pitch);
        }
    }
}

void AppRunLoop::DispatchEnemyProjectilePresentation(
    float deltaTime,
    bool gameplayActive,
    const Vector3& cameraPosition,
    const Vector3& cameraRight,
    const Vector3& cameraUp) {
    const bool previewActive = coursePreviewActorRuntimeBridge_.Active();
    const CourseSpawnRuntime& presentationRuntime = previewActive
        ? coursePreviewActorRuntimeBridge_.Runtime()
        : railShooterSpawnRuntime_;
    if (std::isfinite(deltaTime) && deltaTime > 0.0f) {
        railEnemyProjectilePresentationTime_ +=
            (std::min)(deltaTime, 0.25f);
    }

    EnemyProjectilePresentationInput presentationInput{};
    presentationInput.runtime = &presentationRuntime;
    presentationInput.railPath = &railPath_;
    if (!previewActive) {
        presentationInput.playerDamageResults =
            railShooterCollisionSystem_.PlayerDamageResults();
        presentationInput.shootDownResults =
            railShooterCollisionSystem_.ProjectileShootDown().Frame().results;
    }
    presentationInput.playerDistance = railShooterDistance_;
    presentationInput.playerLateralOffset = railShooterPlayerLateralOffset_;
    presentationInput.playerVerticalOffset = railShooterPlayerVerticalOffset_;
    presentationInput.deltaTime = deltaTime;
    presentationInput.gameplayActive = gameplayActive && !previewActive;
    presentationInput.settings = railEnemyProjectilePresentationSettings_;
    railEnemyProjectilePresentationBridge_.Update(presentationInput);

    EnemyProjectileVfxRenderInput vfxRenderInput{};
    vfxRenderInput.presentation =
        &railEnemyProjectilePresentationBridge_.Frame();
    vfxRenderInput.effectRuntime = &vfxEngine_.Runtime();
    vfxRenderInput.cameraWorldPosition = cameraPosition;
    vfxRenderInput.cameraRight = cameraRight;
    vfxRenderInput.cameraUp = cameraUp;
    vfxRenderInput.elapsedTime = railEnemyProjectilePresentationTime_;
    vfxRenderInput.deltaTime = deltaTime;
    const RenderViewportMetrics projectileMetrics =
        ResolveRenderViewportMetrics(
            imguiLayer_.EditorViewportRenderTargetState(),
            windowWidth_,
            windowHeight_);
    vfxRenderInput.verticalFovRadians = runtimeState_.camera.fovY;
    vfxRenderInput.viewportHeightPixels = projectileMetrics.height;
    vfxRenderInput.gameplayActive = gameplayActive || previewActive;
    vfxRenderInput.settings = railEnemyProjectileVfxRendererSettings_;
    vfxRenderInput.readabilitySettings =
        railEnemyProjectileReadabilitySettings_;
    railEnemyProjectileVfxRenderer_.Update(vfxRenderInput);

    EnemyAttackDefensePresentationInput defensePresentationInput{};
    defensePresentationInput.telegraph =
        &railEnemyAttackTelegraphSystem_.Frame();
    defensePresentationInput.projectiles =
        &railEnemyProjectilePresentationBridge_.Frame();
    defensePresentationInput.runtime = &presentationRuntime;
    defensePresentationInput.mountedDefense =
        &railShooterMountedDefense_.Frame();
    defensePresentationInput.deltaTime = deltaTime;
    defensePresentationInput.gameplayActive = gameplayActive && !previewActive;
    defensePresentationInput.settings =
        railEnemyAttackDefensePresentationSettings_;
    railEnemyAttackDefensePresentationBridge_.Update(
        defensePresentationInput);

    EnemyProjectileAudioInput audioInput{};
    audioInput.presentation = &railEnemyProjectilePresentationBridge_.Frame();
    audioInput.nearMissResults =
        railShooterCollisionSystem_.PlayerNearMissResults();
    audioInput.listenerPosition = cameraPosition;
    audioInput.listenerRight = cameraRight;
    audioInput.gameplayActive = gameplayActive && !previewActive;
    audioInput.settings = railEnemyProjectileAudioSettings_;
    railEnemyProjectileAudioBridge_.Update(audioInput);
    for (const EnemyProjectileAudioCue& cue :
         railEnemyProjectileAudioBridge_.Frame().cues) {
        audio::SoundHandle sound{};
        switch (cue.kind) {
        case EnemyProjectileAudioCueKind::Launch:
            sound = railEnemyProjectileLaunchSound_;
            break;
        case EnemyProjectileAudioCueKind::FlyBy:
            sound = railEnemyProjectileFlyBySound_;
            break;
        case EnemyProjectileAudioCueKind::Impact:
            sound = railEnemyProjectileImpactSound_;
            break;
        }
        if (sound.IsValid()) {
            (void)audio_.PlaySpatial(
                sound,
                cue.volume,
                cue.pan,
                cue.pitch);
        }
    }
}

void AppRunLoop::ResetEnemyProjectilePresentation(bool resetGrazeState) {
    railEnemyProjectilePresentationBridge_.Reset();
    railEnemyProjectileVfxRenderer_.Reset(&vfxEngine_.Runtime());
    railEnemyProjectileAudioBridge_.Reset();
    railEnemyAttackDefensePresentationBridge_.Reset();
    railEnemyAttackDefenseResolutionSystem_.Reset();
    railEnemyAttackDefenseOutcomeFeedbackBridge_.Reset();
    railShooterDefensePromptRenderer_.Reset();
    if (resetGrazeState) railGrazeScoreSystem_.Reset();
    railThreatResponseDirector_.Reset();
    railEnemyEncounterReadabilityDirector_.Reset();
    railEnemyProjectilePresentationTime_ = 0.0f;
}

void AppRunLoop::DispatchEnemyAttackDefenseOutcomeFeedback() {
    const EnemyAttackDefenseOutcomeFeedbackFrame& feedback =
        railEnemyAttackDefenseOutcomeFeedbackBridge_.Frame();
    for (const EnemyAttackDefenseOutcomeAudioCue& cue : feedback.audioCues) {
        audio::SoundHandle sound = railDefenseOutcomeFailedSound_;
        if (cue.outcome == EnemyAttackDefenseOutcome::Success) {
            switch (cue.method) {
            case EnemyAttackDefenseMethod::Interrupt:
                sound = railDefenseInterruptSound_;
                break;
            case EnemyAttackDefenseMethod::ShootDown:
                sound = railDefenseShootDownSound_;
                break;
            case EnemyAttackDefenseMethod::LeanLeft:
            case EnemyAttackDefenseMethod::LeanRight:
            case EnemyAttackDefenseMethod::Duck:
                sound = railDefenseEvadeSound_;
                break;
            case EnemyAttackDefenseMethod::None:
                sound = railDefenseOutcomeSuccessSound_;
                break;
            }
        }
        if (sound.IsValid() && cue.volume > 0.0f) {
            (void)audio_.PlaySpatial(sound, cue.volume, 0.0f, cue.pitch);
        }
        if (cue.outcome == EnemyAttackDefenseOutcome::Success &&
            cue.grade == EnemyAttackDefenseGrade::Perfect &&
            railDefenseOutcomePerfectSound_.IsValid()) {
            (void)audio_.PlaySpatial(
                railDefenseOutcomePerfectSound_,
                cue.volume * 0.72f,
                0.0f,
                1.0f);
        }
    }
    if (feedback.cameraShake > 0.0f ||
        std::abs(feedback.cameraPitchImpulse) > 0.00001f ||
        std::abs(feedback.cameraYawImpulse) > 0.00001f) {
        railShooterCameraDirector_.AddFeedbackImpulse(
            feedback.cameraShake,
            feedback.cameraPitchImpulse,
            feedback.cameraYawImpulse);
    }
}

void AppRunLoop::DispatchThreatResponse() {
    const ThreatResponseFrame& response = railThreatResponseDirector_.Frame();
    for (const ThreatResponseCue& cue : response.cues) {
        audio::SoundHandle sound{};
        float volume = 0.0f;
        switch (cue.kind) {
        case ThreatResponseCueKind::Graze:
            sound = railGrazeSound_;
            volume = 0.22f + cue.intensity * 0.20f;
            break;
        case ThreatResponseCueKind::ChainMilestone:
            sound = railGrazeChainSound_;
            volume = 0.32f + cue.intensity * 0.20f;
            break;
        case ThreatResponseCueKind::CriticalEntered:
            sound = railThreatCriticalSound_;
            volume = 0.24f + cue.intensity * 0.18f;
            break;
        case ThreatResponseCueKind::DangerCleared:
            sound = railThreatClearSound_;
            volume = 0.18f + cue.intensity * 0.12f;
            break;
        }
        if (sound.IsValid() && volume > 0.0f) {
            (void)audio_.PlaySpatial(
                sound,
                (std::clamp)(volume, 0.0f, 0.72f),
                0.0f,
                cue.pitch);
        }
    }

    for (const GrazeScoreResult& graze :
         railGrazeScoreSystem_.ResultsThisFrame()) {
        if (!graze.accepted) continue;
        CourseVfxCueDesc vfx{};
        vfx.id = "player_graze_" + std::to_string(graze.sequence);
        vfx.effectName = "hit_ring";
        vfx.spawnDistance = graze.railDistance;
        vfx.lateralOffset = graze.lateralOffset;
        vfx.verticalOffset = graze.verticalOffset;
        vfx.radius = 0.7f + graze.closeness * 0.65f;
        vfx.lifetime = 0.32f;
        vfx.color = {0.20f, 0.88f, 1.0f, 0.72f};
        railShooterSpawnRuntime_.SpawnVfxCue(std::move(vfx));
    }

    if (response.cameraShake > 0.0f ||
        std::abs(response.cameraPitchImpulse) > 0.00001f ||
        std::abs(response.cameraYawImpulse) > 0.00001f) {
        railShooterCameraDirector_.AddFeedbackImpulse(
            response.cameraShake,
            response.cameraPitchImpulse,
            response.cameraYawImpulse);
    }
}

void AppRunLoop::DispatchGameSessionPresentation(
    const AppGamepadFrame& gamepad) {
    const GameSessionPresentationFrame& presentation =
        railShooterSessionPresentation_.Frame();
    for (const GameSessionPresentationCue& cue : presentation.cues) {
        audio::SoundHandle sound{};
        switch (cue.kind) {
        case GameSessionPresentationCueKind::SessionStarted:
        case GameSessionPresentationCueKind::IntroCompleted:
        case GameSessionPresentationCueKind::Resumed:
        case GameSessionPresentationCueKind::Restart:
            sound = railSessionStartSound_;
            break;
        case GameSessionPresentationCueKind::Checkpoint:
            sound = railSessionCheckpointSound_;
            break;
        case GameSessionPresentationCueKind::PlayerDamaged:
            // PlayerDamagePresentationBridge owns immediate hit feedback.
            continue;
        case GameSessionPresentationCueKind::Victory:
            sound = railSessionVictorySound_;
            break;
        case GameSessionPresentationCueKind::Defeat:
            sound = railSessionDefeatSound_;
            break;
        case GameSessionPresentationCueKind::Retry:
            sound = railSessionRetrySound_;
            break;
        case GameSessionPresentationCueKind::Paused:
        case GameSessionPresentationCueKind::PlayerRecovered:
        case GameSessionPresentationCueKind::Result:
            sound = railSessionStartSound_;
            break;
        }
        if (sound.IsValid() && cue.audioVolume > 0.0f) {
            audio_.PlaySpatial(sound, cue.audioVolume, 0.0f, cue.audioPitch);
        }
        if (cue.cameraShake > 0.0f ||
            std::abs(cue.cameraPitchImpulse) > 0.00001f ||
            std::abs(cue.cameraYawImpulse) > 0.00001f) {
            railShooterCameraDirector_.AddFeedbackImpulse(
                cue.cameraShake,
                cue.cameraPitchImpulse,
                cue.cameraYawImpulse);
        }
    }

    if (!gamepad.connected || gamepad.controllerIndex == UINT32_MAX) {
        if (railSessionVibrationController_ != UINT32_MAX) {
            (void)AppGamepadInput::SetVibration(
                railSessionVibrationController_, 0.0f, 0.0f);
            railSessionVibrationController_ = UINT32_MAX;
        }
        return;
    }
    railSessionVibrationController_ = gamepad.controllerIndex;
    const PlayerDamagePresentationFrame& damagePresentation =
        railPlayerDamagePresentationBridge_.Frame();
    const ThreatResponseFrame& threatResponse =
        railThreatResponseDirector_.Frame();
    const RailVehicleEvasionHapticFrame& evasionHaptics =
        railShooterEvasionFeedback_.Frame().haptics;
    const EnemyAttackTelegraphHapticCommand& telegraphHaptics =
        railEnemyAttackTelegraphFeedbackBridge_.Frame().haptics;
    const RailTrackFeedbackFrame& trackFeedback =
        railShooterTrackFeedbackDirector_.Frame();
    const EnemyAttackDefenseOutcomeFeedbackFrame& defenseOutcome =
        railEnemyAttackDefenseOutcomeFeedbackBridge_.Frame();
    if (presentation.hapticsActive ||
        damagePresentation.hapticRemainingSeconds > 0.0f ||
        threatResponse.hapticRemainingSeconds > 0.0f ||
        evasionHaptics.active || telegraphHaptics.active ||
        defenseOutcome.hapticRemainingSeconds > 0.0f ||
        trackFeedback.hapticLow > 0.0f || trackFeedback.hapticHigh > 0.0f) {
        (void)AppGamepadInput::SetVibration(
            railSessionVibrationController_,
            (std::max)({
                presentation.hapticLow,
                damagePresentation.hapticLow,
                threatResponse.hapticLow,
                evasionHaptics.lowFrequencyMotor,
                telegraphHaptics.lowFrequencyMotor,
                defenseOutcome.hapticLow,
                trackFeedback.hapticLow}),
            (std::max)({
                presentation.hapticHigh,
                damagePresentation.hapticHigh,
                threatResponse.hapticHigh,
                evasionHaptics.highFrequencyMotor,
                telegraphHaptics.highFrequencyMotor,
                defenseOutcome.hapticHigh,
                trackFeedback.hapticHigh}));
    } else {
        (void)AppGamepadInput::SetVibration(
            railSessionVibrationController_, 0.0f, 0.0f);
    }
}

void AppRunLoop::DispatchPlayerDamagePresentation() {
    const PlayerDamagePresentationFrame& presentation =
        railPlayerDamagePresentationBridge_.Frame();
    for (const PlayerDamageAudioCue& cue : presentation.audioCues) {
        if (railSessionDamageSound_.IsValid()) {
            (void)audio_.PlaySpatial(
                railSessionDamageSound_, cue.volume, 0.0f, cue.pitch);
        }
    }
    for (const PlayerDamageVfxCommand& command : presentation.vfxCommands) {
        CourseVfxCueDesc cue{};
        cue.id = "player_damage_" + std::to_string(command.resultSequence);
        cue.effectName = command.effectId;
        cue.spawnDistance = command.railDistance;
        cue.lateralOffset = command.lateralOffset;
        cue.verticalOffset = command.verticalOffset;
        cue.radius = command.radius;
        cue.lifetime = command.lethal ? 1.5f : 1.0f;
        cue.color = command.color;
        railShooterSpawnRuntime_.SpawnVfxCue(std::move(cue));
    }
    if (presentation.cameraShake > 0.0f ||
        std::abs(presentation.cameraPitchImpulse) > 0.00001f ||
        std::abs(presentation.cameraYawImpulse) > 0.00001f) {
        railShooterCameraDirector_.AddFeedbackImpulse(
            presentation.cameraShake,
            presentation.cameraPitchImpulse,
            presentation.cameraYawImpulse);
    }
}

void AppRunLoop::StopGameSessionPresentation() {
    if (railSessionVibrationController_ != UINT32_MAX) {
        (void)AppGamepadInput::SetVibration(
            railSessionVibrationController_, 0.0f, 0.0f);
        railSessionVibrationController_ = UINT32_MAX;
    }
    railShooterSessionPresentation_.Reset();
    railShooterHudRuntimeModel_.Reset();
    railShooterHudPresentation_.Reset();
    railShooterHudRenderer_.Reset();
    railShooterDefensePromptRenderer_.Reset();
    railEnemyAttackDefenseOutcomeFeedbackBridge_.Reset();
    railPlayerDamagePresentationBridge_.Reset();
}

void AppRunLoop::ProcessPostProcessShowcaseShortcuts() {
    if (hwnd_ == nullptr || GetForegroundWindow() != hwnd_) {
        return;
    }
#if defined(GE3_ENABLE_IMGUI) && GE3_ENABLE_IMGUI
    // Number entry in an active editor widget must not change the rendered look.
    if (imguiLayer_.IsVisible() &&
        (ImGui::GetIO().WantTextInput || ImGui::IsAnyItemActive())) {
        return;
    }
#endif

    const auto digitPressed = [this](int digitKey, int numpadKey) {
        const bool mainKeyboard = WasKeyPressed(digitKey);
        const bool numericKeypad = WasKeyPressed(numpadKey);
        return mainKeyboard || numericKeypad;
    };
    const auto toggleSinglePass = [this](const char* passName) {
        PostProcessStack& stack = vfxEngine_.PostProcess();
        stack.SetEnabled(passName, !stack.IsEnabled(passName));
    };
    const auto togglePassPair = [this](const char* firstName, const char* secondName) {
        PostProcessStack& stack = vfxEngine_.PostProcess();
        const bool enabled = stack.IsEnabled(firstName) && stack.IsEnabled(secondName);
        stack.SetEnabled(firstName, !enabled);
        stack.SetEnabled(secondName, !enabled);
    };

    const bool resetPressed = digitPressed('0', VK_NUMPAD0);
    const bool warpTunnelPressed = digitPressed('1', VK_NUMPAD1);
    const bool grayscalePressed = digitPressed('2', VK_NUMPAD2);
    const bool vignettePressed = digitPressed('3', VK_NUMPAD3);
    const bool boxBlurPressed = digitPressed('4', VK_NUMPAD4);
    const bool gaussianBlurPressed = digitPressed('5', VK_NUMPAD5);
    const bool outlinePressed = digitPressed('6', VK_NUMPAD6);
    const bool dissolvePressed = digitPressed('7', VK_NUMPAD7);
    const bool randomPressed = digitPressed('8', VK_NUMPAD8);

    PostProcessStack& stack = vfxEngine_.PostProcess();
    if (resetPressed) {
        stack.SetEnabled("Grayscale", false);
        stack.SetEnabled("Vignette", false);
        stack.SetEnabled("BoxBlurHorizontal", false);
        stack.SetEnabled("BoxBlurVertical", false);
        stack.SetEnabled("GaussianBlurHorizontal", false);
        stack.SetEnabled("GaussianBlurVertical", false);
        stack.SetEnabled("PrewittOutline", false);
        stack.CancelDissolveTransition();
        stack.SetEnabled("Random", false);
        stack.StopWarpTunnel();
        return;
    }
    if (grayscalePressed) {
        toggleSinglePass("Grayscale");
    }
    if (vignettePressed) {
        toggleSinglePass("Vignette");
    }
    if (boxBlurPressed) {
        togglePassPair("BoxBlurHorizontal", "BoxBlurVertical");
    }
    if (gaussianBlurPressed) {
        togglePassPair("GaussianBlurHorizontal", "GaussianBlurVertical");
    }
    if (outlinePressed) {
        toggleSinglePass("PrewittOutline");
    }
    if (warpTunnelPressed) {
        const WarpTunnelPhase phase = stack.GetWarpTunnelPhase();
        if (phase == WarpTunnelPhase::Idle || phase == WarpTunnelPhase::Exit) {
            stack.StartWarpTunnel();
        } else {
            stack.StopWarpTunnel();
        }
    }
    if (dissolvePressed) {
        stack.StartDissolveTransition();
    }
    if (randomPressed) {
        toggleSinglePass("Random");
    }
}

AppRunLoop::CourseObjectEditSnapshot AppRunLoop::CaptureCourseObjectSnapshot() const {
    CourseObjectEditSnapshot snapshot{};
    snapshot.terrainPlacements = railShooterCourse_.terrainPlacements;
    snapshot.rockClusters = railShooterCourse_.rockClusters;
    snapshot.selectionType = runtimeState_.terrain.courseObjectSelectionType;
    snapshot.selectedTerrainPlacement = runtimeState_.terrain.selectedCourseTerrainPlacement;
    snapshot.selectedRockCluster = runtimeState_.terrain.selectedCourseRockCluster;
    snapshot.selectedTerrainPlacements =
        runtimeState_.terrain.selectedCourseTerrainPlacements;
    snapshot.selectedRockClusters = runtimeState_.terrain.selectedCourseRockClusters;
    return snapshot;
}

std::string AppRunLoop::BuildCourseObjectSnapshotSummary(const CourseObjectEditSnapshot& snapshot) const {
    std::ostringstream stream;
    stream << "terrain=" << snapshot.terrainPlacements.size()
           << " rockClusters=" << snapshot.rockClusters.size()
           << " selectionType=" << snapshot.selectionType
           << " selectedTerrain=" << snapshot.selectedTerrainPlacement
           << " selectedRock=" << snapshot.selectedRockCluster;
    return stream.str();
}

void AppRunLoop::RestoreCourseObjectSnapshot(const CourseObjectEditSnapshot& snapshot) {
    railShooterCourse_.terrainPlacements = snapshot.terrainPlacements;
    railShooterCourse_.rockClusters = snapshot.rockClusters;
    runtimeState_.terrain.courseObjectSelectionType = snapshot.selectionType;
    runtimeState_.terrain.selectedCourseTerrainPlacement = snapshot.selectedTerrainPlacement;
    runtimeState_.terrain.selectedCourseRockCluster = snapshot.selectedRockCluster;
    runtimeState_.terrain.selectedCourseTerrainPlacements = snapshot.selectedTerrainPlacements;
    runtimeState_.terrain.selectedCourseRockClusters = snapshot.selectedRockClusters;
}

bool AppRunLoop::BeginCourseObjectGizmoEditSession() {
    if (!app::kRuntimeAuthoringEnabled ||
        !courseObjectDrag_.active || courseObjectDrag_.index < 0) {
        return false;
    }

    editor::EditorPropertyRegistry propertyRegistry;
    editor::RegisterBuiltInCourseObjectProperties(propertyRegistry);
    std::vector<editor::EditorPropertyEditSessionProperty> properties;
    editor::EditorObjectHandle target{};
    for (const CourseObjectDragState::Item& item : courseObjectDrag_.items) {
        const std::size_t index = static_cast<std::size_t>(item.index);
        const editor::EditorObjectHandle itemTarget = item.type == 0
            ? MakeCourseGizmoHandle(
                editor::EditorDomainId::CourseTerrainPlacement,
                "course-terrain",
                "Course Terrain",
                index,
                runtimeState_.terrain.courseObjectEditRevision)
            : MakeCourseGizmoHandle(
                editor::EditorDomainId::CourseRockCluster,
                "course-rock",
                "Course Rock Cluster",
                index,
                runtimeState_.terrain.courseObjectEditRevision);
        if (item.index == courseObjectDrag_.index) target = itemTarget;
        std::vector<editor::EditorPropertyEditSessionProperty> itemProperties =
            BuildCourseGizmoSessionProperties(
                propertyRegistry, itemTarget, item.type, courseObjectDrag_.gizmoMode);
        properties.insert(
            properties.end(),
            std::make_move_iterator(itemProperties.begin()),
            std::make_move_iterator(itemProperties.end()));
    }
    if (properties.empty()) {
        return false;
    }

    editor::CourseObjectPropertyAdapter previewAccessor(&railShooterCourse_, &runtimeState_, false);
    const editor::EditorPropertyEditSessionResult result =
        courseObjectGizmoEditSession_.Begin(
            editor::EditorPropertyEditSessionBeginRequest{
                &previewAccessor,
                std::move(properties),
                CourseGizmoLabel(courseObjectDrag_.gizmoMode),
                target,
                true,
                false,
                "viewport.gizmo"});
    return result.applied;
}

bool AppRunLoop::PreviewCourseObjectGizmoEditSession(
    std::vector<editor::EditorPropertyEditSessionValue> values) {
    if (!app::kRuntimeAuthoringEnabled ||
        !courseObjectGizmoEditSession_.IsActive()) {
        return false;
    }
    values.reserve(values.size() * (std::max)(std::size_t{1}, courseObjectDrag_.items.size()));
    const auto findValue = [&](std::string_view path) -> const editor::EditorPropertyValue* {
        const auto found = std::find_if(values.begin(), values.end(), [&](const auto& value) {
            return value.target.localIndex == static_cast<uint64_t>(courseObjectDrag_.index) &&
                value.propertyPath == path;
        });
        return found != values.end() ? &found->value : nullptr;
    };
    const float safeEpsilon = 0.0001f;
    for (const CourseObjectDragState::Item& item : courseObjectDrag_.items) {
        if (item.index == courseObjectDrag_.index) continue;
        const std::size_t index = static_cast<std::size_t>(item.index);
        const editor::EditorObjectHandle target = item.type == 0
            ? MakeCourseGizmoHandle(
                editor::EditorDomainId::CourseTerrainPlacement,
                "course-terrain", "Course Terrain", index,
                runtimeState_.terrain.courseObjectEditRevision)
            : MakeCourseGizmoHandle(
                editor::EditorDomainId::CourseRockCluster,
                "course-rock", "Course Rock Cluster", index,
                runtimeState_.terrain.courseObjectEditRevision);
        if (item.type == 0) {
            if (courseObjectDrag_.gizmoMode == 2) {
                if (const auto* value = findValue("CourseTerrainPlacement.rotation")) {
                    const Vector3 before = CourseGizmoRotationDegrees(courseObjectDrag_.startRotation);
                    values.push_back({target, "CourseTerrainPlacement.rotation", CourseGizmoVec3Value({
                        CourseGizmoRotationDegrees(item.rotation).x + value->vec3Value.x - before.x,
                        CourseGizmoRotationDegrees(item.rotation).y + value->vec3Value.y - before.y,
                        CourseGizmoRotationDegrees(item.rotation).z + value->vec3Value.z - before.z})});
                }
            } else if (courseObjectDrag_.gizmoMode == 1) {
                if (const auto* value = findValue("CourseTerrainPlacement.scale")) {
                    const Vector3 start = courseObjectDrag_.startScale;
                    values.push_back({target, "CourseTerrainPlacement.scale", CourseGizmoVec3Value({
                        item.scale.x * value->vec3Value.x / (std::max)(safeEpsilon, start.x),
                        item.scale.y * value->vec3Value.y / (std::max)(safeEpsilon, start.y),
                        item.scale.z * value->vec3Value.z / (std::max)(safeEpsilon, start.z)})});
                }
            } else {
                const auto* lateral = findValue("CourseTerrainPlacement.lateralOffset");
                const auto* vertical = findValue("CourseTerrainPlacement.verticalOffset");
                const auto* forward = findValue("CourseTerrainPlacement.forwardOffset");
                if (lateral != nullptr) values.push_back({target,
                    "CourseTerrainPlacement.lateralOffset", CourseGizmoFloatValue(
                        item.lateral + lateral->floatValue - courseObjectDrag_.startLateral)});
                if (vertical != nullptr) values.push_back({target,
                    "CourseTerrainPlacement.verticalOffset", CourseGizmoFloatValue(
                        item.vertical + vertical->floatValue - courseObjectDrag_.startVertical)});
                if (forward != nullptr) values.push_back({target,
                    "CourseTerrainPlacement.forwardOffset", CourseGizmoFloatValue(
                        item.forward + forward->floatValue - courseObjectDrag_.startForward)});
            }
        } else {
            if (courseObjectDrag_.gizmoMode == 2) {
                if (const auto* value = findValue("CourseRockCluster.rotation")) {
                    const Vector3 before = CourseGizmoRotationDegrees(courseObjectDrag_.startRotation);
                    const Vector3 itemDegrees = CourseGizmoRotationDegrees(item.rotation);
                    values.push_back({target, "CourseRockCluster.rotation", CourseGizmoVec3Value({
                        itemDegrees.x + value->vec3Value.x - before.x,
                        itemDegrees.y + value->vec3Value.y - before.y,
                        itemDegrees.z + value->vec3Value.z - before.z})});
                }
            } else if (courseObjectDrag_.gizmoMode == 1) {
                const auto* minScale = findValue("CourseRockCluster.minScale");
                const auto* maxScale = findValue("CourseRockCluster.maxScale");
                const auto* spread = findValue("CourseRockCluster.spread");
                if (minScale != nullptr) values.push_back({target, "CourseRockCluster.minScale",
                    CourseGizmoFloatValue(item.minScale * minScale->floatValue /
                        (std::max)(safeEpsilon, courseObjectDrag_.startMinScale))});
                if (maxScale != nullptr) values.push_back({target, "CourseRockCluster.maxScale",
                    CourseGizmoFloatValue(item.maxScale * maxScale->floatValue /
                        (std::max)(safeEpsilon, courseObjectDrag_.startMaxScale))});
                if (spread != nullptr) values.push_back({target, "CourseRockCluster.spread",
                    CourseGizmoVec3Value({
                        item.spread.x * spread->vec3Value.x /
                            (std::max)(safeEpsilon, courseObjectDrag_.startSpread.x),
                        item.spread.y * spread->vec3Value.y /
                            (std::max)(safeEpsilon, courseObjectDrag_.startSpread.y),
                        item.spread.z * spread->vec3Value.z /
                            (std::max)(safeEpsilon, courseObjectDrag_.startSpread.z)})});
            } else {
                const auto* clearLane = findValue("CourseRockCluster.clearLaneRadius");
                const auto* distance = findValue("CourseRockCluster.distance");
                const auto* spread = findValue("CourseRockCluster.spread");
                if (clearLane != nullptr) values.push_back({target,
                    "CourseRockCluster.clearLaneRadius", CourseGizmoFloatValue(
                        item.clearLaneRadius + clearLane->floatValue -
                            courseObjectDrag_.startClearLaneRadius)});
                if (distance != nullptr) values.push_back({target,
                    "CourseRockCluster.distance", CourseGizmoFloatValue(
                        item.distance + distance->floatValue - courseObjectDrag_.startDistance)});
                if (spread != nullptr) values.push_back({target,
                    "CourseRockCluster.spread", CourseGizmoVec3Value({
                        item.spread.x + spread->vec3Value.x - courseObjectDrag_.startSpread.x,
                        item.spread.y + spread->vec3Value.y - courseObjectDrag_.startSpread.y,
                        item.spread.z + spread->vec3Value.z - courseObjectDrag_.startSpread.z})});
            }
        }
    }
    editor::CourseObjectPropertyAdapter previewAccessor(&railShooterCourse_, &runtimeState_, false);
    const editor::EditorPropertyEditSessionResult result =
        courseObjectGizmoEditSession_.Preview(
            editor::EditorPropertyEditSessionPreviewRequest{
                &previewAccessor,
                std::move(values),
                true,
                false,
                "viewport.gizmo"});
    return result.applied && result.changed;
}

bool AppRunLoop::CancelCourseObjectDragIfNeeded() {
    if (!courseObjectDrag_.active) {
        return false;
    }

    if (courseObjectGizmoEditSession_.IsActive()) {
        editor::CourseObjectPropertyAdapter previewAccessor(&railShooterCourse_, &runtimeState_, false);
        courseObjectGizmoEditSession_.Cancel(
            editor::EditorPropertyEditSessionCancelRequest{
                &previewAccessor,
                false,
                "viewport.gizmo"});
    }
    courseObjectDrag_.active = false;
    courseObjectDrag_.changed = false;
    runtimeState_.terrain.courseObjectActiveAxis = -1;
    return true;
}

bool AppRunLoop::ApplyCourseObjectGizmoEditThroughServiceIfPossible() {
    if (!app::kRuntimeAuthoringEnabled ||
        !courseObjectDrag_.changed || courseObjectDrag_.index < 0) {
        return false;
    }

    editor::EditorPropertyRegistry propertyRegistry;
    editor::RegisterBuiltInCourseObjectProperties(propertyRegistry);
    editor::CourseObjectPropertyAdapter propertyAccessor(&railShooterCourse_, &runtimeState_);
    editor::EditorPropertyEditService propertyEditService;

    std::vector<editor::EditorPropertyBatchEdit> edits;
    const auto addFloatEdit =
        [&](const editor::EditorObjectHandle& target,
            const char* propertyPath,
            float beforeValue,
            float afterValue) {
            if (!CourseGizmoFloatChanged(beforeValue, afterValue)) {
                return;
            }
            const editor::EditorPropertyDescriptor* descriptor =
                propertyRegistry.Find(target.domain, propertyPath);
            if (descriptor == nullptr) {
                return;
            }
            edits.push_back(
                editor::EditorPropertyBatchEdit{
                    target,
                    descriptor,
                    CourseGizmoFloatValue(afterValue)});
        };
    const auto addVec3Edit =
        [&](const editor::EditorObjectHandle& target,
            const char* propertyPath,
            const Vector3& beforeValue,
            const Vector3& afterValue) {
            if (!CourseGizmoVectorChanged(beforeValue, afterValue)) {
                return;
            }
            const editor::EditorPropertyDescriptor* descriptor =
                propertyRegistry.Find(target.domain, propertyPath);
            if (descriptor == nullptr) {
                return;
            }
            edits.push_back(
                editor::EditorPropertyBatchEdit{
                    target,
                    descriptor,
                    CourseGizmoVec3Value(afterValue)});
        };

    const bool scaleMode = courseObjectDrag_.gizmoMode == 1;
    const bool rotateMode = courseObjectDrag_.gizmoMode == 2;
    editor::EditorObjectHandle transactionTarget{};

    if (courseObjectDrag_.type == 0 &&
        courseObjectDrag_.index < static_cast<int>(railShooterCourse_.terrainPlacements.size())) {
        const std::size_t index = static_cast<std::size_t>(courseObjectDrag_.index);
        CourseTerrainPlacement& placement = railShooterCourse_.terrainPlacements[index];
        const editor::EditorObjectHandle target =
            MakeCourseGizmoHandle(
                editor::EditorDomainId::CourseTerrainPlacement,
                "course-terrain",
                "Course Terrain",
                index,
                runtimeState_.terrain.courseObjectEditRevision);
        transactionTarget = target;

        if (rotateMode) {
            addVec3Edit(
                target,
                "CourseTerrainPlacement.rotation",
                CourseGizmoRotationDegrees(courseObjectDrag_.startRotation),
                CourseGizmoRotationDegrees(placement.rotation));
        } else if (scaleMode) {
            addVec3Edit(
                target,
                "CourseTerrainPlacement.scale",
                courseObjectDrag_.startScale,
                placement.scale);
        } else {
            addFloatEdit(
                target,
                "CourseTerrainPlacement.lateralOffset",
                courseObjectDrag_.startLateral,
                placement.lateralOffset);
            addFloatEdit(
                target,
                "CourseTerrainPlacement.verticalOffset",
                courseObjectDrag_.startVertical,
                placement.verticalOffset);
            addFloatEdit(
                target,
                "CourseTerrainPlacement.forwardOffset",
                courseObjectDrag_.startForward,
                placement.forwardOffset);
        }

        const CourseTerrainPlacement finalPlacement = placement;
        placement.distance = courseObjectDrag_.startDistance;
        placement.lateralOffset = courseObjectDrag_.startLateral;
        placement.verticalOffset = courseObjectDrag_.startVertical;
        placement.forwardOffset = courseObjectDrag_.startForward;
        placement.scale = courseObjectDrag_.startScale;
        placement.rotation = courseObjectDrag_.startRotation;
        if (edits.empty()) {
            placement = finalPlacement;
            return false;
        }
        const editor::EditorPropertyBatchEditResult result =
            propertyEditService.ApplyBatch(
                editor::EditorPropertyBatchEditRequest{
                    &propertyAccessor,
                    &courseObjectTransactions_,
                    nullptr,
                    nullptr,
                    std::move(edits),
                    CourseGizmoLabel(courseObjectDrag_.gizmoMode),
                    transactionTarget,
                    true,
                    false,
                    "viewport.gizmo"});
        if (result.applied && result.changed) {
            return true;
        }
        placement = finalPlacement;
        return false;
    }

    if (courseObjectDrag_.type == 1 &&
        courseObjectDrag_.index < static_cast<int>(railShooterCourse_.rockClusters.size())) {
        const std::size_t index = static_cast<std::size_t>(courseObjectDrag_.index);
        CourseRockCluster& cluster = railShooterCourse_.rockClusters[index];
        const editor::EditorObjectHandle target =
            MakeCourseGizmoHandle(
                editor::EditorDomainId::CourseRockCluster,
                "course-rock",
                "Course Rock Cluster",
                index,
                runtimeState_.terrain.courseObjectEditRevision);
        transactionTarget = target;

        if (rotateMode) {
            addVec3Edit(
                target,
                "CourseRockCluster.rotation",
                CourseGizmoRotationDegrees(courseObjectDrag_.startRotation),
                CourseGizmoRotationDegrees(cluster.rotation));
        } else if (scaleMode) {
            addFloatEdit(
                target,
                "CourseRockCluster.minScale",
                courseObjectDrag_.startMinScale,
                cluster.minScale);
            addFloatEdit(
                target,
                "CourseRockCluster.maxScale",
                courseObjectDrag_.startMaxScale,
                cluster.maxScale);
            addVec3Edit(
                target,
                "CourseRockCluster.spread",
                courseObjectDrag_.startSpread,
                cluster.spread);
        } else {
            addFloatEdit(
                target,
                "CourseRockCluster.clearLaneRadius",
                courseObjectDrag_.startClearLaneRadius,
                cluster.clearLaneRadius);
            addFloatEdit(
                target,
                "CourseRockCluster.distance",
                courseObjectDrag_.startDistance,
                cluster.distance);
            addVec3Edit(
                target,
                "CourseRockCluster.spread",
                courseObjectDrag_.startSpread,
                cluster.spread);
        }

        const CourseRockCluster finalCluster = cluster;
        cluster.distance = courseObjectDrag_.startDistance;
        cluster.minScale = courseObjectDrag_.startMinScale;
        cluster.maxScale = courseObjectDrag_.startMaxScale;
        cluster.spread = courseObjectDrag_.startSpread;
        cluster.clearLaneRadius = courseObjectDrag_.startClearLaneRadius;
        cluster.rotation = courseObjectDrag_.startRotation;
        if (edits.empty()) {
            cluster = finalCluster;
            return false;
        }
        const editor::EditorPropertyBatchEditResult result =
            propertyEditService.ApplyBatch(
                editor::EditorPropertyBatchEditRequest{
                    &propertyAccessor,
                    &courseObjectTransactions_,
                    nullptr,
                    nullptr,
                    std::move(edits),
                    CourseGizmoLabel(courseObjectDrag_.gizmoMode),
                    transactionTarget,
                    true,
                    false,
                    "viewport.gizmo"});
        if (result.applied && result.changed) {
            return true;
        }
        cluster = finalCluster;
        return false;
    }

    return false;
}

void AppRunLoop::StageCourseObjectGizmoTransactionIfNeeded() {
    if (!app::kRuntimeAuthoringEnabled ||
        !courseObjectDrag_.changed || courseObjectDrag_.index < 0) {
        return;
    }

    editor::EditorPropertyChange change{};
    change.displayName = CourseGizmoLabel(courseObjectDrag_.gizmoMode);
    change.valueType = "transform";
    change.sourceRevision = runtimeState_.terrain.courseObjectEditRevision;

    const char* suffix = CourseGizmoPropertySuffix(courseObjectDrag_.gizmoMode);
    if (courseObjectDrag_.type == 0 &&
        courseObjectDrag_.index < static_cast<int>(railShooterCourse_.terrainPlacements.size())) {
        const std::size_t index = static_cast<std::size_t>(courseObjectDrag_.index);
        const CourseTerrainPlacement& placement = railShooterCourse_.terrainPlacements[index];
        change.target.domain = editor::EditorDomainId::CourseTerrainPlacement;
        change.target.stableId =
            editor::BuildStableIndexedId("course-terrain", static_cast<uint64_t>(index));
        change.target.localIndex = static_cast<uint64_t>(index);
        change.target.generation = runtimeState_.terrain.courseObjectEditRevision;
        change.target.displayName = "Course Terrain #" + std::to_string(index);
        change.propertyPath = std::string("CourseTerrainPlacement.transform.") + suffix;
        change.beforeValue =
            FormatGizmoTerrainTransform(
                courseObjectDrag_.startDistance,
                courseObjectDrag_.startLateral,
                courseObjectDrag_.startVertical,
                courseObjectDrag_.startForward,
                courseObjectDrag_.startScale,
                courseObjectDrag_.startRotation);
        change.afterValue =
            FormatGizmoTerrainTransform(
                placement.distance,
                placement.lateralOffset,
                placement.verticalOffset,
                placement.forwardOffset,
                placement.scale,
                placement.rotation);
    } else if (courseObjectDrag_.type == 1 &&
        courseObjectDrag_.index < static_cast<int>(railShooterCourse_.rockClusters.size())) {
        const std::size_t index = static_cast<std::size_t>(courseObjectDrag_.index);
        const CourseRockCluster& cluster = railShooterCourse_.rockClusters[index];
        change.target.domain = editor::EditorDomainId::CourseRockCluster;
        change.target.stableId =
            editor::BuildStableIndexedId("course-rock", static_cast<uint64_t>(index));
        change.target.localIndex = static_cast<uint64_t>(index);
        change.target.generation = runtimeState_.terrain.courseObjectEditRevision;
        change.target.displayName = "Course Rock Cluster #" + std::to_string(index);
        change.propertyPath = std::string("CourseRockCluster.transform.") + suffix;
        change.beforeValue =
            FormatGizmoRockTransform(
                courseObjectDrag_.startDistance,
                courseObjectDrag_.startMinScale,
                courseObjectDrag_.startMaxScale,
                courseObjectDrag_.startSpread,
                courseObjectDrag_.startClearLaneRadius,
                courseObjectDrag_.startRotation);
        change.afterValue =
            FormatGizmoRockTransform(
                cluster.distance,
                cluster.minScale,
                cluster.maxScale,
                cluster.spread,
                cluster.clearLaneRadius,
                cluster.rotation);
    } else {
        return;
    }

    if (change.beforeValue != change.afterValue) {
        courseObjectTransactions_.StagePropertyDelta(std::move(change));
    }
}

bool AppRunLoop::CommitCourseObjectDragIfNeeded() {
    if (!app::kRuntimeAuthoringEnabled) {
        CancelCourseObjectDragIfNeeded();
        return false;
    }
    if (!courseObjectDrag_.active) {
        return false;
    }

    bool committed = false;
    if (courseObjectDrag_.changed) {
        if (courseObjectGizmoEditSession_.IsActive()) {
            editor::CourseObjectPropertyAdapter commitAccessor(&railShooterCourse_, &runtimeState_, true);
            editor::CourseObjectPropertyAdapter previewAccessor(&railShooterCourse_, &runtimeState_, false);
            const editor::EditorPropertyEditSessionResult result =
                courseObjectGizmoEditSession_.Commit(
                    editor::EditorPropertyEditSessionCommitRequest{
                        &commitAccessor,
                        &previewAccessor,
                        &courseObjectTransactions_,
                        nullptr,
                        nullptr,
                        true,
                        false,
                        "viewport.gizmo"});
            committed = result.applied && result.changed;
        } else if (!ApplyCourseObjectGizmoEditThroughServiceIfPossible()) {
            StageCourseObjectGizmoTransactionIfNeeded();
            ++runtimeState_.terrain.courseObjectEditRevision;
            committed = true;
        }
    } else if (courseObjectGizmoEditSession_.IsActive()) {
        editor::CourseObjectPropertyAdapter previewAccessor(&railShooterCourse_, &runtimeState_, false);
        courseObjectGizmoEditSession_.Cancel(
            editor::EditorPropertyEditSessionCancelRequest{
                &previewAccessor,
                false,
                "viewport.gizmo"});
    }

    courseObjectDrag_.active = false;
    courseObjectDrag_.changed = false;
    runtimeState_.terrain.courseObjectActiveAxis = -1;
    return committed;
}

void AppRunLoop::EnsureCourseObjectHistoryBaseline() {
    if (courseObjectHistoryInitialized_) {
        return;
    }
    courseObjectHistoryBaseline_ = CaptureCourseObjectSnapshot();
    courseObjectHistoryRevision_ = runtimeState_.terrain.courseObjectEditRevision;
    courseObjectHistoryInitialized_ = true;
    runtimeState_.terrain.courseObjectUndoDepth = 0;
    runtimeState_.terrain.courseObjectRedoDepth = 0;
}

void AppRunLoop::CommitCourseObjectHistoryIfNeeded() {
    EnsureCourseObjectHistoryBaseline();
    TerrainAuthoringState& editor = runtimeState_.terrain;
    if (editor.courseObjectEditRevision == courseObjectHistoryRevision_) {
        editor.courseObjectUndoDepth = static_cast<uint32_t>(courseObjectUndoStack_.size());
        editor.courseObjectRedoDepth = static_cast<uint32_t>(courseObjectRedoStack_.size());
        return;
    }

    constexpr size_t kMaxCourseObjectUndo = 96;
    const CourseObjectEditSnapshot beforeSnapshot = courseObjectHistoryBaseline_;
    const CourseObjectEditSnapshot afterSnapshot = CaptureCourseObjectSnapshot();
    courseObjectUndoStack_.push_back(courseObjectHistoryBaseline_);
    if (courseObjectUndoStack_.size() > kMaxCourseObjectUndo) {
        courseObjectUndoStack_.erase(courseObjectUndoStack_.begin());
    }
    courseObjectTransactions_.SetMaxHistory(kMaxCourseObjectUndo);
    if (courseObjectTransactions_.HasStagedPropertyDelta()) {
        std::vector<editor::EditorPropertyChange> propertyChanges =
            courseObjectTransactions_.ConsumeStagedPropertyDeltas();
        for (editor::EditorPropertyChange& propertyChange : propertyChanges) {
            if (propertyChange.target.domain == editor::EditorDomainId::Unknown) {
                propertyChange.target.domain = editor::EditorDomainId::CourseTerrainPlacement;
                propertyChange.target.stableId = "course-object-history";
                propertyChange.target.displayName = "Course Object History";
            }
            propertyChange.target.generation = editor.courseObjectEditRevision;
        }
        if (!propertyChanges.empty()) {
            const editor::EditorPropertyChange& firstChange = propertyChanges.front();
            editor::EditorObjectHandle transactionTarget = firstChange.target;
            transactionTarget.generation = editor.courseObjectEditRevision;
            const std::string transactionLabel = firstChange.displayName.empty()
                ? (propertyChanges.size() == 1
                    ? std::string("Course Property Edit")
                    : std::string("Course Property Batch Edit"))
                : firstChange.displayName;

            editor::EditorError commandError{};
            const bool commandRegistered = courseObjectTransactions_.PushCommand(
                transactionLabel,
                transactionTarget,
                std::make_shared<editor::CoursePropertyUndoCommand>(
                    editor::MakeCoursePropertyUndoChanges(propertyChanges)),
                &commandError);
            if (!commandRegistered) {
                // Compatibility bridge: a property edit must not become non-undoable while
                // legacy Asset/Runtime transactions are still being migrated.
                if (propertyChanges.size() == 1) {
                    editor::EditorPropertyChange propertyChange = std::move(propertyChanges.front());
                    courseObjectTransactions_.PushPropertyDelta(
                        transactionLabel,
                        std::move(propertyChange.target),
                        std::move(propertyChange.propertyPath),
                        std::move(propertyChange.valueType),
                        std::move(propertyChange.beforeValue),
                        std::move(propertyChange.afterValue));
                } else {
                    courseObjectTransactions_.PushMultiPropertyDelta(
                        transactionLabel,
                        std::move(transactionTarget),
                        std::move(propertyChanges));
                }
            }
        }
    } else {
        editor::EditorObjectHandle transactionTarget{};
        transactionTarget.domain = editor::EditorDomainId::CourseTerrainPlacement;
        transactionTarget.stableId = "course-object-history";
        transactionTarget.displayName = "Course Object History";
        transactionTarget.generation = editor.courseObjectEditRevision;
        courseObjectTransactions_.PushSnapshot(
            "Course Object Edit",
            std::move(transactionTarget),
            BuildCourseObjectSnapshotSummary(beforeSnapshot),
            BuildCourseObjectSnapshotSummary(afterSnapshot));
    }
    courseObjectRedoStack_.clear();
    courseObjectHistoryBaseline_ = afterSnapshot;
    courseObjectHistoryRevision_ = editor.courseObjectEditRevision;
    editor.courseObjectUndoDepth = static_cast<uint32_t>(courseObjectUndoStack_.size());
    editor.courseObjectRedoDepth = 0;
}

void AppRunLoop::ProcessCourseObjectUndoRedo() {
    EnsureCourseObjectHistoryBaseline();
    TerrainAuthoringState& editor = runtimeState_.terrain;
    editor::EditorPropertyRegistry propertyRegistry;
    editor::RegisterBuiltInCourseObjectProperties(propertyRegistry);
    RegisterCourseObjectViewportGizmoProperties(propertyRegistry);
    editor::CourseObjectPropertyAdapter propertyAccessor(&railShooterCourse_, &runtimeState_);
    editor::EditorPropertyEditService propertyEditService;
    editor::CourseEditorExecutionService courseExecutionService(
        propertyAccessor,
        propertyRegistry,
        nullptr,
        nullptr,
        true);
    editor::EditorExecutionContext executionContext;
    editor::EditorError executionContextError{};
    const bool executionContextReady =
        executionContext.Register(courseExecutionService, &executionContextError);

    const auto applyPropertyDelta =
        [&](const editor::EditorTransactionRecord& record,
            editor::EditorTransactionApplyMode mode) {
            if (record.payload.kind != editor::EditorTransactionPayloadKind::PropertyDelta &&
                record.payload.kind != editor::EditorTransactionPayloadKind::MultiPropertyDelta) {
                return false;
            }
            if (mode == editor::EditorTransactionApplyMode::Undo && courseObjectUndoStack_.empty()) {
                return false;
            }
            if (mode == editor::EditorTransactionApplyMode::Redo && courseObjectRedoStack_.empty()) {
                return false;
            }

            const CourseObjectEditSnapshot currentSnapshot = CaptureCourseObjectSnapshot();
            const editor::EditorPropertyApplyDeltaResult result =
                propertyEditService.ApplyDelta(
                    editor::EditorPropertyApplyDeltaRequest{
                        &propertyAccessor,
                        nullptr,
                        nullptr,
                        &propertyRegistry,
                        &record,
                        mode,
                        true,
                        false,
                        "course.undoRedo"});
            if (!result.applied) {
                return false;
            }

            if (mode == editor::EditorTransactionApplyMode::Undo) {
                courseObjectRedoStack_.push_back(currentSnapshot);
                courseObjectUndoStack_.pop_back();
            } else {
                courseObjectUndoStack_.push_back(currentSnapshot);
                courseObjectRedoStack_.pop_back();
            }
            courseObjectHistoryBaseline_ = CaptureCourseObjectSnapshot();
            courseObjectHistoryRevision_ = editor.courseObjectEditRevision;
            return true;
        };

    if (editor.courseObjectUndoRequested) {
        editor.courseObjectUndoRequested = false;
        const editor::EditorTransactionRecord* nextUndo =
            courseObjectTransactions_.NextUndoTransaction();
        const bool commandPending = nextUndo != nullptr && nextUndo->command != nullptr;
        if (commandPending) {
            if (executionContextReady && !courseObjectUndoStack_.empty()) {
                const CourseObjectEditSnapshot currentSnapshot = CaptureCourseObjectSnapshot();
                editor::EditorError undoError{};
                if (courseObjectTransactions_.Undo(executionContext, &undoError)) {
                    courseObjectRedoStack_.push_back(currentSnapshot);
                    courseObjectUndoStack_.pop_back();
                    courseObjectHistoryBaseline_ = CaptureCourseObjectSnapshot();
                    courseObjectHistoryRevision_ = editor.courseObjectEditRevision;
                }
            }
        } else if (!courseObjectUndoStack_.empty() &&
            courseObjectTransactions_.Undo(
                [this, &editor, &applyPropertyDelta](
                    const editor::EditorTransactionRecord& record,
                    editor::EditorTransactionApplyMode mode) {
                    if (applyPropertyDelta(record, mode)) {
                        return true;
                    }
                    if (courseObjectUndoStack_.empty()) {
                        return false;
                    }
                    courseObjectRedoStack_.push_back(CaptureCourseObjectSnapshot());
                    const CourseObjectEditSnapshot snapshot = courseObjectUndoStack_.back();
                    courseObjectUndoStack_.pop_back();
                    RestoreCourseObjectSnapshot(snapshot);
                    courseObjectHistoryBaseline_ = snapshot;
                    ++editor.courseObjectEditRevision;
                    courseObjectHistoryRevision_ = editor.courseObjectEditRevision;
                    return true;
                })) {
            // Applied through EditorTransactionStack bridge.
        } else if (!courseObjectUndoStack_.empty()) {
            courseObjectRedoStack_.push_back(CaptureCourseObjectSnapshot());
            const CourseObjectEditSnapshot snapshot = courseObjectUndoStack_.back();
            courseObjectUndoStack_.pop_back();
            RestoreCourseObjectSnapshot(snapshot);
            courseObjectHistoryBaseline_ = snapshot;
            ++editor.courseObjectEditRevision;
            courseObjectHistoryRevision_ = editor.courseObjectEditRevision;
        }
    }

    if (editor.courseObjectRedoRequested) {
        editor.courseObjectRedoRequested = false;
        const editor::EditorTransactionRecord* nextRedo =
            courseObjectTransactions_.NextRedoTransaction();
        const bool commandPending = nextRedo != nullptr && nextRedo->command != nullptr;
        if (commandPending) {
            if (executionContextReady && !courseObjectRedoStack_.empty()) {
                const CourseObjectEditSnapshot currentSnapshot = CaptureCourseObjectSnapshot();
                editor::EditorError redoError{};
                if (courseObjectTransactions_.Redo(executionContext, &redoError)) {
                    courseObjectUndoStack_.push_back(currentSnapshot);
                    courseObjectRedoStack_.pop_back();
                    courseObjectHistoryBaseline_ = CaptureCourseObjectSnapshot();
                    courseObjectHistoryRevision_ = editor.courseObjectEditRevision;
                }
            }
        } else if (!courseObjectRedoStack_.empty() &&
            courseObjectTransactions_.Redo(
                [this, &editor, &applyPropertyDelta](
                    const editor::EditorTransactionRecord& record,
                    editor::EditorTransactionApplyMode mode) {
                    if (applyPropertyDelta(record, mode)) {
                        return true;
                    }
                    if (courseObjectRedoStack_.empty()) {
                        return false;
                    }
                    courseObjectUndoStack_.push_back(CaptureCourseObjectSnapshot());
                    const CourseObjectEditSnapshot snapshot = courseObjectRedoStack_.back();
                    courseObjectRedoStack_.pop_back();
                    RestoreCourseObjectSnapshot(snapshot);
                    courseObjectHistoryBaseline_ = snapshot;
                    ++editor.courseObjectEditRevision;
                    courseObjectHistoryRevision_ = editor.courseObjectEditRevision;
                    return true;
                })) {
            // Applied through EditorTransactionStack bridge.
        } else if (!courseObjectRedoStack_.empty()) {
            courseObjectUndoStack_.push_back(CaptureCourseObjectSnapshot());
            const CourseObjectEditSnapshot snapshot = courseObjectRedoStack_.back();
            courseObjectRedoStack_.pop_back();
            RestoreCourseObjectSnapshot(snapshot);
            courseObjectHistoryBaseline_ = snapshot;
            ++editor.courseObjectEditRevision;
            courseObjectHistoryRevision_ = editor.courseObjectEditRevision;
        }
    }

    editor.courseObjectUndoDepth = static_cast<uint32_t>(courseObjectUndoStack_.size());
    editor.courseObjectRedoDepth = static_cast<uint32_t>(courseObjectRedoStack_.size());
}

void AppRunLoop::ProcessCourseObjectViewportEditing() {
    TerrainAuthoringState& editor = runtimeState_.terrain;
    const bool leftMouseDown = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
    if (!app::kRuntimeAuthoringEnabled) {
        CancelCourseObjectDragIfNeeded();
        editor.enableCourseObjectViewportEditing = false;
        editor.showCourseObjectFrame = false;
        editor.courseObjectAuthoringInputLocked = true;
        editor.courseObjectActiveAxis = -1;
        editor.selectedCourseTerrainPlacement = -1;
        editor.selectedCourseRockCluster = -1;
        editor.selectedCourseTerrainPlacements.clear();
        editor.selectedCourseRockClusters.clear();
        editor.courseObjectUndoRequested = false;
        editor.courseObjectRedoRequested = false;
        courseObjectDrag_.changed = false;
        previousCourseEditorLeftMouseDown_ = leftMouseDown;
        return;
    }
    const editor::EditorViewportAuthoringInputGuard inputGuard =
        editor::MakeEditorViewportAuthoringInputGuard(!editor.courseObjectAuthoringInputLocked);
    if (!inputGuard.CanMutate()) {
        CancelCourseObjectDragIfNeeded();
        editor.courseObjectUndoRequested = false;
        editor.courseObjectRedoRequested = false;
        editor.courseObjectActiveAxis = -1;
        courseObjectDrag_.changed = false;
        previousCourseEditorLeftMouseDown_ = leftMouseDown;
        return;
    }

    EnsureCourseObjectHistoryBaseline();
    ProcessCourseObjectUndoRedo();
    CommitCourseObjectHistoryIfNeeded();

    const bool leftMousePressed = leftMouseDown && !previousCourseEditorLeftMouseDown_;
    const bool leftMouseReleased = !leftMouseDown && previousCourseEditorLeftMouseDown_;

    if (!inputGuard.CanUseViewportInput(editor.enableCourseObjectViewportEditing) ||
        hwnd_ == nullptr ||
        railPath_.Length() <= 0.0f) {
        CancelCourseObjectDragIfNeeded();
        previousCourseEditorLeftMouseDown_ = leftMouseDown;
        return;
    }

    POINT cursor{};
    if (!GetCursorPos(&cursor) || !ScreenToClient(hwnd_, &cursor)) {
        CancelCourseObjectDragIfNeeded();
        previousCourseEditorLeftMouseDown_ = leftMouseDown;
        return;
    }

    POINT viewportCursor{};
    uint32_t viewportWidth = 0;
    uint32_t viewportHeight = 0;
    const bool cursorInViewport =
        ResolveEditorViewportClientPoint(
            cursor,
            viewportCursor,
            viewportWidth,
            viewportHeight);
    bool imguiWantsMouse = false;
#if defined(GE3_ENABLE_IMGUI) && GE3_ENABLE_IMGUI
    imguiWantsMouse =
        imguiLayer_.IsVisible() && ImGui::GetIO().WantCaptureMouse;
#endif

    if (leftMousePressed && cursorInViewport && !imguiWantsMouse) {
        Vector3 rayOrigin{};
        Vector3 rayDirection{};
        bool rayValid = false;
        CourseObjectBounds hit{};
        CourseObjectBounds localHit{};
        int pickedAxis = -1;
        bool picked = false;
        if ((rayValid = MakeScreenRay(
                viewportCursor,
                viewportWidth,
                viewportHeight,
                frameState_.viewProjectionMatrix,
                rayOrigin,
                rayDirection))) {
            CourseObjectBounds selectedBounds{};
            const bool selectedBoundsAvailable = BuildSelectedCourseObjectBounds(
                    railShooterCourse_,
                    railPath_,
                    editor,
                    selectedBounds);
            if (selectedBoundsAvailable && editor.courseObjectPivotMode == 1) {
                const std::vector<int>& indices = selectedBounds.type == 0
                    ? editor.selectedCourseTerrainPlacements
                    : editor.selectedCourseRockClusters;
                Vector3 centerSum{};
                uint32_t centerCount = 0;
                for (const int index : indices) {
                    CourseObjectBounds itemBounds{};
                    const bool built = selectedBounds.type == 0
                        ? (index >= 0 && index < static_cast<int>(railShooterCourse_.terrainPlacements.size()) &&
                            BuildCourseTerrainPlacementBounds(
                                railShooterCourse_.terrainPlacements[static_cast<std::size_t>(index)],
                                index, railPath_, itemBounds))
                        : (index >= 0 && index < static_cast<int>(railShooterCourse_.rockClusters.size()) &&
                            BuildCourseRockClusterBounds(
                                railShooterCourse_.rockClusters[static_cast<std::size_t>(index)],
                                index, railPath_, itemBounds));
                    if (built) {
                        centerSum = Add(centerSum, itemBounds.center);
                        ++centerCount;
                    }
                }
                if (centerCount > 0) selectedBounds.center = Scale(centerSum, 1.0f / centerCount);
            }
            localHit = selectedBounds;
            if (selectedBoundsAvailable && editor.courseObjectGizmoSpace == 0) {
                selectedBounds.axisX = {1.0f, 0.0f, 0.0f};
                selectedBounds.axisY = {0.0f, 1.0f, 0.0f};
                selectedBounds.axisZ = {0.0f, 0.0f, 1.0f};
            }
            if (selectedBoundsAvailable &&
                PickCourseObjectGizmoAxis(
                    selectedBounds,
                    rayOrigin,
                    rayDirection,
                    editor.courseObjectFramePadding,
                    editor.courseObjectGizmoMode,
                    pickedAxis)) {
                hit = selectedBounds;
                picked = true;
            } else {
                picked = PickCourseObject(
                    railShooterCourse_,
                    railPath_,
                    rayOrigin,
                    rayDirection,
                    editor.courseObjectFramePadding,
                    hit);
                localHit = hit;
            }
        }
        if (picked) {
            editor.courseObjectSelectionType = hit.type;
            editor.selectedCourseTerrainPlacement = hit.type == 0 ? hit.index : -1;
            editor.selectedCourseRockCluster = hit.type == 1 ? hit.index : -1;
            editor.courseObjectActiveAxis = pickedAxis;

            courseObjectDrag_ = {};
            courseObjectDrag_.active = true;
            courseObjectDrag_.type = hit.type;
            courseObjectDrag_.index = hit.index;
            courseObjectDrag_.axis = pickedAxis;
            courseObjectDrag_.gizmoMode = editor.courseObjectGizmoMode;
            courseObjectDrag_.startMouse = viewportCursor;
            courseObjectDrag_.pivotWorld = hit.center;
            courseObjectDrag_.localAxes[0] = localHit.axisX;
            courseObjectDrag_.localAxes[1] = localHit.axisY;
            courseObjectDrag_.localAxes[2] = localHit.axisZ;
            courseObjectDrag_.handleAxes[0] = hit.axisX;
            courseObjectDrag_.handleAxes[1] = hit.axisY;
            courseObjectDrag_.handleAxes[2] = hit.axisZ;
            courseObjectDrag_.handleLength = CourseObjectGizmoLength(
                hit, editor.courseObjectFramePadding);
            if (hit.type == 0 &&
                hit.index >= 0 &&
                hit.index < static_cast<int>(railShooterCourse_.terrainPlacements.size())) {
                const CourseTerrainPlacement& placement =
                    railShooterCourse_.terrainPlacements[static_cast<size_t>(hit.index)];
                courseObjectDrag_.startDistance = placement.distance;
                courseObjectDrag_.startLateral = placement.lateralOffset;
                courseObjectDrag_.startVertical = placement.verticalOffset;
                courseObjectDrag_.startForward = placement.forwardOffset;
                courseObjectDrag_.startScale = placement.scale;
                courseObjectDrag_.startRotation = placement.rotation;
            } else if (hit.type == 1 &&
                hit.index >= 0 &&
                hit.index < static_cast<int>(railShooterCourse_.rockClusters.size())) {
                const CourseRockCluster& cluster =
                    railShooterCourse_.rockClusters[static_cast<size_t>(hit.index)];
                courseObjectDrag_.startDistance = cluster.distance;
                courseObjectDrag_.startMinScale = cluster.minScale;
                courseObjectDrag_.startMaxScale = cluster.maxScale;
                courseObjectDrag_.startSpread = cluster.spread;
                courseObjectDrag_.startClearLaneRadius = cluster.clearLaneRadius;
                courseObjectDrag_.startRotation = cluster.rotation;
            }
            const std::vector<int>& selectedIndices = hit.type == 0
                ? editor.selectedCourseTerrainPlacements
                : editor.selectedCourseRockClusters;
            const bool primaryInSelection = std::find(
                selectedIndices.begin(), selectedIndices.end(), hit.index) != selectedIndices.end();
            const auto captureItem = [&](int index) {
                CourseObjectDragState::Item item{};
                item.type = hit.type;
                item.index = index;
                if (hit.type == 0 && index >= 0 &&
                    index < static_cast<int>(railShooterCourse_.terrainPlacements.size())) {
                    const CourseTerrainPlacement& value =
                        railShooterCourse_.terrainPlacements[static_cast<std::size_t>(index)];
                    item.distance = value.distance;
                    item.lateral = value.lateralOffset;
                    item.vertical = value.verticalOffset;
                    item.forward = value.forwardOffset;
                    item.scale = value.scale;
                    item.rotation = value.rotation;
                    courseObjectDrag_.items.push_back(item);
                } else if (hit.type == 1 && index >= 0 &&
                    index < static_cast<int>(railShooterCourse_.rockClusters.size())) {
                    const CourseRockCluster& value =
                        railShooterCourse_.rockClusters[static_cast<std::size_t>(index)];
                    item.distance = value.distance;
                    item.minScale = value.minScale;
                    item.maxScale = value.maxScale;
                    item.spread = value.spread;
                    item.rotation = value.rotation;
                    item.clearLaneRadius = value.clearLaneRadius;
                    courseObjectDrag_.items.push_back(item);
                }
            };
            if (primaryInSelection) {
                for (const int index : selectedIndices) captureItem(index);
            } else {
                captureItem(hit.index);
            }
            if (editor.courseObjectPivotMode == 1 && courseObjectDrag_.items.size() > 1) {
                Vector3 sum{};
                uint32_t count = 0;
                for (const CourseObjectDragState::Item& item : courseObjectDrag_.items) {
                    CourseObjectBounds itemBounds{};
                    const bool built = item.type == 0
                        ? BuildCourseTerrainPlacementBounds(
                            railShooterCourse_.terrainPlacements[static_cast<std::size_t>(item.index)],
                            item.index, railPath_, itemBounds)
                        : BuildCourseRockClusterBounds(
                            railShooterCourse_.rockClusters[static_cast<std::size_t>(item.index)],
                            item.index, railPath_, itemBounds);
                    if (built) {
                        sum = Add(sum, itemBounds.center);
                        ++count;
                    }
                }
                if (count > 0) courseObjectDrag_.pivotWorld = Scale(sum, 1.0f / count);
            }
            if (rayValid && pickedAxis >= 0 && pickedAxis <= 2) {
                courseObjectDrag_.constraintValid = editor::ClosestEditorGizmoRayAxisParameter(
                    rayOrigin,
                    rayDirection,
                    courseObjectDrag_.pivotWorld,
                    courseObjectDrag_.handleAxes[pickedAxis],
                    &courseObjectDrag_.startAxisParameter);
                if (editor.courseObjectGizmoMode == 2) {
                    courseObjectDrag_.constraintPlaneNormal =
                        courseObjectDrag_.handleAxes[pickedAxis];
                    courseObjectDrag_.constraintValid = editor::IntersectEditorGizmoRayPlane(
                        rayOrigin,
                        rayDirection,
                        courseObjectDrag_.pivotWorld,
                        courseObjectDrag_.constraintPlaneNormal,
                        &courseObjectDrag_.startConstraintPoint);
                }
            } else if (rayValid && pickedAxis >= 3 && pickedAxis <= 5) {
                constexpr int planePairs[3][2] = {{0, 1}, {1, 2}, {2, 0}};
                const int plane = pickedAxis - 3;
                courseObjectDrag_.constraintPlaneNormal = NormalizeOr(
                    Cross(
                        courseObjectDrag_.handleAxes[planePairs[plane][0]],
                        courseObjectDrag_.handleAxes[planePairs[plane][1]]),
                    {0.0f, 1.0f, 0.0f});
                courseObjectDrag_.constraintValid = editor::IntersectEditorGizmoRayPlane(
                    rayOrigin,
                    rayDirection,
                    courseObjectDrag_.pivotWorld,
                    courseObjectDrag_.constraintPlaneNormal,
                    &courseObjectDrag_.startConstraintPoint);
            }
            if (!BeginCourseObjectGizmoEditSession()) {
                courseObjectDrag_.active = false;
                editor.courseObjectActiveAxis = -1;
            }
        } else {
            editor.selectedCourseTerrainPlacement = -1;
            editor.selectedCourseRockCluster = -1;
            editor.courseObjectActiveAxis = -1;
            CancelCourseObjectDragIfNeeded();
        }
    }

    if (courseObjectDrag_.active && (GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0) {
        CancelCourseObjectDragIfNeeded();
        previousCourseEditorLeftMouseDown_ = leftMouseDown;
        return;
    }

    if (courseObjectDrag_.active && leftMouseDown) {
        if (!cursorInViewport) {
            CancelCourseObjectDragIfNeeded();
            previousCourseEditorLeftMouseDown_ = leftMouseDown;
            return;
        }
        const float dx = static_cast<float>(viewportCursor.x - courseObjectDrag_.startMouse.x);
        const float dy = static_cast<float>(viewportCursor.y - courseObjectDrag_.startMouse.y);
        const bool shiftDown = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
        const float moveSensitivity = (std::max)(0.001f, editor.courseObjectMoveSensitivity);
        const float scaleSensitivity = (std::max)(0.0001f, editor.courseObjectScaleSensitivity);
        const bool scaleMode = editor.courseObjectGizmoMode == 1;
        const bool rotateMode = editor.courseObjectGizmoMode == 2;
        float scaleFactor = (std::max)(0.05f, 1.0f + (dx - dy) * scaleSensitivity);
        const float signedDrag = dx - dy;
        Vector3 localTranslationDelta{
            dx * moveSensitivity,
            -dy * moveSensitivity,
            -dy * moveSensitivity};
        float rotationDelta = signedDrag *
            (std::max)(0.0001f, editor.courseObjectRotateSensitivity);
        const int axis = courseObjectDrag_.axis;
        Vector3 currentRayOrigin{};
        Vector3 currentRayDirection{};
        if (courseObjectDrag_.constraintValid &&
            MakeScreenRay(
                viewportCursor,
                viewportWidth,
                viewportHeight,
                frameState_.viewProjectionMatrix,
                currentRayOrigin,
                currentRayDirection)) {
            if (rotateMode && axis >= 0 && axis <= 2) {
                Vector3 currentPoint{};
                if (editor::IntersectEditorGizmoRayPlane(
                        currentRayOrigin,
                        currentRayDirection,
                        courseObjectDrag_.pivotWorld,
                        courseObjectDrag_.constraintPlaneNormal,
                        &currentPoint)) {
                    const Vector3 before = NormalizeOr(
                        Subtract(
                            courseObjectDrag_.startConstraintPoint,
                            courseObjectDrag_.pivotWorld),
                        courseObjectDrag_.handleAxes[(std::max)(0, (axis + 1) % 3)]);
                    const Vector3 after = NormalizeOr(
                        Subtract(currentPoint, courseObjectDrag_.pivotWorld), before);
                    rotationDelta = editor::EditorGizmoSignedAngle(
                        before, after, courseObjectDrag_.constraintPlaneNormal);
                }
            } else if (axis >= 0 && axis <= 2) {
                float currentParameter = 0.0f;
                if (editor::ClosestEditorGizmoRayAxisParameter(
                        currentRayOrigin,
                        currentRayDirection,
                        courseObjectDrag_.pivotWorld,
                        courseObjectDrag_.handleAxes[axis],
                        &currentParameter)) {
                    const Vector3 worldDelta = Scale(
                        courseObjectDrag_.handleAxes[axis],
                        currentParameter - courseObjectDrag_.startAxisParameter);
                    localTranslationDelta = editor::ProjectEditorGizmoWorldDeltaToBasis(
                        worldDelta,
                        courseObjectDrag_.localAxes[0],
                        courseObjectDrag_.localAxes[1],
                        courseObjectDrag_.localAxes[2]);
                    if (scaleMode) {
                        scaleFactor = (std::max)(0.05f,
                            1.0f + (currentParameter - courseObjectDrag_.startAxisParameter) /
                                (std::max)(0.01f, courseObjectDrag_.handleLength));
                    }
                }
            } else if (axis >= 3 && axis <= 5) {
                Vector3 currentPoint{};
                if (editor::IntersectEditorGizmoRayPlane(
                        currentRayOrigin,
                        currentRayDirection,
                        courseObjectDrag_.pivotWorld,
                        courseObjectDrag_.constraintPlaneNormal,
                        &currentPoint)) {
                    const Vector3 worldDelta = Subtract(
                        currentPoint, courseObjectDrag_.startConstraintPoint);
                    localTranslationDelta = editor::ProjectEditorGizmoWorldDeltaToBasis(
                        worldDelta,
                        courseObjectDrag_.localAxes[0],
                        courseObjectDrag_.localAxes[1],
                        courseObjectDrag_.localAxes[2]);
                    if (scaleMode) {
                        constexpr int planePairs[3][2] = {{0, 1}, {1, 2}, {2, 0}};
                        const int plane = axis - 3;
                        const float signedDistance =
                            Dot(worldDelta, courseObjectDrag_.handleAxes[planePairs[plane][0]]) +
                            Dot(worldDelta, courseObjectDrag_.handleAxes[planePairs[plane][1]]);
                        scaleFactor = (std::max)(0.05f,
                            1.0f + signedDistance /
                                (std::max)(0.01f, courseObjectDrag_.handleLength));
                    }
                }
            }
        }
        bool changed = false;

        if (courseObjectDrag_.type == 0 &&
            courseObjectDrag_.index >= 0 &&
            courseObjectDrag_.index < static_cast<int>(railShooterCourse_.terrainPlacements.size())) {
            CourseTerrainPlacement& placement =
                railShooterCourse_.terrainPlacements[static_cast<size_t>(courseObjectDrag_.index)];
            const std::size_t index = static_cast<std::size_t>(courseObjectDrag_.index);
            const editor::EditorObjectHandle target =
                MakeCourseGizmoHandle(
                    editor::EditorDomainId::CourseTerrainPlacement,
                    "course-terrain",
                    "Course Terrain",
                    index,
                    runtimeState_.terrain.courseObjectEditRevision);
            if (rotateMode) {
                Vector3 rotation = courseObjectDrag_.startRotation;
                float delta = rotationDelta;
                if (editor.courseObjectSnapEnabled) {
                    const float snapRadians =
                        editor.courseObjectRotateSnapDegrees * 3.14159265358979323846f / 180.0f;
                    delta = SnapCourseObjectValue(delta, snapRadians);
                }
                const int rotateAxis = axis >= 0 ? axis : 1;
                if (editor.courseObjectGizmoSpace == 0 && rotateAxis >= 0 && rotateAxis <= 2) {
                    const Vector3& worldAxis = courseObjectDrag_.handleAxes[rotateAxis];
                    rotation.x += delta * Dot(worldAxis, courseObjectDrag_.localAxes[0]);
                    rotation.y += delta * Dot(worldAxis, courseObjectDrag_.localAxes[1]);
                    rotation.z += delta * Dot(worldAxis, courseObjectDrag_.localAxes[2]);
                } else if (rotateAxis == 0) {
                    rotation.x = courseObjectDrag_.startRotation.x + delta;
                } else if (rotateAxis == 1) {
                    rotation.y = courseObjectDrag_.startRotation.y + delta;
                } else {
                    rotation.z = courseObjectDrag_.startRotation.z + delta;
                }
                changed =
                    placement.rotation.x != rotation.x ||
                    placement.rotation.y != rotation.y ||
                    placement.rotation.z != rotation.z;
                changed =
                    PreviewCourseObjectGizmoEditSession(
                        {
                            editor::EditorPropertyEditSessionValue{
                                target,
                                "CourseTerrainPlacement.rotation",
                                CourseGizmoVec3Value(CourseGizmoRotationDegrees(rotation))},
                        }) || changed;
            } else if (scaleMode) {
                Vector3 scale = Scale(courseObjectDrag_.startScale, scaleFactor);
                if (editor.courseObjectGizmoSpace == 0 && axis >= 0 && axis <= 2) {
                    const Vector3& worldAxis = courseObjectDrag_.handleAxes[axis];
                    scale = courseObjectDrag_.startScale;
                    scale.x *= 1.0f + (scaleFactor - 1.0f) *
                        std::abs(Dot(worldAxis, courseObjectDrag_.localAxes[0]));
                    scale.y *= 1.0f + (scaleFactor - 1.0f) *
                        std::abs(Dot(worldAxis, courseObjectDrag_.localAxes[1]));
                    scale.z *= 1.0f + (scaleFactor - 1.0f) *
                        std::abs(Dot(worldAxis, courseObjectDrag_.localAxes[2]));
                } else if (axis == 0) {
                    scale = courseObjectDrag_.startScale;
                    scale.x = courseObjectDrag_.startScale.x * scaleFactor;
                } else if (axis == 1) {
                    scale = courseObjectDrag_.startScale;
                    scale.y = courseObjectDrag_.startScale.y * scaleFactor;
                } else if (axis == 2) {
                    scale = courseObjectDrag_.startScale;
                    scale.z = courseObjectDrag_.startScale.z * scaleFactor;
                } else if (axis == 3) {
                    scale = courseObjectDrag_.startScale;
                    scale.x *= scaleFactor;
                    scale.y *= scaleFactor;
                } else if (axis == 4) {
                    scale = courseObjectDrag_.startScale;
                    scale.y *= scaleFactor;
                    scale.z *= scaleFactor;
                } else if (axis == 5) {
                    scale = courseObjectDrag_.startScale;
                    scale.z *= scaleFactor;
                    scale.x *= scaleFactor;
                }
                if (editor.courseObjectSnapEnabled) {
                    scale = SnapCourseObjectVector(scale, editor.courseObjectScaleSnap);
                }
                scale.x = (std::max)(0.01f, scale.x);
                scale.y = (std::max)(0.01f, scale.y);
                scale.z = (std::max)(0.01f, scale.z);
                changed =
                    placement.scale.x != scale.x ||
                    placement.scale.y != scale.y ||
                    placement.scale.z != scale.z;
                changed =
                    PreviewCourseObjectGizmoEditSession(
                        {
                            editor::EditorPropertyEditSessionValue{
                                target,
                                "CourseTerrainPlacement.scale",
                                CourseGizmoVec3Value(scale)},
                        }) || changed;
            } else {
                float lateral = courseObjectDrag_.startLateral + localTranslationDelta.x;
                float vertical = courseObjectDrag_.startVertical + localTranslationDelta.y;
                float forward = courseObjectDrag_.startForward + localTranslationDelta.z;
                if (axis < 0 && !shiftDown) forward = courseObjectDrag_.startForward;
                if (axis < 0 && shiftDown) vertical = courseObjectDrag_.startVertical;
                if (editor.courseObjectSnapEnabled) {
                    lateral = SnapCourseObjectValue(lateral, editor.courseObjectMoveSnap);
                    vertical = SnapCourseObjectValue(vertical, editor.courseObjectMoveSnap);
                    forward = SnapCourseObjectValue(forward, editor.courseObjectMoveSnap);
                }
                changed =
                    placement.lateralOffset != lateral ||
                    placement.verticalOffset != vertical ||
                    placement.forwardOffset != forward;
                changed =
                    PreviewCourseObjectGizmoEditSession(
                        {
                            editor::EditorPropertyEditSessionValue{
                                target,
                                "CourseTerrainPlacement.lateralOffset",
                                CourseGizmoFloatValue(lateral)},
                            editor::EditorPropertyEditSessionValue{
                                target,
                                "CourseTerrainPlacement.verticalOffset",
                                CourseGizmoFloatValue(vertical)},
                            editor::EditorPropertyEditSessionValue{
                                target,
                                "CourseTerrainPlacement.forwardOffset",
                                CourseGizmoFloatValue(forward)},
                        }) || changed;
            }
        } else if (courseObjectDrag_.type == 1 &&
            courseObjectDrag_.index >= 0 &&
            courseObjectDrag_.index < static_cast<int>(railShooterCourse_.rockClusters.size())) {
            CourseRockCluster& cluster =
                railShooterCourse_.rockClusters[static_cast<size_t>(courseObjectDrag_.index)];
            const std::size_t index = static_cast<std::size_t>(courseObjectDrag_.index);
            const editor::EditorObjectHandle target =
                MakeCourseGizmoHandle(
                    editor::EditorDomainId::CourseRockCluster,
                    "course-rock",
                    "Course Rock Cluster",
                    index,
                    runtimeState_.terrain.courseObjectEditRevision);
            if (rotateMode) {
                Vector3 rotation = courseObjectDrag_.startRotation;
                float delta = rotationDelta;
                if (editor.courseObjectSnapEnabled) {
                    const float snapRadians =
                        editor.courseObjectRotateSnapDegrees * 3.14159265358979323846f / 180.0f;
                    delta = SnapCourseObjectValue(delta, snapRadians);
                }
                const int rotateAxis = axis >= 0 ? axis : 1;
                if (editor.courseObjectGizmoSpace == 0 && rotateAxis >= 0 && rotateAxis <= 2) {
                    const Vector3& worldAxis = courseObjectDrag_.handleAxes[rotateAxis];
                    rotation.x += delta * Dot(worldAxis, courseObjectDrag_.localAxes[0]);
                    rotation.y += delta * Dot(worldAxis, courseObjectDrag_.localAxes[1]);
                    rotation.z += delta * Dot(worldAxis, courseObjectDrag_.localAxes[2]);
                } else if (rotateAxis == 0) {
                    rotation.x = courseObjectDrag_.startRotation.x + delta;
                } else if (rotateAxis == 1) {
                    rotation.y = courseObjectDrag_.startRotation.y + delta;
                } else {
                    rotation.z = courseObjectDrag_.startRotation.z + delta;
                }
                changed =
                    cluster.rotation.x != rotation.x ||
                    cluster.rotation.y != rotation.y ||
                    cluster.rotation.z != rotation.z;
                changed =
                    PreviewCourseObjectGizmoEditSession(
                        {
                            editor::EditorPropertyEditSessionValue{
                                target,
                                "CourseRockCluster.rotation",
                                CourseGizmoVec3Value(CourseGizmoRotationDegrees(rotation))},
                        }) || changed;
            } else if (scaleMode) {
                float minScale = courseObjectDrag_.startMinScale * scaleFactor;
                float maxScale = courseObjectDrag_.startMaxScale * scaleFactor;
                Vector3 spread = Scale(courseObjectDrag_.startSpread, scaleFactor);
                if (editor.courseObjectGizmoSpace == 0 && axis >= 0 && axis <= 2) {
                    const Vector3& worldAxis = courseObjectDrag_.handleAxes[axis];
                    minScale = courseObjectDrag_.startMinScale;
                    maxScale = courseObjectDrag_.startMaxScale;
                    spread = courseObjectDrag_.startSpread;
                    spread.x *= 1.0f + (scaleFactor - 1.0f) *
                        std::abs(Dot(worldAxis, courseObjectDrag_.localAxes[0]));
                    spread.y *= 1.0f + (scaleFactor - 1.0f) *
                        std::abs(Dot(worldAxis, courseObjectDrag_.localAxes[1]));
                    spread.z *= 1.0f + (scaleFactor - 1.0f) *
                        std::abs(Dot(worldAxis, courseObjectDrag_.localAxes[2]));
                } else if (axis == 0) {
                    minScale = courseObjectDrag_.startMinScale;
                    maxScale = courseObjectDrag_.startMaxScale;
                    spread = courseObjectDrag_.startSpread;
                    spread.x = courseObjectDrag_.startSpread.x * scaleFactor;
                } else if (axis == 1) {
                    minScale = courseObjectDrag_.startMinScale;
                    maxScale = courseObjectDrag_.startMaxScale;
                    spread = courseObjectDrag_.startSpread;
                    spread.y = courseObjectDrag_.startSpread.y * scaleFactor;
                } else if (axis == 2) {
                    minScale = courseObjectDrag_.startMinScale;
                    maxScale = courseObjectDrag_.startMaxScale;
                    spread = courseObjectDrag_.startSpread;
                    spread.z = courseObjectDrag_.startSpread.z * scaleFactor;
                } else if (axis == 3) {
                    minScale = courseObjectDrag_.startMinScale;
                    maxScale = courseObjectDrag_.startMaxScale;
                    spread = courseObjectDrag_.startSpread;
                    spread.x *= scaleFactor;
                    spread.y *= scaleFactor;
                } else if (axis == 4) {
                    minScale = courseObjectDrag_.startMinScale;
                    maxScale = courseObjectDrag_.startMaxScale;
                    spread = courseObjectDrag_.startSpread;
                    spread.y *= scaleFactor;
                    spread.z *= scaleFactor;
                } else if (axis == 5) {
                    minScale = courseObjectDrag_.startMinScale;
                    maxScale = courseObjectDrag_.startMaxScale;
                    spread = courseObjectDrag_.startSpread;
                    spread.z *= scaleFactor;
                    spread.x *= scaleFactor;
                }
                if (editor.courseObjectSnapEnabled) {
                    minScale = SnapCourseObjectValue(minScale, editor.courseObjectScaleSnap);
                    maxScale = SnapCourseObjectValue(maxScale, editor.courseObjectScaleSnap);
                    spread = SnapCourseObjectVector(spread, editor.courseObjectMoveSnap);
                }
                minScale = (std::max)(0.01f, minScale);
                maxScale = (std::max)(minScale, maxScale);
                spread.x = (std::max)(0.0f, spread.x);
                spread.y = (std::max)(0.0f, spread.y);
                spread.z = (std::max)(0.0f, spread.z);
                changed =
                    cluster.minScale != minScale ||
                    cluster.maxScale != maxScale ||
                    cluster.spread.x != spread.x ||
                    cluster.spread.y != spread.y ||
                    cluster.spread.z != spread.z;
                changed =
                    PreviewCourseObjectGizmoEditSession(
                        {
                            editor::EditorPropertyEditSessionValue{
                                target,
                                "CourseRockCluster.minScale",
                                CourseGizmoFloatValue(minScale)},
                            editor::EditorPropertyEditSessionValue{
                                target,
                                "CourseRockCluster.maxScale",
                                CourseGizmoFloatValue(maxScale)},
                            editor::EditorPropertyEditSessionValue{
                                target,
                                "CourseRockCluster.spread",
                                CourseGizmoVec3Value(spread)},
                        }) || changed;
            } else {
                float clearLane = courseObjectDrag_.startClearLaneRadius + localTranslationDelta.x;
                float distance = courseObjectDrag_.startDistance + localTranslationDelta.z;
                Vector3 spread = courseObjectDrag_.startSpread;
                spread.y += localTranslationDelta.y;
                if (axis < 0 && !shiftDown) distance = courseObjectDrag_.startDistance;
                if (axis < 0 && shiftDown) spread.y = courseObjectDrag_.startSpread.y;
                if (editor.courseObjectSnapEnabled) {
                    clearLane = SnapCourseObjectValue(clearLane, editor.courseObjectMoveSnap);
                    distance = SnapCourseObjectValue(distance, editor.courseObjectMoveSnap);
                    spread = SnapCourseObjectVector(spread, editor.courseObjectMoveSnap);
                }
                clearLane = (std::max)(0.0f, clearLane);
                distance = (std::clamp)(distance, 0.0f, (std::max)(0.0f, railPath_.Length()));
                spread.x = (std::max)(0.0f, spread.x);
                spread.y = (std::max)(0.0f, spread.y);
                spread.z = (std::max)(0.0f, spread.z);
                changed =
                    cluster.clearLaneRadius != clearLane ||
                    cluster.distance != distance ||
                    cluster.spread.x != spread.x ||
                    cluster.spread.y != spread.y ||
                    cluster.spread.z != spread.z;
                changed =
                    PreviewCourseObjectGizmoEditSession(
                        {
                            editor::EditorPropertyEditSessionValue{
                                target,
                                "CourseRockCluster.clearLaneRadius",
                                CourseGizmoFloatValue(clearLane)},
                            editor::EditorPropertyEditSessionValue{
                                target,
                                "CourseRockCluster.distance",
                                CourseGizmoFloatValue(distance)},
                            editor::EditorPropertyEditSessionValue{
                                target,
                                "CourseRockCluster.spread",
                                CourseGizmoVec3Value(spread)},
                        }) || changed;
            }
        }

        if (changed) {
            courseObjectDrag_.changed = true;
        }
    }

    if (leftMouseReleased) {
        CommitCourseObjectDragIfNeeded();
    }
    previousCourseEditorLeftMouseDown_ = leftMouseDown;
    CommitCourseObjectHistoryIfNeeded();
}

void AppRunLoop::ClearShowcaseEffects() {
    runtimeState_.vfx.electricOrbStrikeActive = false;
    runtimeState_.vfx.electricOrbStrikeLoop = false;
    runtimeState_.vfx.electricOrbStrikeTimer = 0.0f;
    runtimeState_.vfx.iceProjectilePreviewActive = false;
    runtimeState_.vfx.iceProjectileImpactSpawned = false;
    runtimeState_.vfx.iceProjectileInstanceId = 0;
    runtimeState_.vfx.iceProjectileTimer = 0.0f;
    for (AppVfxRuntimeState::IceProjectileShotState& shot : runtimeState_.vfx.iceProjectileShots) {
        shot = {};
    }
    vfxEngine_.Runtime().ClearInstances();
}

void AppRunLoop::ConfigureShowcasePostProcess() {
    if (runtimeState_.submissionShowcase.enabled) {
        PostProcessStack& stack = vfxEngine_.PostProcess();
        // DistanceFog is authored for the canyon rail course and turns a dark
        // presentation clear color into a flat grey field. ContactAO also
        // over-darkens this close-up model because its depth radius targets the
        // much larger rail scene. Keep the neutral scene/VFX/tone/glow chain.
        stack.SetEnabled("DistortionComposite", true);
        stack.SetEnabled("ContactAO", false);
        stack.SetEnabled("DistanceFog", false);
        stack.SetEnabled("AccretionComposite", false);
        stack.SetEnabled("ToneMapping", true);
        stack.SetEnabled("GlowComposite", true);
        stack.SetIntensity("DistortionComposite", 1.0f);
        stack.SetIntensity("ToneMapping", 1.0f);
        stack.SetIntensity("GlowComposite", 0.94f);
        for (PostProcessPass pass : stack.Passes()) {
            if (pass.name == "ToneMapping") {
                pass.parameters.toneExposure = 1.12f;
            } else if (pass.name == "GlowComposite") {
                pass.parameters.glowWeight = 0.82f;
                pass.parameters.glowTintR = 0.96f;
                pass.parameters.glowTintG = 0.98f;
                pass.parameters.glowTintB = 1.08f;
            } else if (pass.name == "DistortionComposite") {
                pass.parameters.distortionScale = 0.0f;
            }

        (void)stack.ConfigurePass(pass);
    }
        return;
    }

    const bool blackHole =
        runtimeState_.vfx.showcaseEffect == AppVfxRuntimeState::ShowcaseEffect::BlackHole;
    AppVfxRuntimeState::ShowcaseTuning& tuning =
        runtimeState_.vfx.showcaseTuning[ShowcaseIndex(runtimeState_.vfx.showcaseEffect)];

    vfxEngine_.PostProcess().SetEnabled("AccretionComposite", blackHole);
    vfxEngine_.PostProcess().SetIntensity("AccretionComposite", blackHole ? tuning.param4 : 1.0f);
    vfxEngine_.PostProcess().SetIntensity("GlowComposite", blackHole ? (0.92f + tuning.param4 * 0.42f) : 1.0f);
    vfxEngine_.PostProcess().SetIntensity("DistortionComposite", blackHole ? (0.85f + tuning.param3 * 0.58f) : 1.0f);

    for (PostProcessPass pass : vfxEngine_.PostProcess().Passes()) {
        if (pass.name == "AccretionComposite") {
            pass.parameters.accretionRadius = 0.30f + tuning.param2 * 0.14f;
            pass.parameters.accretionDiskStretch = 1.65f + tuning.param2 * 0.92f;
            pass.parameters.accretionTurbulence = 0.48f + tuning.param1 * 0.52f;
            pass.parameters.accretionChromaticAberration = 0.42f + tuning.param3 * 0.62f;
            pass.parameters.accretionCoreSize = 0.10f + tuning.param1 * 0.075f;
            pass.parameters.accretionCoreDarkness = 0.78f + tuning.param1 * 0.15f;
            pass.parameters.accretionLensStrength = 0.44f + tuning.param3 * 0.82f;
            pass.parameters.accretionGuideOpacity = 0.42f + tuning.param4 * 0.62f;
            pass.parameters.accretionGuideWidth = 0.08f + tuning.param2 * 0.08f;
        } else if (pass.name == "DistortionComposite") {
            pass.parameters.distortionScale = blackHole ? (0.010f + tuning.param3 * 0.026f) : 0.020f;
        }

        (void)vfxEngine_.PostProcess().ConfigurePass(pass);
    }
}

void AppRunLoop::FireShowcaseIceProjectile() {
    runtimeState_.vfx.iceProjectileStart = {-2.15f, -1.28f, -3.05f};
    runtimeState_.vfx.iceProjectileTarget = {2.20f, 0.58f, 0.42f};
    runtimeState_.vfx.iceProjectilePreviewActive = true;
    runtimeState_.vfx.iceProjectileImpactSpawned = false;
    runtimeState_.vfx.iceProjectileInstanceId = 0;
    runtimeState_.vfx.iceProjectileTimer = 0.0f;
}

void AppRunLoop::ConfigureRenderGraphDebugDump() {
    if (!renderGraphDumpConfigured_) {
        renderGraphDumpConfigured_ = true;
        renderGraphDumpFrameLimit_ = ReadEnvironmentUInt("GE3_RENDERGRAPH_DUMP_FRAMES", 0);
        const std::filesystem::path dumpRequestPath{"logs/rendergraph_dump_request.txt"};
        if (renderGraphDumpFrameLimit_ == 0 && std::filesystem::exists(dumpRequestPath)) {
            renderGraphDumpFrameLimit_ = 180;
            std::ifstream request(dumpRequestPath);
            if (request) {
                uint32_t requestedFrames = 0;
                request >> requestedFrames;
                if (requestedFrames > 0) {
                    renderGraphDumpFrameLimit_ = requestedFrames;
                }
            }
        }
        renderGraphDumpEnabled_ = renderGraphDumpFrameLimit_ > 0;
        if (renderGraphDumpEnabled_) {
            std::filesystem::create_directories("logs");
            renderGraphDump_.open("logs/rendergraph_debug.csv", std::ios::out | std::ios::trunc);
            if (!renderGraphDump_) {
                renderGraphDumpEnabled_ = false;
            } else {
                renderGraphDump_ <<
                    "frame,totalPasses,executedPasses,vfxRegistered,vfxExecuted,vfxExecutedPasses,"
                    "terrainRenderChunks,terrainDebrisInstances,terrainEligibleCullChunks,"
                    "terrainHiZBuilds,terrainHiZMipDispatches,terrainDebrisCullDispatches\n";
                renderGraphDump_.flush();
            }
        }
    }
}

void AppRunLoop::DumpRenderGraphDebugFrame() {
    ConfigureRenderGraphDebugDump();
    if (!renderGraphDumpEnabled_ ||
        renderGraphDumpFrameIndex_ >= renderGraphDumpFrameLimit_ ||
        !renderGraphDump_) {
        return;
    }

    uint32_t executedPasses = 0;
    uint32_t vfxRegistered = 0;
    uint32_t vfxExecuted = 0;
    std::ostringstream vfxNames;
    bool firstVfx = true;
    for (const ge3::graphics::RenderPassDebugInfo& pass : lastRenderPassDebugInfo_) {
        if (pass.executed) {
            ++executedPasses;
        }
        if (pass.layer != ge3::graphics::RenderPassLayer::Vfx) {
            continue;
        }
        ++vfxRegistered;
        if (!pass.executed) {
            continue;
        }
        ++vfxExecuted;
        if (!firstVfx) {
            vfxNames << "|";
        }
        firstVfx = false;
        vfxNames << pass.name;
    }

    const TerrainDebrisCullingStats& debrisStats = terrainChunkManager_.LastDebrisCullingStats();
    renderGraphDump_ <<
        renderGraphDumpFrameIndex_ << "," <<
        lastRenderPassDebugInfo_.size() << "," <<
        executedPasses << "," <<
        vfxRegistered << "," <<
        vfxExecuted << "," <<
        CsvQuote(vfxNames.str()) << "," <<
        debrisStats.renderChunkCount << "," <<
        debrisStats.debrisInstanceCount << "," <<
        debrisStats.eligibleChunkCount << "," <<
        debrisStats.hiZBuildCount << "," <<
        debrisStats.hiZMipDispatchCount << "," <<
        debrisStats.debrisCullDispatchCount << "\n";
    renderGraphDump_.flush();
    ++renderGraphDumpFrameIndex_;
}

void AppRunLoop::PlayShowcaseEffect(AppVfxRuntimeState::ShowcaseEffect effect, bool resetAutoTimer) {
    runtimeState_.vfx.showcaseMode = true;
    runtimeState_.vfx.showcaseEffect = effect;
    runtimeState_.vfx.autoPlayVfxDemo = false;
    runtimeState_.vfx.iceProjectileClickToFire =
        effect == AppVfxRuntimeState::ShowcaseEffect::IceProjectile;
    runtimeState_.vfx.enableTrailMeshStream = true;
    runtimeState_.vfx.enableTrailMeshStreamAutoFallback = false;
    runtimeState_.vfx.trailMeshStreamFallbackActive = false;
    runtimeState_.useMonsterBall = false;
    runtimeState_.showAnimatedCube = false;
    runtimeState_.showSkinnedModel = false;
    runtimeState_.showSkeletonDebug = false;
    runtimeState_.showSkybox = false;
    runtimeState_.showProceduralBackdrop = true;
    runtimeState_.showVfxModelObjects = false;
    runtimeState_.clearColor[0] = 0.78f;
    runtimeState_.clearColor[1] = 0.76f;
    runtimeState_.clearColor[2] = 0.74f;
    runtimeState_.clearColor[3] = 1.0f;
    runtimeState_.directionalLightData.color = {0.55f, 0.7f, 1.0f, 1.0f};
    runtimeState_.directionalLightData.direction = {0.25f, -1.0f, 0.2f};
    runtimeState_.directionalLightData.intensity = 0.12f;
    runtimeState_.pointLightData.color = {0.35f, 0.65f, 1.0f, 1.0f};
    runtimeState_.pointLightData.intensity = 0.0f;
    runtimeState_.pointLightData.radius = 6.0f;
    runtimeState_.pointLightData.decay = 2.0f;

    ClearShowcaseEffects();

    switch (effect) {
    case AppVfxRuntimeState::ShowcaseEffect::ElectricOrbStrike:
        runtimeState_.vfx.enableParticles = false;
        runtimeState_.vfx.enableTrails = false;
        runtimeState_.vfx.enableBeams = false;
        runtimeState_.vfx.enableDistortions = false;
        runtimeState_.vfx.enableRings = false;
        runtimeState_.vfx.enableCylinders = false;
        runtimeState_.vfx.enableElectricOrbStrike = true;
        runtimeState_.vfx.electricOrbStrikeActive = true;
        runtimeState_.vfx.electricOrbStrikeDuration = 4.25f;
        break;
    case AppVfxRuntimeState::ShowcaseEffect::IceProjectile:
        runtimeState_.vfx.enableParticles = true;
        runtimeState_.vfx.enableTrails = true;
        runtimeState_.vfx.enableBeams = false;
        runtimeState_.vfx.enableDistortions = false;
        runtimeState_.vfx.enableRings = true;
        runtimeState_.vfx.enableCylinders = true;
        runtimeState_.vfx.enableElectricOrbStrike = false;
        break;
    case AppVfxRuntimeState::ShowcaseEffect::BlackHole: {
        runtimeState_.vfx.enableParticles = false;
        runtimeState_.vfx.enableTrails = false;
        runtimeState_.vfx.enableBeams = false;
        runtimeState_.vfx.enableDistortions = true;
        runtimeState_.vfx.enableRings = false;
        runtimeState_.vfx.enableCylinders = false;
        runtimeState_.vfx.enableElectricOrbStrike = false;
        break;
    }
    default:
        break;
    }

    ConfigureShowcasePostProcess();
    if (resetAutoTimer) {
        runtimeState_.vfx.showcaseAutoTimer =
            effect == AppVfxRuntimeState::ShowcaseEffect::ElectricOrbStrike ? 4.75f :
            effect == AppVfxRuntimeState::ShowcaseEffect::IceProjectile ? 8.00f :
            5.20f;
    }
    releaseShowcaseTitleDirty_ = true;
}

void AppRunLoop::UpdateShowcaseWindowTitle() {
    if (hwnd_ == nullptr || !releaseShowcaseTitleDirty_) {
        return;
    }
    releaseShowcaseTitleDirty_ = false;

    if (!runtimeState_.vfx.showcaseHudVisible) {
        SetWindowTextA(hwnd_, "CG5 Showcase");
        return;
    }

    const AppVfxRuntimeState::ShowcaseEffect effect = runtimeState_.vfx.showcaseEffect;
    char title[512] = {};
    std::snprintf(
        title,
        sizeof(title),
        "CG5 Showcase - %s | 1 Electric Orb  2 Ice Projectile  3 Black Hole | Ice: Left Click",
        ShowcaseEffectName(effect));
    SetWindowTextA(hwnd_, title);
}

void AppRunLoop::ProcessReleaseShowcaseControls(float deltaTime) {
#if defined(GE3_ENABLE_IMGUI) && GE3_ENABLE_IMGUI
    (void)deltaTime;
    return;
#else
    (void)deltaTime;
    if (!releaseShowcaseInitialized_) {
        releaseShowcaseInitialized_ = true;
        runtimeState_.vfx.showcaseHudVisible = true;
        runtimeState_.vfx.showcaseTuningVisible = false;
        runtimeState_.vfx.showcaseAutoRotate = false;
        PlayShowcaseEffect(AppVfxRuntimeState::ShowcaseEffect::ElectricOrbStrike, true);
    }

    if (WasKeyPressed(VK_ESCAPE)) {
        PostQuitMessage(0);
        return;
    }
    if (WasKeyPressed('1')) {
        PlayShowcaseEffect(AppVfxRuntimeState::ShowcaseEffect::ElectricOrbStrike, true);
    }
    if (WasKeyPressed('2')) {
        PlayShowcaseEffect(AppVfxRuntimeState::ShowcaseEffect::IceProjectile, true);
    }
    if (WasKeyPressed('3')) {
        PlayShowcaseEffect(AppVfxRuntimeState::ShowcaseEffect::BlackHole, true);
    }
    UpdateShowcaseWindowTitle();
#endif
}

void AppRunLoop::ProcessIceProjectileMouseLaunch() {
    const bool leftMouseDown = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
    railInputRouteDebug_.leftMouseDown = leftMouseDown;
    railInputRouteDebug_.railSceneActive = IsRailShooterSceneActive();
    railInputRouteDebug_.showcaseClickToFireEnabled = runtimeState_.vfx.iceProjectileClickToFire;
    railInputRouteDebug_.showcaseClickBlockedInRail = false;
    railInputRouteDebug_.showcaseClickFired = false;
    railInputRouteDebug_.showcaseClickIgnoredByImgui = false;

    if (!runtimeState_.vfx.iceProjectileClickToFire || hwnd_ == nullptr) {
        previousLeftMouseDown_ = leftMouseDown;
        return;
    }
    if (railInputRouteDebug_.railSceneActive) {
        railInputRouteDebug_.showcaseClickBlockedInRail = true;
        previousLeftMouseDown_ = leftMouseDown;
        return;
    }

    const bool clicked = leftMouseDown && !previousLeftMouseDown_;
    previousLeftMouseDown_ = leftMouseDown;
    if (!clicked) {
        return;
    }
#if defined(GE3_ENABLE_IMGUI) && GE3_ENABLE_IMGUI
    if (imguiLayer_.IsVisible() && ImGui::GetIO().WantCaptureMouse) {
        railInputRouteDebug_.showcaseClickIgnoredByImgui = true;
        return;
    }
#endif

    POINT cursor{};
    if (!GetCursorPos(&cursor) || !ScreenToClient(hwnd_, &cursor)) {
        return;
    }
    POINT viewportCursor{};
    uint32_t viewportWidth = 0;
    uint32_t viewportHeight = 0;
    if (!ResolveEditorViewportClientPoint(
            cursor,
            viewportCursor,
            viewportWidth,
            viewportHeight)) {
        return;
    }

    Vector3 target{};
    if (!IntersectScreenPointWithZPlane(
            viewportCursor,
            viewportWidth,
            viewportHeight,
            frameState_.viewProjectionMatrix,
            0.0f,
            target)) {
        return;
    }

    runtimeState_.vfx.showcaseMode = true;
    runtimeState_.vfx.autoPlayVfxDemo = false;
    runtimeState_.vfx.enableParticles = true;
    runtimeState_.vfx.enableTrails = true;
    runtimeState_.vfx.enableRings = true;
    runtimeState_.vfx.enableCylinders = true;
    runtimeState_.vfx.enableBeams = false;
    runtimeState_.vfx.enableDistortions = false;
    runtimeState_.vfx.enableTrailMeshStream = true;
    runtimeState_.vfx.enableTrailMeshStreamAutoFallback = false;
    runtimeState_.vfx.trailMeshStreamFallbackActive = false;
    runtimeState_.vfx.showcaseEffect = AppVfxRuntimeState::ShowcaseEffect::IceProjectile;
    runtimeState_.vfx.iceProjectileClickToFire = true;
    runtimeState_.useMonsterBall = false;
    runtimeState_.showAnimatedCube = false;
    runtimeState_.showSkinnedModel = false;
    runtimeState_.showSkeletonDebug = false;
    runtimeState_.showSkybox = false;
    runtimeState_.showProceduralBackdrop = true;
    runtimeState_.showVfxModelObjects = false;
    runtimeState_.clearColor[0] = 0.78f;
    runtimeState_.clearColor[1] = 0.76f;
    runtimeState_.clearColor[2] = 0.74f;
    runtimeState_.clearColor[3] = 1.0f;
    runtimeState_.directionalLightData.color = {0.55f, 0.7f, 1.0f, 1.0f};
    runtimeState_.directionalLightData.direction = {0.25f, -1.0f, 0.2f};
    runtimeState_.directionalLightData.intensity = 0.12f;
    runtimeState_.pointLightData.color = {0.35f, 0.65f, 1.0f, 1.0f};
    runtimeState_.pointLightData.intensity = 0.0f;
    runtimeState_.pointLightData.radius = 6.0f;
    runtimeState_.pointLightData.decay = 2.0f;
    runtimeState_.vfx.showcaseAutoTimer = 8.0f;
    releaseShowcaseTitleDirty_ = true;

    AppVfxRuntimeState::IceProjectileShotState* slot = nullptr;
    for (AppVfxRuntimeState::IceProjectileShotState& shot : runtimeState_.vfx.iceProjectileShots) {
        if (!shot.active) {
            slot = &shot;
            break;
        }
    }
    if (slot == nullptr) {
        slot = &runtimeState_.vfx.iceProjectileShots.front();
        for (AppVfxRuntimeState::IceProjectileShotState& shot : runtimeState_.vfx.iceProjectileShots) {
            if (shot.timer > slot->timer) {
                slot = &shot;
            }
        }
        if (slot->instanceId != 0) {
            vfxEngine_.Runtime().StopEffect(slot->instanceId);
        }
    }

    const Vector3 shotStart = {0.0f, -1.55f, -3.05f};
    const Vector3 shotTarget = {target.x, target.y, 0.42f};
    const Vector3 launchNdc = TransformCoord(shotStart, frameState_.viewProjectionMatrix);
    const float cursorNdcX =
        (static_cast<float>(viewportCursor.x) / static_cast<float>(viewportWidth)) * 2.0f - 1.0f;
    const float cursorNdcY =
        1.0f - (static_cast<float>(viewportCursor.y) / static_cast<float>(viewportHeight)) * 2.0f;

    *slot = {};
    slot->active = true;
    slot->hasExplicitRotationZ = true;
    slot->rotationZ = std::atan2(cursorNdcY - launchNdc.y, cursorNdcX - launchNdc.x);
    slot->start = shotStart;
    slot->target = shotTarget;
    railInputRouteDebug_.showcaseClickFired = true;
}

void AppRunLoop::RenderVfxPreviewFrame() {
    const auto renderStart = RailPerfClock::now();
    LogRailFrameStage(railShooterFrameIndex_, railShooterDistance_, "render.begin");
    BeginFrameSystems();
    bool imguiFrameOpen = true;
    const auto closeImguiFrameOnAbort = [&]() {
        if (!imguiFrameOpen) {
            return;
        }
        imguiLayer_.EndFrame();
        imguiFrameOpen = false;
    };
    imguiLayer_.RefreshEditorViewportRenderTargetLayout();
    ApplyEditorViewportRenderTargetForRender();
    LogRailFrameStage(railShooterFrameIndex_, railShooterDistance_, "render.afterBeginFrameSystems");

    UINT backBufferIndex = swapChain_.CurrentIndex();
    LogRailFrameStage(railShooterFrameIndex_, railShooterDistance_, "render.beforeWaitForFrameSlot");
    const auto waitStart = RailPerfClock::now();
    if (!frameCoordinator_.WaitForFrameSlot(backBufferIndex)) {
        gRailPerfFrame.waitFrameSlotMs = ElapsedMs(waitStart, RailPerfClock::now());
        LogRailFrameStage(railShooterFrameIndex_, railShooterDistance_, "render.waitForFrameSlotFailed");
        closeImguiFrameOnAbort();
        return;
    }
    gRailPerfFrame.waitFrameSlotMs = ElapsedMs(waitStart, RailPerfClock::now());
    ResolveCompletedRailGpuTiming(backBufferIndex);
    LogRailFrameStage(railShooterFrameIndex_, railShooterDistance_, "render.afterWaitForFrameSlot");
    bool terrainMaterialReloadRequested =
        scene_.PollTerrainMaterialHotReload();
    if (terrainMaterialReloadRequested && !frameCoordinator_.FlushGpu()) {
        OutputDebugStringA(
            "[TerrainMaterialHotReload] GPU flush failed; reload deferred.\n");
        terrainMaterialReloadRequested = false;
    }
    const auto commandBeginStart = RailPerfClock::now();
    ComPtr<ID3D12GraphicsCommandList> commandList =
        clPool_.Begin(backBufferIndex, appPipelines_.GetMainPSO());
    gRailPerfFrame.commandListBeginMs = ElapsedMs(commandBeginStart, RailPerfClock::now());
    if (commandList == nullptr) {
        LogRailFrameStage(railShooterFrameIndex_, railShooterDistance_, "render.commandListBeginFailed");
        closeImguiFrameOnAbort();
        return;
    }
    if (terrainMaterialReloadRequested) {
        std::string terrainMaterialReloadError;
        if (!scene_.ReloadTerrainMaterialAssets(
                commandList.Get(),
                &terrainMaterialReloadError)) {
            OutputDebugStringA(
                ("[TerrainMaterialHotReload] " +
                 terrainMaterialReloadError + "\n")
                    .c_str());
        }
    }
    BeginRailGpuTiming(commandList.Get(), backBufferIndex);
    LogRailFrameStage(railShooterFrameIndex_, railShooterDistance_, "render.afterCommandListBegin");
    const auto gpuParticleInitStart = RailPerfClock::now();
    vfxEngine_.InitializeGpuParticles(
        dev_.GetDevice(),
        commandList.Get(),
        heaps_,
        appPipelines_);
    gRailPerfFrame.gpuParticleInitMs = ElapsedMs(gpuParticleInitStart, RailPerfClock::now());
    LogRailFrameStage(railShooterFrameIndex_, railShooterDistance_, "render.afterInitializeGpuParticles");

    ID3D12Resource* backBuffer = swapChain_.BackBuffer(backBufferIndex);
    auto dsvHandle = heaps_.dsv.GetHandle(engineContext_.GetMainDsvIndex()).cpu;
    auto readOnlyDsvHandle = heaps_.dsv.GetHandle(engineContext_.GetReadOnlyDsvIndex()).cpu;
    D3D12_CPU_DESCRIPTOR_HANDLE rtv = swapChain_.RTV(backBufferIndex);

    const auto sceneTransformsStart = RailPerfClock::now();
    scene_.UpdateTransforms(
        runtimeState_,
        wvpData_,
        frameState_.viewMatrix,
        frameState_.projMatrix,
        windowWidth_,
        windowHeight_);
    gRailPerfFrame.sceneTransformsMs = ElapsedMs(sceneTransformsStart, RailPerfClock::now());
    const auto syncCourseMeshStart = RailPerfClock::now();
    const CourseSpawnRuntime& coursePresentationRuntime =
        coursePreviewActorRuntimeBridge_.Active()
        ? coursePreviewActorRuntimeBridge_.Runtime()
        : railShooterSpawnRuntime_;
    const EnemyCombatPresentationBridge* enemyCombatPresentation =
        coursePreviewActorRuntimeBridge_.Active()
        ? nullptr
        : &railEnemyCombatPresentationBridge_;
    const EnemyEncounterReadabilityDirector* enemyEncounterReadability =
        coursePreviewActorRuntimeBridge_.Active()
        ? nullptr
        : &railEnemyEncounterReadabilityDirector_;
    scene_.SyncCourseMeshRenderQueue(
        coursePresentationRuntime,
        &railShooterCourse_,
        railShooterCourseRuntime_.Distance(),
        railPath_,
        frameState_.viewMatrix,
        frameState_.projMatrix,
        enemyCombatPresentation,
        enemyEncounterReadability);
    gRailPerfFrame.syncCourseMeshMs = ElapsedMs(syncCourseMeshStart, RailPerfClock::now());
    LogRailFrameStage(railShooterFrameIndex_, railShooterDistance_, "render.afterSyncCourseMeshes");

    const PostProcessExecutionPlan postExecutionPlan = vfxEngine_.PostProcess().BuildExecutionPlan();
    const D3D12_GPU_DESCRIPTOR_HANDLE spriteTextureHandle =
        runtimeState_.useMonsterBall ? scene_.textureSrvHandleGPU2 : scene_.textureSrvHandleGPU;

    const auto imguiStart = RailPerfClock::now();
    const auto imguiBuildUiStart = RailPerfClock::now();
    imguiLayer_.BuildUi(
        AppImGuiFrameContext{
            &runtimeState_,
            &vfxEngine_.Runtime(),
            vfxEngine_.AuthoringRegistry(),
            &vfxEngine_.LoadedEffectAssets(),
            &vfxEngine_.PostProcess(),
            &lastRenderGraphDescription_,
            &lastRenderGraphError_,
            &lastRenderPassDebugInfo_,
            lastTransientTargetCount_,
            lastTransientTargetStorageCount_,
            lastTransientBufferCount_,
            lastTransientBufferStorageCount_,
            vfxEngine_.RenderTargets().GetSrvHandle("SceneColor"),
            vfxEngine_.RenderTargets().GetSrvHandle("VfxAccumulation"),
            vfxEngine_.RenderTargets().GetSrvHandle(postExecutionPlan.finalOutputResource),
            vfxEngine_.RenderTargets().GetSrvHandle("DebugDepthPreview"),
            vfxEngine_.RenderTargets().GetSrvHandle("DebugEmissivePreview"),
            runtimeState_.terrain.showHiZDebugPreview
                ? terrainChunkManager_.GetHiZDebugSrv(static_cast<uint32_t>((std::max)(runtimeState_.terrain.hiZDebugMip, 0)))
                : D3D12_GPU_DESCRIPTOR_HANDLE{},
            &renderResources_,
            &scene_,
            &appPipelines_,
            &vfxEngine_.GpuParticles(),
            &frameState_,
            srvDescriptorHeap_.Get(),
            commandList.Get(),
            frameCoordinator_.CompletedFenceValue(),
            frameCoordinator_.NextFenceValue(),
            spriteTextureHandle,
            engineContext_.GetDepthSrvGpuHandle(),
            &railShooterCourse_,
            coursePreviewActorRuntimeBridge_.Active()
                ? &coursePreviewActorRuntimeBridge_.Runtime()
                : &railShooterSpawnRuntime_,
            &railShooterCollisionSystem_,
            &railShooterCheckpointSystem_,
            &railShooterCombatFeelSystem_,
            &railShooterCourseLoadStatus_,
            &railShooterCoursePath_,
            railShooterDistance_,
            (runtimeState_.terrain.freezeCourseRuntime || !imguiLayer_.ShouldAdvanceEditorRuntimeFrame())
                ? 0.0f
                : railShooterVehicleMovement_.State().speed,
            railPath_.Length(),
            &railShooterRideTuningTelemetry_,
            [&](std::string* errorMessage) {
                return SaveRailShooterCourse(errorMessage);
            },
            [&]() {
                railShooterCourse_.SortForRuntime();
                ApplyRailShooterCourse();
            },
            [&]() {
                LoadRailShooterCourse();
                ApplyRailShooterCourse();
            },
            [&](float distance) {
                TeleportRailShooterCourse(distance);
            },
            [&]() {
                Emitter emitterState{};
                emitterState.transform = runtimeState_.emitter.transform;
                emitterState.count = runtimeState_.emitter.count;
                emitterState.frequency = runtimeState_.emitter.frequency;
                emitterState.frequencyTime = runtimeState_.emitter.frequencyTime;
                particleSystem_.Emit(emitterState);
            },
            [&]() {
                DrawRailLockOnDebugPanel();
            },
            [&](editor::EditorViewportOverlayService& overlay) {
                BuildRailVisibilityDebugOverlay(overlay);
                auto sink = overlay.Sink(courseRailViewportRenderer_.Layer());
                courseRailViewportRenderer_.Build(overlay.FrameContext(), sink);
                auto enemySink = overlay.Sink(courseEnemyViewportRenderer_.Layer());
                courseEnemyViewportRenderer_.Build(overlay.FrameContext(), enemySink);
                auto waveSink = overlay.Sink(courseWaveViewportRenderer_.Layer());
                courseWaveViewportRenderer_.Build(overlay.FrameContext(), waveSink);
                auto previewSink = overlay.Sink(coursePreviewSimulationSystem_.Layer());
                coursePreviewSimulationSystem_.Build(overlay.FrameContext(), previewSink);
            },
            &courseRailEditorController_,
            &courseRailPickingService_,
            &courseRailViewportRenderer_,
            &courseEnemyEditorController_,
            &courseEnemyPickingService_,
            &courseEnemyViewportRenderer_,
            &courseWaveEditorController_,
            &courseWavePickingService_,
            &courseWaveViewportRenderer_,
            &coursePreviewSimulationSystem_,
            &coursePreviewActorRuntimeBridge_,
            &railShooterGameplayWaveBridge_,
            &courseObjectTransactions_,
            [&](std::string* errorMessage) {
                return BeginEditorGameplaySpawns(errorMessage);
            },
            [&]() {
                StopEditorGameplaySpawns();
            },
            IsRailShooterSceneActive()});
    gRailPerfFrame.imguiBuildUiMs = ElapsedMs(imguiBuildUiStart, RailPerfClock::now());
    const auto imguiEndFrameStart = RailPerfClock::now();
    imguiLayer_.EndFrame();
    imguiFrameOpen = false;
    std::string runtimeSceneReconcileError;
    if (!ReconcileEditorSceneRuntime(&runtimeSceneReconcileError) &&
        !runtimeSceneReconcileError.empty()) {
        OutputDebugStringA(
            ("[RuntimeScene] Reconcile failed: " +
             runtimeSceneReconcileError + "\n").c_str());
    }
    gRailPerfFrame.imguiEndFrameMs = ElapsedMs(imguiEndFrameStart, RailPerfClock::now());
    gRailPerfFrame.imguiMs = ElapsedMs(imguiStart, RailPerfClock::now());
    LogRailFrameStage(railShooterFrameIndex_, railShooterDistance_, "render.afterImgui");

    ApplyEditorViewportRenderTargetForRender();
    scene_.UpdateTransforms(
        runtimeState_,
        wvpData_,
        frameState_.viewMatrix,
        frameState_.projMatrix,
        static_cast<uint32_t>(runtimeState_.viewport.Width),
        static_cast<uint32_t>(runtimeState_.viewport.Height));
    if (railTitleScreenVisible_) {
        scene_.SyncRailVehicleRenderFrame(railTitleScene_.Vehicle(),frameState_.viewMatrix,frameState_.projMatrix);
        scene_.SyncCourseRailTrackRenderer(railTitleScene_.Track(),railTitleScene_.Distance(),
            &railTitleScene_.Wheels(),frameState_.viewMatrix,frameState_.projMatrix);
        scene_.SyncRailVehicleOccupantActorFrame({},frameState_.viewMatrix,frameState_.projMatrix);
        scene_.SyncCourseMeshRenderQueue(railTitleEmptySpawns_,&railTitleScene_.Scenery(),
            railTitleScene_.SceneryDistance(),railTitleScene_.SceneryPath(),frameState_.viewMatrix,frameState_.projMatrix,nullptr,nullptr,
            &railTitleScene_.Pursuer());
    } else {
    scene_.SyncRailVehicleRenderFrame(
        railShooterVehicleRenderer_.Frame(),
        frameState_.viewMatrix,
        frameState_.projMatrix);
    scene_.SyncCourseRailTrackRenderer(
        railShooterTrackMeshBake_.Result(),
        railShooterCourseRuntime_.Distance(),
        &railShooterWheelContactPresentation_.Frame(),
        frameState_.viewMatrix,
        frameState_.projMatrix);
    scene_.SyncRailVehicleOccupantActorFrame(
        railShooterOccupantActor_.Frame(),
        frameState_.viewMatrix,
        frameState_.projMatrix);
    scene_.SyncCourseMeshRenderQueue(
        coursePresentationRuntime,
        &railShooterCourse_,
        railShooterCourseRuntime_.Distance(),
        railPath_,
        frameState_.viewMatrix,
        frameState_.projMatrix,
        enemyCombatPresentation,
        enemyEncounterReadability);

    ProcessCourseObjectViewportEditing();
    ProcessIceProjectileMouseLaunch();

    }

    const auto sceneRuntimeSyncStart = RailPerfClock::now();
    float vehicleToneExposure = 1.0f;
    for (const auto& pass : vfxEngine_.PostProcess().Passes()) {
        if (pass.name == "ToneMapping" && pass.enabled) {
            const float titleExposure = (std::max)(pass.parameters.toneExposure,0.0001f) *
                (std::max)(pass.intensity,0.0001f);
            float gameplayExposure = titleExposure;
            for (const auto& saved : railTitleSavedPostProcess_) {
                if (saved.name == "ToneMapping" && saved.enabled) {
                    gameplayExposure = (std::max)(saved.parameters.toneExposure,0.0001f) *
                        (std::max)(saved.intensity,0.0001f);
                    break;
                }
            }
            vehicleToneExposure = gameplayExposure/titleExposure;
            break;
        }
    }
    scene_.SyncRuntimeState(runtimeState_, frameState_.deltaTime, vehicleToneExposure);
    UpdateHandParticleAttachment();
    UpdateWeaponAttachment();
    particleSystem_.SetAccelerationField({
        runtimeState_.accelerationField.acceleration,
        {runtimeState_.accelerationField.area.min, runtimeState_.accelerationField.area.max}
    });
    gRailPerfFrame.sceneRuntimeSyncMs = ElapsedMs(sceneRuntimeSyncStart, RailPerfClock::now());

    AppFrameGraphBuildContext graphContext{};
    graphContext.renderGraph = &renderGraph_;
    graphContext.runtimeState = &runtimeState_;
    graphContext.frameRenderer = &frameRenderer_;
    graphContext.imguiLayer = &imguiLayer_;
    graphContext.appPipelines = &appPipelines_;
    graphContext.renderResources = &renderResources_;
    graphContext.scene = &scene_;
    graphContext.frameState = &frameState_;
    graphContext.srvDescriptorHeap = srvDescriptorHeap_.Get();
    graphContext.backBuffer = backBuffer;
    graphContext.depthTextureResource = engineContext_.GetDepthStencil();
    graphContext.rtv = rtv;
    graphContext.dsv = dsvHandle;
    graphContext.depthTextureHandle = engineContext_.GetDepthSrvGpuHandle();
    graphContext.terrainChunkManager = &terrainChunkManager_;
    graphContext.productionScenePipeline = &imguiLayer_.ProductionScenePipeline();
    graphContext.transientMeshRenderPath = &imguiLayer_.TransientMeshRenderPath();
    graphContext.productionMaterialPipeline = &imguiLayer_.ProductionMaterialPipeline();
    graphContext.productionTexturePipeline = &imguiLayer_.ProductionTexturePipeline();
    graphContext.productionShaderPipeline = &imguiLayer_.ProductionShaderPipeline();
    graphContext.productionLightingPipeline = &imguiLayer_.ProductionLightingPipeline();
    graphContext.productionGpuDrivenPipeline = &imguiLayer_.ProductionGpuDrivenPipeline();
    graphContext.worldPartitionPipeline = &imguiLayer_.WorldPartitionPipeline();
    const auto registerPassesStart = RailPerfClock::now();
    const std::string railHudTargetResource =
        imguiLayer_.WantsDeveloperDiagnostics()
            ? (postExecutionPlan.finalOutputResource.empty()
                ? std::string("SceneColor")
                : postExecutionPlan.finalOutputResource)
            : std::string("BackBuffer");
    RegisterRailLockOnHudPass(commandList.Get(), railHudTargetResource);
    vfxEngine_.RegisterRenderPasses(
        frameGraphBuilder_,
        graphContext,
        dev_.GetDevice(),
        commandList.Get(),
        scene_,
        spriteTextureHandle);
    // The submission controls and live animation state use one native, final
    // UI pass. It deliberately does
    // not depend on AppImGuiLayer visibility, so Release can keep editor UI
    // disabled while still presenting the assignment controls.
    RegisterSubmissionHudPass(commandList.Get(), "BackBuffer");
    gRailPerfFrame.registerPassesMs = ElapsedMs(registerPassesStart, RailPerfClock::now());
    LogRailFrameStage(railShooterFrameIndex_, railShooterDistance_, "render.afterRegisterPasses");
    const auto prepareGraphStart = RailPerfClock::now();
    const RenderViewportMetrics renderTargetMetrics =
        ResolveRenderViewportMetrics(
            imguiLayer_.EditorViewportRenderTargetState(),
            windowWidth_,
            windowHeight_);
    const VfxGraphResourceStats vfxGraphResourceStats = vfxEngine_.PrepareGraphResources(
        dev_.GetDevice(),
        heaps_,
        resourceRegistry_,
        renderGraph_,
        renderTargetMetrics.width,
        renderTargetMetrics.height);
    gRailPerfFrame.prepareGraphResourcesMs = ElapsedMs(prepareGraphStart, RailPerfClock::now());
    LogRailFrameStage(railShooterFrameIndex_, railShooterDistance_, "render.afterPrepareGraphResources");

    resourceRegistry_.RegisterRenderTarget({
        "BackBuffer",
        {},
        rtv,
        {},
        DXGI_FORMAT_R8G8B8A8_UNORM,
        windowWidth_,
        windowHeight_
    });

    TransitionSceneDepthIfNeeded(
        commandList.Get(),
        engineContext_.GetDepthStencil(),
        sceneDepthState_,
        D3D12_RESOURCE_STATE_DEPTH_WRITE);

    frameRenderer_.BeginFrame(
        commandList.Get(),
        backBuffer,
        rtv,
        dsvHandle,
        runtimeState_.clearColor);
    vfxEngine_.BeginScene(commandList.Get(), dsvHandle);
    renderGraph_.RegisterResource("BackBuffer", backBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET);
    renderGraph_.RegisterResource(
        "SceneDepth",
        engineContext_.GetDepthStencil(),
        D3D12_RESOURCE_STATE_DEPTH_WRITE);
    renderGraph_.RegisterRenderTargetBinding("BackBuffer", rtv, windowWidth_, windowHeight_);
    renderGraph_.RegisterDepthTargetBinding("SceneDepthReadOnly", readOnlyDsvHandle);
    vfxEngine_.RegisterGraphResources(
        renderGraph_,
        dsvHandle,
        [&](std::string_view name, D3D12_RESOURCE_STATES state) {
            if (name == "SceneDepth") {
                sceneDepthState_ = state;
            }
        });

    std::string renderGraphError;
    if (!renderGraph_.Validate(&renderGraphError)) {
        OutputDebugStringA("[RenderGraph] ");
        OutputDebugStringA(renderGraphError.c_str());
        OutputDebugStringA("\n");
    }
    ConfigureRenderGraphDebugDump();
    const bool renderGraphDumpCapturing =
        renderGraphDumpEnabled_ &&
        renderGraphDumpFrameIndex_ < renderGraphDumpFrameLimit_;
    const bool developerDiagnosticsVisible = imguiLayer_.WantsDeveloperDiagnostics();
    const bool refreshRenderGraphDebug =
        renderGraphDumpCapturing ||
        developerDiagnosticsVisible ||
        lastRenderPassDebugInfo_.empty();
    const auto renderGraphDebugStart = RailPerfClock::now();
    if (refreshRenderGraphDebug) {
        lastRenderPassDebugInfo_ = renderGraph_.BuildPassDebugInfo();
        lastRenderGraphDescription_ = renderGraph_.Describe(lastRenderPassDebugInfo_);
    }
    gRailPerfFrame.renderGraphDebugMs = ElapsedMs(renderGraphDebugStart, RailPerfClock::now());
    lastRenderGraphError_ = renderGraphError;
    lastTransientTargetCount_ = vfxGraphResourceStats.transientTargetCount;
    lastTransientTargetStorageCount_ = vfxGraphResourceStats.transientTargetStorageCount;
    lastTransientBufferCount_ = vfxGraphResourceStats.transientBufferCount;
    lastTransientBufferStorageCount_ = vfxGraphResourceStats.transientBufferStorageCount;
    terrainChunkManager_.ResetDebrisCullingStats();
    const auto cascadeShadowStart = RailPerfClock::now();
    RenderCascadeShadowMaps(commandList.Get());
    gRailPerfFrame.cascadeShadowMs = ElapsedMs(cascadeShadowStart, RailPerfClock::now());
    LogRailFrameStage(railShooterFrameIndex_, railShooterDistance_, "render.afterCascadeShadow");
    const auto renderGraphExecuteStart = RailPerfClock::now();
    renderGraph_.Execute(commandList.Get());
    gRailPerfFrame.renderGraphExecuteMs = ElapsedMs(renderGraphExecuteStart, RailPerfClock::now());
    LogRailFrameStage(railShooterFrameIndex_, railShooterDistance_, "render.afterRenderGraphExecute");
    DumpRenderGraphDebugFrame();
    const auto telemetryStart = RailPerfClock::now();
    const VfxFrameTelemetryOptions vfxTelemetryOptions =
        BuildVfxTelemetryOptions(
            runtimeState_.vfx,
            vfxTelemetryFrameIndex_++,
            developerDiagnosticsVisible);
    vfxEngine_.CaptureFrameTelemetry(commandList.Get(), vfxTelemetryOptions);
    gRailPerfFrame.telemetryMs = ElapsedMs(telemetryStart, RailPerfClock::now());
    LogRailFrameStage(railShooterFrameIndex_, railShooterDistance_, "render.afterCaptureTelemetry");
    const auto endFrameStart = RailPerfClock::now();
    frameRenderer_.EndFrame(commandList.Get(), backBuffer);
    gRailPerfFrame.endFrameMs = ElapsedMs(endFrameStart, RailPerfClock::now());
    EndRailGpuTiming(commandList.Get(), backBufferIndex);
    LogRailFrameStage(railShooterFrameIndex_, railShooterDistance_, "render.afterEndFrame");

    LogRailFrameStage(railShooterFrameIndex_, railShooterDistance_, "render.beforeEndAndExecute");
    const auto endAndExecuteStart = RailPerfClock::now();
    if (!clPool_.EndAndExecute(dev_)) {
        gRailPerfFrame.endAndExecuteMs = ElapsedMs(endAndExecuteStart, RailPerfClock::now());
        LogRailFrameStage(railShooterFrameIndex_, railShooterDistance_, "render.endAndExecuteFailed");
        return;
    }
    gRailPerfFrame.endAndExecuteMs = ElapsedMs(endAndExecuteStart, RailPerfClock::now());
    LogRailFrameStage(railShooterFrameIndex_, railShooterDistance_, "render.afterEndAndExecute");
    const auto signalStart = RailPerfClock::now();
    if (!frameCoordinator_.SignalFrame(backBufferIndex)) {
        gRailPerfFrame.signalMs = ElapsedMs(signalStart, RailPerfClock::now());
        LogRailFrameStage(railShooterFrameIndex_, railShooterDistance_, "render.signalFrameFailed");
        return;
    }
    gRailPerfFrame.signalMs = ElapsedMs(signalStart, RailPerfClock::now());
    LogRailFrameStage(railShooterFrameIndex_, railShooterDistance_, "render.afterSignalFrame");
    LogRailFrameStage(railShooterFrameIndex_, railShooterDistance_, "render.beforePresent");
    const auto presentStart = RailPerfClock::now();
    const HRESULT presentHr = swapChain_.Present(dev_, frameCoordinator_.PresentSyncInterval());
    gRailPerfFrame.presentMs = ElapsedMs(presentStart, RailPerfClock::now());
    gRailPerfFrame.renderMs = ElapsedMs(renderStart, RailPerfClock::now());
    RecordRailCameraTuningSample(
        frameState_.deltaTime,
        railShooterSpeedDirector_.LastFrame(),
        railShooterCameraDirector_.LastFrame(),
        railShooterCollisionSystem_.LastFrameStats());
    CaptureRailGpuTimingCpuMetadata(backBufferIndex);
    LogRailShooterPerfSpike();
    LogRailFrameStage(railShooterFrameIndex_, railShooterDistance_, "render.afterPresent");
    if (FAILED(presentHr)) {
        if (!gpuDeviceLost_) {
            gpuDeviceLost_ = true;
            if (railShooterInitialized_) {
                LogRailShooterRuntimeDiagnostics("present_failed");
            }
            LogGpuFailure(
                "Present",
                presentHr,
                dev_.GetDevice() != nullptr ? dev_.GetDevice()->GetDeviceRemovedReason() : presentHr);
            DumpDredBreadcrumbs(dev_.GetDevice());
            PostMessage(hwnd_, WM_CLOSE, 0, 0);
        }
        return;
    }

    if (vfxTelemetryOptions.AnyEnabled()) {
        if (!frameCoordinator_.WaitForFrameSlot(backBufferIndex)) {
            return;
        }
    }
    vfxEngine_.ResolveFrameTelemetry(vfxTelemetryOptions);
}
