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
    Require(!Trace({},{0,0,1},10).hit,"query does not escape its gameplay scope");
    {
        Scope outer([](Vector3,Vector3,float){RailAimHit h;h.hit=true;h.sourceIndex=1;return h;});
        Require(Trace({},{0,0,1},10).sourceIndex==1,"scoped primary query enabled");
        {Scope inner([](Vector3,Vector3,float){RailAimHit h;h.hit=true;h.sourceIndex=2;return h;});
            Require(Trace({},{0,0,1},10).sourceIndex==2,"nested preview query isolated");}
        Require(Trace({},{0,0,1},10).sourceIndex==1,"outer query restored");
    }
    Require(!Trace({},{0,0,1},10).hit,"no callback survives frame teardown");
    Require(IsStone("organic_arch_large") && IsStone("root_spire_column") && !IsStone("title_stake"),"only authored stone opts in");
    Require(!RailVisualOcclusion::Blocked({},{0,.4f,0}),"no visibility callback outside a live frame");
    {
        RailVisualOcclusion::Scope visibility([&](Vector3 eye,Vector3 target) {
            auto direction=Sub(target,eye);float range=std::sqrt(Dot(direction,direction));
            direction=Unit(direction);Vector3 n{};return mesh.Trace(eye,direction,range,n);
        },{0,.4f,-5});
        Require(RailVisualOcclusion::FromCamera({0,.4f,5}),"actual arch lintel hides a lock target");
        Require(!RailVisualOcclusion::Blocked({0,-.5f,-5},{0,-.5f,5}),"open aperture keeps visible targets lockable");
        {RailVisualOcclusion::Scope clear([](Vector3,Vector3){return false;});
            Require(!RailVisualOcclusion::FromCamera({0,.4f,5}),"nested review visibility is isolated");}
        Require(RailVisualOcclusion::FromCamera({0,.4f,5}),"frame visibility restores after nested review");
    }
    Require(!RailVisualOcclusion::Blocked({},{0,.4f,0}),"visibility callback cannot survive a frame");
    std::puts("PASS: actual arch aperture, lintel, pillar, nearest hit, scale, empty space, scoped lifetime");
}
