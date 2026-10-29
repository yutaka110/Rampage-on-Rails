#pragma once

#include <functional>
#include <string>

struct AppFrameGraphBuildContext;
class PostProcessStack;
struct PostProcessExecutionPlan;
namespace ge3::graphics { struct RenderPassContext; }

class AppPostProcessPipeline {
public:
    static PostProcessExecutionPlan BuildExecutionPlan(const PostProcessStack& stack,bool sunfall);
    // Read-only-depth world effects belong after atmosphere, before exposure.
    static const char* WorldVfxTarget(const AppFrameGraphBuildContext& context);
    static void RegisterWorldVfxPass(const AppFrameGraphBuildContext& context,
        const std::string& name,
        std::function<void(ge3::graphics::RenderPassContext&)> draw);
    void RegisterPasses(const AppFrameGraphBuildContext& context) const;
    // Register after gameplay HUD and before ImGui reads the viewport image.
    void RegisterFilmBurnPresentationPass(const AppFrameGraphBuildContext& context) const;
};
