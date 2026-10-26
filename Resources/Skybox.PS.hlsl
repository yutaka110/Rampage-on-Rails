#include "Skybox.hlsli"
#include "SunfallLighting.hlsli"

TextureCube<float4> gTexture : register(t0);
SamplerState gSampler : register(s0);
cbuffer Sunlight : register(b1) {
    float4 gSunDirectionAndEnabled;
    float4 gSunColorAndIntensity;
};

float4 main(VertexShaderOutput input) : SV_TARGET0
{
    float3 dir = normalize(input.texcoord);
    if(gSunDirectionAndEnabled.w>0.5f) {
        float3 toSun=normalize(-gSunDirectionAndEnabled.xyz);
        float3 daylight=SunfallSkyRadiance(dir,toSun,gSunColorAndIntensity.rgb,gSunColorAndIntensity.w);
        return float4(daylight,1);
    }
    float horizon = saturate(dir.y * 0.5f + 0.5f);
    float sunCore = pow(saturate(dot(dir, normalize(float3(0.05f, 0.24f, 0.97f)))), 24.0f);
    float sunBloom = pow(saturate(dot(dir, normalize(float3(0.03f, 0.16f, 0.99f)))), 1.55f);
    float canyonExit = pow(saturate(dot(dir, normalize(float3(-0.18f, 0.01f, 0.98f)))), 1.55f);
    float overhead = pow(saturate(dir.y), 1.55f);
    float dustHaze = pow(saturate(1.0f - abs(dir.y)), 2.1f);
    float leftExit = pow(saturate(dot(dir, normalize(float3(-0.78f, -0.04f, 0.62f)))), 1.12f);
    float exitCore = pow(saturate(dot(dir, normalize(float3(-0.54f, 0.00f, 0.84f)))), 3.6f);
    float leftWhiteCore = pow(saturate(dot(dir, normalize(float3(-0.82f, -0.02f, 0.57f)))), 2.15f);

    float3 low = float3(0.82f, 0.87f, 0.90f);
    float3 mid = float3(0.94f, 0.97f, 0.98f);
    float3 high = float3(1.04f, 1.03f, 0.99f);
    float3 whiteBacklight = float3(1.42f, 1.40f, 1.26f);
    float3 sky = lerp(low, mid, horizon);
    sky = lerp(sky, high, overhead * 0.48f);
    sky = lerp(sky, whiteBacklight, saturate(sunBloom * 0.58f + canyonExit * 0.48f + leftExit * 0.58f + exitCore * 0.46f + leftWhiteCore * 0.62f));
    sky += float3(1.0f, 0.97f, 0.88f) * (sunCore * 1.02f + canyonExit * 0.28f + exitCore * 0.42f + leftWhiteCore * 0.52f);
    sky += float3(0.08f, 0.08f, 0.08f) * dustHaze * 0.026f;
    return float4(saturate(sky), 1.0f);
}
