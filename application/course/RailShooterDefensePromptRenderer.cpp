#include "RailShooterDefensePromptRenderer.h"

#include <algorithm>
#include <string>
#include <string_view>

namespace {

std::string Utf8(std::u8string_view text) {
    return {
        reinterpret_cast<const char*>(text.data()),
        reinterpret_cast<const char*>(text.data() + text.size())};
}

Vector4 OptionColor(EnemyAttackDefensePromptAction action, float alpha) {
    switch (action) {
    case EnemyAttackDefensePromptAction::Interrupt:
        return {1.0f, 0.72f, 0.16f, alpha};
    case EnemyAttackDefensePromptAction::ShootDown:
        return {0.30f, 0.95f, 1.0f, alpha};
    case EnemyAttackDefensePromptAction::LeanLeft:
    case EnemyAttackDefensePromptAction::LeanRight:
        return {1.0f, 0.20f, 0.76f, alpha};
    case EnemyAttackDefensePromptAction::Duck:
        return {0.46f, 0.68f, 1.0f, alpha};
    default:
        return {0.72f, 0.78f, 0.82f, alpha};
    }
}

std::string ActionCallout(EnemyAttackDefensePromptAction action) {
    switch (action) {
    case EnemyAttackDefensePromptAction::Interrupt:
        return Utf8(u8"\u6575\u3092\u6483\u3066");
    case EnemyAttackDefensePromptAction::ShootDown:
        return Utf8(u8"\u5f3e\u3092\u6483\u3066");
    case EnemyAttackDefensePromptAction::LeanLeft:
        return Utf8(u8"\u5de6\u3078\u907f\u3051\u308d");
    case EnemyAttackDefensePromptAction::LeanRight:
        return Utf8(u8"\u53f3\u3078\u907f\u3051\u308d");
    case EnemyAttackDefensePromptAction::Duck:
        return Utf8(u8"\u4e0b\u3078\u907f\u3051\u308d");
    default:
        return {};
    }
}

std::string ActionChip(EnemyAttackDefensePromptAction action) {
    switch (action) {
    case EnemyAttackDefensePromptAction::Interrupt:
        return Utf8(u8"\u963b\u6b62");
    case EnemyAttackDefensePromptAction::ShootDown:
        return Utf8(u8"\u8fce\u6483");
    case EnemyAttackDefensePromptAction::LeanLeft:
        return Utf8(u8"\u5de6\u56de\u907f");
    case EnemyAttackDefensePromptAction::LeanRight:
        return Utf8(u8"\u53f3\u56de\u907f");
    case EnemyAttackDefensePromptAction::Duck:
        return Utf8(u8"\u4e0b\u56de\u907f");
    default:
        return {};
    }
}

std::string AvailabilityChip(
    EnemyAttackDefenseDecisionAvailability availability) {
    switch (availability) {
    case EnemyAttackDefenseDecisionAvailability::AvailableNow:
        return Utf8(u8"\u4eca");
    case EnemyAttackDefenseDecisionAvailability::AfterLaunch:
        return Utf8(u8"\u767a\u5c04\u5f8c");
    case EnemyAttackDefenseDecisionAvailability::AtImpact:
        return Utf8(u8"\u7740\u5f3e\u6642");
    case EnemyAttackDefenseDecisionAvailability::Closed:
        return Utf8(u8"\u7d42\u4e86");
    }
    return {};
}

std::string PhaseLabel(EnemyAttackDefenseDecisionPhase phase) {
    switch (phase) {
    case EnemyAttackDefenseDecisionPhase::EarlyWarning:
        return Utf8(u8"\u653b\u6483\u4e88\u544a");
    case EnemyAttackDefenseDecisionPhase::FinalCommit:
        return Utf8(u8"\u7740\u5f3e\u76f4\u524d");
    case EnemyAttackDefenseDecisionPhase::ProjectileInFlight:
        return Utf8(u8"\u6575\u5f3e\u63a5\u8fd1");
    }
    return {};
}

bool IsUrgentThreat(
    const EnemyAttackDefensePresentationCue& cue,
    const RailShooterDefensePromptRendererSettings& settings) noexcept {
    return cue.projectileInFlight ||
        cue.decisionPhase == EnemyAttackDefenseDecisionPhase::FinalCommit ||
        (cue.timeToFire > 0.0f &&
         cue.timeToFire <= settings.urgentThreatSeconds) ||
        cue.urgency >= settings.urgentThreatThreshold;
}

} // namespace

void RailShooterDefensePromptRenderer::Reset() {
    frame_ = {};
    revision_ = 0;
}

void RailShooterDefensePromptRenderer::Update(
    const RailShooterDefensePromptRenderInput& input) {
    RailShooterDefensePromptRenderFrame next{};
    next.revision = ++revision_;
    const bool hasPrompt = input.presentation != nullptr &&
        !input.presentation->cues.empty();
    const bool hasOutcome = input.outcome != nullptr && input.outcome->visible;
    if (!input.settings.enabled || (!hasPrompt && !hasOutcome) ||
        input.viewportWidth < 320 || input.viewportHeight < 180) {
        frame_ = std::move(next);
        return;
    }

    const EnemyAttackDefensePresentationCue* primary = hasPrompt
        ? &input.presentation->cues.front() : nullptr;
    next.urgentThreatActive = primary != nullptr &&
        IsUrgentThreat(*primary, input.settings);
    next.outcomeDemoted = hasOutcome && next.urgentThreatActive;
    next.sourceRevision = next.urgentThreatActive
        ? input.presentation->revision
        : (hasOutcome ? input.outcome->revision : input.presentation->revision);

    const float width = static_cast<float>(input.viewportWidth);
    const float height = static_cast<float>(input.viewportHeight);
    const float scale = (std::clamp)(
        (std::min)(width / 1600.0f, height / 900.0f), 0.72f, 1.35f);
    const uint32_t budget = (std::max)(4u, input.settings.maximumDrawCommands);
    next.commands.reserve((std::min)(budget, 32u));
    const auto push = [&](RailShooterHudDrawCommand command) {
        if (next.commands.size() < budget) {
            next.commands.push_back(std::move(command));
        } else {
            ++next.droppedCommands;
        }
    };
    const auto rect = [&](float x, float y, float w, float h, Vector4 color) {
        RailShooterHudDrawCommand command{};
        command.x = x; command.y = y; command.width = w; command.height = h;
        command.color = color;
        push(std::move(command));
    };
    const auto plate = [&](float x,float y,float w,float h,Vector4 color) {
        RailShooterHudDrawCommand command{};
        command.kind=RailShooterHudDrawCommandKind::Plate;
        command.x=x; command.y=y; command.width=w; command.height=h; command.color=color;
        push(std::move(command));
    };
    const auto text = [&](std::string value, float x, float y,
                          float fontScale, Vector4 color) {
        RailShooterHudDrawCommand command{};
        command.kind = RailShooterHudDrawCommandKind::Text;
        command.textAlignment = RailShooterHudTextAlignment::Center;
        command.x = x; command.y = y; command.fontScale = fontScale;
        command.color = color; command.text = std::move(value);
        push(std::move(command));
    };

    const bool promptOnLeft = primary == nullptr ||
        primary->directionFromCenter.x >= 0.0f;
    const auto drawOutcome = [&](bool compact) {
        const float alpha = input.settings.opacity * input.outcome->alpha;
        Vector4 color = input.outcome->color;
        color.w = alpha;
        Vector4 secondary = input.outcome->secondaryColor;
        secondary.w = alpha;

        if (compact) {
            const float panelWidth = (input.compactOutcome ? 202.0f : 290.0f) * scale;
            const float panelHeight = (input.compactOutcome ? 37.0f : 54.0f) * scale;
            const float margin = 28.0f * scale;
            const float panelX = promptOnLeft
                ? width - margin - panelWidth : margin;
            const float panelY = height * (input.compactOutcome ? 0.55f : 0.36f);
            plate(panelX, panelY, panelWidth, panelHeight,
                 {0.008f, 0.018f, 0.028f, alpha * 0.86f});
            rect(panelX, panelY, 5.0f * scale, panelHeight, color);
            text(input.outcome->headline, panelX + panelWidth * 0.5f,
                 panelY + 27.0f * scale, 0.76f * scale, color);
            if (!input.compactOutcome)
                text(input.outcome->detail, panelX + panelWidth * 0.5f,
                     panelY + 46.0f * scale, 0.42f * scale, secondary);
            return;
        }

        const float pulse = (std::clamp)(
            input.outcome->impactPulse, 0.0f, 1.0f);
        float basePanelWidth = 390.0f;
        switch (input.outcome->celebrationStyle) {
        case EnemyAttackDefenseCelebrationStyle::InterruptBreak:
            basePanelWidth = 480.0f;
            break;
        case EnemyAttackDefenseCelebrationStyle::ShootDownBurst:
            basePanelWidth = 430.0f;
            break;
        case EnemyAttackDefenseCelebrationStyle::EvasionFlow:
            basePanelWidth = 410.0f;
            break;
        case EnemyAttackDefenseCelebrationStyle::FailureImpact:
        case EnemyAttackDefenseCelebrationStyle::None:
            break;
        }
        const float punchScale = (std::clamp)(
            input.outcome->bannerScale, 1.0f, 1.24f);
        const float panelWidth = (std::min)(
            basePanelWidth * scale * punchScale, width - 24.0f * scale);
        const float panelHeight = 92.0f * scale * punchScale;
        const float panelX = width * 0.5f - panelWidth * 0.5f;
        const float panelY = height * 0.72f -
            (panelHeight - 92.0f * scale) * 0.5f;
        const float centerX = width * 0.5f;
        const float centerY = panelY + panelHeight * 0.5f;

        if (input.outcome->screenFlashAlpha > 0.001f) {
            Vector4 flash = color;
            flash.w = input.settings.opacity * input.outcome->screenFlashAlpha;
            rect(0.0f, 0.0f, width, height, flash);
        }
        switch (input.outcome->celebrationStyle) {
        case EnemyAttackDefenseCelebrationStyle::InterruptBreak:
            for (int index = 0; index < 4; ++index) {
                const float y = centerY +
                    (static_cast<float>(index) - 1.5f) * 13.0f * scale;
                const float length = (48.0f + 19.0f * index) * scale * pulse;
                const float gap = (12.0f + 7.0f * index) * scale;
                rect(panelX - gap - length, y, length, 4.0f * scale, color);
                rect(panelX + panelWidth + gap, y, length,
                     4.0f * scale, color);
            }
            break;
        case EnemyAttackDefenseCelebrationStyle::ShootDownBurst:
            for (int index = 0; index < 3; ++index) {
                const float reach = (panelWidth * 0.5f + 18.0f * scale +
                    index * 18.0f * scale) * pulse;
                const float thickness = (5.0f - index) * scale;
                rect(centerX - reach - 24.0f * scale, centerY,
                     24.0f * scale, thickness, secondary);
                rect(centerX + reach, centerY,
                     24.0f * scale, thickness, secondary);
                rect(centerX - thickness * 0.5f,
                     panelY - (30.0f + index * 14.0f) * scale * pulse,
                     thickness, 24.0f * scale, secondary);
                rect(centerX - thickness * 0.5f,
                     panelY + panelHeight +
                         (6.0f + index * 14.0f) * scale * pulse,
                     thickness, 24.0f * scale, secondary);
            }
            break;
        case EnemyAttackDefenseCelebrationStyle::EvasionFlow: {
            const float direction = input.outcome->accentDirectionX == 0.0f
                ? 1.0f : input.outcome->accentDirectionX;
            for (int index = 0; index < 6; ++index) {
                const float y = panelY +
                    (13.0f + static_cast<float>(index) * 13.0f) * scale;
                const float length =
                    (70.0f + static_cast<float>(index % 3) * 34.0f) *
                    scale * (0.45f + 0.55f * pulse);
                const float edge = direction < 0.0f
                    ? panelX - (18.0f + index * 9.0f) * scale
                    : panelX + panelWidth + (18.0f + index * 9.0f) * scale;
                rect(direction < 0.0f ? edge - length : edge, y,
                     length, 3.0f * scale,
                     index % 2 == 0 ? color : secondary);
            }
            break;
        }
        case EnemyAttackDefenseCelebrationStyle::FailureImpact:
        case EnemyAttackDefenseCelebrationStyle::None:
            break;
        }

        plate(panelX, panelY, panelWidth, panelHeight,
             {0.008f, 0.018f, 0.028f, alpha * 0.90f});
        rect(panelX, panelY, panelWidth, 6.0f * scale, color);
        text(input.outcome->headline, centerX,
             panelY + 47.0f * scale * punchScale,
             1.16f * scale * punchScale, color);
        text(input.outcome->detail, centerX,
             panelY + 78.0f * scale * punchScale,
             0.62f * scale, secondary);
    };

    if (hasOutcome && !next.outcomeDemoted) {
        drawOutcome(input.compactOutcome);
        next.visiblePrompts = 0;
        next.visible = !next.commands.empty();
        frame_ = std::move(next);
        return;
    }
    if (next.outcomeDemoted) {
        drawOutcome(true);
    }

    if (primary != nullptr) {
        const float pulse = 0.76f + 0.24f *
            (std::clamp)(primary->pulse, 0.0f, 1.0f);
        const float alpha = input.settings.opacity * pulse;
        const float panelWidth = (std::min)(
            430.0f * scale, width - 56.0f * scale);
        const float panelHeight = 118.0f * scale;
        const float margin = 28.0f * scale;
        const float panelX = promptOnLeft
            ? margin : width - margin - panelWidth;
        const float panelY = (std::min)(
            height * 0.68f, height - panelHeight - 24.0f * scale);
        const Vector4 actionColor = primary->actionSatisfied
            ? Vector4{0.30f, 1.0f, 0.55f, alpha}
            : Vector4{primary->color.x, primary->color.y,
                      primary->color.z, alpha};
        plate(panelX, panelY, panelWidth, panelHeight,
             {0.008f, 0.018f, 0.028f, alpha * 0.88f});
        rect(panelX, panelY, 7.0f * scale, panelHeight, actionColor);

        std::string phase = PhaseLabel(primary->decisionPhase);
        if (!primary->projectileInFlight && primary->timeToFire > 0.0f) {
            phase += "  " + FormatEnemyAttackCountdown(primary->timeToFire);
        }
        if (input.presentation->cues.size() > 1) {
            phase += "  " + Utf8(u8"\u8105\u5a01+") +
                std::to_string(input.presentation->cues.size() - 1);
        }
        text(std::move(phase), panelX + panelWidth * 0.5f,
             panelY + 24.0f * scale, 0.62f * scale, actionColor);
        text(ActionCallout(primary->primaryAction),
             panelX + panelWidth * 0.5f,
             panelY + 62.0f * scale, 1.12f * scale, actionColor);

        const uint32_t optionCount = (std::min)(
            primary->decisionOptionCount,
            static_cast<uint32_t>(primary->decisionOptions.size()));
        if (optionCount > 0) {
            const float gap = 7.0f * scale;
            const float contentX = panelX + 14.0f * scale;
            const float contentWidth = panelWidth - 28.0f * scale;
            const float chipWidth =
                (contentWidth - gap * static_cast<float>(optionCount - 1)) /
                static_cast<float>(optionCount);
            const float chipY = panelY + 80.0f * scale;
            const float chipHeight = 27.0f * scale;
            for (uint32_t index = 0; index < optionCount; ++index) {
                const EnemyAttackDefenseDecisionOption& option =
                    primary->decisionOptions[index];
                const bool available = option.availability ==
                    EnemyAttackDefenseDecisionAvailability::AvailableNow;
                const bool closed = option.availability ==
                    EnemyAttackDefenseDecisionAvailability::Closed;
                const float optionAlpha = closed
                    ? alpha * 0.18f : (available ? alpha : alpha * 0.40f);
                const float chipX = contentX +
                    static_cast<float>(index) * (chipWidth + gap);
                const Vector4 optionColor = option.actionSatisfied
                    ? Vector4{0.30f, 1.0f, 0.55f, optionAlpha}
                    : OptionColor(option.action, optionAlpha);
                rect(chipX, chipY, chipWidth, chipHeight,
                     {0.025f, 0.045f, 0.060f,
                      available ? alpha * 0.92f : alpha * 0.48f});
                if (option.recommended) {
                    rect(chipX, chipY, chipWidth, 3.0f * scale, optionColor);
                }
                std::string label = ActionChip(option.action) + " " +
                    AvailabilityChip(option.availability);
                text(std::move(label), chipX + chipWidth * 0.5f,
                     chipY + 19.0f * scale, 0.48f * scale, optionColor);
            }
        }
        next.visiblePrompts = static_cast<uint32_t>(
            input.presentation->cues.size());
    }

    next.visible = !next.commands.empty();
    frame_ = std::move(next);
}
