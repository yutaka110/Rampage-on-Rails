Texture2D<float4> gSceneColor : register(t0);
Texture2D<float4> gVfxAccumulation : register(t1);
Texture2D<float4> gPostColor : register(t2);
SamplerState gSampler : register(s0);

// Original optical film-burn treatment: analog mottling and local image warping.
// Visual research: Materia's CONTROL title textures and Codrops' WebGL transitions.
// No reference code, footage or textures are bundled.
float FilmBurnHash(float2 cell) {
    float3 v = frac(float3(cell.x, cell.y, cell.x) * 0.1031f);
    v += dot(v, v.yzx + 33.33f);
    return frac((v.x + v.y) * v.z);
}

float FilmBurnNoise(float2 noisePosition) {
    float2 cell = floor(noisePosition);
    float2 f = frac(noisePosition);
    f = f * f * (3.0f - 2.0f * f);
    float lower = lerp(FilmBurnHash(cell), FilmBurnHash(cell + float2(1, 0)), f.x);
    float upper = lerp(FilmBurnHash(cell + float2(0, 1)), FilmBurnHash(cell + 1.0f), f.x);
    return lerp(lower, upper, f.y);
}

void EvaluateRailFilmBurn(float2 screenUv, float progress, float treatmentStrength,
                         out float2 sceneUv, out float2 chromaticOffset,
                         out float3 tint, out float visibility, out float3 burnLight) {
    sceneUv = screenUv;
    chromaticOffset = 0.0f;
    tint = 1.0f;
    visibility = 1.0f;
    burnLight = 0.0f;
    if (progress <= 0.0f) return;
    if (progress >= 1.0f) {
        visibility = 0.0f;
        return;
    }

    uint width, height;
    gPostColor.GetDimensions(width, height);
    float2 texel = rcp(float2(max(width, 1u), max(height, 1u)));
    float2 uv = screenUv;
    float2 opticalUv = uv * float2(float(width) / max(float(height), 1.0f), 1.0f);
    float strength = saturate(treatmentStrength);
    float onset = smoothstep(0.0f, 0.10f, progress);
    float finish = 1.0f - smoothstep(0.86f, 1.0f, progress);

    // The visibility field is time-independent: blackout never reveals a
    // previously covered pixel. Animate the optical surface, not the mask.
    float2 bend = float2(
        FilmBurnNoise(opticalUv * 2.2f + float2(3.7f, 9.1f)),
        FilmBurnNoise(opticalUv * 2.2f + float2(17.4f, 2.8f))) - 0.5f;
    float2 filmUv = opticalUv + bend * 0.28f;
    float broad = FilmBurnNoise(filmUv * float2(2.8f, 4.1f) + 8.7f);
    float folds = FilmBurnNoise(filmUv * float2(7.0f, 2.4f) + 21.3f);
    float2 remnantUv = (uv - float2(0.86f, 0.52f)) * float2(6.0f, 6.5f);
    float remnant = exp(-dot(remnantUv, remnantUv));
    float field = 0.86f * uv.x - 0.34f * pow(uv.y - 0.55f, 2.0f)
        + 0.14f * (broad - 0.5f) + 0.045f * (folds - 0.5f) + 0.34f * remnant;
    float phase = lerp(progress, smoothstep(0.0f, 1.0f, progress), 0.35f);
    float threshold = lerp(-0.22f, 1.22f, phase);
    float edgeDistance = field - threshold;
    visibility = smoothstep(-0.040f, 0.018f, edgeDistance);
    [branch] if (visibility <= 0.0f) return;

    // Stretched optical veins flow along the warm film, with finer surface
    // structure breaking up exposure rather than drawing a neon contour.
    float2 flowUv = filmUv + float2(-progress * 0.13f, progress * 0.08f);
    float veins = FilmBurnNoise(flowUv * float2(5.5f, 18.0f) + 13.2f);
    float surface = FilmBurnNoise(flowUv * float2(35.0f, 11.0f) + broad * 2.0f);
    float bleedWidth = lerp(0.065f, 0.145f, broad);
    float warmSheet = exp(-pow((edgeDistance - 0.045f) / bleedWidth, 2.0f));
    float amber = exp(-pow((edgeDistance - 0.017f) / (bleedWidth * 0.52f), 2.0f));
    float paleExposure = smoothstep(0.70f, 0.88f, veins) *
        exp(-pow((edgeDistance - 0.027f) / 0.027f, 2.0f));
    float exposure = lerp(0.42f, 1.04f, veins * 0.7f + surface * 0.3f);
    float lateEdge = 1.0f - 0.85f * smoothstep(0.64f, 0.84f, progress);
    float opticalBand = warmSheet * onset * finish * strength;

    // Resolution-independent refraction stays within a few display pixels.
    // The unburned image retains its framing; all channels share one surface.
    float2 flowDirection = float2(0.80f + bend.x, (veins - 0.5f) * 0.65f);
    sceneUv = clamp(uv + flowDirection * texel * 5.0f * opticalBand,
                    texel * 0.5f, 1.0f - texel * 0.5f);
    chromaticOffset = flowDirection * texel * 0.65f * opticalBand;
    float originalColor = smoothstep(0.010f, 0.22f, edgeDistance);
    tint = lerp(float3(0.33f, 0.16f, 0.105f), 1.0f,
                lerp(1.0f, originalColor, onset * strength));
    burnLight = (float3(0.24f, 0.038f, 0.022f) * warmSheet +
                 float3(0.38f, 0.19f, 0.070f) * amber +
                 float3(0.23f, 0.18f, 0.105f) * paleExposure) *
                 exposure * onset * finish * strength * lateEdge;

    // A localized copper afterglow shrinks with the same mask. Grain is
    // quantized in time, independent of frame rate, and only lives in the burn.
    float afterglow = remnant * smoothstep(0.60f, 0.78f, progress) * finish;
    burnLight += float3(0.28f, 0.11f, 0.043f) * afterglow * strength;
    float grain = FilmBurnHash(floor(screenUv * float2(width, height)) +
        floor(progress * 25.0f) * float2(37.0f, 19.0f)) - 0.5f;
    burnLight = max(0.0f, burnLight + grain * 0.028f * opticalBand);
}


cbuffer CompositeParams : register(b0)
{
    float gBloomIntensity;
    float gDistortionIntensity;
    float gGlowIntensity;
    float gPostIntensity;
    float gAux4;
    float gAux5;
    float gAux6;
    float gAux7;
};

struct PSInput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

float4 main(PSInput input) : SV_TARGET
{
    float2 uv = saturate(input.uv);
    // Mode 1 presents the already composed scene + HUD exactly once. Do not
    // apply the normal luminance fallback or add VFX again during blackout.
    if (gAux5 > 0.5f) {
        float2 sceneUv;
        float2 chromaticOffset;
        float3 tint;
        float visibility;
        float3 burnLight;
        EvaluateRailFilmBurn(uv, gAux4, gAux6, sceneUv, chromaticOffset, tint, visibility, burnLight);
        if (visibility <= 0.0f) return float4(0, 0, 0, 1);
        float3 presentation = gPostColor.Sample(gSampler, sceneUv).rgb;
        presentation.r = gPostColor.Sample(gSampler, saturate(sceneUv + chromaticOffset)).r;
        presentation.b = gPostColor.Sample(gSampler, saturate(sceneUv - chromaticOffset)).b;
        return float4((presentation * tint + burnLight) * visibility, 1.0f);
    }
    float4 scene = gSceneColor.Sample(gSampler, uv);
    float4 vfx = gVfxAccumulation.Sample(gSampler, uv);
    float4 post = gPostColor.Sample(gSampler, uv);
    float postBlend = saturate(gPostIntensity);
    float postLuminance = dot(post.rgb, float3(0.2126f, 0.7152f, 0.0722f));
    float effectiveBlend = postLuminance > 0.0001f ? postBlend : 0.0f;
    float3 color = lerp(scene.rgb, post.rgb, effectiveBlend);
    // Raw VFX belongs to the final presentation composite. Keeping this here
    // makes particles independent of optional bloom/glow and editor UI paths.
    color += vfx.rgb * max(vfx.a, 0.2f);
    return float4(saturate(color), 1.0f);
}
