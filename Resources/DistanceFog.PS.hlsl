Texture2D<float4> gInputTexture : register(t0);
Texture2D<float> gSceneDepth : register(t1);
Texture2D<float4> gUnused2 : register(t2);
SamplerState gSampler : register(s0);

cbuffer PostProcessParams : register(b0)
{
    float gIntensity;
    float gFogStart;
    float gFogEnd;
    float gFogDensity;
    float gFogColorR;
    float gFogColorG;
    float gFogColorB;
    float gNearPlane;
    float gFarPlane;
    float gFogDepthBoost;
    float gFogDepthBoostStart;
    float gBacklitFogLift;
    float gOpeningGlowStrength;
    float gForegroundSilhouetteStrength;
    float gLowFogLayerStrength;
    float gCoolFloorHazeStrength;
};

struct PSInput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

float LinearizeDepth(float deviceDepth, float nearPlane, float farPlane)
{
    return (nearPlane * farPlane) / max(farPlane - deviceDepth * (farPlane - nearPlane), 1.0e-5f);
}

float4 main(PSInput input) : SV_TARGET
{
    float2 uv = saturate(input.uv);
    float4 color = gInputTexture.Sample(gSampler, uv);
    float deviceDepth = saturate(gSceneDepth.SampleLevel(gSampler, uv, 0.0f));

    float nearPlane = max(gNearPlane, 0.001f);
    float farPlane = max(gFarPlane, nearPlane + 1.0f);
    float linearDepth = LinearizeDepth(deviceDepth, nearPlane, farPlane);
    float fogRange = max(gFogEnd - gFogStart, 0.001f);
    float rangeMask = saturate((linearDepth - gFogStart) / fogRange);
    float exponentialMask = 1.0f - exp(-rangeMask * rangeMask * max(gFogDensity, 0.0f) * 1.45f);
    float farBoostMask = smoothstep(saturate(gFogDepthBoostStart), 1.0f, rangeMask);
    float depthBoost = farBoostMask * max(gFogDepthBoost, 0.0f) * (0.35f + rangeMask * 0.65f);
    // With near=0.1, depth 0.988 is only about eight world units away.
    // Reserve sky treatment for the clear depth, not nearby combat geometry.
    float skyMask = smoothstep(0.999995f, 1.0f, deviceDepth) * 0.26f;

    float3 fogColor = saturate(float3(gFogColorR, gFogColorG, gFogColorB));
    float verticalLift = smoothstep(0.48f, 1.0f, uv.y);
    float backlitLift = farBoostMask * max(gBacklitFogLift, 0.0f);
    float2 glowDelta = (uv - float2(0.43f, 0.39f)) * float2(0.82f, 0.78f);
    float openingGlowShape = exp(-dot(glowDelta, glowDelta) * 2.72f);
    float2 slotDelta = (uv - float2(0.44f, 0.52f)) * float2(1.12f, 0.58f);
    float distantCanyonSlot = exp(-dot(slotDelta, slotDelta) * 2.05f);
    float2 leftExitDelta = (uv - float2(0.24f, 0.46f)) * float2(0.86f, 0.66f);
    float leftExitGlow = exp(-dot(leftExitDelta, leftExitDelta) * 1.72f);
    float openingGlow =
        max(gOpeningGlowStrength, 0.0f) *
        saturate(farBoostMask * 0.90f + skyMask * 3.2f) *
        (0.14f + openingGlowShape * 0.42f + distantCanyonSlot * 0.30f + leftExitGlow * 0.42f);
    float floorLayer =
        max(gLowFogLayerStrength, 0.0f) *
        smoothstep(0.48f, 0.92f, uv.y) *
        smoothstep(0.04f, 0.72f, rangeMask) *
        (1.0f - smoothstep(0.90f, 1.0f, rangeMask));
    float coolFloor =
        max(gCoolFloorHazeStrength, 0.0f) *
        smoothstep(0.56f, 0.98f, uv.y) *
        smoothstep(0.18f, 0.84f, rangeMask);

    float fogMask = saturate(max(exponentialMask + depthBoost, skyMask) * gIntensity + floorLayer * 0.18f);
    fogMask *= smoothstep(max(nearPlane + 1.0f, gFogStart * 0.50f),
        max(gFogStart, nearPlane + 2.0f), linearDepth);
    fogMask = min(fogMask, 0.52f + openingGlow * 0.12f);
    float3 liftedFog =
        fogColor +
        float3(0.075f, 0.058f, 0.042f) * verticalLift +
        float3(0.18f, 0.13f, 0.055f) * backlitLift +
        float3(0.48f, 0.46f, 0.42f) * openingGlow +
        float3(0.11f, 0.14f, 0.17f) * coolFloor;
    float3 result = lerp(color.rgb, liftedFog, fogMask);

    float topMask = 1.0f - smoothstep(0.14f, 0.58f, uv.y);
    float2 vignetteDelta = (uv - 0.5f) * float2(1.28f, 0.84f);
    float sideMask = smoothstep(0.34f, 0.82f, dot(vignetteDelta, vignetteDelta));
    float nearMask = 1.0f - smoothstep(0.04f, 0.46f, rangeMask);
    float silhouetteMask =
        saturate(max(gForegroundSilhouetteStrength, 0.0f) *
        nearMask *
        (topMask * 0.78f + sideMask * 0.54f) *
        (1.0f - openingGlow * 0.22f));
    result = lerp(result, result * float3(0.23f, 0.20f, 0.18f), silhouetteMask);

    float3 whiteHot = float3(1.0f, 0.965f, 0.90f);
    result += whiteHot * openingGlow * 0.26f;
    result += float3(0.22f, 0.30f, 0.36f) * floorLayer * coolFloor * 0.11f;
    return float4(max(result,0.0f), color.a);
}
