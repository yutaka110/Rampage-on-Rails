#pragma once

#include <cstdint>
#include <vector>

#include "EnemyAttackDefensePresentationBridge.h"
#include "EnemyAttackDefenseOutcomeFeedbackBridge.h"
#include "RailShooterHudRenderer.h"

struct RailShooterDefensePromptRendererSettings final {
    bool enabled = true;
    uint32_t maximumDrawCommands = 32;
    float opacity = 0.94f;
    float urgentThreatSeconds = 0.40f;
    float urgentThreatThreshold = 0.82f;
};

struct RailShooterDefensePromptRenderInput final {
    const EnemyAttackDefensePresentationFrame* presentation = nullptr;
    const EnemyAttackDefenseOutcomeFeedbackFrame* outcome = nullptr;
    uint32_t viewportWidth = 0;
    uint32_t viewportHeight = 0;
    RailShooterDefensePromptRendererSettings settings{};
    bool compactOutcome = false;
};

struct RailShooterDefensePromptRenderFrame final {
    bool visible = false;
    bool urgentThreatActive = false;
    bool outcomeDemoted = false;
    uint32_t visiblePrompts = 0;
    uint32_t droppedCommands = 0;
    uint64_t sourceRevision = 0;
    uint64_t revision = 0;
    std::vector<RailShooterHudDrawCommand> commands;
};

// Backend-independent HUD layer dedicated to actionable defense language.
// It deliberately consumes presentation cues instead of combat definitions.
class RailShooterDefensePromptRenderer final {
public:
    void Reset();
    void Update(const RailShooterDefensePromptRenderInput& input);
    const RailShooterDefensePromptRenderFrame& Frame() const noexcept {
        return frame_;
    }

private:
    RailShooterDefensePromptRenderFrame frame_{};
    uint64_t revision_ = 0;
};
