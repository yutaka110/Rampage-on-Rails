Texture2D<float4> gSceneColor : register(t0);
Texture2D<float4> gVfxAccumulation : register(t1);
Texture2D<float4> gPostColor : register(t2);
SamplerState gSampler : register(s0);

// Original implementation of a photographic film-burn transition.
// Visual reference: johh's basic film burn, https://www.shadertoy.com/view/ltlBzn.
// No Shadertoy code or texture is bundled. The static mottling is generated here.
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

void EvaluateRailFilmBurn(float2 screenUv, float progress,
                         out float2 sceneUv, out float3 tint, out float visibility, out float3 burnLight) {
    sceneUv = screenUv;
    tint = 1.0f;
    visibility = 1.0f;
    burnLight = 0.0f;
    if (progress <= 0.0f) return;
    if (progress >= 1.0f) {
        visibility = 0.0f;
        return;
    }

    // Work in bottom-left coordinates to reproduce the reference's zoom pivot.
    float2 uv = float2(screenUv.x, 1.0f - screenUv.y);
    // Broad static patches resemble an uneven film surface. Small, dense
    // octaves made the reference's broad burn look like a ragged dissolve.
    float2 noisePosition = uv * 2.4f;
    float mottling = 0.0f;
    float weight = 0.5f;
    [unroll] for (int octave = 0; octave < 4; ++octave) {
        mottling += weight * FilmBurnNoise(noisePosition);
        noisePosition = noisePosition * 2.03f + float2(13.7f, 8.1f);
        weight *= 0.5f;
    }
    mottling = saturate((mottling / 0.9375f - 0.5f) * 1.8f + 0.45f);
    float boundary = (2.0f * uv.x + mottling - 0.5f) / 3.0f;
    // Extend the demonstration's range so an actual scene change has exact,
    // continuous unmodified/opaque endpoints, without its sine-wave reversal.
    // One monotonic half-cycle gives the demonstration's sinusoidal sweep.
    // The presentation clock is linear; apply this easing exactly once.
    float phase = 0.5f - 0.5f * cos(saturate(progress) * 3.14159265359f);
    float threshold = lerp(-0.4f, 1.0f, phase);
    visibility = smoothstep(threshold - 0.10f, threshold, boundary);
    float distortion = smoothstep(threshold - 0.30f, threshold + 0.05f, boundary);
    float originalColor = smoothstep(threshold - 0.20f, threshold + 0.15f, boundary);
    originalColor = originalColor * originalColor * originalColor;
    tint = lerp(float3(0.52f, 0.23f, 0.13f), 1.0f, originalColor);
    // Broad, uneven red-brown/amber exposure bleeds into the surviving image.
    // Avoid an equally bright contour along the entire mask: only sparse
    // hot patches retain a faint pale core. All light shares the blackout mask.
    float edgeDistance=boundary-threshold;
    float exposurePatch=FilmBurnNoise(uv*float2(4.3f,3.2f)+float2(7.1f,19.4f));
    float bleedWidth=lerp(0.075f,0.14f,exposurePatch);
    float redBleed=exp(-pow((edgeDistance+0.008f)/bleedWidth,2.0f));
    float amberBleed=exp(-pow((edgeDistance-0.042f)/(bleedWidth*0.72f),2.0f));
    float unevenExposure=lerp(0.65f,1.0f,exposurePatch);
    float hotPatch=smoothstep(0.68f,0.88f,exposurePatch);
    float core=exp(-pow((edgeDistance+0.018f)/0.006f,2.0f))*hotPatch*0.12f;
    burnLight=(float3(0.42f,0.075f,0.025f)*redBleed+
        float3(0.46f,0.22f,0.065f)*amberBleed)*unevenExposure+
        float3(1.0f,0.78f,0.48f)*core;
    float zoom = lerp(0.7f, 1.0f, distortion * distortion);
    float2 warped = uv * zoom;
    sceneUv = float2(warped.x, 1.0f - warped.y);
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
        float3 tint;
        float visibility;
        float3 burnLight;
        EvaluateRailFilmBurn(uv, gAux4, sceneUv, tint, visibility, burnLight);
        if (visibility <= 0.0f) return float4(0, 0, 0, 1);
        float3 presentation = gPostColor.Sample(gSampler, sceneUv).rgb;
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
