// Original shadowed sunlight/dust renderer. No Shadertoy source is embedded.
// Visual reference: Vinicius Santos, https://vgracianos.github.io/blog/making-of-sunfall/
#include "SunfallLighting.hlsli"
Texture2D<float> gDepth : register(t0);
Texture2D<float> gSunShadow : register(t1);
SamplerState gSampler : register(s0);

cbuffer SunfallFrame : register(b0) {
    float4x4 gInverseViewProjection;
    float4x4 gLightViewProjection;
    float4 gCameraAndTime;
    float4 gSunDirectionAndStrength;
    float4 gSunColorAndDensity;
    float4 gRangeHeightAndGround;
    float4 gWindAndShadow;
};
struct PSInput { float4 position : SV_POSITION; float2 uv : TEXCOORD0; };

float Hash(float3 p) {
    p=frac(p*0.1031f);
    p+=dot(p,p.yzx+33.33f);
    return frac((p.x+p.y)*p.z);
}
float Noise(float3 p) {
    float3 i=floor(p),f=frac(p);
    f=f*f*(3.0f-2.0f*f);
    return lerp(lerp(lerp(Hash(i),Hash(i+float3(1,0,0)),f.x),
        lerp(Hash(i+float3(0,1,0)),Hash(i+float3(1,1,0)),f.x),f.y),
        lerp(lerp(Hash(i+float3(0,0,1)),Hash(i+float3(1,0,1)),f.x),
        lerp(Hash(i+float3(0,1,1)),Hash(i+float3(1,1,1)),f.x),f.y),f.z);
}
float SunVisibility(float3 p) {
    if(gWindAndShadow.w<0.5f) return 0.0f;
    float4 projected=mul(float4(p,1),gLightViewProjection);
    float3 ndc=projected.xyz/max(projected.w,1e-5f);
    float2 uv=float2(ndc.x*0.5f+0.5f,0.5f-ndc.y*0.5f);
    // Outside the resident shadow window no sunlight is invented through walls.
    if(any(uv<0.002f) || any(uv>0.998f) || ndc.z<0 || ndc.z>1) return 0;
    uint w,h;gSunShadow.GetDimensions(w,h);
    float2 pixel=1.0f/float2(w,h);
    float visibility=0;
    [unroll] for(int i=0;i<4;++i) {
        float2 offset=float2(i&1,i>>1)-0.5f;
        float d=gSunShadow.SampleLevel(gSampler,uv+offset*pixel,0);
        visibility+=ndc.z-gWindAndShadow.z<=d ? 0.25f : 0.0f;
    }
    return visibility;
}

float4 main(PSInput input) : SV_TARGET {
    float2 uv=saturate(input.uv);
    uint width,height;gDepth.GetDimensions(width,height);
    // Use the nearest of a 2x2 depth footprint to protect thin foreground edges.
    int2 base=int2(uv*float2(width,height)-0.5f);
    float depth=1;
    [unroll] for(int d=0;d<4;++d)
        depth=min(depth,gDepth.Load(int3(clamp(base+int2(d&1,d>>1),int2(0,0),int2(width-1,height-1)),0)));
    float4 world=mul(float4(uv.x*2-1,1-uv.y*2,min(depth,0.99999f),1),gInverseViewProjection);
    world.xyz/=max(world.w,1e-6f);
    float3 delta=world.xyz-gCameraAndTime.xyz;
    float surfaceDistance=length(delta);
    float3 ray=delta/max(surfaceDistance,1e-5f);
    float travel=min(max(surfaceDistance-0.35f,0),gRangeHeightAndGround.x);
    float stepLength=travel/16.0f;
    float jitter=0.2f+0.6f*Hash(float3(floor(input.position.xy),3.17f));
    float3 toSun=normalize(-gSunDirectionAndStrength.xyz);
    if(depth>0.99999f && gRangeHeightAndGround.w>0) {
        // Own the uncovered sky in HDR too. Depending on an earlier UNORM
        // background loses both its colour gradient and the sun's radiance.
        return float4(SunfallSkyRadiance(ray,toSun,gSunColorAndDensity.rgb,
            gSunDirectionAndStrength.w),0);
    }
    float phase=0.32f+0.68f*pow(saturate(dot(ray,toSun)),8);
    float3 scatter=0;
    float transmission=1;
    [loop] for(int i=0;i<16;++i) {
        float distance=(float(i)+jitter)*stepLength;
        float3 p=gCameraAndTime.xyz+ray*distance;
        float3 drift=float3(gWindAndShadow.x,0.08f,gWindAndShadow.y)*gCameraAndTime.w;
        float3 q=(p-drift)*0.065f;
        float dust=0.54f*Noise(q)+0.29f*Noise(q*2.03f+13.4f)+0.17f*Noise(q*4.07f);
        float height=exp(-max(p.y-gRangeHeightAndGround.z,0)/max(gRangeHeightAndGround.y,1));
        float visibility=SunVisibility(p);
        // Sunlit dust glows; enclosed air stays thin rather than becoming a
        // bright, opaque overlay. Nearby silhouettes retain their contrast.
        float density=gSunColorAndDensity.w*height*(0.28f+1.1f*dust)*
            smoothstep(30.0f,75.0f,distance)*(0.20f+0.80f*visibility)*
            lerp(1.0f,0.65f,gRangeHeightAndGround.w);
        float extinction=1-exp(-density*stepLength);
        float3 skyBounce=SunfallZenith(gSunColorAndDensity.rgb)*0.10f*gRangeHeightAndGround.w;
        float3 illumination=float3(0.045f,0.055f,0.075f)+skyBounce+
            SunfallHorizon(gSunColorAndDensity.rgb,gSunDirectionAndStrength.w)*
            gSunDirectionAndStrength.w*visibility*phase;
        scatter+=transmission*extinction*illumination;
        transmission*=1-extinction;
    }
    return float4(scatter,max(transmission,0.78f));
}
