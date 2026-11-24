#include "course/RailVisualOcclusion.h"
#include "course/RailSceneryImpact.h"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

using namespace RailSceneryImpact;
static void Require(bool condition,const char* reason) {
    if(!condition) {std::fprintf(stderr,"FAIL: %s\n",reason);std::exit(1);}
}
static ModelData LoadGeometry(const char* path) {
    ModelData model;std::ifstream file(path);Require(bool(file),"reference OBJ must exist");
    std::string line;
    while(std::getline(file,line)) {
        std::istringstream input(line);std::string tag;input>>tag;
        if(tag=="v") {VertexData vertex{};input>>vertex.position.x>>vertex.position.y>>vertex.position.z;vertex.position.w=1;model.vertices.push_back(vertex);}
        if(tag=="f") {
            std::string index;std::vector<uint32_t> face;
            while(input>>index)face.push_back(uint32_t(std::stoul(index.substr(0,index.find('/')))-1));
            for(size_t i=1;i+1<face.size();++i)model.indices.insert(model.indices.end(),{face[0],face[i],face[i+1]});
        }
    }
    return model;
}
int main() {
    const auto arch=LoadGeometry("Resources/course_meshes/OrganicArchLarge/OrganicArchLarge.obj");
    Mesh mesh(arch);float distance=20;Vector3 normal{};
    Require(!mesh.Trace({0,-.50f,-5},{0,0,1},distance,normal),"actual arch aperture must remain open");
    Require(mesh.Trace({0,.40f,-5},{0,0,1},distance,normal) && distance>3 && distance<6,"actual lintel must receive primary contact");
    Require(normal.z<0 && std::abs(Dot(normal,normal)-1)<.001f,"contact normal faces incoming shot");
    const float first=distance;distance=first-.01f;
    Require(!mesh.Trace({0,.40f,-5},{0,0,1},distance,normal),"stone behind an earlier hit cannot steal it");
    distance=100;
    Require(mesh.Trace({0,.40f,-5},{0,0,.5f},distance,normal) && std::abs(distance-first*2)<.001f,
        "nonuniform model scale preserves world-ray distance");
    distance=20;Require(!mesh.Trace({10,.4f,-5},{0,0,1},distance,normal),"empty bounds cannot create contact");
    const auto column=LoadGeometry("Resources/course_meshes/RootSpireColumn/RootSpireColumn.obj");
    Mesh pillar(column);distance=20;
    Require(pillar.Trace({0,0,-5},{0,0,1},distance,normal) && distance>4 && distance<5,"actual pillar surface must receive contact");
    MeshCache gameplayCache;
    MeshCache previewCache;
    const Mesh& gameplayArch = gameplayCache.Prepare(arch);
    const Mesh& previewArch = previewCache.Prepare(arch);
    Require(&gameplayArch == &gameplayCache.Prepare(arch), "unchanged mesh data reuses the collision mesh");
    Require(&gameplayArch != &previewArch, "gameplay and preview caches own their meshes");
    gameplayCache.Clear();
    distance = 20;
    Require(previewArch.Trace({0,.40f,-5},{0,0,1},distance,normal), "clearing gameplay collision meshes leaves preview intact");
    Require(IsStone("organic_arch_large") && IsStone("root_spire_column") && !IsStone("title_stake"),"only authored stone opts in");
    const RailVisualOcclusion visibility([&](Vector3 eye, Vector3 target) {
        auto direction = Sub(target, eye);
        float range = std::sqrt(Dot(direction, direction));
        Vector3 normal{};
        return mesh.Trace(eye, Unit(direction), range, normal);
    }, {0,.4f,-5});
    const RailVisualOcclusion clear;
    Require(!clear.FromCamera({0,.4f,5}), "a view without a callback has no visual occluders");
    Require(visibility.FromCamera({0,.4f,5}), "actual arch lintel hides a lock target");
    Require(!visibility.Blocked({0,-.5f,-5}, {0,-.5f,5}), "open aperture keeps visible targets lockable");
    Require(!clear.FromCamera({0,.4f,5}), "a separate view is unaffected by the blocked view");
    std::puts("PASS: actual arch aperture, lintel, pillar, nearest hit, scale, empty space, cache isolation");
}
