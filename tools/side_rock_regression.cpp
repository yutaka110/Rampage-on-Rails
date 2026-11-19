#include "course/RailSideRock.h"
#include <cstdio>
#include <cstdlib>

namespace {
void Require(bool condition,const char* message) {
    if(!condition) {std::fprintf(stderr,"FAIL: %s\n",message);std::exit(1);}
}
bool Near(float a,float b) {return std::abs(a-b)<.0001f;}
}

int main() {
    CourseObstacleActor rock;
    rock.desc.id=RailSideRock::Id;
    rock.desc.halfExtents={3.4f,2.7f,3.1f};
    rock.desc.spawnDistance=2140;rock.desc.distanceOffset=180;
    rock.desc.verticalOffset=2.5f;
    for(float side:{-1.0f,1.0f}) {
        rock.desc.lateralOffset=9*side;
        for(size_t i=0;i<8;++i) {
            rock.age=0;
            const auto start=RailSideRock::Evaluate(rock,i);
            const auto pivot=RailSideRockGeometry::Pivots[i];
            Require(Near(start.rail.x,9*side+pivot.x*3.4f) &&
                Near(start.rail.y,2.5f+pivot.y*2.7f) &&
                Near(start.rail.z,2320+pivot.z*3.1f),"fragment must assemble exactly at its authored pivot");
            for(float t:{.04f,.20f,.70f,1.20f,1.65f}) {
                rock.age=t;const auto f=RailSideRock::Evaluate(rock,i);
                Require(side*(f.rail.x-start.rail.x)>0,"debris must always travel away from road");
                Require(std::abs(f.rail.x)>5.5f,"debris must remain outside traffic corridor");
                Require(f.alpha>=0 && f.alpha<=1,"valid debris coverage");
            }
            rock.age=RailSideRock::DebrisSeconds;
            Require(RailSideRock::Evaluate(rock,i).alpha==0,"expired debris must be invisible");
        }
    }
    rock.desc.hitPoints=24;rock.age=10;
    RailSideRock::Hit(rock,{12.25f,-3.125f,50.75f},{0,1,0});
    Require(rock.age==0 && RailSideRock::Impact(rock).valid,"accepted contact starts its own effect clock");
    rock.desc.hitPoints=0;RailSideRock::Hit(rock,{1,2,3},{1,0,0});
    Require(rock.desc.lifetime==RailSideRock::DebrisSeconds,"destruction bounds actor lifetime");
    const auto checkpoint=rock;
    const auto contact=RailSideRock::Impact(checkpoint);
    Require(contact.valid && Near(contact.point.z,3) && Near(contact.normal.x,1),"checkpoint copy retains fracture contact");
    CourseObstacleActor fresh;
    Require(!RailSideRock::Impact(fresh).valid,"new instance cannot inherit old contact");
    std::puts("PASS: assembly, road clearance, debris expiry, impact clock, checkpoint contact");
}
