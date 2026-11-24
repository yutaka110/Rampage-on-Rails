#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <ostream>

// Opt-in CPU diagnostics only. Timings are inclusive: do not add a parent
// (spawn/environment/raycast) to its nested children when interpreting a frame.
// Launch with CG4_DRONE_PROFILE=1 to append the breakdown to rail_perf_spikes.log.
namespace drone_perf {
enum class Stage : size_t {
    Spawn, Environment, Formation, Clearance, Camera, Telegraph,
    WorldRay, ProceduralRay, TerrainBvh, Count
};

inline bool Enabled() {
    static const bool enabled = [] {
        char value[8]{};
        size_t length = 0;
        return getenv_s(&length, value, sizeof(value), "CG4_DRONE_PROFILE") == 0 &&
            length == 2 && value[0] == '1';
    }();
    return enabled;
}

class FrameProfile final {
private:
    struct Sample {
        double milliseconds = 0.0;
        uint32_t calls = 0;
    };

    struct Frame {
        std::array<Sample, static_cast<size_t>(Stage::Count)> stages{};
        uint64_t nearestRailEvaluations = 0;
        uint64_t terrainRayCacheHits = 0;
        uint64_t railSampleCacheHits = 0;
        uint64_t terrainBvhQueries = 0, terrainBvhNodes = 0, terrainBvhTriangles = 0;
        uint64_t terrainReferenceFallbacks = 0;
        uint64_t terrainComparisonQueries = 0, terrainComparisonHitMismatches = 0, terrainComparisonDistanceMismatches = 0;
        double terrainComparisonMaximumDistanceError = 0.0;
        float simulationDeltaTime = 0.0f;
    };

    static thread_local Frame frame_;

public:
    static void Reset() { if (Enabled()) frame_ = {}; }
    static void RecordSimulationDeltaTime(float seconds) {
        if (Enabled()) frame_.simulationDeltaTime = seconds;
    }
    static void CountNearestRailEvaluation() { if (Enabled()) ++frame_.nearestRailEvaluations; }
    static void CountRailSampleCacheHit() { if (Enabled()) ++frame_.railSampleCacheHits; }
    static void CountTerrainRayCacheHit() { if (Enabled()) ++frame_.terrainRayCacheHits; }
    static void CountTerrainReferenceFallback() { if (Enabled()) ++frame_.terrainReferenceFallbacks; }
    static void RecordTerrainBvhQuery(uint64_t nodesVisited, uint64_t trianglesTested) {
        if (!Enabled()) return;
        ++frame_.terrainBvhQueries;
        frame_.terrainBvhNodes += nodesVisited;
        frame_.terrainBvhTriangles += trianglesTested;
    }
    static void RecordTerrainComparison(
        uint64_t queries, uint64_t hitMismatches, uint64_t distanceMismatches, double maxDistanceError) {
        if (!Enabled()) return;
        frame_.terrainComparisonQueries = queries;
        frame_.terrainComparisonHitMismatches = hitMismatches;
        frame_.terrainComparisonDistanceMismatches = distanceMismatches;
        frame_.terrainComparisonMaximumDistanceError = maxDistanceError;
    }
    static void AppendToLog(std::ostream& log);

    class Scope final {
    public:
        explicit Scope(Stage stage) : stage_(stage), enabled_(Enabled()) {
            if (enabled_) begin_ = Clock::now();
        }
        ~Scope() {
            if (!enabled_) return;
            Sample& sample = frame_.stages[static_cast<size_t>(stage_)];
            sample.milliseconds +=
                std::chrono::duration<double, std::milli>(Clock::now() - begin_).count();
            ++sample.calls;
        }
        Scope(const Scope&) = delete;
        Scope& operator=(const Scope&) = delete;
    private:
        using Clock = std::chrono::steady_clock;
        Stage stage_;
        bool enabled_;
        Clock::time_point begin_{};
    };
};

inline thread_local FrameProfile::Frame FrameProfile::frame_{};
using Scope = FrameProfile::Scope;

inline void FrameProfile::AppendToLog(std::ostream& log) {
    if (!Enabled()) return;
    const Frame& frame = frame_;
    constexpr const char* names[] = {
        "droneSpawn", "droneEnvironment", "droneFormation", "droneClearance",
        "droneCamera", "droneTelegraph", "worldRay", "proceduralRay", "terrainBvh"
    };
    log << " droneProfile=1";
    for (size_t i = 0; i < frame.stages.size(); ++i) {
        log << ' ' << names[i] << "Ms=" << frame.stages[i].milliseconds
            << ' ' << names[i] << "Calls=" << frame.stages[i].calls;
    }
    log << " simulationDeltaTime=" << frame.simulationDeltaTime
        << " nearestRailEvaluations=" << frame.nearestRailEvaluations
        << " terrainRayCacheHits=" << frame.terrainRayCacheHits
        << " railSampleCacheHits=" << frame.railSampleCacheHits
        << " terrainBvhQueries=" << frame.terrainBvhQueries
        << " terrainBvhNodes=" << frame.terrainBvhNodes
        << " terrainBvhTriangles=" << frame.terrainBvhTriangles
        << " terrainReferenceFallbacks=" << frame.terrainReferenceFallbacks
        << " terrainComparisonQueries=" << frame.terrainComparisonQueries
        << " terrainComparisonHitMismatches=" << frame.terrainComparisonHitMismatches
        << " terrainComparisonDistanceMismatches=" << frame.terrainComparisonDistanceMismatches
        << " terrainComparisonMaximumDistanceError=" << frame.terrainComparisonMaximumDistanceError;
}
} // namespace drone_perf
