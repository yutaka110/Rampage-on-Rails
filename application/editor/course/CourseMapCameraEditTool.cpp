#include "CourseMapCameraEditTool.h"
#if defined(_MSC_VER)
#pragma execution_character_set("utf-8")
#endif

#include <algorithm>
#include <cmath>

namespace editor {
namespace {
Vector3 Add(Vector3 a, Vector3 b) { return {a.x+b.x,a.y+b.y,a.z+b.z}; }
Vector3 Sub(Vector3 a, Vector3 b) { return {a.x-b.x,a.y-b.y,a.z-b.z}; }
Vector3 Scale(Vector3 a,float s) { return {a.x*s,a.y*s,a.z*s}; }
float Dot(Vector3 a,Vector3 b) { return a.x*b.x+a.y*b.y+a.z*b.z; }
Vector3 Cross(Vector3 a,Vector3 b) { return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x}; }
Vector3 Normalize(Vector3 v,Vector3 fallback) {
    const float l=std::sqrt(Dot(v,v));return l>0.00001f ? Scale(v,1/l) : fallback;
}
float Distance2(Vector2 a,Vector2 b) { return (a.x-b.x)*(a.x-b.x)+(a.y-b.y)*(a.y-b.y); }
float SegmentDistance2(Vector2 p,Vector2 a,Vector2 b) {
    const float length=Distance2(a,b);
    if (length<64) return 1e20f;
    const float t=(std::clamp)(((p.x-a.x)*(b.x-a.x)+(p.y-a.y)*(b.y-a.y))/length,0.25f,1.0f);
    return Distance2(p,{a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t});
}
} // namespace

CourseMapRailPick CourseMapCameraRangeTool::PickRail(const CourseMap3DFrame& frame,
    const RailPath& path, Vector2 mouse, float preferredDistance) {
    if (!frame.valid || !frame.viewport.Contains(mouse) || !frame.camera.Validate() ||
        !std::isfinite(mouse.x) || !std::isfinite(mouse.y) || path.Length()<MinimumLength) return {};
    const int steps=(std::clamp)(static_cast<int>(std::ceil(path.Length()/2.0f)),128,4096);
    float bestScore=144.0f, bestStart=0, bestEnd=0, bestDepth=1e30f;
    bool found=false;
    auto a=ProjectCourseMap3DPoint(path.Evaluate(0).position,frame.camera,frame.viewport);
    for (int i=1;i<=steps;++i) {
        const float d0=path.Length()*(i-1)/steps, d1=path.Length()*i/steps;
        const auto b=ProjectCourseMap3DPoint(path.Evaluate(d1).position,frame.camera,frame.viewport);
        if (a.valid && b.valid) {
            const float dx=b.screen.x-a.screen.x,dy=b.screen.y-a.screen.y;
            const float denom=dx*dx+dy*dy;
            const float t=denom>0.000001f ? (std::clamp)(((mouse.x-a.screen.x)*dx+(mouse.y-a.screen.y)*dy)/denom,0.0f,1.0f) : 0;
            const float worldT=t*a.depth/((1-t)*b.depth+t*a.depth);
            const float distance=d0+(d1-d0)*worldT;
            const float depth=a.depth+(b.depth-a.depth)*worldT;
            float score=Distance2(mouse,{a.screen.x+dx*t,a.screen.y+dy*t});
            if (preferredDistance>=0) score+=(std::min)(0.5f,std::abs(distance-preferredDistance)*0.005f);
            if (score<bestScore-0.0001f || (std::abs(score-bestScore)<=0.0001f && depth<bestDepth)) {
                bestScore=score;bestStart=d0;bestEnd=d1;bestDepth=depth;found=true;
            }
        }
        a=b;
    }
    if (!found) return {};
    const auto error=[&](float distance) {
        const auto point=ProjectCourseMap3DPoint(path.Evaluate(distance).position,frame.camera,frame.viewport);
        return point.valid ? Distance2(mouse,point.screen) : 1e30f;
    };
    const float segmentStart=bestStart, segmentEnd=bestEnd;
    for (int i=0;i<14;++i) {
        const float l=(bestStart*2+bestEnd)/3,r=(bestStart+bestEnd*2)/3;
        if (error(l)<error(r)) bestEnd=r; else bestStart=l;
    }
    float distance=(bestStart+bestEnd)*0.5f;
    if (error(segmentStart)<error(distance)) distance=segmentStart;
    if (error(segmentEnd)<error(distance)) distance=segmentEnd;
    if (error(distance)>144) return {};
    return {true,distance,path.Evaluate(distance).position};
}

CourseMapRangeHandle CourseMapCameraRangeTool::PickHandle(const CourseMap3DFrame& frame,
    const RailPath& path, const CourseCinematicCameraShot& shot, Vector2 mouse) {
    if (!frame.valid || !frame.viewport.Contains(mouse)) return CourseMapRangeHandle::None;
    auto a=ProjectCourseMap3DPoint(path.Evaluate(shot.startDistance).position,frame.camera,frame.viewport);
    auto b=ProjectCourseMap3DPoint(path.Evaluate(shot.endDistance).position,frame.camera,frame.viewport);
    a.screen.y-=12;b.screen.y+=12;
    const float da=a.valid?Distance2(mouse,a.screen):1e30f, db=b.valid?Distance2(mouse,b.screen):1e30f;
    if ((std::min)(da,db)>196) return CourseMapRangeHandle::None;
    return da<=db?CourseMapRangeHandle::Start:CourseMapRangeHandle::End;
}

CourseMap3DCamera CourseMapCameraRangeTool::FocusRange(const RailPath& path,float start,float end,
    CourseMap3DCamera camera,float aspect) {
    start=(std::clamp)(start,0.0f,path.Length());end=(std::clamp)(end,0.0f,path.Length());
    const auto a=path.Evaluate(start).position,b=path.Evaluate(end).position;
    camera.target=Scale(Add(a,b),0.5f);
    float radius=12;
    for (int i=0;i<=32;++i) {
        const auto v=Sub(path.Evaluate(start+(end-start)*i/32.0f).position,camera.target);
        radius=(std::max)(radius,std::sqrt(Dot(v,v)));
    }
    const float tanHalf=std::tan(camera.verticalFovRadians*0.5f)*(std::clamp)(aspect,0.1f,1.0f);
    camera.distance=(std::clamp)(radius*std::sqrt(1+tanHalf*tanHalf)/tanHalf*1.18f,35.0f,200000.0f);
    return camera;
}

bool CourseMapCameraRangeTool::ClickCreate(CourseMapRailPick pick,float railLength) {
    if (mode_!=CourseMapCameraEditMode::Create || !pick.hit || !std::isfinite(pick.distance) ||
        !std::isfinite(railLength) || railLength<MinimumLength) return false;
    const float distance=(std::clamp)(pick.distance,0.0f,railLength);
    hover_=pick;
    if (!creating_) { anchor_=distance;creating_=true;return false; }
    start_=(std::min)(anchor_,distance);end_=(std::max)(anchor_,distance);
    if (end_-start_<MinimumLength) return false;
    creating_=false;return true;
}

bool CourseMapCameraRangeTool::BeginDrag(const CourseMap3DFrame& frame,
    const CourseCinematicCameraShot& shot,CourseMapRangeHandle handle) {
    if (mode_!=CourseMapCameraEditMode::Range || !frame.valid || handle==CourseMapRangeHandle::None ||
        !std::isfinite(shot.startDistance) || !std::isfinite(shot.endDistance) || shot.startDistance<0 ||
        shot.endDistance-shot.startDistance<MinimumLength) return false;
    originalStart_=start_=shot.startDistance;originalEnd_=end_=shot.endDistance;
    shotId_=shot.id;handle_=handle;dragging_=true;
    dragFrame_={};dragFrame_.valid=true;dragFrame_.camera=frame.camera;
    dragFrame_.basis=frame.basis;dragFrame_.viewport=frame.viewport;
    return true;
}

bool CourseMapCameraRangeTool::Drag(const RailPath& path,Vector2 mouse) {
    if (!dragging_ || path.Length()<MinimumLength) return false;
    const auto& r=dragFrame_.viewport;
    mouse.y+=handle_==CourseMapRangeHandle::Start?12.0f:-12.0f;
    mouse.x=(std::clamp)(mouse.x,r.x,r.x+r.width-0.001f);
    mouse.y=(std::clamp)(mouse.y,r.y,r.y+r.height-0.001f);
    const auto pick=PickRail(dragFrame_,path,mouse,handle_==CourseMapRangeHandle::Start?start_:end_);
    if (!pick.hit) return false;
    hover_=pick;
    if (handle_==CourseMapRangeHandle::Start) start_=(std::clamp)(std::abs(pick.distance-originalStart_)<0.02f?originalStart_:pick.distance,0.0f,end_-MinimumLength);
    else end_=(std::clamp)(std::abs(pick.distance-originalEnd_)<0.02f?originalEnd_:pick.distance,start_+MinimumLength,path.Length());
    return true;
}

bool CourseMapCameraRangeTool::Matches(const CourseMap3DFrame& frame,const CourseCinematicCameraShot* shot) const {
    const auto& a=dragFrame_.viewport;const auto& b=frame.viewport;
    return shot && shot->id==shotId_ && shot->startDistance==originalStart_ && shot->endDistance==originalEnd_ &&
        a.x==b.x && a.y==b.y && a.width==b.width && a.height==b.height;
}

CourseMapCameraGeometry CourseMapCameraEditTool::BuildGeometry(
    const CourseCameraCompositionController& composition) {
    CourseMapCameraGeometry g;
    if (!composition.HasPose()) return g;
    g.valid=true;g.eye=composition.Eye();g.target=composition.Target();
    const auto f=composition.Forward();
    auto up=composition.RailUp();
    const float roll=composition.Ready()?0.0f:composition.DisplayPose().rotate.z;
    up=Add(Add(Scale(up,std::cos(roll)),Scale(Cross(f,up),std::sin(roll))),Scale(f,Dot(f,up)*(1-std::cos(roll))));
    const auto r=Normalize(Cross(up,f),{1,0,0});
    const auto u=Normalize(Cross(f,r),{0,1,0});
    const float length=(std::clamp)(std::sqrt(Dot(Sub(g.target,g.eye),Sub(g.target,g.eye))),2.0f,40.0f);
    const auto center=Add(g.eye,Scale(f,length));
    const float h=std::tan(composition.DisplayFovY()*0.5f)*length;
    const float w=h*composition.AspectRatio();
    const std::array<Vector3,4> corners{
        Add(center,Add(Scale(r,-w),Scale(u,h))),Add(center,Add(Scale(r,w),Scale(u,h))),
        Add(center,Add(Scale(r,w),Scale(u,-h))),Add(center,Add(Scale(r,-w),Scale(u,-h)))};
    for (int i=0;i<4;++i) {g.frustum[i]={g.eye,corners[i]};g.frustum[i+4]={corners[i],corners[(i+1)%4]};}
    g.frustum[8]={g.eye,g.target};
    return g;
}

Vector3 CourseMapCameraEditTool::AxisDirection(CourseMapCameraAxis axis) {
    switch(axis) {
    case CourseMapCameraAxis::X:return {1,0,0};
    case CourseMapCameraAxis::Y:return {0,1,0};
    case CourseMapCameraAxis::Z:return {0,0,1};
    default:return {};
    }
}
CourseMap3DCamera CourseMapCameraEditTool::FocusCamera(
    const CourseCameraCompositionController& composition, CourseMap3DCamera camera, float viewportAspect) {
    const auto geometry=BuildGeometry(composition);
    if (!geometry.valid) return camera;
    camera.target=Scale(Add(geometry.eye,geometry.target),0.5f);
    float radius=12;
    for (const auto& line:geometry.frustum)
        radius=(std::max)(radius,std::sqrt(Dot(Sub(line.end,camera.target),Sub(line.end,camera.target))));
    const float tanHalf=std::tan(camera.verticalFovRadians*0.5f)*(std::clamp)(viewportAspect,0.1f,1.0f);
    // Bounding-sphere fit remains valid when the map is a narrow split pane.
    camera.distance=(std::clamp)(radius*std::sqrt(1+tanHalf*tanHalf)/tanHalf*1.12f,35.0f,200000.0f);
    return camera;
}
float CourseMapCameraEditTool::AxisLength(const CourseMap3DFrame& frame,Vector3 origin) {
    const auto projected=ProjectCourseMap3DPoint(origin,frame.camera,frame.viewport);
    return (std::clamp)(2*projected.depth*std::tan(frame.camera.verticalFovRadians*0.5f)*
        48.0f/(std::max)(1.0f,frame.viewport.height),0.2f,500.0f);
}
CourseMapCameraPick CourseMapCameraEditTool::Pick(const CourseMap3DFrame& frame,
    const CourseMapCameraGeometry& g,Vector2 mouse,CourseMapCameraHandle selected) {
    if (!g.valid || !frame.valid || !frame.viewport.Contains(mouse)) return {};
    const auto eye=ProjectCourseMap3DPoint(g.eye,frame.camera,frame.viewport);
    const auto target=ProjectCourseMap3DPoint(g.target,frame.camera,frame.viewport);
    // Endpoint buttons take precedence over axes, including at the axis origin.
    if (eye.valid && Distance2(mouse,eye.screen)<=121 &&
        (!target.valid || Distance2(mouse,eye.screen)<=Distance2(mouse,target.screen)))
        return {true,CourseMapCameraHandle::Eye,CourseMapCameraAxis::ViewPlane};
    if (target.valid && Distance2(mouse,target.screen)<=121)
        return {true,CourseMapCameraHandle::Target,CourseMapCameraAxis::ViewPlane};
    const auto world=selected==CourseMapCameraHandle::Eye?g.eye:g.target;
    const auto start=ProjectCourseMap3DPoint(world,frame.camera,frame.viewport);
    if (!start.valid) return {};
    float best=49;CourseMapCameraPick result;
    for (auto axis:{CourseMapCameraAxis::X,CourseMapCameraAxis::Y,CourseMapCameraAxis::Z}) {
        const auto end=ProjectCourseMap3DPoint(Add(world,Scale(AxisDirection(axis),AxisLength(frame,world))),
            frame.camera,frame.viewport);
        if (!end.valid) continue;
        const float distance=SegmentDistance2(mouse,start.screen,end.screen);
        if (distance<best) {best=distance;result={true,selected,axis};}
    }
    return result;
}

bool CourseMapCameraEditTool::Intersect(Vector2 mouse,Vector3& hit) const {
    // Release outside the viewport is allowed; clamp the ray to its last edge.
    mouse.x=(std::clamp)(mouse.x,dragViewport_.x,dragViewport_.x+dragViewport_.width-0.001f);
    mouse.y=(std::clamp)(mouse.y,dragViewport_.y,dragViewport_.y+dragViewport_.height-0.001f);
    const auto ray=BuildCourseMap3DScreenRay(mouse,dragCamera_,dragViewport_);
    const float denominator=Dot(ray.direction,normal_);
    if (!ray.valid || std::abs(denominator)<0.0001f) return false;
    const float t=Dot(Sub(origin_,ray.origin),normal_)/denominator;
    if (!std::isfinite(t) || t<=0) return false;
    hit=Add(ray.origin,Scale(ray.direction,t));
    return true;
}
bool CourseMapCameraEditTool::Begin(CourseCameraCompositionController& composition,
    const CourseMap3DFrame& frame,CourseMapCameraPick pick,Vector2 mouse) {
    if (!composition.Ready() || !frame.valid || !pick.hit) return false;
    originalEye_=composition.Eye();originalTarget_=composition.Target();
    selected_=pick.handle;axis_=pick.axis;
    origin_=selected_==CourseMapCameraHandle::Eye?originalEye_:originalTarget_;
    dragCamera_=frame.camera;dragViewport_=frame.viewport;
    normal_=frame.basis.forward;
    if (axis_!=CourseMapCameraAxis::ViewPlane) {
        const auto direction=AxisDirection(axis_);
        normal_=Sub(normal_,Scale(direction,Dot(normal_,direction)));
        if (Dot(normal_,normal_)<0.0064f) {message_="マップを回転し、この軸が横から見える向きにしてください。";return false;}
        normal_=Normalize(normal_,frame.basis.forward);
    }
    if (!Intersect(mouse,pressHit_)) return false;
    dragging_=true;message_.clear();return true;
}
bool CourseMapCameraEditTool::Drag(CourseCameraCompositionController& composition,Vector2 mouse) {
    if (!dragging_) return false;
    if (!composition.Ready()) {End();return false;}
    Vector3 hit;
    if (!Intersect(mouse,hit)) return false;
    auto delta=Sub(hit,pressHit_);
    if (axis_!=CourseMapCameraAxis::ViewPlane) {
        const auto direction=AxisDirection(axis_);delta=Scale(direction,Dot(delta,direction));
    }
    if (Dot(delta,delta)>1000000.0f) {message_="移動量が編集可能な範囲を超えています。";return false;}
    const auto eye=selected_==CourseMapCameraHandle::Eye?Add(originalEye_,delta):originalEye_;
    const auto target=selected_==CourseMapCameraHandle::Target?Add(originalTarget_,delta):originalTarget_;
    return composition.SetWorldPose(eye,target,&message_);
}
void CourseMapCameraEditTool::Cancel(CourseCameraCompositionController& composition) {
    const bool wasDragging=dragging_;
    if (dragging_ && composition.Ready()) composition.SetWorldPose(originalEye_,originalTarget_);
    End();
    message_=wasDragging ? "移動を取り消しました。" : "";
}

} // namespace editor
