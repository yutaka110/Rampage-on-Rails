#include "EnemyAttackDefenseOutcomeFeedbackBridge.h"

#include <algorithm>
#include <cmath>
#include <string_view>

namespace {
std::string Utf8(std::u8string_view text) {
    return {
        reinterpret_cast<const char*>(text.data()),
        reinterpret_cast<const char*>(text.data() + text.size())};
}

struct OutcomeProfile final {
    EnemyAttackDefenseCelebrationStyle style =
        EnemyAttackDefenseCelebrationStyle::None;
    Vector4 primaryColor{0.24f, 1.0f, 0.64f, 1.0f};
    Vector4 secondaryColor{1.0f, 1.0f, 1.0f, 1.0f};
    float displayDurationScale = 1.0f;
    float hapticDurationScale = 1.0f;
    float hapticLow = 0.0f;
    float hapticHigh = 0.0f;
    float cameraShake = 0.0f;
    float cameraPitch = 0.0f;
    float cameraYaw = 0.0f;
    float audioVolume = 0.0f;
    float audioPitch = 1.0f;
};

OutcomeProfile ProfileFor(const EnemyAttackDefenseResult& result) noexcept {
    if (result.outcome == EnemyAttackDefenseOutcome::Failed) {
        return {
            EnemyAttackDefenseCelebrationStyle::FailureImpact,
            {1.0f, 0.20f, 0.28f, 1.0f},
            {1.0f, 0.62f, 0.18f, 1.0f},
            1.0f, 1.15f, 0.58f, 0.34f,
            0.115f, 0.018f, 0.0f, 0.34f, 0.76f};
    }

    OutcomeProfile profile{};
    switch (result.method) {
    case EnemyAttackDefenseMethod::Interrupt:
        profile = {
            EnemyAttackDefenseCelebrationStyle::InterruptBreak,
            {1.0f, 0.56f, 0.10f, 1.0f},
            {1.0f, 0.88f, 0.30f, 1.0f},
            1.05f, 1.35f, 0.64f, 0.30f,
            0.090f, -0.016f, 0.0f, 0.54f, 0.84f};
        break;
    case EnemyAttackDefenseMethod::ShootDown:
        profile = {
            EnemyAttackDefenseCelebrationStyle::ShootDownBurst,
            {0.18f, 0.92f, 1.0f, 1.0f},
            {0.82f, 1.0f, 1.0f, 1.0f},
            0.98f, 0.82f, 0.18f, 0.86f,
            0.055f, -0.007f, 0.0f, 0.46f, 1.24f};
        break;
    case EnemyAttackDefenseMethod::LeanLeft:
    case EnemyAttackDefenseMethod::LeanRight:
    case EnemyAttackDefenseMethod::Duck:
        profile = {
            EnemyAttackDefenseCelebrationStyle::EvasionFlow,
            {0.96f, 0.24f, 0.84f, 1.0f},
            {0.58f, 0.76f, 1.0f, 1.0f},
            0.90f, 1.15f, 0.30f, 0.58f,
            0.035f, 0.002f, 0.0f, 0.42f, 1.04f};
        if (result.method == EnemyAttackDefenseMethod::LeanLeft) {
            profile.cameraYaw = -0.018f;
        } else if (result.method == EnemyAttackDefenseMethod::LeanRight) {
            profile.cameraYaw = 0.018f;
        } else {
            profile.cameraPitch = 0.014f;
        }
        break;
    case EnemyAttackDefenseMethod::None:
        break;
    }

    if (result.grade == EnemyAttackDefenseGrade::Perfect) {
        profile.secondaryColor = {1.0f, 0.84f, 0.22f, 1.0f};
        profile.audioVolume *= 1.10f;
        profile.audioPitch *= 1.08f;
        profile.hapticLow = (std::min)(1.0f, profile.hapticLow + 0.08f);
        profile.hapticHigh = (std::min)(1.0f, profile.hapticHigh + 0.08f);
        profile.cameraShake *= 1.18f;
    }
    return profile;
}

std::string Headline(const EnemyAttackDefenseResult& result) {
    if (result.outcome == EnemyAttackDefenseOutcome::Failed) {
        return Utf8(u8"\u9632\u5fa1\u5931\u6557");
    }
    const bool perfect = result.grade == EnemyAttackDefenseGrade::Perfect;
    switch (result.method) {
    case EnemyAttackDefenseMethod::Interrupt:
        return perfect
            ? Utf8(u8"\u5b8c\u5168\u963b\u6b62!")
            : Utf8(u8"\u653b\u6483\u963b\u6b62!");
    case EnemyAttackDefenseMethod::ShootDown:
        return perfect
            ? Utf8(u8"\u5b8c\u5168\u8fce\u6483!")
            : Utf8(u8"\u8fce\u6483\u6210\u529f!");
    case EnemyAttackDefenseMethod::LeanLeft:
    case EnemyAttackDefenseMethod::LeanRight:
    case EnemyAttackDefenseMethod::Duck:
        return perfect
            ? Utf8(u8"\u5b8c\u5168\u56de\u907f!")
            : Utf8(u8"\u56de\u907f\u6210\u529f!");
    case EnemyAttackDefenseMethod::None: break;
    }
    return Utf8(u8"\u9632\u5fa1\u6210\u529f!");
}

std::string Detail(const EnemyAttackDefenseResult& result) {
    if (result.outcome == EnemyAttackDefenseOutcome::Failed) {
        return Utf8(u8"\u9023\u7d9a\u7d42\u4e86");
    }
    std::string detail = "+" + std::to_string(result.scoreAwarded);
    if (result.chainAfter > 1) {
        detail += "  " + Utf8(u8"\u9023\u7d9a") + "x" +
            std::to_string(result.chainAfter);
    }
    return detail;
}

float FlashStrength(EnemyAttackDefenseCelebrationStyle style) noexcept {
    switch (style) {
    case EnemyAttackDefenseCelebrationStyle::InterruptBreak: return 0.18f;
    case EnemyAttackDefenseCelebrationStyle::ShootDownBurst: return 0.24f;
    case EnemyAttackDefenseCelebrationStyle::EvasionFlow: return 0.12f;
    case EnemyAttackDefenseCelebrationStyle::FailureImpact: return 0.22f;
    case EnemyAttackDefenseCelebrationStyle::None: break;
    }
    return 0.0f;
}

float BannerPunch(EnemyAttackDefenseCelebrationStyle style) noexcept {
    switch (style) {
    case EnemyAttackDefenseCelebrationStyle::InterruptBreak: return 0.12f;
    case EnemyAttackDefenseCelebrationStyle::ShootDownBurst: return 0.16f;
    case EnemyAttackDefenseCelebrationStyle::EvasionFlow: return 0.08f;
    case EnemyAttackDefenseCelebrationStyle::FailureImpact: return 0.10f;
    case EnemyAttackDefenseCelebrationStyle::None: break;
    }
    return 0.0f;
}
}

void EnemyAttackDefenseOutcomeFeedbackBridge::Reset() {
    lastConsumedSequence_ = 0;
    displayRemainingSeconds_ = 0.0f;
    displayDurationSeconds_ = 0.0f;
    hapticRemainingSeconds_ = 0.0f;
    frame_ = {};
    revision_ = 0;
}

void EnemyAttackDefenseOutcomeFeedbackBridge::Update(
    const EnemyAttackDefenseOutcomeFeedbackInput& input) {
    const float dt = std::isfinite(input.deltaTime)
        ? (std::clamp)(input.deltaTime, 0.0f, 0.25f) : 0.0f;
    EnemyAttackDefenseOutcomeFeedbackFrame next = frame_;
    next.audioCues.clear();
    next.cameraShake = 0.0f;
    next.cameraPitchImpulse = 0.0f;
    next.cameraYawImpulse = 0.0f;
    const auto localSuccess=[&](uint32_t actor,EnemyAttackDefenseOutcome outcome) {
        return actor!=0 && outcome==EnemyAttackDefenseOutcome::Success &&
            std::find(input.localSuccessActors.begin(),input.localSuccessActors.end(),actor)!=input.localSuccessActors.end();
    };
    if(localSuccess(next.actorId,next.outcome)) {
        // Also clear a retained toast when entering this presentation policy.
        next={};displayRemainingSeconds_=displayDurationSeconds_=hapticRemainingSeconds_=0;
    }
    displayRemainingSeconds_ = (std::max)(0.0f, displayRemainingSeconds_ - dt);
    hapticRemainingSeconds_ = (std::max)(0.0f, hapticRemainingSeconds_ - dt);

    if (input.settings.enabled && input.gameplayActive) {
        for (const EnemyAttackDefenseResult& result : input.results) {
            if (!result.accepted || result.sequence <= lastConsumedSequence_) continue;
            lastConsumedSequence_ = result.sequence;
            if(localSuccess(result.actorId,result.outcome))continue;
            next.resultSequence = result.sequence;
            next.actorId = result.actorId;
            next.method = result.method;
            next.outcome = result.outcome;
            next.grade = result.grade;
            next.headline = Headline(result);
            next.detail = Detail(result);
            next.scoreAwarded = result.scoreAwarded;
            next.chain = result.chainAfter;
            const bool success = result.outcome == EnemyAttackDefenseOutcome::Success;
            const OutcomeProfile profile = ProfileFor(result);
            next.celebrationStyle = profile.style;
            next.color = profile.primaryColor;
            next.secondaryColor = profile.secondaryColor;
            next.accentDirectionX = result.method ==
                    EnemyAttackDefenseMethod::LeanLeft
                ? -1.0f
                : (result.method == EnemyAttackDefenseMethod::LeanRight
                    ? 1.0f : 0.0f);
            displayDurationSeconds_ = success
                ? input.settings.displayDurationSeconds *
                    profile.displayDurationScale
                : input.settings.failureDisplayDurationSeconds;
            displayRemainingSeconds_ = displayDurationSeconds_;
            hapticRemainingSeconds_ = input.settings.hapticDurationSeconds *
                profile.hapticDurationScale;
            next.hapticLow = profile.hapticLow;
            next.hapticHigh = profile.hapticHigh;
            next.cameraShake = profile.cameraShake;
            next.cameraPitchImpulse = profile.cameraPitch;
            next.cameraYawImpulse = profile.cameraYaw;
            if (next.audioCues.size() < input.settings.maximumAudioCuesPerFrame) {
                EnemyAttackDefenseOutcomeAudioCue cue{};
                cue.resultSequence = result.sequence;
                cue.method = result.method;
                cue.outcome = result.outcome;
                cue.grade = result.grade;
                cue.volume = profile.audioVolume;
                cue.pitch = profile.audioPitch;
                next.audioCues.push_back(cue);
            }
        }
    }

    next.visible = input.settings.enabled && displayRemainingSeconds_ > 0.0f &&
        !next.headline.empty();
    next.alpha = next.visible && displayDurationSeconds_ > 0.0f
        ? (std::clamp)(displayRemainingSeconds_ /
            (std::min)(0.18f, displayDurationSeconds_), 0.0f, 1.0f)
        : 0.0f;
    const float lifeRatio = next.visible && displayDurationSeconds_ > 0.0f
        ? (std::clamp)(
            displayRemainingSeconds_ / displayDurationSeconds_, 0.0f, 1.0f)
        : 0.0f;
    next.normalizedAge = next.visible ? 1.0f - lifeRatio : 1.0f;
    next.impactPulse = next.visible
        ? std::pow((std::max)(0.0f,
              1.0f - next.normalizedAge / 0.28f), 2.0f)
        : 0.0f;
    next.screenFlashAlpha = next.impactPulse *
        FlashStrength(next.celebrationStyle);
    next.bannerScale = 1.0f + next.impactPulse *
        BannerPunch(next.celebrationStyle);
    next.hapticRemainingSeconds = hapticRemainingSeconds_;
    if (hapticRemainingSeconds_ <= 0.0f) {
        next.hapticLow = 0.0f;
        next.hapticHigh = 0.0f;
    }
    next.revision = ++revision_;
    frame_ = std::move(next);
}

const char* ToString(EnemyAttackDefenseCelebrationStyle style) noexcept {
    switch (style) {
    case EnemyAttackDefenseCelebrationStyle::None: return "None";
    case EnemyAttackDefenseCelebrationStyle::InterruptBreak:
        return "InterruptBreak";
    case EnemyAttackDefenseCelebrationStyle::ShootDownBurst:
        return "ShootDownBurst";
    case EnemyAttackDefenseCelebrationStyle::EvasionFlow: return "EvasionFlow";
    case EnemyAttackDefenseCelebrationStyle::FailureImpact:
        return "FailureImpact";
    }
    return "Unknown";
}
