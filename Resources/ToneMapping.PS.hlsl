Texture2D<float4> gInputTexture : register(t0);
Texture2D<float4> gUnused1 : register(t1);
Texture2D<float4> gUnused2 : register(t2);
SamplerState gSampler : register(s0);

cbuffer PostProcessParams : register(b0)
{
    float gIntensity;
    float gToneExposure;
    float gEncodeSrgb;
    float gAux2;
    float gAux3;
    float gAux4;
    float gAux5;
    float gAux6;
};

struct PSInput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

float3 ToneMap(float3 color)
{
    return color / (1.0f + color);
}

float3 LinearToSrgb(float3 color)
{
    color = saturate(color);
    return lerp(1.055f * pow(color, 1.0f / 2.4f) - 0.055f,
        color * 12.92f, step(color, 0.0031308f));
}

float4 main(PSInput input) : SV_TARGET
{
    float exposure = max(gToneExposure, 0.0001f);
    float3 color = gInputTexture.Sample(gSampler, saturate(input.uv)).rgb * exposure * max(gIntensity, 0.0001f);
    float3 mapped = ToneMap(max(color, 0.0f));
    // The presentation RTV is UNORM, not SRGB. Encode exactly once after
    // linear atmosphere/bloom; HUD and authored weapon cores are drawn later.
    if (gEncodeSrgb > 0.5f) mapped = LinearToSrgb(mapped);
    return float4(mapped, 1.0f);
}
