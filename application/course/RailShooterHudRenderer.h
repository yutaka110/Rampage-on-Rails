#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "RailShooterHudDefinitionAsset.h"
#include "RailShooterHudPresentationBridge.h"

enum class RailShooterHudDrawCommandKind : uint8_t {
    Rectangle,
    Text,
    TitleLogo,
    Plate,
    TunnelTransition,
};

enum class RailShooterHudTextAlignment : uint8_t {
    Left,
    Center,
};

struct RailShooterHudDrawCommand final {
    RailShooterHudDrawCommandKind kind =
        RailShooterHudDrawCommandKind::Rectangle;
    RailShooterHudTextAlignment textAlignment =
        RailShooterHudTextAlignment::Left;
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
    float fontScale = 1.0f;
    Vector4 color{1.0f, 1.0f, 1.0f, 1.0f};
    std::string text;
    float transitionProgress = 0.0f;
    Vector2 transitionFocus{0.5f,0.4f};
};

struct RailShooterHudRenderInput final {
    const RailShooterHudDefinitionAsset* definition = nullptr;
    const RailShooterHudPresentationFrame* presentation = nullptr;
    uint32_t viewportWidth = 0;
    uint32_t viewportHeight = 0;
    bool showTitleScreen = false;
    int titleMenuSelection = 0;
    bool titleControlsVisible = false;
    float titleOpacity = 1.0f;
    float titleBlackout = 0.0f;
    bool titleLogoAvailable = true;
    float startupGaugeProgress = 1.0f;
    float startupInformationOpacity = 1.0f;
    Vector2 titleTransitionFocus{0.5f,0.4f};
};

struct RailShooterHudRenderFrame final {
    bool visible = false;
    uint32_t viewportWidth = 0;
    uint32_t viewportHeight = 0;
    uint64_t revision = 0;
    std::vector<RailShooterHudDrawCommand> commands;
};

// Pure layout renderer. It emits bounded, backend-independent screen-space
// commands and owns no D3D resources or gameplay references.
class RailShooterHudRenderer final {
public:
    void Reset();
    void Update(const RailShooterHudRenderInput& input);
    const RailShooterHudRenderFrame& Frame() const noexcept { return frame_; }

private:
    RailShooterHudRenderFrame frame_{};
    uint64_t revision_ = 0;
};
