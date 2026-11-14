#include "RailShadowReceiver.hlsli"
Texture2D<float4> gScene : register(t0);
Texture2D<float> gDepth : register(t1);
#ifdef GE3_INLINE_RAY_SHADOW
RaytracingAccelerationStructure gCasters : register(t3);
#else
Texture2D<float> gShadow : register(t2);
SamplerComparisonState gComparison : register(s0);
#endif

cbuffer ShadowFrame : register(b0) {
    float4x4 gInverseViewProjection;
    float4x4 gLightViewProjection;
    float4 gEyeAndRange;
    float4 gSunAndStrength;
    float4 gMapTexelAndDepth;
};

struct PSInput { float4 position : SV_POSITION; float2 uv : TEXCOORD0; };

float3 WorldPosition(float2 uv, float depth) {
    float4 world=mul(float4(uv.x*2-1,1-uv.y*2,depth,1),gInverseViewProjection);
    return world.xyz/world.w;
}

float Visibility(float3 position,float3 normal,float3 sun,float facing) {
#ifdef GE3_INLINE_RAY_SHADOW
    const float3 side=normalize(cross(sun,abs(sun.y)<.95f?float3(0,1,0):float3(1,0,0)));
    const float3 up=cross(sun,side);
    float visibility=0;
    [unroll] for(int sampleIndex=0;sampleIndex<4;++sampleIndex) {
        const float2 offset=float2((sampleIndex&1)?1:-1,(sampleIndex&2)?1:-1)*.008f;
        RayDesc ray;
        ray.Origin=position+normal*.045f;
        ray.Direction=normalize(sun+side*offset.x+up*offset.y);
        ray.TMin=.025f/max(.26f,facing);
        ray.TMax=160.0f;
        RayQuery<RAY_FLAG_FORCE_OPAQUE|RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH|
            RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES> query;
        query.TraceRayInline(gCasters,RAY_FLAG_NONE,0xff,ray);
        while(query.Proceed()) {}
        visibility+=query.CommittedStatus()==COMMITTED_NOTHING?.25f:0;
    }
    return visibility;
#else
    float4 clip=mul(float4(position+normal*.105f,1),gLightViewProjection);
    float3 ndc=clip.xyz/clip.w;
    float2 uv=ndc.xy*float2(.5f,-.5f)+.5f;
    if(any(uv<.003f) || any(uv>.997f) || ndc.z<=0 || ndc.z>=1) return 1;
    const float bias=gMapTexelAndDepth.y*(.09f+.12f*(1-facing));
    const float texel=gMapTexelAndDepth.x;
    float visibility=0;
    [unroll] for(int y=-1;y<=1;++y) {
        [unroll] for(int x=-1;x<=1;++x)
            visibility+=gShadow.SampleCmpLevelZero(gComparison,uv+float2(x,y)*texel,ndc.z-bias);
    }
    return visibility/9.0f;
#endif
}

float4 main(PSInput input) : SV_TARGET {
    const int2 pixel=int2(input.position.xy);
    const float4 scene=gScene.Load(int3(pixel,0));
    // Opaque carrier material carries its own lighting. Restore its alpha
    // here before atmosphere and VFX while retaining its caster geometry.
    if(abs(scene.a-kRailShadowCasterOnly)<.001f) return float4(scene.rgb,1.0f);
    const float depth=gDepth.Load(int3(pixel,0));
    if(depth>=.999999f) return scene;
    const float3 world=WorldPosition(input.uv,depth);
    const float3 dx=ddx(world);
    const float3 dy=ddy(world);
    const float distanceToEye=length(world-gEyeAndRange.xyz);
    if(distanceToEye>=gEyeAndRange.w) return scene;
    const float3 crossNormal=cross(dy,dx);
    if(dot(crossNormal,crossNormal)<1e-12f) return scene;
    float3 normal=normalize(crossNormal);
    if(dot(normal,gEyeAndRange.xyz-world)<0) normal=-normal;
    const float3 sun=gSunAndStrength.xyz;
    const float facing=dot(normal,sun);
    if(facing<=.06f) return scene;
    const float fade=1-smoothstep(gEyeAndRange.w*.76f,gEyeAndRange.w,distanceToEye);
    const float occlusion=1-Visibility(world,normal,sun,facing);
    const float weight=fade*smoothstep(.06f,.48f,facing)*occlusion*gSunAndStrength.w;
    return float4(scene.rgb*lerp(1.0f,float3(.41f,.48f,.57f),weight),scene.a);
}
