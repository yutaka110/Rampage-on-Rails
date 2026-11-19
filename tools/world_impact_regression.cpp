#include "course/RailCombatImpactMarks.h"
#include "course/RailPrimaryKillPresentation.h"
#include <cstdio>
#include <cstdlib>
#include <limits>

using namespace RailCombatImpact;
static void Require(bool condition,const char* text) {
    if(!condition) {std::fprintf(stderr,"FAIL: %s\n",text);std::exit(1);}
}
static Mark Ground(uint64_t seed,Vector3 p={}) {
    Mark m;m.owner=UINT32_MAX;m.seed=seed;m.origin=p;m.normal={0,2,0};
    m.kind=Kind::Sand;m.radius=1.15f;return m;
}
int main() {
    Marks marks;
    Require(marks.AddWorld(Ground(1)),"valid ground contact");
    Require(marks.Items()[0].normal.y==1,"normalize contact normal");
    Require(!marks.AddWorld(Ground(1)),"duplicate contact ignored");
    marks.Advance(.15f);
    Require(marks.AddWorld(Ground(2,{.1f,0,0})) && marks.Items().size()==1 && marks.Items()[0].age==0,
        "nearby sustained fire refreshes one patch");
    auto wall=Ground(3);wall.normal={0,0,-1};wall.kind=Kind::Rock;
    Require(marks.AddWorld(wall) && marks.Items().size()==2,"wall must not merge into floor");
    Mark cart;cart.owner=CartOwner;cart.seed=100;cart.kind=Kind::Cart;marks.Add(cart);
    for(uint64_t i=4;i<100;++i)Require(marks.AddWorld(Ground(i,{float(i)*3,0,0})),"bounded world mark insertion");
    Require(marks.Items().size()==9,"at most eight world patches alongside combat marks");
    Require(std::any_of(marks.Items().begin(),marks.Items().end(),[](const auto& m){return m.seed==100 && m.owner==CartOwner;}),
        "world mark pressure cannot evict cart feedback");
    auto invalid=Ground(101);invalid.origin.x=std::numeric_limits<float>::quiet_NaN();
    Require(!marks.AddWorld(invalid),"nonfinite contact ignored");
    invalid=Ground(102);invalid.normal={};Require(!marks.AddWorld(invalid),"zero normal ignored");
    marks.Advance(WorldLifetime);
    Require(std::none_of(marks.Items().begin(),marks.Items().end(),[](const auto& m){return WorldSurface(m.kind);}),"world marks expire");
    Require(TerrainSurface({0,1,0})==Kind::Sand && TerrainSurface({1,0,0})==Kind::Rock,"terrain material follows slope blend");
    marks.Reset();Require(marks.Items().empty(),"retry clears marks");
    RailPrimaryKillPresentation arrivals;
    WeaponFeedbackEvent hit;hit.sequence=hit.shotId=1;
    hit.hitKind=RailAimHitKind::ProceduralTerrain;hit.damageType=WeaponDamageType::Energy;hit.blocked=true;
    arrivals.QueueFeedback(hit,90,.08f,.18f);
    Require(arrivals.TakeArrivals().empty(),"scorch must wait for visible bolt arrival");
    arrivals.Update(.04f);Require(arrivals.TakeArrivals().empty(),"flight still pending");
    arrivals.Update(.05f);Require(arrivals.TakeArrivals().size()==1,"terrain contact arrives exactly once");
    arrivals.QueueFeedback(hit,90,.08f,.18f);arrivals.Update(.1f);
    Require(arrivals.TakeArrivals().empty(),"replayed terrain feedback cannot create another mark");
    std::puts("PASS: merging, facing, capacity, combat priority, validation, expiry, classification, retry reset, visible arrival");
}
