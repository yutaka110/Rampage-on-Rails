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
    WorldRay, ProceduralRay, Count
};

struct Sample {
    double milliseconds = 0.0;
    uint32_t calls = 0;
};

struct Frame {
    std::array<Sample, static_cast<size_t>(Stage::Count)> stages{};
    uint64_t nearestRailEvaluations = 0;
    uint64_t terrainRayCacheHits = 0;
    uint64_t railSampleCacheHits = 0;
    float simulationDeltaTime = 0.0f;
};

inline thread_local Frame frame{};

inline bool Enabled() {
    static const bool enabled = [] {
        char value[8]{};
        size_t length = 0;
        return getenv_s(&length, value, sizeof(value), "CG4_DRONE_PROFILE") == 0 &&
            length == 2 && value[0] == '1';
    }();
    return enabled;
}

inline void ResetFrame() {
    if (Enabled()) frame = {};
}

class Scope final {
public:
    explicit Scope(Stage stage) : stage_(stage), enabled_(Enabled()) {
        if (enabled_) begin_ = Clock::now();
    }
    ~Scope() {
        if (!enabled_) return;
        Sample& sample = frame.stages[static_cast<size_t>(stage_)];
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

inline void AppendToLog(std::ostream& log) {
    if (!Enabled()) return;
    constexpr const char* names[] = {
        "droneSpawn", "droneEnvironment", "droneFormation", "droneClearance",
        "droneCamera", "droneTelegraph", "worldRay", "proceduralRay"
    };
    log << " droneProfile=1";
    for (size_t i = 0; i < frame.stages.size(); ++i) {
        log << ' ' << names[i] << "Ms=" << frame.stages[i].milliseconds
            << ' ' << names[i] << "Calls=" << frame.stages[i].calls;
    }
    log << " simulationDeltaTime=" << frame.simulationDeltaTime
        << " nearestRailEvaluations=" << frame.nearestRailEvaluations
        << " terrainRayCacheHits=" << frame.terrainRayCacheHits
        << " railSampleCacheHits=" << frame.railSampleCacheHits;
}
} // namespace drone_perf
