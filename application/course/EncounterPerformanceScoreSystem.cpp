#include "EncounterPerformanceScoreSystem.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include "CourseSpawnRuntime.h"
#include "WeaponFeedbackSystem.h"

namespace {

void SetError(std::string* errorMessage, const char* message) {
    if (errorMessage != nullptr) *errorMessage = message;
}

bool Contains(const std::vector<std::string>& values, const std::string& value) {
    return std::find(values.begin(), values.end(), value) != values.end();
}

bool Contains(const std::vector<uint32_t>& values, uint32_t value) {
    return std::find(values.begin(), values.end(), value) != values.end();
}

uint32_t SaturatedScore(float value) {
    if (!std::isfinite(value) || value <= 0.0f) return 0;
    const double rounded = std::round(static_cast<double>(value));
    const double maximum = static_cast<double>(
        (std::numeric_limits<uint32_t>::max)());
    return rounded >= maximum
        ? (std::numeric_limits<uint32_t>::max)()
        : static_cast<uint32_t>(rounded);
}

} // namespace

bool EncounterPerformanceScoreSettings::Validate(
    std::string* errorMessage) const {
    if (defeatBaseScore == 0 || clearBaseScore == 0 ||
        !std::isfinite(cleanClearMultiplier) || cleanClearMultiplier < 1.0f ||
        maximumResultsPerFrame == 0 || maximumResultsPerFrame > 128) {
        SetError(errorMessage, "Encounter score settings are invalid.");
        return false;
    }
    if (errorMessage != nullptr) errorMessage->clear();
    return true;
}

void EncounterPerformanceScoreSystem::Reset() {
    state_ = {};
    resultsThisFrame_.clear();
}

void EncounterPerformanceScoreSystem::BeginBeat(
    const EnemyEncounterPacingFrame& pacing,
    const EnemyEncounterPacingEvent& event) {
    if (event.beatGuid.empty() ||
        Contains(state_.completedBeatGuids, event.beatGuid)) {
        return;
    }
    state_.activeBeatGuid = event.beatGuid;
    state_.activeEncounterId = event.encounterId;
    state_.activeWaveGuid = pacing.definition.waveGuid;
    state_.maximumConcurrentAttackers = (std::max)(
        1u, pacing.definition.maximumConcurrentAttackers);
    state_.maximumThreatBudget = (std::max)(
        0.0f, pacing.definition.maximumThreatBudget);
    state_.activeDefeatedActors = 0;
    state_.activePeakEligibleActors = pacing.eligibleActors;
    state_.activeBeatNoDamage = true;
    state_.defeatedActorIds.clear();
    ++state_.revision;
}

void EncounterPerformanceScoreSystem::AddResult(
    EncounterPerformanceScoreResult result,
    size_t maximumResults) {
    if (result.scoreAwarded == 0 || resultsThisFrame_.size() >= maximumResults) {
        return;
    }
    result.accepted = true;
    state_.totalScore += result.scoreAwarded;
    state_.chain = result.chainAfter;
    state_.maximumChain = (std::max)(state_.maximumChain, state_.chain);
    resultsThisFrame_.push_back(std::move(result));
    ++state_.revision;
}

void EncounterPerformanceScoreSystem::Update(
    const EncounterPerformanceScoreInput& input,
    const EncounterPerformanceScoreSettings& settings) {
    resultsThisFrame_.clear();
    if (!input.gameplayActive || !settings.Validate()) return;

    for (const PlayerDamageResult& damage : input.damageResults) {
        if (!damage.accepted || damage.appliedDamage <= 0.0f) continue;
        state_.activeBeatNoDamage = false;
        state_.chain = 0;
        ++state_.revision;
    }

    // One visible combo spans every intentional success. Unlike the local
    // multiplier chains owned by graze/defense systems, this run chain does
    // not expire during authored breathing room and always breaks on damage.
    for (const EnemyAttackDefenseResult& defense : input.defenseResults) {
        if (!defense.accepted ||
            defense.outcome != EnemyAttackDefenseOutcome::Success) {
            continue;
        }
        ++state_.chain;
        state_.maximumChain = (std::max)(state_.maximumChain, state_.chain);
        ++state_.revision;
    }
    for (const GrazeScoreResult& graze : input.grazeResults) {
        if (!graze.accepted) continue;
        ++state_.chain;
        state_.maximumChain = (std::max)(state_.maximumChain, state_.chain);
        ++state_.revision;
    }

    if (input.pacing != nullptr) {
        for (const EnemyEncounterPacingEvent& event : input.pacing->events) {
            if (event.kind == EnemyEncounterPacingEventKind::BeatStarted) {
                BeginBeat(*input.pacing, event);
            }
        }
        if (input.pacing->activeBeatGuid == state_.activeBeatGuid) {
            state_.activePeakEligibleActors = (std::max)(
                state_.activePeakEligibleActors,
                input.pacing->eligibleActors);
        }
    }

    const float threat = (std::max)(0.0f, state_.maximumThreatBudget);
    const uint32_t concurrent = (std::max)(
        1u, state_.maximumConcurrentAttackers);
    for (const EnemyCombatEvent& event : input.combatEvents) {
        if (event.kind != EnemyCombatEventKind::Defeated ||
            state_.activeBeatGuid.empty() ||
            event.waveId != state_.activeWaveGuid ||
            Contains(state_.defeatedActorIds, event.actorId)) {
            continue;
        }
        state_.defeatedActorIds.push_back(event.actorId);
        ++state_.activeDefeatedActors;
        EncounterPerformanceScoreResult result{};
        result.kind = EncounterPerformanceScoreKind::EnemyDefeat;
        result.actorId = event.actorId;
        result.beatGuid = state_.activeBeatGuid;
        result.encounterId = state_.activeEncounterId;
        result.scoreAwarded = SaturatedScore(
            static_cast<float>(settings.defeatBaseScore) +
            static_cast<float>(settings.defeatConcurrencyStep) *
                static_cast<float>(concurrent - 1u) +
            static_cast<float>(settings.defeatThreatStep) * threat);
        result.chainAfter = state_.chain + 1u;
        result.defeatedActors = state_.activeDefeatedActors;
        result.peakEligibleActors = state_.activePeakEligibleActors;
        AddResult(std::move(result), settings.maximumResultsPerFrame);
    }

    if (input.showcasesEnabled) UpdateShowcases(input, settings);
    if (input.pacing == nullptr) return;
    for (const EnemyEncounterPacingEvent& event : input.pacing->events) {
        const bool recoveryReleased =
            event.kind == EnemyEncounterPacingEventKind::PhaseChanged &&
            event.phase == EnemyEncounterBeatPhase::ExitResolve;
        const bool completedFallback =
            event.kind == EnemyEncounterPacingEventKind::BeatCompleted;
        if ((!recoveryReleased && !completedFallback) ||
            event.beatGuid != state_.activeBeatGuid ||
            Contains(state_.completedBeatGuids, event.beatGuid)) {
            continue;
        }
        const bool fullSweep = state_.activePeakEligibleActors > 0 &&
            state_.activeDefeatedActors >= state_.activePeakEligibleActors;
        float clearScore =
            static_cast<float>(settings.clearBaseScore) +
            static_cast<float>(settings.clearConcurrencyStep) * concurrent +
            static_cast<float>(settings.clearThreatStep) * threat;
        if (fullSweep) clearScore += settings.fullSweepBonus;
        if (state_.activeBeatNoDamage) {
            clearScore *= settings.cleanClearMultiplier;
        }
        EncounterPerformanceScoreResult result{};
        result.kind = state_.activeBeatNoDamage
            ? EncounterPerformanceScoreKind::CleanClear
            : EncounterPerformanceScoreKind::EncounterClear;
        result.beatGuid = event.beatGuid;
        result.encounterId = event.encounterId;
        result.scoreAwarded = SaturatedScore(clearScore);
        result.chainAfter = state_.chain + (state_.activeBeatNoDamage ? 2u : 1u);
        result.defeatedActors = state_.activeDefeatedActors;
        result.peakEligibleActors = state_.activePeakEligibleActors;
        result.fullSweep = fullSweep;
        AddResult(std::move(result), settings.maximumResultsPerFrame);
        state_.completedBeatGuids.push_back(event.beatGuid);
        state_.activeBeatGuid.clear();
        state_.activeEncounterId.clear();
        state_.activeWaveGuid.clear();
        ++state_.revision;
    }
}

void EncounterPerformanceScoreSystem::AwardShowcase(EncounterPerformanceScoreKind kind,
    const std::string& key, uint32_t count, uint32_t score, size_t maximumResults) {
    if (Contains(state_.showcaseKeys,key) || state_.showcaseKeys.size()>=128) return;
    EncounterPerformanceScoreResult result;
    result.kind=kind;result.encounterId=key;result.scoreAwarded=score;
    // The kills/defense events already extend the chain. This is an extra
    // reward for the way they were performed, not a second combo increment.
    result.chainAfter=state_.chain;result.defeatedActors=count;
    const auto before=resultsThisFrame_.size();
    AddResult(std::move(result),maximumResults);
    if (resultsThisFrame_.size()==before) return;
    state_.showcaseKeys.push_back(key);
    state_.showcaseKind=kind;state_.showcaseCount=count;state_.showcaseScore=score;
    state_.showcaseSecondsRemaining=1.25f;
}

void EncounterPerformanceScoreSystem::UpdateShowcases(const EncounterPerformanceScoreInput& input,
    const EncounterPerformanceScoreSettings& settings) {
    const float dt=std::isfinite(input.deltaTime)?(std::clamp)(input.deltaTime,0.0f,0.25f):0.0f;
    if (dt<=0) return;
    state_.showcaseSecondsRemaining=(std::max)(0.0f,state_.showcaseSecondsRemaining-dt);
    state_.interceptWindowRemaining=(std::max)(0.0f,state_.interceptWindowRemaining-dt);
    if (state_.interceptWindowRemaining==0) state_.interceptCount=0;
    const auto wave=input.pacing?input.pacing->waveGuid:std::string{};
    if (wave=="short_lock" || wave=="short-exit-guard") {
        for (const auto& volley:input.lockVolleys) if (volley.defeats>=4)
            AwardShowcase(EncounterPerformanceScoreKind::LockSweep,"sweep:"+wave,volley.defeats,
                settings.lockSweepScore,settings.maximumResultsPerFrame);
    }
    if (input.weaponFeedback) for (const auto& event:input.combatEvents) {
        if (event.kind!=EnemyCombatEventKind::Defeated || event.actorAssetId!="short_turret") continue;
        const auto& recent=input.weaponFeedback->RecentEvents();
        const auto hit=std::find_if(recent.begin(),recent.end(),[&](const auto& feedback) {
            return feedback.shotId==event.shotId && feedback.targetActorId==event.actorId &&
                feedback.destroyed && !feedback.lockOnContact;
        });
        if (hit!=recent.end()) AwardShowcase(EncounterPerformanceScoreKind::BatteryBreak,
            "battery:"+std::to_string(event.actorId),1,settings.batteryBreakScore,settings.maximumResultsPerFrame);
    }
    if (input.runtime && wave=="short_intercept") {
        for (const auto& actor:input.runtime->Enemies()) if(actor.desc.waveId==wave &&
            !Contains(state_.interceptOwnerIds,actor.actorId) && state_.interceptOwnerIds.size()<64)
            state_.interceptOwnerIds.push_back(actor.actorId);
    }
    for (const auto& shot:input.shootDownResults) {
        if (!shot.accepted || !shot.destroyed || !shot.projectileId ||
            !Contains(state_.interceptOwnerIds,shot.ownerActorId) || state_.interceptProjectileIds.size()>=256 ||
            std::find(state_.interceptProjectileIds.begin(),state_.interceptProjectileIds.end(),shot.projectileId)!=state_.interceptProjectileIds.end()) continue;
        state_.interceptProjectileIds.push_back(shot.projectileId);
        if (!state_.interceptCount) state_.interceptWindowRemaining=2.5f;
        if (++state_.interceptCount>=2) AwardShowcase(EncounterPerformanceScoreKind::ProjectileIntercept,
            "intercept:short_intercept",state_.interceptCount,settings.projectileInterceptScore,settings.maximumResultsPerFrame);
    }
}

bool EncounterPerformanceScoreSystem::RestoreState(
    const EncounterPerformanceScoreRuntimeState& state,
    std::string* errorMessage) {
    if (state.chain > state.maximumChain ||
        !std::isfinite(state.maximumThreatBudget) ||
        state.maximumThreatBudget < 0.0f ||
        state.maximumConcurrentAttackers == 0 || state.showcaseKeys.size()>128 || state.interceptOwnerIds.size()>64 ||
        state.interceptProjectileIds.size()>256 || !std::isfinite(state.interceptWindowRemaining) ||
        state.interceptWindowRemaining<0 || state.interceptWindowRemaining>2.5f ||
        !std::isfinite(state.showcaseSecondsRemaining) || state.showcaseSecondsRemaining<0 || state.showcaseSecondsRemaining>1.25f) {
        SetError(errorMessage, "Encounter score checkpoint is invalid.");
        return false;
    }
    state_ = state;
    // Projectiles are cleared by checkpoint retry; an old partial interception
    // and a success toast must not carry across that recovery.
    state_.interceptCount=0;state_.interceptWindowRemaining=0;state_.showcaseSecondsRemaining=0;
    resultsThisFrame_.clear();
    if (errorMessage != nullptr) errorMessage->clear();
    return true;
}

const char* ToString(EncounterPerformanceScoreKind kind) noexcept {
    switch (kind) {
    case EncounterPerformanceScoreKind::EnemyDefeat: return "EnemyDefeat";
    case EncounterPerformanceScoreKind::EncounterClear: return "EncounterClear";
    case EncounterPerformanceScoreKind::CleanClear: return "CleanClear";
    case EncounterPerformanceScoreKind::LockSweep: return "LockSweep";
    case EncounterPerformanceScoreKind::BatteryBreak: return "BatteryBreak";
    case EncounterPerformanceScoreKind::ProjectileIntercept: return "ProjectileIntercept";
    }
    return "Unknown";
}
