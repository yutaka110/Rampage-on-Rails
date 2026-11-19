#pragma once
#include "CourseSpawnRuntime.h"
#include "RailSideRockGeometry.h"
#include <algorithm>
#include <cmath>
#include <sstream>

namespace RailSideRock {
inline constexpr const char* Id="side_breakable_rock";
inline constexpr const char* Mesh="side_rock";
inline constexpr float Health=36.0f, DebrisSeconds=1.65f;
inline constexpr std::array<const char*,8> Pieces{{"side_rock_0","side_rock_1","side_rock_2","side_rock_3",
    "side_rock_4","side_rock_5","side_rock_6","side_rock_7"}};
inline bool Is(const CourseObstacleActor& rock) {return rock.desc.id==Id;}
inline float Smooth(float x,float a,float b) {const float t=(std::clamp)((x-a)/(b-a),0.0f,1.0f);return t*t*(3-2*t);}
struct Contact {Vector3 point{},normal{};bool valid=false;};
inline Contact Impact(const CourseObstacleActor& rock) {
    Contact result;
    std::istringstream data(rock.desc.payload);
    result.valid=bool(data>>result.point.x>>result.point.y>>result.point.z>>result.normal.x>>result.normal.y>>result.normal.z);
    return result;
}
inline void Hit(CourseObstacleActor& rock,Vector3 point,Vector3 normal) {
    // This retained contact also survives checkpoints; no parallel VFX actor
    // can outlive or accidentally break a new instance after retry.
    rock.desc.payload=std::to_string(point.x)+" "+std::to_string(point.y)+" "+std::to_string(point.z)+" "+
        std::to_string(normal.x)+" "+std::to_string(normal.y)+" "+std::to_string(normal.z);
    rock.age=0;
    if(rock.desc.hitPoints<=0)rock.desc.lifetime=DebrisSeconds;
}
struct Fragment {Vector3 rail{},rotation{};float alpha=1;};
inline Fragment Evaluate(const CourseObstacleActor& rock,size_t index) {
    const auto p=RailSideRockGeometry::Pivots[index];const auto size=rock.desc.halfExtents;
    const float t=(std::max)(0.0f,rock.age-.018f);
    const float side=std::copysign(1.0f,rock.desc.lateralOffset);
    const float variation=float(index)*2.39996f;
    const float out=(1.6f+.29f*float(index))*t;
    Fragment f;
    f.rail={rock.desc.lateralOffset+p.x*size.x+side*out,
        rock.desc.verticalOffset+p.y*size.y+(2.1f+.31f*float(index))*t-4.9f*t*t,
        rock.desc.spawnDistance+rock.desc.distanceOffset+p.z*size.z+std::sin(variation)*1.7f*t};
    f.rotation={t*(.51f+.16f*std::sin(variation)),t*(.34f+.17f*std::cos(variation)),side*t*(.33f+.055f*float(index))};
    f.alpha=1-Smooth(rock.age,.95f,DebrisSeconds);
    return f;
}
}
