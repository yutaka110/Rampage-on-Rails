#pragma once
#include <string>

struct AppVfxRuntimeState;
struct AppRuntimeState;

enum class AppStartupScene {
    RailShooter,
    VfxPreview,
    MultiMaterialShowcase,
};

enum class CombatLoopDefenseUiProofVariant {
    Disabled,
    Sequence,
    Interrupt,
    ShootDown,
    Evade,
};

AppStartupScene ParseAppStartupSceneArguments(
    int argumentCount,
    const wchar_t* const* arguments);
AppStartupScene ResolveAppStartupSceneFromCommandLine();
bool ParseCombatLoop10SecondModeArguments(
    int argumentCount,
    const wchar_t* const* arguments) noexcept;
bool ResolveCombatLoop10SecondModeFromCommandLine();
bool ParseCombatLoopExpansionModeArguments(
    int argumentCount,
    const wchar_t* const* arguments) noexcept;
bool ResolveCombatLoopExpansionModeFromCommandLine();
bool ParseCombatLoopDefenseUiProofArguments(
    int argumentCount,
    const wchar_t* const* arguments) noexcept;
bool ResolveCombatLoopDefenseUiProofFromCommandLine();
CombatLoopDefenseUiProofVariant ParseCombatLoopDefenseUiProofVariantArguments(
    int argumentCount,
    const wchar_t* const* arguments) noexcept;
CombatLoopDefenseUiProofVariant
ResolveCombatLoopDefenseUiProofVariantFromCommandLine();

void ResetMultiMaterialShowcaseHumanoidPose(AppRuntimeState& runtimeState);
float ResolveHumanoidMovementYaw(float moveX, float moveY) noexcept;
bool DidHumanoidMovementStart(
    float previousMoveMagnitude,
    float currentMoveMagnitude) noexcept;
float AdvanceHumanoidMovementYaw(
    float currentYaw,
    float targetYaw,
    float maxDelta) noexcept;
void ApplyMultiMaterialShowcasePresentationDefaults(AppRuntimeState& runtimeState);
bool BeginSkinnedAnimationBlend(
    AppRuntimeState& runtimeState,
    unsigned int targetModelIndex,
    float durationSeconds = 0.25f) noexcept;
float AdvanceSkinnedAnimationBlend(
    AppRuntimeState& runtimeState,
    float deltaTime) noexcept;
void CompleteSkinnedAnimationBlend(AppRuntimeState& runtimeState) noexcept;
void CancelSkinnedAnimationBlend(AppRuntimeState& runtimeState) noexcept;
bool ShouldAdvancePreviewRuntime(
    bool editorRuntimeAdvance,
    bool submissionShowcaseEnabled) noexcept;

void ApplyEnvironmentRuntimeConfig(AppRuntimeState& runtimeState);
void ApplyEnvironmentRuntimeConfig(AppVfxRuntimeState& runtimeState);

std::string ParseRailCourseArguments(int count,const wchar_t* const* args);
std::string ResolveRailCoursePathFromCommandLine();
