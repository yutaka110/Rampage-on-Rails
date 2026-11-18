#pragma once

#include "RailCartImpactEnergy.h"
#include "RailRearPursuitRules.h"
#include "RailVeilLightning.h"

namespace RailRearCartImpact {
enum class Type { None, Lightning, Beam, Wing };

inline Type FromSource(std::string_view source) {
    if (source==RailVeilLightning::Source) return Type::Lightning;
    if (source==kVeilBeamSource) return Type::Beam;
    if (IsFangChaseRound(source)) return Type::Wing;
    return Type::None;
}

inline constexpr float LightningDuration=.50f;
inline constexpr float BeamDuration=.34f;
inline constexpr float WingDuration=.34f;
inline constexpr float WingSparkDuration=.12f;
inline constexpr float LightningSettle=.25f;
inline constexpr float BeamSettle=.23f;
inline constexpr float WingSettle=.14f;

inline float Pulse(float age,float start,float peak,float end) {
    using RailCartImpactEnergy::Smooth;
    return Smooth(age,start,peak)*(1.0f-Smooth(age,peak,end));
}

inline float LightningReflection(float age) {
    return Pulse(age,-.012f,.047f,.177f);
}

inline float BeamReflection(float age) {
    return Pulse(age,-.020f,.057f,.199f);
}

struct Contact {
    Vector3 point{}, normal{};
};

inline Contact ResolveWingContact(Vector3 point,Vector3 normal) {
    const float x=std::abs(normal.x), y=std::abs(normal.y), z=std::abs(normal.z);
    if(y>x && y>z) {
        const bool trim=std::abs(point.x)>1.70f;
        const float height=normal.y<0?-.30f:trim?1.88f:.38f;
        return {{(std::clamp)(point.x,-2.27f,2.27f),height,(std::clamp)(point.z,-3.14f,3.14f)},
            {0,std::copysign(1.0f,normal.y),0}};
    }
    const float height=(std::clamp)(point.y,-.25f,1.82f);
    if(z>=x && std::abs(point.x)<2.13f) {
        const float end=std::copysign(1.0f,normal.z);
        const float face=height<.38f?3.15f:std::abs(point.x)<1.23f && height<.97f?3.34f:3.22f;
        return {{(std::clamp)(point.x,-1.75f,1.75f),height,end*face},{0,0,end}};
    }
    const float side=std::copysign(1.0f,point.x);
    const float face=height<.38f?2.15f:height>=1.55f?2.31f:2.25f;
    return {{side*face,height,(std::clamp)(point.z,-3.18f,3.18f)},{side,0,0}};
}

struct Frame {
    struct Flash {
        Vector3 position{}, normal{};
        float intensity=0, radius=0;
        bool atContact=false;
        bool contactReflection=false;
    };
    std::array<RailCartImpactEnergy::Stroke,112> strokes{};
    std::array<RailCartImpactEnergy::Stroke,16> hotTips{};
    std::array<Flash,8> flashes{};
    size_t count=0;
    size_t hotTipCount=0;
    size_t flashCount=0;
};

inline Frame Evaluate(Type type,Vector3 contact,float age,uint64_t seed,Vector3 normal={},bool drawStrokes=true) {
    Frame result;
    const bool lightning=type==Type::Lightning;
    const bool wing=type==Type::Wing;
    const float duration=lightning?LightningDuration:wing?WingDuration:BeamDuration;
    if(type==Type::None || !std::isfinite(age) || age<0 || age>=duration)return result;

    using RailCartImpactEnergy::Smooth;
    const float tick=std::floor(age*49.0f);
    const float variation=float(seed%127)*.713f;
    const float shape=std::sin(variation);
    const float life=1.0f-Smooth(age,lightning?.30f:wing?.22f:.235f,duration);
    const auto along=[](Vector3 a,Vector3 b,float t) {
        return Vector3{std::lerp(a.x,b.x,t),std::lerp(a.y,b.y,t),std::lerp(a.z,b.z,t)};
    };
    const auto flash=[&](Vector3 position,Vector3 facing,float arrival,float gain,float radius) {
        const float intensity=gain*life*Pulse(age-arrival,-.007f,.012f,wing?.055f:.076f);
        if(intensity>.012f && result.flashCount<result.flashes.size())
            result.flashes[result.flashCount++]={position,facing,intensity,radius};
    };

    unsigned branch=0;
    const auto trace=[&](std::span<const Vector3> nodes,Vector3 surface,float starts,float speed,
            float width,float gain,float afterglow=.084f) {
        const float pattern=variation+2.391f*float(++branch);
        for(size_t i=1;i<nodes.size();++i) {
            const auto a=nodes[i-1],b=nodes[i];
            const Vector3 delta{b.x-a.x,b.y-a.y,b.z-a.z};
            const float length=std::hypot(delta.x,delta.y,delta.z);
            if(length<.012f)continue;
            const float travel=length/speed*(1.0f+.17f*std::sin(pattern+float(i)*2.413f));
            const float progress=(age-starts)/travel;
            starts+=travel;
            if(!drawStrokes || progress<=0.0f || age-starts>=afterglow)continue;

            const Vector3 across{(surface.y*delta.z-surface.z*delta.y)/length,
                (surface.z*delta.x-surface.x*delta.z)/length,
                (surface.x*delta.y-surface.y*delta.x)/length};
            const int cells=(std::clamp)(int(std::ceil(length/.48f)),1,7);
            const auto knot=[&](int cell) {
                auto p=along(a,b,float(cell)/cells);
                if(cell==0 || cell==cells)return p;
                const float phase=pattern+float(cell)*3.49f+float(i)*4.67f;
                const float motion=wing?0.0f:tick;
                const float bend=.16f*std::sin(phase)+.056f*std::sin(phase+motion*1.71f);
                const float lift=.035f+.050f*std::abs(std::sin(phase+motion*1.27f));
                return Vector3{p.x+across.x*bend+surface.x*lift,
                    p.y+across.y*bend+surface.y*lift,p.z+across.z*bend+surface.z*lift};
            };
            const float flicker=.79f+.21f*std::sin(tick*1.91f+pattern+float(i)*3.12f);
            for(int cell=0;cell<cells && result.count<result.strokes.size();++cell) {
                const float from=float(cell)/cells;
                const float to=(std::min)(progress,float(cell+1)/cells);
                if(to<=from+.002f)break;
                const auto start=knot(cell);
                const auto end=along(start,knot(cell+1),(to-from)*cells);
                const float since=travel*(progress-.5f*(to+from));
                const float trail=1.0f-Smooth(since,.009f,afterglow);
                const float opacity=life*gain*flicker*trail;
                if(opacity>.006f)
                    result.strokes[result.count++]={start,end,opacity,width*(.44f+.56f*trail)};
                // Each live branch has a short white leader. Its path stays
                // connected while individual kinks can shift between frames.
                if(progress<=1.0f && to>=progress && result.hotTipCount<result.hotTips.size()) {
                    const float tipLength=wing?.18f:.44f;
                    const auto tip=along(end,start,(std::min)(1.0f,tipLength/((to-from)*length)));
                    const float heat=wing?.76f:1.0f;
                    result.hotTips[result.hotTipCount++]={tip,end,life*gain*heat*(.91f+.09f*flicker),width*.72f};
                }
            }
        }
        return starts;
    };

    constexpr Vector3 top{0,1,0}, bottom{0,-1,0}, front{0,0,1}, back{0,0,-1};
    const float side=std::abs(contact.x)>.20f?std::copysign(1.0f,contact.x):(seed%2?1.0f:-1.0f);
    const Vector3 flank{side,0,0};
    if(lightning) {
        flash(contact,top,0,1.35f,.93f);
        const Vector3 bed{side*.31f,.448f,(std::clamp)(contact.z+.49f,-1.90f,.77f)};
        const Vector3 bedSplit{-side*.76f,.448f,bed.z+.74f};
        const float bedTime=trace(std::array{contact,Vector3{contact.x+side*.14f,1.32f,bed.z-.13f},bed},
            top,-.006f,47,.105f,1.0f);
        flash(bed,top,bedTime,.79f,.69f);
        const float splitTime=trace(std::array{bed,Vector3{-side*.19f,.448f,bed.z+.21f},
            bedSplit},top,bedTime,39,.084f,.86f);
        trace(std::array{bedSplit,Vector3{-side*1.28f,.448f,bed.z+.31f},
            Vector3{-side*1.73f,.54f,bed.z-.18f}},top,splitTime,33,.051f,.59f);
        trace(std::array{bed,Vector3{side*.95f,.448f,bed.z+1.36f},
            Vector3{side*1.65f,.54f,bed.z+1.63f}},top,bedTime+.013f,31,.061f,.75f);

        for(float direction:{side,-side}) {
            const bool firstSide=direction==side;
            const Vector3 outward{direction,0,0};
            const float depth=(firstSide?.99f:1.68f)+.17f*shape;
            const Vector3 lip{direction*2.36f,1.90f,depth};
            const Vector3 split{direction*2.46f,.99f,depth+.22f};
            const float lipTime=trace(std::array{contact,Vector3{direction*.95f,1.94f,depth-.52f},lip},
                top,firstSide?-.004f:.023f,firstSide?54.0f:41.0f,.090f,firstSide?1.0f:.84f,.114f);
            flash(lip,top,lipTime,.81f,.55f);
            const float splitArrival=trace(std::array{lip,split},outward,lipTime,52,.084f,.94f);
            const Vector3 cover{direction*2.56f,.21f,1.86f};
            const float coverTime=trace(std::array{split,cover},outward,splitArrival,35,.069f,.91f,.15f);
            flash(cover,outward,coverTime,.91f,.57f);
            trace(std::array{cover,Vector3{direction*2.52f,-.01f,2.16f+.14f*shape},
                Vector3{direction*2.37f,-.28f,2.42f}},
                outward,coverTime,23,.058f,.83f,.28f);
            trace(std::array{cover,Vector3{direction*2.54f,.06f,1.56f},
                Vector3{direction*2.38f,-.18f,1.36f+.12f*shape}},
                outward,coverTime+.021f,19,.043f,.59f,.30f);

            const Vector3 corner{direction*2.21f,firstSide?.96f:.61f,3.43f};
            const float cornerTime=trace(std::array{split,Vector3{direction*2.40f,.83f,2.52f},corner},
                outward,splitArrival+.009f,51,.073f,.89f);
            trace(std::array{corner,Vector3{direction*1.57f,firstSide?1.05f:.51f,3.46f},
                Vector3{direction*(firstSide?.44f:1.03f),firstSide?.66f:.87f,3.46f}},
                front,cornerTime,49,.071f,firstSide?.93f:.69f);
        }
        return result;
    }

    if(type==Type::Beam) {
        flash(contact,back,0,1.25f,.81f);
        const Vector3 sill{side*1.24f,-.40f,-2.79f};
        const Vector3 underSplit{side*1.83f,-.39f,.29f+.31f*shape};
        const Vector3 breakout{side*2.56f,.23f,1.91f};
        const float splitTime=trace(std::array{contact,Vector3{side*.69f,.16f,-3.51f},sill,
            Vector3{side*.91f,-.42f,-1.21f},underSplit},bottom,-.014f,103,.105f,.97f);
        const float breakoutTime=trace(std::array{underSplit,Vector3{side*2.26f,-.37f,1.40f},breakout},
            flank,splitTime,46,.108f,1.0f);
        flash(breakout,flank,breakoutTime,1.22f,.77f);
        trace(std::array{breakout,Vector3{side*2.50f,-.10f,1.75f},Vector3{side*2.32f,-.25f,2.18f}},
            flank,breakoutTime,33,.055f,.58f);

        const Vector3 lip{side*1.93f,1.36f,3.46f};
        const float lipTime=trace(std::array{breakout,Vector3{side*2.43f,.75f,2.64f},
            Vector3{side*2.22f,1.04f,3.34f},lip},flank,breakoutTime,59,.098f,1.0f);
        flash(lip,front,lipTime,.98f,.63f);
        const Vector3 fork{side*.96f,.96f,3.46f};
        const float forkTime=trace(std::array{lip,Vector3{side*1.47f,1.24f+.10f*shape,3.45f},fork},
            front,lipTime,37,.092f,1.0f);
        flash(fork,front,forkTime,.82f,.49f);
        trace(std::array{fork,Vector3{side*.63f,1.16f,3.44f},Vector3{side*.22f,1.19f,3.29f}},
            front,forkTime,27,.056f,.64f);

        const Vector3 otherCorner{-side*2.22f,.61f,3.42f};
        const float farTime=trace(std::array{fork,Vector3{side*.26f,.65f,3.45f},
            Vector3{-side*.77f,.83f+.11f*shape,3.46f},Vector3{-side*1.48f,.51f,3.46f},otherCorner},
            front,forkTime+.004f,55,.078f,.91f);
        flash(otherCorner,front,farTime,.93f,.66f);
        trace(std::array{otherCorner,Vector3{-side*2.49f,.45f,2.87f},Vector3{-side*2.55f,.14f,2.15f}},
            {-side,0,0},farTime,34,.061f,.73f);
        return result;
    }

    const float contactHeat=1.62f*(1.0f-Smooth(age,.018f,.082f));
    if(contactHeat>.012f)result.flashes[result.flashCount++]={contact,normal,contactHeat,.54f,true};
    if(contactHeat>.06f && contact.z<-2.66f && contact.y<1.50f &&
        (normal.z<-.5f || normal.y>.5f)) {
        // The rear camera sees the inner lip above a contact hidden by the
        // far wall, including a descending round that first touches the bed.
        const Vector3 reflection{(std::clamp)(contact.x,-1.65f,1.65f),1.55f,-2.75f};
        result.flashes[result.flashCount++]={reflection,top,.58f*contactHeat,.54f,false,true};
    }
    const Vector3 root{contact.x+normal.x*.075f,contact.y+normal.y*.075f,contact.z+normal.z*.075f};
    const float angle=std::sin(variation+2.19f);
    const float reach=std::sin(variation+4.38f);
    const float depthDirection=contact.z>.76f*angle?-1.0f:1.0f;
    const auto floorBranch=[&](Vector3 entry,Vector3 surface,float arrival,float gain) {
        const float inward=std::abs(entry.x)>.36f?-std::copysign(1.0f,entry.x):-side;
        const float depth=entry.z>.70f*reach?-1.0f:1.0f;
        const Vector3 split{(std::clamp)(entry.x+inward*(.49f+.13f*angle),-1.56f,1.56f),
            entry.y,(std::clamp)(entry.z+depth*(.41f+.18f*shape),-2.43f,2.58f)};
        const Vector3 end{(std::clamp)(split.x+inward*(.73f+.19f*reach),-1.56f,1.56f),
            entry.y,(std::clamp)(split.z+depth*(.61f+.22f*angle),-2.39f,2.53f)};
        const float splitTime=trace(std::array{entry,split},surface,arrival,30,.068f,gain,.068f);
        flash(split,surface,splitTime,gain*.56f,.50f);
        trace(std::array{split,end},surface,splitTime,28,.050f,gain*.82f,.064f);
        trace(std::array{split,Vector3{(std::clamp)(split.x+inward*.22f,-1.53f,1.53f),
            entry.y,(std::clamp)(split.z-depth*(.45f+.18f*reach),-2.42f,2.55f)}},
            surface,splitTime+.008f,28,.035f,gain*.59f,.052f);
    };

    if(std::abs(normal.z)>.5f) {
        const float end=normal.z;
        const float inward=std::abs(contact.x)>.34f?-side:(angle>0?1.0f:-1.0f);
        const float faceHeight=contact.y>.76f?.55f+.08f*reach:.98f+.10f*reach;
        const float faceDepth=end*3.44f;
        const Vector3 elbow{(std::clamp)(contact.x+inward*.25f,-1.70f,1.70f),
            std::lerp(root.y,faceHeight,.38f),faceDepth};
        const Vector3 fork{(std::clamp)(contact.x+inward*(.63f+.16f*shape),-1.59f,1.59f),
            faceHeight,faceDepth};
        const float forkTime=trace(std::array{root,elbow,fork},normal,-.012f,32,.093f,.92f);
        const Vector3 faceEnd{(std::clamp)(fork.x+inward*(.61f+.24f*reach),-1.62f,1.62f),
            faceHeight>.78f?.63f+.09f*angle:1.04f+.07f*angle,faceDepth};
        trace(std::array{fork,faceEnd},normal,forkTime,31,.057f,.65f,.071f);

        const float wallTop=end>0?1.32f:1.54f;
        const float wallInside=end>0?2.68f:2.57f;
        const float crossX=(std::clamp)(fork.x+inward*(.18f+.25f*angle),-1.47f,1.47f);
        const Vector3 cap{crossX,wallTop,end*3.27f};
        const float capTime=trace(std::array{fork,cap},normal,forkTime+.012f,37,.048f,.60f);
        const Vector3 inner{crossX+inward*.16f,wallTop,end*wallInside};
        const float innerTime=trace(std::array{cap,inner},top,capTime,43,.048f,.59f);
        const Vector3 entry{inner.x,.46f,inner.z-end*.10f};
        const float entryTime=trace(std::array{inner,entry},{0,0,-end},innerTime,43,.048f,.57f);
        floorBranch(entry,top,entryTime,.61f);
        return result;
    }

    if(std::abs(normal.y)>.5f && contact.y<1.50f) {
        floorBranch(root,normal,-.014f,.93f);
        return result;
    }

    // Keep the first fork on the struck side. Only its weaker branch climbs
    // over the wall, then drops onto the exposed bed.
    const float z=(std::clamp)(contact.z+depthDirection*(.72f+.24f*shape),-2.87f,2.87f);
    const bool trimHit=normal.y>.5f;
    const float forkHeight=trimHit?1.95f:contact.y>.91f?.55f+.18f*angle:1.14f+.19f*angle;
    const Vector3 fork{side*(trimHit?2.02f:2.332f),forkHeight,z};
    const Vector3 elbow{std::lerp(root.x,fork.x,.48f),std::lerp(root.y,fork.y,.36f),
        std::lerp(root.z,fork.z,.63f)};
    const auto surface=trimHit?top:flank;
    const float forkTime=trace(std::array{root,elbow,fork},surface,-.013f,35,.094f,.95f);
    const Vector3 panelEnd{side*(trimHit?1.86f:2.33f),
        trimHit?1.95f:forkHeight>.88f?.55f+.15f*reach:1.18f+.15f*reach,
        (std::clamp)(z+depthDirection*(.51f+.25f*reach),-2.91f,2.91f)};
    trace(std::array{fork,panelEnd},surface,forkTime,30,.058f,.67f,.072f);

    const float crossZ=(std::clamp)(z+depthDirection*(.21f+.29f*angle),-2.66f,2.72f);
    const Vector3 cap{side*(trimHit?1.89f:2.37f),1.95f,crossZ};
    const float capTime=trace(std::array{fork,cap},surface,forkTime+.012f,38,.060f,.69f);
    flash(cap,top,capTime,.32f,.43f);
    const Vector3 inner{side*1.66f,1.95f,crossZ+depthDirection*.17f};
    const float innerTime=trace(std::array{cap,inner},top,capTime,43,.053f,.64f);
    const Vector3 entry{side*1.52f,.46f,inner.z+depthDirection*(.34f+.16f*shape)};
    const float entryTime=trace(std::array{inner,entry},{-side,0,0},innerTime,48,.048f,.59f);
    flash(entry,top,entryTime,.27f,.51f);
    floorBranch(entry,top,entryTime,.65f);
    return result;
}
}
