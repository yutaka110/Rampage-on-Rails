#pragma once

struct AppFrameGraphBuildContext;

class AppPostProcessPipeline {
public:
    void RegisterPasses(const AppFrameGraphBuildContext& context) const;
    // Register after gameplay HUD and before ImGui reads the viewport image.
    void RegisterFilmBurnPresentationPass(const AppFrameGraphBuildContext& context) const;
};
