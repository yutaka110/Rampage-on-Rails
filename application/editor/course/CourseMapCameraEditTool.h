#pragma once

#include "CourseCameraCompositionController.h"
#include "CourseMap3DViewportRenderer.h"

#include <array>
#include <string>

namespace editor {

enum class CourseMapCameraHandle { Eye, Target };
enum class CourseMapCameraAxis { ViewPlane, X, Y, Z };

struct CourseMapCameraLine {
    Vector3 start{}, end{};
};
struct CourseMapCameraGeometry {
    bool valid = false;
    Vector3 eye{}, target{};
    std::array<CourseMapCameraLine, 9> frustum{};
};
struct CourseMapCameraPick {
    bool hit = false;
    CourseMapCameraHandle handle = CourseMapCameraHandle::Eye;
    CourseMapCameraAxis axis = CourseMapCameraAxis::ViewPlane;
};

enum class CourseMapCameraEditMode { Range, Create, Composition };
enum class CourseMapRangeHandle { None, Start, End };
struct CourseMapRailPick {
    bool hit = false;
    float distance = 0;
    Vector3 world{};
};

// Range gestures keep their preview outside the document. Release commits one
// sequencer transaction; cancellation never needs to repair authored data.
class CourseMapCameraRangeTool final {
public:
    static constexpr float MinimumLength = 1.0f;
    static CourseMapRailPick PickRail(const CourseMap3DFrame& frame, const RailPath& path,
        Vector2 mouse, float preferredDistance = -1.0f);
    static CourseMapRangeHandle PickHandle(const CourseMap3DFrame& frame, const RailPath& path,
        const CourseCinematicCameraShot& shot, Vector2 mouse);
    static CourseMap3DCamera FocusRange(const RailPath& path, float start, float end,
        CourseMap3DCamera camera, float aspect);
    void SetMode(CourseMapCameraEditMode mode) { Cancel(); mode_ = mode; }
    CourseMapCameraEditMode Mode() const { return mode_; }
    void Cancel() { dragging_ = false; creating_ = false; hover_ = {}; }
    bool Creating() const { return creating_; }
    bool Dragging() const { return dragging_; }
    bool Busy() const { return creating_ || dragging_; }
    float Start() const { return start_; }
    float End() const { return end_; }
    float Anchor() const { return anchor_; }
    const CourseMapRailPick& Hover() const { return hover_; }
    void SetHover(CourseMapRailPick hover) { hover_ = hover; }
    bool ClickCreate(CourseMapRailPick pick, float railLength);
    bool BeginDrag(const CourseMap3DFrame& frame, const CourseCinematicCameraShot& shot,
        CourseMapRangeHandle handle);
    bool Drag(const RailPath& path, Vector2 mouse);
    void EndDrag() { dragging_ = false; }
    bool Matches(const CourseMap3DFrame& frame, const CourseCinematicCameraShot* shot) const;
    CourseMapRangeHandle Handle() const { return handle_; }
    const std::string& ShotId() const { return shotId_; }
private:
    CourseMapCameraEditMode mode_ = CourseMapCameraEditMode::Range;
    CourseMapRangeHandle handle_ = CourseMapRangeHandle::None;
    bool dragging_ = false, creating_ = false;
    float start_ = 0, end_ = 0, anchor_ = 0;
    float originalStart_ = 0, originalEnd_ = 0;
    std::string shotId_;
    CourseMap3DFrame dragFrame_{};
    CourseMapRailPick hover_{};
};

// Independent of the retained terrain/actor frame. Camera drags must never
// rebuild terrain or select/move course actors, and commit only on Register.
class CourseMapCameraEditTool final {
public:
    static CourseMapCameraGeometry BuildGeometry(const CourseCameraCompositionController& composition);
    static CourseMap3DCamera FocusCamera(const CourseCameraCompositionController& composition,
        CourseMap3DCamera camera, float viewportAspect = 1.0f);
    static float AxisLength(const CourseMap3DFrame& frame, Vector3 origin);
    static Vector3 AxisDirection(CourseMapCameraAxis axis);
    static CourseMapCameraPick Pick(const CourseMap3DFrame& frame,
        const CourseMapCameraGeometry& geometry, Vector2 mouse, CourseMapCameraHandle selected);

    bool Begin(CourseCameraCompositionController& composition, const CourseMap3DFrame& frame,
        CourseMapCameraPick pick, Vector2 mouse);
    bool Drag(CourseCameraCompositionController& composition, Vector2 mouse);
    void End() { dragging_ = false; }
    void Cancel(CourseCameraCompositionController& composition);
    bool Dragging() const { return dragging_; }
    CourseMapCameraHandle SelectedHandle() const { return selected_; }
    void SelectHandle(CourseMapCameraHandle handle) { if (!dragging_) selected_ = handle; }
    const std::string& Message() const { return message_; }
    const CourseOverviewMapRect& DragViewport() const { return dragViewport_; }

private:
    bool Intersect(Vector2 mouse, Vector3& hit) const;
    CourseMapCameraHandle selected_ = CourseMapCameraHandle::Eye;
    CourseMapCameraAxis axis_ = CourseMapCameraAxis::ViewPlane;
    CourseMap3DCamera dragCamera_{};
    CourseOverviewMapRect dragViewport_{};
    Vector3 originalEye_{}, originalTarget_{}, origin_{}, normal_{}, pressHit_{};
    bool dragging_ = false;
    std::string message_;
};

} // namespace editor
