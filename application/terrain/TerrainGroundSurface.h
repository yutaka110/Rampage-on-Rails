#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <span>
#include <unordered_map>
#include <utility>
#include <vector>
#include "utils/math/Vector.h"

struct TerrainGroundTriangle { Vector3 a{},b{},c{}; };
struct TerrainGroundSurfaceHit { Vector3 position{},normal{0,1,0}; };

// Immutable height queries against the exact resident render-mesh floor.
// A small XZ grid keeps cinematic hull probes independent of render LOD work.
class TerrainGroundSurface final {
public:
    explicit TerrainGroundSurface(std::vector<TerrainGroundTriangle> triangles) {
        for(const auto& t:triangles) {
            const Vector3 a{t.b.x-t.a.x,t.b.y-t.a.y,t.b.z-t.a.z};
            const Vector3 b{t.c.x-t.a.x,t.c.y-t.a.y,t.c.z-t.a.z};
            Vector3 normal{a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};
            const float length=std::sqrt(normal.x*normal.x+normal.y*normal.y+normal.z*normal.z);
            if(!std::isfinite(length) || length<0.00001f || std::abs(normal.y)<length*0.55f) continue;
            const float sign=normal.y<0?-1.0f:1.0f;
            normal={normal.x*sign/length,normal.y*sign/length,normal.z*sign/length};
            const float minX=(std::min)({t.a.x,t.b.x,t.c.x}),maxX=(std::max)({t.a.x,t.b.x,t.c.x});
            const float minZ=(std::min)({t.a.z,t.b.z,t.c.z}),maxZ=(std::max)({t.a.z,t.b.z,t.c.z});
            if(!std::isfinite(minX) || !std::isfinite(maxX) || !std::isfinite(minZ) || !std::isfinite(maxZ)) continue;
            const size_t id=triangles_.size();triangles_.push_back({t,normal});
            const int x0=Cell(minX),x1=Cell(maxX),z0=Cell(minZ),z1=Cell(maxZ);
            if(int64_t(x1-x0+1)*(z1-z0+1)>4096) {large_.push_back(id);continue;}
            for(int x=x0;x<=x1;++x) for(int z=z0;z<=z1;++z) cells_[Key(x,z)].push_back(id);
        }
    }
    bool Sample(Vector3 point,TerrainGroundSurfaceHit& hit,float maximumHeight=(std::numeric_limits<float>::max)()) const {
        if(!std::isfinite(point.x) || !std::isfinite(point.z)) return false;
        bool found=false;float highest=-(std::numeric_limits<float>::max)();
        const auto probe=[&](size_t id) {
            const auto& item=triangles_[id];const auto& t=item.triangle;
            const float denominator=(t.b.z-t.c.z)*(t.a.x-t.c.x)+(t.c.x-t.b.x)*(t.a.z-t.c.z);
            if(std::abs(denominator)<0.000001f) return;
            const float u=((t.b.z-t.c.z)*(point.x-t.c.x)+(t.c.x-t.b.x)*(point.z-t.c.z))/denominator;
            const float v=((t.c.z-t.a.z)*(point.x-t.c.x)+(t.a.x-t.c.x)*(point.z-t.c.z))/denominator;
            const float w=1-u-v;
            if(u<-0.00001f || v<-0.00001f || w<-0.00001f) return;
            const float y=u*t.a.y+v*t.b.y+w*t.c.y;
            if(y>maximumHeight || y<=highest) return;
            found=true;highest=y;hit={{point.x,y,point.z},item.normal};
        };
        const auto cell=cells_.find(Key(Cell(point.x),Cell(point.z)));
        if(cell!=cells_.end()) for(const auto id:cell->second) probe(id);
        for(const auto id:large_) probe(id);
        return found;
    }
    size_t TriangleCount() const { return triangles_.size(); }
private:
    static int Cell(float p) { return int(std::floor(p/8.0f)); }
    static uint64_t Key(int x,int z) { return (uint64_t(uint32_t(x))<<32)|uint32_t(z); }
    struct Triangle { TerrainGroundTriangle triangle;Vector3 normal; };
    std::vector<Triangle> triangles_;
    std::unordered_map<uint64_t,std::vector<size_t>> cells_;
    std::vector<size_t> large_;
};

