#pragma once

#include "utils/math/MathUtils.h"
#include <algorithm>
#include <cmath>

// Keep the authored billboard silhouette/UVs, but depth-test its visible
// hemisphere rather than a card through the centre of the projectile.
// Terrain and the vehicle still occlude portions genuinely behind them.
namespace RailProjectileOrbSurface {
inline Vector4 Project(const Vector3& point, const Matrix4x4& matrix) {
    return {
        point.x*matrix.m[0][0]+point.y*matrix.m[1][0]+point.z*matrix.m[2][0]+matrix.m[3][0],
        point.x*matrix.m[0][1]+point.y*matrix.m[1][1]+point.z*matrix.m[2][1]+matrix.m[3][1],
        point.x*matrix.m[0][2]+point.y*matrix.m[1][2]+point.z*matrix.m[2][2]+matrix.m[3][2],
        point.x*matrix.m[0][3]+point.y*matrix.m[1][3]+point.z*matrix.m[2][3]+matrix.m[3][3]};
}

inline Vector4 Vertex(const Vector3& center, const Vector3& right,
    const Vector3& up, const Vector3& towardCamera, float drawRadius,
    float bodyRadius, Vector2 uv, const Matrix4x4& viewProjection) {
    const float x=(uv.x*2.0f-1.0f)*drawRadius;
    const float y=(1.0f-uv.y*2.0f)*drawRadius;
    const Vector3 plane{center.x+right.x*x+up.x*y,
        center.y+right.y*x+up.y*y,center.z+right.z*x+up.z*y};
    Vector4 clip=Project(plane,viewProjection);
    const float bulge=std::sqrt((std::max)(0.0f,bodyRadius*bodyRadius-x*x-y*y));
    const Vector3 surface{plane.x+towardCamera.x*bulge,
        plane.y+towardCamera.y*bulge,plane.z+towardCamera.z*bulge};
    const Vector4 front=Project(surface,viewProjection);
    if (clip.w>0.0001f && front.w>0.0001f) {
        // Retain XY/W and therefore the original screen footprint and UV
        // interpolation. Only depth changes; no radius or hitbox is enlarged.
        clip.z=front.z/front.w*clip.w;
    }
    return clip;
}

template<class Emit>
void EmitSurface(const Vector3& center,const Vector3& right,const Vector3& up,
    const Vector3& towardCamera,float drawRadius,float bodyRadius,
    const Matrix4x4& viewProjection,int segments,Emit&& emit) {
    const auto vertex=[&](int x,int y) {
        const Vector2 uv{float(x)/float(segments),float(y)/float(segments)};
        emit(Vertex(center,right,up,towardCamera,drawRadius,bodyRadius,uv,viewProjection),uv);
    };
    for(int y=0;y<segments;++y) for(int x=0;x<segments;++x) {
        vertex(x,y);vertex(x+1,y);vertex(x,y+1);
        vertex(x,y+1);vertex(x+1,y);vertex(x+1,y+1);
    }
}
} // namespace RailProjectileOrbSurface
