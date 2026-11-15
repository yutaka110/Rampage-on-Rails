#pragma once

#include "CourseAsset.h"
#include <array>
#include <string_view>

// The optional Stone Needles patrol follows the speed lesson and clears the
// firing lane before the first battery starts its warning.
class RailNeedlesPatrol {
public:
    static constexpr float FirstDistance=815.0f, LastArrival=1010.0f, PeelDistance=1016.0f;
    static constexpr float StaggerSeconds=2.72f;
    static constexpr std::array<std::string_view,2> ActorWaves{"needles-scout-left","needles-scout-right"};
    static constexpr std::array<std::string_view,2> RouteIds{"needles-pillar-left","needles-pillar-right"};
    static int Slot(std::string_view wave) {
        if(wave==ActorWaves[0])return 0;
        if(wave==ActorWaves[1])return 1;
        return -1;
    }
    void Reset() {*this={};}
    int TakeArrival(float dt,float distance,bool lessonComplete) {
        if(dt<=0 || distance<FirstDistance || distance>=LastArrival || !lessonComplete || next_>=2)return -1;
        if(next_==1 && (sinceFirst_+=dt)<StaggerSeconds)return -1;
        return next_++;
    }
    static CourseEventMarker Arrival(int slot,float distance) {
        CourseEventMarker event;
        event.distance=distance;
        event.type="enemy_wave";
        event.id=slot==0?"route_patrol_left":"route_patrol_right";
        event.payload="route:support";
        event.editorGuid=ActorWaves[slot];
        return event;
    }
private:
    int next_=0;
    float sinceFirst_=0;
};
