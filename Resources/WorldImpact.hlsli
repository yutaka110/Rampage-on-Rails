#ifndef GE3_WORLD_IMPACT
#define GE3_WORLD_IMPACT
#include "TitleLighting.hlsli"

static float WorldMarkNoise(float2 p)
{
    float2 c=floor(p), f=frac(p);f=f*f*(3-2*f);
    float4 h=frac(sin(float4(dot(c,float2(127.1,311.7)),dot(c+float2(1,0),float2(127.1,311.7)),
        dot(c+float2(0,1),float2(127.1,311.7)),dot(c+1,float2(127.1,311.7))))*43758.5453);
    return lerp(lerp(h.x,h.y,f.x),lerp(h.z,h.w,f.x),f.y);
}

// Evaluate on opaque receiver geometry: no floating quad, depth fighting, or
// blue veil over a projectile. Negative tags are reserved for static scenery.
static float3 ApplyWorldScorch(float3 lit,float3 world,float3 geometricNormal,bool terrainReceiver)
{
    if(titleSubject.w>.5f)return lit;
    [unroll] for(int i=0;i<6;++i) {
        float4 stamp=combatMarkPosition[i], contact=combatMarkNormalAge[i], ink=combatMarkTintRadius[i];
        [branch] if((stamp.w==-2 || stamp.w==-3) && ink.w>0) {
            float3 delta=world-stamp.xyz;
            bool sand=stamp.w==-2;
            float along=dot(delta,contact.xyz);
            float maxDepth=terrainReceiver && !sand?4.5f:ink.w*.32f;
            if(abs(along)>maxDepth)continue;
            // Collision terrain and render LOD can differ by a few metres on
            // cliffs. Project along the real incoming bolt, so the footprint
            // meets the visible surface on that same ray instead of sliding
            // sideways with a deep normal-oriented decal box.
            float3 incoming=dot(ink.xyz,ink.xyz)>.01f?normalize(ink.xyz):-contact.xyz;
            float incidence=dot(incoming,contact.xyz);
            float safeIncidence=(incidence<0?-1:1)*max(abs(incidence),.20f);
            float3 projected=delta-incoming*(along/safeIncidence);
            if(dot(projected,projected)>ink.w*ink.w*2.0f)continue;
            float3 tangent=normalize(cross(abs(contact.y)>.9f?float3(1,0,0):float3(0,1,0),contact.xyz));
            float3 bitangent=cross(contact.xyz,tangent);
            float2 p=float2(dot(projected,tangent),dot(projected,bitangent))/ink.w;
            float grain=WorldMarkNoise(p*(sand?13:8)+frac(stamp.xz)*17);
            float broad=WorldMarkNoise(p*4.1f+frac(stamp.xy)*11);
            float radius=length(p*float2(.87f,1.13f))+(broad-.5f)*(sand?.30f:.23f);
            float face=smoothstep(.43f,.88f,dot(geometricNormal,contact.xyz));
            float depth=1-smoothstep(maxDepth*.65f,maxDepth,abs(along));
            float age=contact.w, fade=1-smoothstep(1.80f,2.80f,age);
            float coverage=(1-smoothstep(sand?.22f:.40f,sand?1.0f:.88f,radius))*face*depth;
            float grit=sand?lerp(.74f,1.0f,grain):1.0f;
            float burn=coverage*grit*fade;
            lit=lerp(lit,lit*float3(.12f,.17f,.29f)+float3(.004f,.008f,.018f),burn*(sand?.86f:.90f));
            float aa=max(fwidth(radius)*.8f,.025f);
            float front=.15f+.64f*smoothstep(0,.12f,age);
            float edge=1-smoothstep(.028f,.085f+aa,abs(radius-front));
            float crack=0;
            [unroll] for(int branch=0;branch<3;++branch) {
                float angle=.37f+branch*2.39996f+broad*.22f;
                float2 axis=float2(cos(angle),sin(angle));
                float x=dot(p,axis),y=dot(p,float2(-axis.y,axis.x));
                float bend=.055f*sin(x*26+branch*7.1f);
                crack=max(crack,(1-smoothstep(.012f,.035f+aa,abs(y-bend)))*
                    smoothstep(0,.13f,x)*(1-smoothstep(.36f,.80f,x)));
            }
            float core=(1-smoothstep(.06f,.28f,radius))*(1-smoothstep(.012f,.075f,age));
            float heat=1-smoothstep(sand?.055f:.080f,sand?.30f:.40f,age);
            float shape=sand?edge*.64f*lerp(.55f,1.0f,grain):max(edge*.55f,crack*.73f);
            // The heat front reaches the edge of the burn. Do not attenuate
            // it by the soft interior soot mask a second time.
            float heatCoverage=(1-smoothstep(.82f,1.08f,radius))*face*depth;
            lit+=float3(.08f,.68f,2.65f)*(shape*heat+core*.42f)*heatCoverage;
        }
    }
    return lit;
}
#endif
