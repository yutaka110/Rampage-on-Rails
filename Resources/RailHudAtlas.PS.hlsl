Texture2D<float4> gAtlas : register(t0);
SamplerState gSampler : register(s0);
#include "RailChargeStroke.hlsli"
#include "RailGatlingRing.hlsli"
#include "RailKillBoostOverlay.hlsli"

struct PixelInput {
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
    float4 color : COLOR0;
};

float4 main(PixelInput input) : SV_TARGET0 {
    if(input.texcoord.x>=48.0f && input.texcoord.x<=49.0f)
        return RailKillBoostOverlay(input.texcoord,input.color);
    if(input.texcoord.x>=46.0f && input.texcoord.x<=47.0f)
        return RailGatlingRing(input.texcoord,input.color);
    if(input.texcoord.x>=44.0f && input.texcoord.x<=45.0f)
        return RailChargeStroke(input.texcoord,input.color);
    float4 tex = gAtlas.Sample(gSampler, input.texcoord);
    return tex * input.color;
}
