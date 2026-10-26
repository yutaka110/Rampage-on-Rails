#pragma once

struct AppFrameGraphBuildContext;

class AppSceneRenderPipeline {
public:
    void RegisterPasses(const AppFrameGraphBuildContext& context) const;
    void RegisterRailBoostProtectionPass(const AppFrameGraphBuildContext& context) const;
};
