#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <span>
#include <string_view>
#include <vector>
#include "RailGameOverScene.h"

struct RailGameOverDioramaModel { std::string_view mesh;const ModelData* model=nullptr; };
enum class RailGameOverDioramaLayer { Ground,Shadow,Model,Smoke };
struct RailGameOverDioramaVertex { Vector2 position{};Vector4 color{}; };
struct RailGameOverDioramaTriangle {
    std::array<RailGameOverDioramaVertex,3> vertices{};
    RailGameOverDioramaLayer layer=RailGameOverDioramaLayer::Model;
};
struct RailGameOverDioramaFrame {
    std::vector<RailGameOverDioramaTriangle> triangles;
    size_t pieces=0,wheels=0,panels=0,modelTriangles=0;
    float minimumHeight=0;
};

// A fixed, lit 3D still life made from the game's original solid meshes.
// Projected triangles share the ordered HUD batch: blackout, still life, text.
// No gameplay camera, world transforms, depth target or checkpoint is changed.
class RailGameOverDiorama final {
public:
    static constexpr size_t MaxTriangles=3000;
    bool Ready() const { return !geometry_.empty(); }
    void Reset() { geometry_.clear();shadows_.clear();pieces_=wheels_=panels_=0; }
    void Initialize(std::span<const RailGameOverDioramaModel> models) {
        using namespace rail_game_over_detail;
        Reset();std::vector<Piece> parts;
        const auto find=[&](std::string_view name)->const ModelData* {
            for(const auto& model:models) if(model.mesh==name) return model.model;
            return nullptr;
        };
        const auto add=[&](std::string_view name,Matrix4x4 world,int group,bool wheel=false,bool panel=false) {
            if(const auto* model=find(name);model && !model->indices.empty()) parts.push_back({model,world,group,wheel,panel});
        };
        const auto about=[](Vector3 pivot,Vector3 angles,Vector3 offset) {
            return Multiply(Multiply(MakeTranslateMatrix({-pivot.x,-pivot.y,-pivot.z}),
                MakeAffineMatrix(Vector3{1,1,1},angles,Vector3{})),MakeTranslateMatrix(Add(pivot,offset)));
        };
        const auto body=MakeAffineMatrix(Vector3{1,1,1},Vector3{0.10f,0.24f,0.46f},Vector3{});
        for(size_t i=0;i<RailVehicleBreakaway::FractureParts.size();++i) {
            const auto& shape=RailVehicleBreakaway::FractureParts[i];
            const auto centre=Mul(Add(shape.minimum,shape.maximum),0.5f);
            Vector3 angle{},offset{};
            if(i<2) {angle={i?0.13f:-0.10f,i?0.025f:-0.025f,0};offset={i?0.12f:-0.12f,0,i?0.14f:-0.14f};}
            if(i==2) {angle={0,0,0.38f};offset={0.14f,0,0};}
            if(i==3) {angle={0.60f,0.04f,0};offset={0,-0.20f,-0.12f};}
            if(i==4) {angle={-0.65f,-0.08f,0};offset={0,-0.17f,0.14f};}
            if(i==7) {angle={0,0.12f,-0.26f};offset={-0.22f,-0.05f,0};}
            add(shape.mesh,Multiply(about(centre,angle,offset),body),0);
        }
        for(size_t i:{0u,1u,3u}) {
            auto wheel=MakeAffineMatrix(Vector3{0.42f,1.24f,1.24f},Vector3{},Vector3{i%2?1.9f:-1.9f,-0.62f,i<2?-3.28f:3.28f});
            add("course_rail.wheel_proxy",Multiply(wheel,body),0,true);
        }
        add("course_rail.wheel_proxy",MakeAffineMatrix(Vector3{0.42f,1.24f,1.24f},Vector3{0.18f,-0.35f,0.24f},Vector3{-3.50f,0,-2.50f}),1,true);
        auto panel=Multiply(MakeTranslateMatrix(Mul(RailVehicleBreakaway::PanelCentre,-1)),
            MakeAffineMatrix(Vector3{1,1,1},Vector3{0,-0.42f,1.48f},Vector3{-3.25f,0,1.65f}));
        add(RailVehicleBreakaway::PanelMesh,panel,2,false,true);
        // Each independent object rests on the same y=0 ground plane.
        for(int group=0;group<3;++group) {
            float minimum=std::numeric_limits<float>::infinity();
            for(const auto& part:parts) if(part.group==group) for(const auto& v:part.model->vertices)
                minimum=(std::min)(minimum,Point({v.position.x,v.position.y,v.position.z},part.world).y);
            if(std::isfinite(minimum)) for(auto& part:parts) if(part.group==group)
                part.world=Multiply(part.world,MakeTranslateMatrix({0,0.025f-minimum,0}));
        }
        minimumHeight_=std::numeric_limits<float>::infinity();
        Vector3 minimum{10000,10000,10000},maximum{-10000,-10000,-10000};
        for(const auto& part:parts) {
            ++pieces_;wheels_+=part.wheel?1:0;panels_+=part.panel?1:0;
            Vector3 lo{10000,10000,10000},hi{-10000,-10000,-10000};
            for(const auto& v:part.model->vertices) {
                const auto p=Point({v.position.x,v.position.y,v.position.z},part.world);
                lo={(std::min)(lo.x,p.x),(std::min)(lo.y,p.y),(std::min)(lo.z,p.z)};
                hi={(std::max)(hi.x,p.x),(std::max)(hi.y,p.y),(std::max)(hi.z,p.z)};
            }
            minimumHeight_=(std::min)(minimumHeight_,lo.y);
            minimum={(std::min)(minimum.x,lo.x),(std::min)(minimum.y,lo.y),(std::min)(minimum.z,lo.z)};
            maximum={(std::max)(maximum.x,hi.x),(std::max)(maximum.y,hi.y),(std::max)(maximum.z,hi.z)};
            shadows_.push_back({{(lo.x+hi.x)*0.5f,0.01f,(lo.z+hi.z)*0.5f},
                (std::max)(0.30f,(hi.x-lo.x)*0.52f),(std::max)(0.35f,(hi.z-lo.z)*0.52f)});
            auto inverse=part.world;
            const auto normalMatrix=Transpose(Inverse(inverse));
            for(const auto& sub:part.model->subMeshes) {
                const auto& material=part.model->materials[sub.materialIndex];
                Vector3 albedo{0.40f,0.31f,0.20f};
                if(material.name=="safety_trim") albedo={0.67f,0.33f,0.075f};
                if(part.wheel || material.name=="iron_running_gear") albedo={0.105f,0.115f,0.125f};
                for(size_t i=sub.indexStart;i+2<sub.indexStart+sub.indexCount;i+=3) {
                    WorldTriangle triangle;
                    for(int j=0;j<3;++j) {
                        const auto& v=part.model->vertices[part.model->indices[i+j]];
                        const auto p=Point({v.position.x,v.position.y,v.position.z},part.world);
                        auto n=Point(v.normal,normalMatrix);n=Unit(Add(n,Mul(Origin(normalMatrix),-1)),{0,1,0});
                        const float key=(std::max)(0.0f,Dot(n,Unit({-0.5f,1.0f,-0.6f})));
                        const float rim=(std::max)(0.0f,Dot(n,Unit({0.7f,0.5f,0.4f})));
                        const float soot=0.76f+0.08f*std::sin(p.x*3.1f+p.z*2.7f);
                        triangle.position[j]=p;
                        triangle.color[j]={(albedo.x*(0.40f+0.95f*key)+0.08f*rim)*soot,
                            (albedo.y*(0.43f+0.85f*key)+0.10f*rim)*soot,
                            (albedo.z*(0.48f+0.73f*key)+0.13f*rim)*soot,1};
                    }
                    geometry_.push_back(triangle);
                }
            }
        }
        centre_=Mul(Add(minimum,maximum),0.5f);
        smokeHeight_=maximum.y-0.55f;
        eye_=Add(centre_,{-13,9,-17});forward_=Unit(Add(centre_,Mul(eye_,-1)));
        right_=Unit(Cross({0,1,0},forward_),{1,0,0});up_=Cross(forward_,right_);
        // Backface cull against the still-life camera, then keep a stable painter
        // order for this fixed view. Smoke is inserted into that depth order.
        std::erase_if(geometry_,[&](const auto& t) {
            const auto normal=Cross(Add(t.position[1],Mul(t.position[0],-1)),Add(t.position[2],Mul(t.position[0],-1)));
            return Dot(normal,Add(eye_,Mul(t.position[0],-1)))<=0;
        });
        projectedMin_={10000,10000};projectedMax_={-10000,-10000};
        for(const auto& t:geometry_) for(const auto& p:t.position) {
            const auto q=Project(p);
            projectedMin_={(std::min)(projectedMin_.x,q.x),(std::min)(projectedMin_.y,q.y)};
            projectedMax_={(std::max)(projectedMax_.x,q.x),(std::max)(projectedMax_.y,q.y)};
        }
    }
    RailGameOverDioramaFrame Evaluate(float width,float height,float time,float opacity) const {
        using namespace rail_game_over_detail;
        RailGameOverDioramaFrame frame;
        if(!Ready() || width<=0 || height<=0 || opacity<=0) return frame;
        frame.pieces=pieces_;frame.wheels=wheels_;frame.panels=panels_;frame.minimumHeight=minimumHeight_;
        const auto layout=GameOverLayout(width,height);
        const float scale=(std::min)(layout.preview.width/(projectedMax_.x-projectedMin_.x),layout.preview.height/(projectedMax_.y-projectedMin_.y))*0.90f;
        const Vector2 midpoint{(projectedMin_.x+projectedMax_.x)*0.5f,(projectedMin_.y+projectedMax_.y)*0.5f};
        const auto screen=[&](Vector3 p) {const auto q=Project(p);return Vector2{layout.preview.x+layout.preview.width*0.5f+(q.x-midpoint.x)*scale,
            layout.preview.y+layout.preview.height*0.5f-(q.y-midpoint.y)*scale};};
        const auto emit=[&](const WorldTriangle& world,RailGameOverDioramaLayer layer) {
            if(frame.triangles.size()>=MaxTriangles) return;
            RailGameOverDioramaTriangle triangle;triangle.layer=layer;
            for(int i=0;i<3;++i) {
                const auto p=screen(world.position[i]);auto color=world.color[i];color.w*=opacity;
                // Feather the ground into black before the text column.
                if(layer==RailGameOverDioramaLayer::Ground || layer==RailGameOverDioramaLayer::Shadow)
                    color.w*=Ramp(p.x,0,width*0.06f)*(1-Ramp(p.x,width*0.44f,width*0.50f));
                triangle.vertices[i]={p,color};
            }
            const auto& a=triangle.vertices[0].position;const auto& b=triangle.vertices[1].position;const auto& c=triangle.vertices[2].position;
            if(std::abs((b.x-a.x)*(c.y-a.y)-(b.y-a.y)*(c.x-a.x))>0.0001f) frame.triangles.push_back(triangle);
        };
        const auto ellipse=[&](Vector3 centre,float xRadius,float zRadius,Vector3 color,float alpha,RailGameOverDioramaLayer layer) {
            constexpr int segments=24;
            for(int ring=0;ring<2;++ring) for(int i=0;i<segments;++i) {
                const float a=float(i)*6.2831853f/segments,b=float(i+1)*6.2831853f/segments;
                const float inner=ring?0.55f:0,outer=ring?1:0.55f;
                const auto p=[&](float angle,float r){return Add(centre,{std::cos(angle)*xRadius*r,0,std::sin(angle)*zRadius*r});};
                const auto c=[&](float r){return Vector4{color.x,color.y,color.z,alpha*(1-r*r)};};
                WorldTriangle first{{p(a,inner),p(b,inner),p(a,outer)},{c(inner),c(inner),c(outer)}};
                WorldTriangle second{{p(a,outer),p(b,inner),p(b,outer)},{c(outer),c(inner),c(outer)}};
                emit(first,layer);emit(second,layer);
            }
        };
        ellipse({centre_.x,-0.04f,centre_.z},8.5f,7.5f,{0.095f,0.085f,0.069f},0.95f,RailGameOverDioramaLayer::Ground);
        for(const auto& shadow:shadows_) ellipse(shadow.centre,shadow.x,shadow.z,{0.002f,0.002f,0.002f},0.38f,RailGameOverDioramaLayer::Shadow);
        std::vector<WorldTriangle> sorted=geometry_;
        for(int i=0;i<5;++i) {
            const float age=std::fmod((std::max)(0.0f,time)+float(i)*0.61f,3.2f);
            const Vector3 center{centre_.x-0.6f-0.12f*age,smokeHeight_+age*0.65f,centre_.z-0.4f+float(i%2)*0.45f};
            const float radius=0.32f+age*0.18f,alpha=0.12f*Ramp(age,0,0.45f)*(1-Ramp(age,2.1f,3.2f));
            for(int j=0;j<24;++j) {
                const float a=float(j)*6.2831853f/24,b=float(j+1)*6.2831853f/24;
                const auto at=[&](float angle){return Add(center,Add(Mul(right_,std::cos(angle)*radius),Mul(up_,std::sin(angle)*radius*1.35f)));};
                WorldTriangle smoke{{center,at(a),at(b)},{{{0.36f,0.37f,0.38f,alpha},{0.36f,0.37f,0.38f,0},{0.36f,0.37f,0.38f,0}}}};
                smoke.smoke=true;sorted.push_back(smoke);
            }
        }
        const auto depth=[&](const WorldTriangle& t){return Dot(Mul(Add(Add(t.position[0],t.position[1]),t.position[2]),1.0f/3),forward_);};
        std::stable_sort(sorted.begin(),sorted.end(),[&](const auto& a,const auto& b){return depth(a)>depth(b);});
        for(const auto& t:sorted) {emit(t,t.smoke?RailGameOverDioramaLayer::Smoke:RailGameOverDioramaLayer::Model);if(!t.smoke) ++frame.modelTriangles;}
        return frame;
    }
private:
    struct Piece {const ModelData* model;Matrix4x4 world;int group;bool wheel,panel;};
    struct WorldTriangle {std::array<Vector3,3> position{};std::array<Vector4,3> color{};bool smoke=false;};
    struct Shadow {Vector3 centre;float x,z;};
    Vector2 Project(Vector3 p) const {
        using namespace rail_game_over_detail;
        const auto v=Add(p,Mul(eye_,-1));const float z=(std::max)(0.1f,Dot(v,forward_));
        return {Dot(v,right_)/z,Dot(v,up_)/z};
    }
    std::vector<WorldTriangle> geometry_;
    std::vector<Shadow> shadows_;
    size_t pieces_=0,wheels_=0,panels_=0;
    float minimumHeight_=0,smokeHeight_=0;
    Vector3 centre_{},eye_{},right_{1,0,0},up_{0,1,0},forward_{0,0,1};
    Vector2 projectedMin_{},projectedMax_{};
};
