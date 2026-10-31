#pragma once

struct AppFrameGraphBuildContext;

class AppSceneRenderPipeline {
public:
    static void RegisterEnemyThreatVisualPass(const AppFrameGraphBuildContext& context);
    void RegisterPasses(const AppFrameGraphBuildContext& context) const;
    void RegisterRailBoostProtectionPass(const AppFrameGraphBuildContext& context) const;
};
