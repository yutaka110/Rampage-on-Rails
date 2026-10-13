#include "TerrainCollisionWorld.h"
#include "TerrainVolumeField.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cfloat>
#include <cmath>
#include <cstring>
#include <future>
#include <limits>
#include <numeric>
#include <vector>

namespace {
// Shared integer-metre rings and a closing angle of exactly zero prevent
// cracks between independently built chunks, including the final short chunk.
constexpr float kChunkLength = 64.0f;
constexpr float kRingStep = 1.0f;
constexpr uint32_t kRadialSegments = 128;
constexpr uint32_t kLeafTriangles = 8;
constexpr uint32_t kMaximumWorkers = 2;
constexpr float kTau = 6.28318530717958647692f;
Vector3 Add(Vector3 a, Vector3 b) { return {a.x+b.x,a.y+b.y,a.z+b.z}; }
Vector3 Sub(Vector3 a, Vector3 b) { return {a.x-b.x,a.y-b.y,a.z-b.z}; }
Vector3 Mul(Vector3 a, float s) { return {a.x*s,a.y*s,a.z*s}; }
float Dot(Vector3 a, Vector3 b) { return a.x*b.x+a.y*b.y+a.z*b.z; }
Vector3 Cross(Vector3 a, Vector3 b) { return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x}; }
float Axis(Vector3 a, uint32_t axis) { return axis==0?a.x:(axis==1?a.y:a.z); }
struct Bounds {
    Vector3 low{FLT_MAX,FLT_MAX,FLT_MAX}, high{-FLT_MAX,-FLT_MAX,-FLT_MAX};
    void Include(Vector3 p) {
        low = {(std::min)(low.x,p.x),(std::min)(low.y,p.y),(std::min)(low.z,p.z)};
        high = {(std::max)(high.x,p.x),(std::max)(high.y,p.y),(std::max)(high.z,p.z)};
    }
    void Include(const Bounds& b) { Include(b.low); Include(b.high); }
    bool Intersects(Vector3 origin, Vector3 direction, float limit, float padding=0.0001f) const {
        float nearDistance=0, farDistance=limit;
        for (uint32_t axis=0;axis<3;++axis) {
            const float o=Axis(origin,axis), d=Axis(direction,axis);
            // Small expansion handles rays exactly on shared triangle edges.
            const float lo=Axis(low,axis)-padding, hi=Axis(high,axis)+padding;
            if (std::abs(d)<1.0e-8f) { if (o<lo || o>hi) return false; continue; }
            float a=(lo-o)/d,b=(hi-o)/d;
            if (a>b) std::swap(a,b);
            nearDistance=(std::max)(nearDistance,a); farDistance=(std::min)(farDistance,b);
            if (nearDistance>farDistance) return false;
        }
        return true;
    }
};
struct Triangle { Vector3 a{},edge1{},edge2{}; Bounds bounds{}; };
struct Node { Bounds bounds{}; uint32_t first=0,count=0,right=0; };
struct Chunk {
    int index=0;
    float start=0,end=0;
    std::vector<Triangle> triangles;
    std::vector<Node> nodes;
    uint32_t BuildNode(uint32_t first,uint32_t count) {
        const uint32_t index=static_cast<uint32_t>(nodes.size());
        nodes.emplace_back();
        Bounds bounds{},centroids{};
        for (uint32_t i=first;i<first+count;++i) {
            bounds.Include(triangles[i].bounds);
            centroids.Include(Add(triangles[i].bounds.low,triangles[i].bounds.high));
        }
        nodes[index].bounds=bounds;
        if (count<=kLeafTriangles) { nodes[index].first=first; nodes[index].count=count; return index; }
        const Vector3 span=Sub(centroids.high,centroids.low);
        uint32_t axis=span.y>span.x?1u:0u;
        if (span.z>Axis(span,axis)) axis=2;
        const uint32_t middle=first+count/2;
        std::nth_element(triangles.begin()+first,triangles.begin()+middle,triangles.begin()+first+count,
            [axis](const Triangle& a,const Triangle& b) {
                return Axis(Add(a.bounds.low,a.bounds.high),axis)<Axis(Add(b.bounds.low,b.bounds.high),axis);
            });
        const uint32_t left=BuildNode(first,middle-first);
        const uint32_t right=BuildNode(middle,first+count-middle);
        nodes[index].first=left; nodes[index].right=right;
        return index;
    }
    void Cast(Vector3 origin,Vector3 direction,TerrainCollisionWorld::Hit& best) const {
        if (nodes.empty()) return;
        // Median splits have depth < 32 for the fixed-resolution chunk.
        std::array<uint32_t,64> stack{}; uint32_t size=1; stack[0]=0;
        while (size) {
            const Node& node=nodes[stack[--size]]; ++best.nodesVisited;
            if (!node.bounds.Intersects(origin,direction,best.distance,1.5f)) continue;
            if (!node.count) { stack[size++]=node.right; stack[size++]=node.first; continue; }
            for (uint32_t i=node.first;i<node.first+node.count;++i) {
                ++best.trianglesTested;
                const Triangle& t=triangles[i];
                if (t.bounds.Intersects(origin,direction,best.distance,1.5f)) best.nearSurface=true;
                const Vector3 p=Cross(direction,t.edge2);
                const float determinant=Dot(t.edge1,p);
                // Two-sided: rays start inside caves, with either winding.
                if (std::abs(determinant)<1.0e-8f) continue;
                const float inverse=1.0f/determinant;
                const Vector3 offset=Sub(origin,t.a);
                const float u=Dot(offset,p)*inverse;
                if (u< -0.00001f || u>1.00001f) continue;
                const Vector3 q=Cross(offset,t.edge1);
                const float v=Dot(direction,q)*inverse;
                if (v< -0.00001f || u+v>1.00001f) continue;
                const float distance=Dot(t.edge2,q)*inverse;
                if (distance<0 || distance>best.distance) continue;
                Vector3 normal=Cross(t.edge1,t.edge2);
                const float length=std::sqrt(Dot(normal,normal));
                if (length<1.0e-8f) continue;
                normal=Mul(normal,1.0f/length);
                if (Dot(normal,direction)>0) normal=Mul(normal,-1);
                best.hit=true; best.distance=distance; best.normal=normal;
            }
        }
    }
};
bool SamePoint(const RailPathControlPoint& a,const RailPathControlPoint& b) {
    return a.position.x==b.position.x && a.position.y==b.position.y && a.position.z==b.position.z &&
        a.corridorRadius==b.corridorRadius && a.speed==b.speed && a.tangentMode==b.tangentMode &&
        a.incomingTangent.x==b.incomingTangent.x && a.incomingTangent.y==b.incomingTangent.y && a.incomingTangent.z==b.incomingTangent.z &&
        a.outgoingTangent.x==b.outgoingTangent.x && a.outgoingTangent.y==b.outgoingTangent.y && a.outgoingTangent.z==b.outgoingTangent.z;
}
bool SameEdits(const TerrainEditLayer& captured,const TerrainEditLayer* live) {
    if (!live) return captured.Stamps().empty();
    if (captured.Revision()!=live->Revision() || captured.Stamps().size()!=live->Stamps().size()) return false;
    for (size_t i=0;i<captured.Stamps().size();++i) {
        const auto& a=captured.Stamps()[i]; const auto& b=live->Stamps()[i];
        if (a.operation!=b.operation || a.distance!=b.distance || a.angle!=b.angle || a.radius!=b.radius ||
            a.surfaceRadius!=b.surfaceRadius || a.strength!=b.strength || a.hardness!=b.hardness ||
            a.strokeGuid!=b.strokeGuid || a.materialLayer!=b.materialLayer) return false;
    }
    return true;
}
struct Source {
    RailPath rail;
    TerrainGenerationSettings settings{};
    TerrainEditLayer edits,preview;
    std::atomic<bool> cancelled{false};
};
std::array<float,18> GeometryKey(const TerrainGenerationSettings& s) {
    // Only values used by TerrainVolumeField. Presentation/LOD/scatter changes
    // do not alter collision topology and must not restart background jobs.
    return {s.chunkLength,s.corridorRadius,s.canyonHalfWidth,s.wallHeight,
        s.volumeRoughness,s.volumeArchScale,s.sdfCarveDensity,s.sdfCarveStrength,s.sdfCarveScale,
        s.openingSilhouetteStrength,s.openingSilhouetteScale,s.openCanyonStartDistance,
        s.openCanyonTransitionLength,s.openCanyonStrength,s.motherRockErosionStrength,
        s.largeScaleErosionStrength,s.surfaceBreakupDensity,s.archDensity};
}
std::shared_ptr<const Chunk> BuildChunk(const std::shared_ptr<Source>& source,int index) {
    auto chunk=std::make_shared<Chunk>(); chunk->index=index;
    chunk->start=static_cast<float>(index)*kChunkLength;
    chunk->end=(std::min)(chunk->start+kChunkLength,source->rail.Length()-0.001f);
    if (chunk->end<=chunk->start) return nullptr;
    const uint32_t rings=static_cast<uint32_t>(std::ceil((chunk->end-chunk->start)/kRingStep));
    TerrainVolumeField field(source->rail,source->settings,&source->edits,&source->preview);
    std::vector<Vector3> vertices; vertices.reserve((rings+1)*(kRadialSegments+1));
    for (uint32_t s=0;s<=rings;++s) {
        if (source->cancelled) return nullptr;
        const float distance=s==rings?chunk->end:chunk->start+static_cast<float>(s)*kRingStep;
        const RailPathSample sample=source->rail.Evaluate(distance);
        for (uint32_t a=0;a<=kRadialSegments;++a) {
            const float angle=a==kRadialSegments?0.0f:kTau*static_cast<float>(a)/kRadialSegments;
            vertices.push_back(field.CollisionSurfacePoint(distance,angle,sample));
        }
    }
    chunk->triangles.reserve(rings*kRadialSegments*2);
    auto triangle=[&](uint32_t a,uint32_t b,uint32_t c) {
        Triangle t{}; t.a=vertices[a]; t.edge1=Sub(vertices[b],t.a); t.edge2=Sub(vertices[c],t.a);
        t.bounds.Include(t.a); t.bounds.Include(vertices[b]); t.bounds.Include(vertices[c]);
        chunk->triangles.push_back(t);
    };
    for (uint32_t s=0;s<rings;++s) {
        if (source->cancelled) return nullptr;
        for (uint32_t a=0;a<kRadialSegments;++a) {
            const uint32_t i=s*(kRadialSegments+1)+a,j=i+kRadialSegments+1;
            triangle(i,j,i+1); triangle(i+1,j,j+1);
        }
    }
    if (source->cancelled) return nullptr;
    chunk->nodes.reserve(chunk->triangles.size()/2);
    chunk->BuildNode(0,static_cast<uint32_t>(chunk->triangles.size()));
    return source->cancelled?nullptr:chunk;
}
} // namespace

struct TerrainCollisionWorld::Impl {
    struct Job { int index; std::shared_ptr<Source> source; std::future<std::shared_ptr<const Chunk>> future; };
    const RailPath* liveRail=nullptr;
    std::shared_ptr<Source> source;
    std::vector<std::shared_ptr<const Chunk>> chunks;
    std::vector<Job> jobs;
    uint64_t generation=0;
    void Capture(const RailPath& rail,const TerrainGenerationSettings& settings,
        const TerrainEditLayer* edits,const TerrainEditLayer* preview) {
        if (source) source->cancelled=true;
        chunks.clear(); liveRail=&rail;
        source=std::make_shared<Source>(); source->rail=rail;
        std::memcpy(&source->settings,&settings,sizeof(settings));
        if (edits) source->edits=*edits;
        if (preview) source->preview=*preview;
        ++generation;
    }
    bool Has(int index) const {
        for (const auto& c:chunks) if (c->index==index) return true;
        return false;
    }
    void Collect() {
        for (auto it=jobs.begin();it!=jobs.end();) {
            if (it->future.wait_for(std::chrono::seconds(0))!=std::future_status::ready) { ++it; continue; }
            const auto chunk=it->future.get();
            if (it->source==source && chunk && !Has(it->index)) chunks.push_back(chunk);
            it=jobs.erase(it);
        }
    }
};
TerrainCollisionWorld::TerrainCollisionWorld():impl_(std::make_unique<Impl>()) {}
TerrainCollisionWorld::~TerrainCollisionWorld() {
    if (impl_->source) impl_->source->cancelled=true;
    for (auto& job:impl_->jobs) job.source->cancelled=true;
}
bool TerrainCollisionWorld::Matches(const RailPath& rail,const TerrainGenerationSettings& settings,
    const TerrainEditLayer* edits,const TerrainEditLayer* preview) const {
    const auto& source=impl_->source;
    if (!source || impl_->liveRail!=&rail || source->rail.Length()!=rail.Length() ||
        source->rail.ControlPoints().size()!=rail.ControlPoints().size() ||
        source->settings.seed!=settings.seed || GeometryKey(source->settings)!=GeometryKey(settings) ||
        !SameEdits(source->edits,edits) || !SameEdits(source->preview,preview)) return false;
    for (size_t i=0;i<rail.ControlPoints().size();++i)
        if (!SamePoint(source->rail.ControlPoints()[i],rail.ControlPoints()[i])) return false;
    return true;
}
void TerrainCollisionWorld::Update(const RailPath& rail,const TerrainGenerationSettings& settings,
    const TerrainEditLayer* edits,const TerrainEditLayer* preview,float focusDistance) {
    if (!Matches(rail,settings,edits,preview)) impl_->Capture(rail,settings,edits,preview);
    impl_->Collect();
    if (rail.Length()<=0 || !std::isfinite(focusDistance)) return;
    focusDistance=(std::clamp)(focusDistance,0.0f,rail.Length());
    const int last=static_cast<int>((std::max)(rail.Length()-0.001f,0.0f)/kChunkLength);
    const int firstWanted=(std::max)(0,static_cast<int>((focusDistance-192.0f)/kChunkLength));
    const int lastWanted=(std::min)(last,static_cast<int>((focusDistance+256.0f)/kChunkLength));
    // Bound memory even during a camera jump. Old workers are allowed to finish
    // without blocking the update; their results outside the window are pruned.
    std::erase_if(impl_->chunks,[&](const auto& c){return c->index<firstWanted || c->index>lastWanted;});
    std::vector<int> wanted;
    for (int i=firstWanted;i<=lastWanted;++i) {
        bool queued=false;
        for (const auto& job:impl_->jobs) if (job.source==impl_->source && job.index==i) queued=true;
        if (!queued && !impl_->Has(i)) wanted.push_back(i);
    }
    std::sort(wanted.begin(),wanted.end(),[&](int a,int b) {
        return std::abs((a+0.5f)*kChunkLength-focusDistance)<std::abs((b+0.5f)*kChunkLength-focusDistance);
    });
    for (int index:wanted) {
        if (impl_->jobs.size()>=kMaximumWorkers) break;
        const auto source=impl_->source;
        impl_->jobs.push_back({index,source,std::async(std::launch::async,[source,index]{return BuildChunk(source,index);})});
    }
}
void TerrainCollisionWorld::BuildSynchronously(const RailPath& rail,const TerrainGenerationSettings& settings,
    const TerrainEditLayer* edits,const TerrainEditLayer* preview,float startDistance,float endDistance) {
    impl_->Capture(rail,settings,edits,preview);
    if (rail.Length()<=0 || !std::isfinite(startDistance) || !std::isfinite(endDistance)) return;
    const float railEnd=(std::max)(rail.Length()-0.001f,0.0f);
    const int first=static_cast<int>((std::clamp)(startDistance,0.0f,railEnd)/kChunkLength);
    const int last=static_cast<int>((std::clamp)(endDistance,0.0f,railEnd)/kChunkLength);
    for (int index=first;index<=last;++index) {
        const auto chunk=BuildChunk(impl_->source,index);
        if (chunk) impl_->chunks.push_back(chunk);
    }
}
TerrainCollisionWorld::Hit TerrainCollisionWorld::Query(const Vector3& origin,const Vector3& direction,
    float maxDistance,float searchStart,float searchEnd) const {
    Hit hit{};
    const auto finite=[](Vector3 v){return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z);};
    if (!impl_->source || !finite(origin) || !finite(direction) || std::abs(Dot(direction,direction)-1.0f)>0.001f ||
        !std::isfinite(maxDistance) || maxDistance<=0 ||
        !std::isfinite(searchStart) || !std::isfinite(searchEnd) || searchEnd<searchStart) return hit;
    if (searchStart<0 || searchEnd>impl_->source->rail.Length()-0.0009f) return hit;
    const int first=static_cast<int>((std::max)(searchStart,0.0f)/kChunkLength);
    const int last=static_cast<int>((std::max)(searchEnd,0.0f)/kChunkLength);
    for (int index=first;index<=last;++index) if (!impl_->Has(index)) return hit;
    hit.available=true; hit.distance=maxDistance;
    for (const auto& chunk:impl_->chunks) if (chunk->index>=first && chunk->index<=last)
        chunk->Cast(origin,direction,hit);
    return hit;
}
TerrainCollisionWorld::Stats TerrainCollisionWorld::GetStats() const {
    Stats stats{}; stats.generation=impl_->generation;
    stats.residentChunks=static_cast<uint32_t>(impl_->chunks.size());
    stats.pendingBuilds=static_cast<uint32_t>(impl_->jobs.size());
    for (const auto& chunk:impl_->chunks) {
        stats.triangles+=chunk->triangles.size();
        stats.bytes+=chunk->triangles.capacity()*sizeof(Triangle)+chunk->nodes.capacity()*sizeof(Node);
    }
    return stats;
}
