Texture2D<float4> gScene : register(t0);
Texture2D<float4> gVolume : register(t1);
Texture2D<float> gDepth : register(t2);
#include "SunfallLighting.hlsli"
SamplerState gSampler : register(s0);
cbuffer Params : register(b0) {
    float gStrength; float gNear; float gFar; float gOutdoor;
    float3 gSunColor; float gSunIntensity;
};
struct PSInput { float4 position : SV_POSITION; float2 uv : TEXCOORD0; };
float LinearDepth(float d) {return gNear*gFar/max(gFar-d*(gFar-gNear),1e-5f);}
float4 main(PSInput input) : SV_TARGET {
    float2 uv=saturate(input.uv);
    uint width,height;gVolume.GetDimensions(width,height);
    uint dw,dh;gDepth.GetDimensions(dw,dh);
    int2 depthPixel=clamp(int2(uv*float2(dw,dh)),int2(0,0),int2(dw-1,dh-1));
    float reference=LinearDepth(gDepth.Load(int3(depthPixel,0)));
    float2 texelPosition=uv*float2(width,height)-0.5f;
    int2 base=int2(floor(texelPosition));float2 fraction=frac(texelPosition);
    float4 volume=0;float weights=0;
    [unroll] for(int i=0;i<4;++i) {
        int2 offset=int2(i&1,i>>1);
        int2 pixel=clamp(base+offset,int2(0,0),int2(width-1,height-1));
        float2 neighbor=(float2(pixel)+0.5f)/float2(width,height);
        int2 full=clamp(int2(neighbor*float2(dw,dh)-0.5f),int2(0,0),int2(dw-1,dh-1));
        float nearest=1;
        [unroll] for(int j=0;j<4;++j)
            nearest=min(nearest,gDepth.Load(int3(min(full+int2(j&1,j>>1),int2(dw-1,dh-1)),0)));
        float error=abs(LinearDepth(nearest)-reference);
        float2 blend=lerp(1-fraction,fraction,float2(offset));
        float weight=blend.x*blend.y*exp(-error/max(reference*0.025f,0.5f));
        volume+=gVolume.Load(int3(pixel,0))*weight;weights+=weight;
    }
    // No matching depth means a thin foreground silhouette: leave it crisp.
    volume=weights>1e-5f ? volume/weights : float4(0,0,0,1);
    float4 scene=gScene.SampleLevel(gSampler,uv,0);
    if(reference>=gFar*0.98f && gOutdoor>0 && weights>1e-5f) {
        return float4(lerp(scene.rgb,volume.rgb,smoothstep(0.0f,0.25f,gOutdoor)),scene.a);
    }
    // Fade extinction and scattering together: a close drone must not inherit
    // the bright veil accumulated behind it or lose its silhouette contrast.
    float combatClear=smoothstep(45.0f,110.0f,reference);
    float strength=gStrength*combatClear;
    float3 color=scene.rgb*lerp(1.0f,volume.a,strength)+volume.rgb*strength;
    float haze=SunfallDistanceHaze(reference,gOutdoor);
    // Distance-limited haze leaves the close combat space clear. Distant
    // silhouettes converge toward the same sunlit horizon as the sky.
    float3 air=SunfallBackdropHorizon(gSunColor);
    float distant=smoothstep(300.0f,1250.0f,reference);
    air=lerp(lerp(SunfallZenith(gSunColor)*.55f,air,0.70f),air,distant);
    if(reference<gFar*0.98f) color=lerp(color,air,haze);
    return float4(color,scene.a);
}
