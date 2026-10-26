#pragma once

struct AppFrameGraphBuildContext;
class PostProcessStack;
struct PostProcessExecutionPlan;

class AppPostProcessPipeline {
public:
    static PostProcessExecutionPlan BuildExecutionPlan(const PostProcessStack& stack,bool sunfall);
    void RegisterPasses(const AppFrameGraphBuildContext& context) const;
    // Register after gameplay HUD and before ImGui reads the viewport image.
    void RegisterFilmBurnPresentationPass(const AppFrameGraphBuildContext& context) const;
};
