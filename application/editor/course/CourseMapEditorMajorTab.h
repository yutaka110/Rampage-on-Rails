#pragma once

#include "CourseMapEditorWorkspace.h"
#include "CourseOverviewMapPanel.h"
#include "../EditorPanelLayoutService.h"

namespace editor {

class CourseMapEditorMajorTab final {
public:
    static EditorPanelRect ResolvePresentationRect(
        const EditorPanelLayoutService& layout,
        bool maximized, bool cameraEditing = false);

    void Draw(
        CourseMapEditorWorkspace& workspace,
        CourseOverviewMapController& controller,
        const CourseOverviewMapPanelContext& context,
        const EditorPanelRect& rect);

    void DrawCompactEntry(CourseMapEditorWorkspace& workspace) const;
    void SetCameraEditing(bool editing) { cameraEditing_ = editing; }
    bool CameraEditing() const { return cameraEditing_; }

private:
    CourseMapCameraEditTool cameraTool_{};
    CourseMapCameraRangeTool rangeTool_{};
    CourseMapEditCategory editCategory_ = CourseMapEditCategory::Course;
    bool cameraEditing_ = false;
    bool wasCompositionReady_ = false;
    bool followPreview_ = true;
    bool wasPlaying_ = false;
    std::string lastCameraShot_;
};

} // namespace editor
