#include "RailShooterHudPresentationBridge.h"

#include "GameSessionPresentationBridge.h"
#include "PlayerDamagePresentationBridge.h"
#include "RailRearPursuitRules.h"
#include "RailVehicleCollisionFeedbackBridge.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <string_view>

namespace {
std::string Utf8(std::u8string_view text) {
    return {
        reinterpret_cast<const char*>(text.data()),
        reinterpret_cast<const char*>(text.data() + text.size())};
}

float Smooth(float current, float target, float response, float deltaTime) {
    const float dt = (std::max)(0.0f, deltaTime);
    const float blend = 1.0f - std::exp(-(std::max)(0.1f, response) * dt);
    return current + (target - current) * blend;
}

std::string Whole(float value) {
    return std::to_string(static_cast<int>((std::max)(0.0f, std::round(value))));
}
} // namespace

void RailShooterHudPresentationBridge::Reset() {
    frame_ = {};
    initialized_ = false;
    elapsedSeconds_ = 0.0f;
    scoreGainRemaining_ = 0.0f;
    displayedScoreGain_ = 0;
    revision_ = 0;
    damageTrailHold_ = {}; damageDirectionRemaining_ = {}; lastVehicleDamageSequence_ = 0;
}

void RailShooterHudPresentationBridge::Update(
    const RailShooterHudPresentationInput& input) {
    if (input.definition == nullptr || input.runtime == nullptr ||
        !input.definition->enabled || !input.runtime->visible) {
        damageTrailHold_ = {}; damageDirectionRemaining_ = {}; lastVehicleDamageSequence_ = 0;
        frame_ = {};
        frame_.revision = ++revision_;
        initialized_ = false;
        return;
    }

    const RailShooterHudDefinitionAsset& definition = *input.definition;
    const RailShooterHudRuntimeFrame& runtime = *input.runtime;
    RailShooterHudPresentationFrame next = frame_;
    next.visible = true;
    next.revision = ++revision_;
    elapsedSeconds_ += (std::max)(0.0f, input.deltaTime);

    const float feedbackDt = runtime.gameplayActive ? (std::max)(0.0f, input.deltaTime) : 0.0f;
    scoreGainRemaining_ = (std::max)(0.0f, scoreGainRemaining_ - feedbackDt);
    if (!initialized_ || runtime.score < frame_.score) {
        scoreGainRemaining_ = 0.0f;
        displayedScoreGain_ = 0;
    } else if (runtime.score > frame_.score) {
        const uint64_t delta = runtime.score - frame_.score;
        displayedScoreGain_ = scoreGainRemaining_ > 0.0f ? displayedScoreGain_ + delta : delta;
        scoreGainRemaining_ = 0.9f;
    }
    next.scoreGainText = scoreGainRemaining_ > 0.0f ? "+" + std::to_string(displayedScoreGain_) : "";
    next.scoreGainAlpha = (std::min)(1.0f, scoreGainRemaining_ / 0.25f);

    if (!initialized_) {
        next.playerHealthNormalized = runtime.playerHealthNormalized;
        next.vehicleIntegrityNormalized = runtime.vehicleIntegrityNormalized;
        next.courseProgressNormalized = runtime.courseProgressNormalized;
        next.speedNormalized = runtime.speedNormalized;
        next.threatNormalized = runtime.threatNormalized;
        next.adrenalineNormalized = runtime.adrenalineNormalized;
        initialized_ = true;
    } else {
        next.courseProgressNormalized = Smooth(
            frame_.courseProgressNormalized,
            runtime.courseProgressNormalized,
            definition.smoothingResponse,
            input.deltaTime);
        next.speedNormalized = Smooth(
            frame_.speedNormalized,
            runtime.speedNormalized,
            definition.smoothingResponse,
            input.deltaTime);
        next.threatNormalized = Smooth(
            frame_.threatNormalized,
            runtime.threatNormalized,
            definition.smoothingResponse,
            input.deltaTime);
        next.adrenalineNormalized = Smooth(
            frame_.adrenalineNormalized,
            runtime.adrenalineNormalized,
            definition.smoothingResponse,
            input.deltaTime);
    }

    const float health=(std::clamp)(runtime.playerHealthNormalized,0.0f,1.0f);
    const float integrity=(std::clamp)(runtime.vehicleIntegrityNormalized,0.0f,1.0f);
    const bool fresh=!frame_.visible;
    const auto updateTrail=[&](float actual,float previous,float trail,size_t index) {
        if(fresh || actual>previous+0.0001f) { damageTrailHold_[index]=0; return actual; }
        if(actual<previous-0.0001f) {
            damageTrailHold_[index]=0.22f;
            return (std::max)(trail,previous);
        }
        const float decayDt=(std::max)(0.0f,feedbackDt-damageTrailHold_[index]);
        damageTrailHold_[index]=(std::max)(0.0f,damageTrailHold_[index]-feedbackDt);
        return (std::max)(actual,trail-decayDt*0.85f);
    };
    next.playerHealthTrail=updateTrail(health,frame_.playerHealthNormalized,frame_.playerHealthTrail,0);
    next.vehicleIntegrityTrail=updateTrail(integrity,frame_.vehicleIntegrityNormalized,frame_.vehicleIntegrityTrail,1);
    next.playerHealthNormalized=health;
    next.vehicleIntegrityNormalized=integrity;
    for(float& remaining:damageDirectionRemaining_) remaining=(std::max)(0.0f,remaining-feedbackDt);
    if(input.vehicleDamage && input.vehicleDamage->hasImpactDirection &&
        input.vehicleDamage->lastConsumedResultSequence>lastVehicleDamageSequence_) {
        lastVehicleDamageSequence_=input.vehicleDamage->lastConsumedResultSequence;
        const Vector2 direction=input.vehicleDamage->impactDirectionScreen;
        const float magnitude=(std::max)(std::abs(direction.x),std::abs(direction.y));
        if(magnitude>0.1f) {
            if(std::abs(direction.x)>magnitude*0.45f) damageDirectionRemaining_[direction.x<0?0:1]=0.65f;
            if(std::abs(direction.y)>magnitude*0.45f) damageDirectionRemaining_[direction.y>0?2:3]=0.65f;
        }
    }
    for(size_t index=0;index<4;++index) {
        const float fade=(std::clamp)(damageDirectionRemaining_[index]/0.45f,0.0f,1.0f);
        next.damageDirectionAlpha[index]=fade*fade;
    }

    next.playerHealthCritical =
        runtime.playerHealthNormalized <= definition.healthCriticalThreshold;
    next.vehicleIntegrityCritical = runtime.maximumVehicleIntegrity > 0.0f &&
        runtime.vehicleIntegrityNormalized <= definition.vehicleCriticalThreshold;
    next.threatWarning = runtime.threatBand == ThreatResponseBand::Alert ||
        runtime.threatBand == ThreatResponseBand::Critical;
    constexpr float kTau = 6.28318530718f;
    next.warningPulse = 0.5f + 0.5f * std::sin(
        elapsedSeconds_ * definition.criticalPulseHz * kTau);

    next.score = runtime.score;
    next.combo = runtime.combo;
    next.retriesRemaining = runtime.retriesRemaining;
    next.completedWaves = runtime.completedWaves;
    next.totalWaves = runtime.totalWaves;
    next.activeEnemies = runtime.activeEnemies;
    next.grazeChain = runtime.grazeChain;
    next.nearbyThreats = runtime.nearbyThreats;
    next.lockCount = runtime.lockCount;
    next.maximumLocks = runtime.maximumLocks;
    next.primaryWeapon = runtime.primaryWeapon;

    next.healthText = Utf8(u8"\u4f53\u529b ") + Whole(runtime.playerHealth) + "/" +
        Whole(runtime.maximumPlayerHealth);
    next.vehicleText = Utf8(u8"\u8eca\u4f53 ") + Whole(runtime.vehicleIntegrity) + "/" +
        Whole(runtime.maximumVehicleIntegrity);
    next.speedText = Whole(runtime.speed) + " m/s";
    next.scoreText = Utf8(u8"\u5f97\u70b9 ") + std::to_string(runtime.score);
    next.comboText = runtime.combo > 1
        ? Utf8(u8"\u9023\u7d9a ") + std::to_string(runtime.combo)
        : std::string{};
    next.waveText = runtime.totalWaves > 0
        ? Utf8(u8"\u6ce2 ") + std::to_string(runtime.completedWaves) + "/" +
            std::to_string(runtime.totalWaves)
        : Utf8(u8"\u6ce2 --");
    next.enemyText = !runtime.combatStatusText.empty()
        ? runtime.combatStatusText
        : runtime.activeEnemies > 0
            ? Utf8(u8"\u6575 ") + std::to_string(runtime.activeEnemies)
            : Utf8(u8"\u5b89\u5168");
    next.grazeText = runtime.grazeChain > 0
        ? Utf8(u8"\u304b\u3059\u308a ") + std::to_string(runtime.grazeChain)
        : Utf8(u8"\u304b\u3059\u308a \u6e96\u5099");
    if (runtime.primaryWeapon.available) {
        next.weaponText = runtime.primaryWeapon.unlimitedAmmo
            ? Utf8(u8"\u4e3b\u7832 \u7121\u9650")
            : Utf8(u8"\u4e3b\u7832 ") + std::to_string(runtime.primaryWeapon.ammoInMagazine) +
                "/" + std::to_string(runtime.primaryWeapon.reserveAmmo);
        if (runtime.primaryWeapon.overheated) next.weaponStatusText = Utf8(u8"\u904e\u71b1");
        else if (runtime.primaryWeapon.reloading) next.weaponStatusText = Utf8(u8"\u88c5\u586b\u4e2d");
        else next.weaponStatusText = Utf8(u8"\u6e96\u5099");
    } else {
        next.weaponText = Utf8(u8"\u4e3b\u7832 --");
        next.weaponStatusText.clear();
    }
    if (next.threatWarning) {
        next.threatText = runtime.threatBand == ThreatResponseBand::Critical
            ? Utf8(u8"\u5371\u967a")
            : Utf8(u8"\u8b66\u6212");
        if (runtime.nearbyThreats > 0) {
            next.threatText += " X" + std::to_string(runtime.nearbyThreats);
        }
    } else {
        next.threatText.clear();
    }

    next.showObstacleWarning = runtime.obstacleApproaching;
    next.obstacleWarningText.clear();
    next.obstacleActionText.clear();
    if (runtime.obstacleApproaching) {
        std::ostringstream seconds;
        seconds << std::fixed << std::setprecision(1) << runtime.obstacleTimeToContact;
        next.obstacleWarningText = Utf8(u8"正面の障害物  接触まで ") + seconds.str() + "s";
        next.obstacleActionText = runtime.approachingObstacleBreakable
            ? Utf8(u8"撃って破壊  耐久 ") + Whole(runtime.approachingObstacleHealth)
            : Utf8(u8"破壊不可  衝突注意");
    }

    next.showDamageNotice = false;
    next.damageNoticeLethal = false;
    next.damageNoticeAlpha = 0.0f;
    next.damageNoticeText.clear();
    next.damageHealthText.clear();
    if (input.playerDamage != nullptr && input.playerDamage->showDamageNotice) {
        const auto& damage = input.playerDamage->lastDamage;
        std::string cause;
        switch (damage.request.kind) {
        case PlayerHitKind::EnemyProjectile: cause = Utf8(u8"敵弾"); break;
        case PlayerHitKind::ObstacleContact: cause = Utf8(u8"障害物との衝突"); break;
        case PlayerHitKind::TerrainContact: cause = Utf8(u8"地形との衝突"); break;
        case PlayerHitKind::ScriptedHazard:
            cause = damage.request.sourceId == kVeilBeamSource
                ? Utf8(u8"ヴェイル収束ビーム") : Utf8(u8"危険地帯");
            break;
        }
        next.showDamageNotice = true;
        next.damageNoticeLethal = damage.lethal;
        next.damageNoticeAlpha = damage.lethal ? 1.0f : (std::clamp)(
            input.playerDamage->damageNoticeRemainingSeconds / 0.4f, 0.0f, 1.0f);
        next.damageNoticeText = (damage.lethal ? Utf8(u8"敗因: ") : Utf8(u8"被害: ")) +
            cause + "  -" + Whole(damage.appliedDamage);
        next.damageHealthText = Utf8(u8"耐久 ") + Whole(damage.hitPointsBefore) +
            " -> " + Whole(damage.hitPointsAfter);
    }

    next.showBanner = false;
    next.bannerAlpha = 0.0f;
    next.bannerHeadline.clear();
    next.bannerDetail.clear();
    if (input.sessionPresentation != nullptr) {
        const GameSessionHudView& hud = input.sessionPresentation->hud;
        next.showBanner = hud.showBanner;
        next.bannerAlpha = (std::clamp)(hud.bannerAlpha, 0.0f, 1.0f);
        next.bannerHeadline = hud.headline;
        next.bannerDetail = hud.detail;
        next.bannerColor = {
            hud.bannerColor.r,
            hud.bannerColor.g,
            hud.bannerColor.b,
            hud.bannerColor.a};
    }
    frame_ = std::move(next);
}
