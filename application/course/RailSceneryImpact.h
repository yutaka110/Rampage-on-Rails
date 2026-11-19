#pragma once
#include "RailAimState.h"
#include "utils/math/MathUtils.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <numeric>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

// Primary-fire contact against immutable authored stone. This does not turn
// decorative landmarks into vehicle obstacles or camera/lock-on targets.
namespace RailSceneryImpact {
inline bool IsStone(std::string_view mesh) {
    return mesh=="organic_arch_large" || mesh=="root_spire_column" || mesh=="canyon_mesa" ||
        mesh=="rear_boulder" || mesh=="rib_tunnel_wall" || mesh=="rib_tunnel_inner" ||
        mesh=="curved_canyon_wall" || mesh=="vista_hole_wall" || mesh=="spire_broken_bridge_arc";
}
inline Vector3 Sub(Vector3 a,Vector3 b) {return {a.x-b.x,a.y-b.y,a.z-b.z};}
inline Vector3 Cross(Vector3 a,Vector3 b) {return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
inline float Dot(Vector3 a,Vector3 b) {return a.x*b.x+a.y*b.y+a.z*b.z;}
inline Vector3 Unit(Vector3 p) {const float n=std::sqrt(Dot(p,p));return n>.000001f?Vector3{p.x/n,p.y/n,p.z/n}:Vector3{};}
inline Vector3 TransformPoint(Vector3 p,const Matrix4x4& m,float w) {
    return {p.x*m.m[0][0]+p.y*m.m[1][0]+p.z*m.m[2][0]+w*m.m[3][0],
        p.x*m.m[0][1]+p.y*m.m[1][1]+p.z*m.m[2][1]+w*m.m[3][1],
        p.x*m.m[0][2]+p.y*m.m[1][2]+p.z*m.m[2][2]+w*m.m[3][2]};
}
struct Triangle {Vector3 a,b,c;};
struct Bounds {
    Vector3 lo{1.e30f,1.e30f,1.e30f},hi{-1.e30f,-1.e30f,-1.e30f};
    void Include(Vector3 p) {
        lo={(std::min)(lo.x,p.x),(std::min)(lo.y,p.y),(std::min)(lo.z,p.z)};
        hi={(std::max)(hi.x,p.x),(std::max)(hi.y,p.y),(std::max)(hi.z,p.z)};
    }
    bool Hit(Vector3 origin,Vector3 direction,float limit) const {
        const float o[]{origin.x,origin.y,origin.z},d[]{direction.x,direction.y,direction.z};
        const float a[]{lo.x,lo.y,lo.z},b[]{hi.x,hi.y,hi.z};
        float near=0,far=limit;
        for(int i=0;i<3;++i) {
            if(std::abs(d[i])<1.e-9f) {if(o[i]<a[i] || o[i]>b[i])return false;continue;}
            float x=(a[i]-o[i])/d[i],y=(b[i]-o[i])/d[i];if(x>y)std::swap(x,y);
            near=(std::max)(near,x);far=(std::min)(far,y);if(near>far)return false;
        }
        return true;
    }
};
class Mesh final {
    struct Node {Bounds bounds;uint32_t begin=0,count=0,left=0,right=0;};
    std::vector<Triangle> triangles_;
    std::vector<uint32_t> order_;
    std::vector<Node> nodes_;
    uint32_t Build(uint32_t begin,uint32_t end) {
        const auto index=uint32_t(nodes_.size());nodes_.push_back({});
        Bounds bounds,centers;
        for(auto i=begin;i<end;++i) {
            const auto& t=triangles_[order_[i]];bounds.Include(t.a);bounds.Include(t.b);bounds.Include(t.c);
            centers.Include({t.a.x+t.b.x+t.c.x,t.a.y+t.b.y+t.c.y,t.a.z+t.b.z+t.c.z});
        }
        nodes_[index].bounds=bounds;
        if(end-begin<=8) {nodes_[index].begin=begin;nodes_[index].count=end-begin;return index;}
        const auto extent=Sub(centers.hi,centers.lo);const int axis=extent.x>extent.y?(extent.x>extent.z?0:2):(extent.y>extent.z?1:2);
        const auto center=[&](uint32_t i) {const auto& t=triangles_[i];return axis==0?t.a.x+t.b.x+t.c.x:axis==1?t.a.y+t.b.y+t.c.y:t.a.z+t.b.z+t.c.z;};
        const auto middle=begin+(end-begin)/2;
        std::nth_element(order_.begin()+begin,order_.begin()+middle,order_.begin()+end,[&](auto a,auto b){return center(a)<center(b);});
        const auto left=Build(begin,middle),right=Build(middle,end);
        nodes_[index].left=left;nodes_[index].right=right;return index;
    }
    bool TraceNode(uint32_t index,Vector3 origin,Vector3 direction,float& distance,Vector3& normal) const {
        const auto& node=nodes_[index];if(!node.bounds.Hit(origin,direction,distance))return false;
        if(!node.count) {
            const bool left=TraceNode(node.left,origin,direction,distance,normal);
            const bool right=TraceNode(node.right,origin,direction,distance,normal);return left || right;
        }
        bool found=false;
        for(auto i=node.begin;i<node.begin+node.count;++i) {
            const auto& t=triangles_[order_[i]];const auto e1=Sub(t.b,t.a),e2=Sub(t.c,t.a);
            const auto p=Cross(direction,e2);const float determinant=Dot(e1,p);
            if(std::abs(determinant)<1.e-9f)continue;
            const auto s=Sub(origin,t.a);const float u=Dot(s,p)/determinant;if(u<0 || u>1)continue;
            const auto q=Cross(s,e1);const float v=Dot(direction,q)/determinant;if(v<0 || u+v>1)continue;
            const float hit=Dot(e2,q)/determinant;if(hit<.0001f || hit>=distance)continue;
            distance=hit;normal=Unit(Cross(e1,e2));
            if(Dot(normal,direction)>0)normal={-normal.x,-normal.y,-normal.z};found=true;
        }
        return found;
    }
public:
    explicit Mesh(const ModelData& model) {
        const auto count=model.indices.empty()?model.vertices.size():model.indices.size();
        triangles_.reserve(count/3);
        const auto point=[&](size_t i) {const auto& p=model.vertices[model.indices.empty()?i:model.indices[i]].position;return Vector3{p.x,p.y,p.z};};
        for(size_t i=0;i+2<count;i+=3)triangles_.push_back({point(i),point(i+1),point(i+2)});
        order_.resize(triangles_.size());std::iota(order_.begin(),order_.end(),0u);
        if(!order_.empty())Build(0,uint32_t(order_.size()));
    }
    // Do not normalize a transformed direction: t remains a world-distance
    // parameter even under the landmark's nonuniform scale.
    bool Trace(Vector3 origin,Vector3 direction,float& distance,Vector3& normal) const {
        return !nodes_.empty() && TraceNode(0,origin,direction,distance,normal);
    }
};
inline auto& Cache() {static std::unordered_map<const VertexData*,Mesh> meshes;return meshes;}
inline void ResetCache() {Cache().clear();}
inline const Mesh& Prepare(const ModelData& model) {
    return Cache().try_emplace(model.vertices.data(),model).first->second;
}
using Query=std::function<RailAimHit(Vector3,Vector3,float)>;
inline const Query*& ActiveQuery() {static thread_local const Query* query=nullptr;return query;}
class Scope final {
    Query query_;
    const Query* previous_;
public:
    explicit Scope(Query query):query_(std::move(query)),previous_(ActiveQuery()) {ActiveQuery()=&query_;}
    ~Scope() {ActiveQuery()=previous_;}
    Scope(const Scope&)=delete;Scope& operator=(const Scope&)=delete;
};
inline RailAimHit Trace(Vector3 origin,Vector3 direction,float limit) {
    return ActiveQuery()?(*ActiveQuery())(origin,direction,limit):RailAimHit{};
}
}
