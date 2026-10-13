Texture2D<float4> gSceneColor : register(t0);
Texture2D<float4> gVfxAccumulation : register(t1);
Texture2D<float4> gPostColor : register(t2);
SamplerState gSampler : register(s0);

// Title-to-game presentation: directional film burn and rail-impact light leak.
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

float FilmBurnTravelTime(float2 delta, float2 direction) {
    float along = dot(delta, direction);
    float across = dot(delta, float2(-direction.y, direction.x));
    // Heat runs toward the rail's vanishing point first, then spreads laterally
    // and behind the source. Unequal travel rates give an asymmetric spread.
    float lateralSpread = sqrt(across * across + 0.20f * 0.20f) - 0.20f;
    return max(along, 0.0f) * 0.70f + max(-along, 0.0f) * 1.45f +
        lateralSpread * 0.64f + across * across * 0.20f;
}

void EvaluateRailFilmBurn(float2 screenUv, float progress, float treatmentStrength,
                         float2 tunnelFocus, float2 lightOrigin, float incoming,
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
    float aspect = float(width) / max(float(height), 1.0f);
    if (incoming > 0.5f) {
        float reveal = 1.0f - progress;
        float2 depthUv = (screenUv - tunnelFocus) * float2(aspect * 1.4f, 1.8f);
        float centerExposure = exp(-dot(depthUv, depthUv));
        float revealStart = 0.10f * (1.0f - centerExposure);
        float revealEnd = 0.78f + 0.14f * (1.0f - centerExposure);
        float firstLight = saturate((reveal - revealStart) / (revealEnd - revealStart));
        // Lift the first visible exposure promptly, keeping soft endpoints.
        visibility = lerp(smoothstep(revealStart, revealEnd, reveal), firstLight, 0.25f);
        tint = lerp(float3(0.88f, 0.80f, 0.74f), 1.0f, smoothstep(0.12f, 0.75f, reveal));
        return;
    }
    float2 texel = rcp(float2(max(width, 1u), max(height, 1u)));
    float strength = saturate(treatmentStrength);
    float onset = smoothstep(0.0f, 0.10f, progress);
    float finish = 1.0f - smoothstep(0.86f, 1.0f, progress);
    // Ignite inside the image at the confirmed strike's exposure imprint.
    // The black region grows outward while its fastest lobe runs down the rail.
    // Keep an interior seed even when a strike projects close to a screen edge.
    float2 burnSeed = clamp(lightOrigin, float2(0.12f, 0.12f), float2(0.88f, 0.88f));
    float2 travelDirection = (tunnelFocus - burnSeed) * float2(aspect, 1.0f);
    if (dot(travelDirection, travelDirection) < 0.0001f) travelDirection = float2(0.0f, -1.0f);
    travelDirection *= rsqrt(max(dot(travelDirection, travelDirection), 0.00001f));
    float2 lateralDirection = float2(-travelDirection.y, travelDirection.x);
    float2 sourceDelta = (screenUv - burnSeed) * float2(aspect, 1.0f);
    float2 flowPosition = float2(dot(sourceDelta, travelDirection), dot(sourceDelta, lateralDirection));
    float2 bend = float2(
        FilmBurnNoise(flowPosition * 2.2f + float2(3.7f, 9.1f)),
        FilmBurnNoise(flowPosition * 2.2f + float2(17.4f, 2.8f))) - 0.5f;
    float2 filmUv = flowPosition + bend * 0.45f;
    float broad = FilmBurnNoise(filmUv * float2(2.8f, 4.1f) + 8.7f);
    float folds = FilmBurnNoise(filmUv * float2(7.0f, 2.4f) + 21.3f);
    float charTexture = FilmBurnNoise(filmUv * float2(22.0f, 17.0f) + 47.1f);
    // Keep arrival times fixed while the threshold advances. Moving surface
    // texture can flicker and flow without making burnt regions reopen.
    float sourceSpread = smoothstep(0.02f, 0.24f, length(sourceDelta));
    // Vary propagation over large connected patches, not just along the rim.
    // Fast tongues run ahead while slow pockets hold the scene between them.
    // Multiplicative variation breaks the underlying oval at every scale;
    // keeping it static preserves one-way blackout without shape pulsation.
    float propagation = FilmBurnNoise(flowPosition * float2(1.65f, 2.15f) + bend * 0.70f +
                                      float2(12.7f, 6.3f));
    float travelRate = lerp(0.58f, 1.42f, smoothstep(0.18f, 0.82f, propagation));
    float field = FilmBurnTravelTime(sourceDelta, travelDirection) *
        lerp(1.0f, travelRate, sourceSpread) + sourceSpread *
        (0.22f * (broad - 0.5f) + 0.10f * (folds - 0.5f) +
         0.020f * (charTexture - 0.5f));
    float farthestArrival = max(
        max(FilmBurnTravelTime((float2(0, 0) - burnSeed) * float2(aspect, 1), travelDirection),
            FilmBurnTravelTime((float2(1, 0) - burnSeed) * float2(aspect, 1), travelDirection)),
        max(FilmBurnTravelTime((float2(0, 1) - burnSeed) * float2(aspect, 1), travelDirection),
            FilmBurnTravelTime((float2(1, 1) - burnSeed) * float2(aspect, 1), travelDirection)));
    float phase = lerp(progress, smoothstep(0.0f, 1.0f, progress), 0.20f);
    // Travel time is convex before modulation, so its corner maximum bounds
    // the whole image. Include the maximum rate and all noise amplitudes to
    // cover even interior delayed pockets before progress reaches one.
    float edgeDistance = field - lerp(-0.12f, farthestArrival * 1.42f + 0.23f, phase);
    // Evaluate derivatives before rejecting black pixels, so the burn front
    // keeps a stable refraction direction across partially covered quads.
    float2 gradient = float2(ddx(field) / texel.x, ddy(field) / texel.y);
    float fieldWidth = fwidth(field);
    visibility = smoothstep(-0.04f, 0.018f, edgeDistance);
    float2 frontNormal = gradient * rsqrt(max(dot(gradient, gradient), 0.00001f));
    float impactSide = exp(-1.15f * length(sourceDelta));
    float ignition = smoothstep(0.06f, 0.20f, progress) *
        (1.0f - smoothstep(0.44f, 0.62f, progress));
    float sourceExposure = (0.38f + 0.67f * impactSide) * (0.72f + 0.55f * ignition);
    float2 flowUv = filmUv + float2(-progress * 0.32f, progress * 0.035f);
    float veins = FilmBurnNoise(flowUv * float2(5.5f, 18.0f) + 13.2f);
    float surface = FilmBurnNoise(flowUv * float2(35.0f, 11.0f) + broad * 2.0f);
    float bleedWidth = lerp(0.065f, 0.145f, broad);
    float warmSheet = exp(-pow((edgeDistance - 0.045f) / bleedWidth, 2.0f));
    float amber = exp(-pow((edgeDistance - 0.017f) / (bleedWidth * 0.52f), 2.0f));
    float paleExposure = smoothstep(0.70f, 0.88f, veins) *
        exp(-pow((edgeDistance - 0.027f) / 0.027f, 2.0f));
    float exposure = lerp(0.38f, 1.04f, veins * 0.7f + surface * 0.3f);
    float lateEdge = 1.0f - 0.72f * smoothstep(0.78f, 0.95f, progress);
    float opticalBand = warmSheet * onset * finish * strength * sourceExposure;
    float streamBend = FilmBurnNoise(float2(filmUv.y * 5.2f - progress * 0.45f, filmUv.x * 1.7f + 31.6f));
    float2 streamUv = float2(edgeDistance * 31.0f + streamBend * 1.8f, filmUv.y * 3.8f - progress * 0.65f);
    float streamSurface = FilmBurnNoise(streamUv + float2(41.3f, 11.7f));
    float threadWidth = max(0.022f, fwidth(streamSurface) * 0.8f);
    float threads = 1.0f - smoothstep(threadWidth * 0.25f, threadWidth, abs(streamSurface - 0.5f));
    float streamBreaks = FilmBurnNoise(flowUv * float2(3.1f, 12.7f) + 37.5f);
    // Let the luminous rim break into bright exposure patches and dim copper
    // wisps, independently of the continuous advance of the black region.
    exposure *= lerp(0.36f, 1.0f, smoothstep(0.26f, 0.70f, streamBreaks));
    float fineStreams = threads * smoothstep(0.34f, 0.68f, streamBreaks) *
        exp(-pow((edgeDistance - 0.048f) / 0.075f, 2.0f));
    float sharpWidth = max(0.0035f, fieldWidth * 1.5f);
    float sharpFragments = exp(-pow((edgeDistance - 0.008f) / sharpWidth, 2.0f)) *
        smoothstep(0.61f, 0.80f, streamBreaks) * lerp(0.45f, 1.0f, surface);
    float2 frontTangent = float2(-frontNormal.y, frontNormal.x);
    float compression = sin(edgeDistance * 37.0f + streamBend * 3.0f - progress * 6.0f);
    float2 refractedPixels = frontNormal * (12.0f + compression * 6.0f) +
        frontTangent * ((veins - 0.5f) * 3.5f + (streamBend - 0.5f) * 2.5f);
    float refractionBand = exp(-pow((edgeDistance - 0.04f) / 0.07f, 2.0f)) *
        onset * finish * strength * sourceExposure;
    float resolutionScale = max(float(height) / 1080.0f, 0.5f);
    sceneUv = clamp(screenUv + refractedPixels * texel * resolutionScale * refractionBand,
                    texel * 0.5f, 1.0f - texel * 0.5f);
    chromaticOffset = frontNormal * texel * 0.65f * resolutionScale * refractionBand;
    float originalColor = smoothstep(0.010f, 0.22f, edgeDistance);
    float3 shadowTint = lerp(float3(0.31f, 0.24f, 0.18f), float3(0.50f, 0.27f, 0.14f), impactSide);
    tint = lerp(shadowTint, 1.0f, lerp(1.0f, originalColor, onset * strength));
    burnLight = (float3(0.24f, 0.038f, 0.022f) * warmSheet +
                 float3(0.60f, 0.28f, 0.070f) * amber +
                 float3(0.23f, 0.18f, 0.105f) * paleExposure) *
        exposure * onset * finish * strength * lateEdge * sourceExposure;
    burnLight += (float3(0.105f, 0.048f, 0.022f) * fineStreams +
                  float3(0.115f, 0.079f, 0.039f) * sharpFragments) *
        onset * finish * strength * lateEdge * sourceExposure;
    float grain = FilmBurnHash(floor(screenUv * float2(width, height)) +
        floor(progress * 25.0f) * float2(37.0f, 19.0f)) - 0.5f;
    burnLight = max(0.0f, burnLight + grain * 0.028f * opticalBand);
}
float3 EvaluateRailLensLeak(float2 uv, float2 source, float2 tunnelFocus,
                            float strength, float age, float shade) {
    if (strength <= 0.0f) return 0.0f;
    uint width, height;
    gPostColor.GetDimensions(width, height);
    float aspect = float(width) / max(float(height), 1.0f);
    // A brief bloom begins on the actual rail strike. Retain its softer exposure
    // imprint inside the glass, connecting the impact to the later burn seed.
    float2 sourceUv = (uv - source) * float2(aspect * 14.0f, 18.0f);
    float flash = exp(-dot(sourceUv, sourceUv)) * exp(-age * 14.0f);
    float2 burnSeed = clamp(source, float2(0.12f, 0.12f), float2(0.88f, 0.88f));
    float2 leakCenter = lerp(source, burnSeed, smoothstep(0.025f, 0.45f, age));
    leakCenter += (tunnelFocus - burnSeed) * 0.12f * smoothstep(0.05f, 0.50f, age);
    float2 leakUv = (uv - leakCenter) * float2(aspect * 3.0f, 3.8f);
    float mottling = FilmBurnNoise(uv * float2(6.0f, 11.0f) + float2(-age * 0.17f, 18.7f));
    float veil = exp(-dot(leakUv, leakUv)) * lerp(0.55f, 1.0f, mottling);
    return (float3(0.21f, 0.13f, 0.058f) * flash +
            float3(0.16f, 0.054f, 0.021f) * veil) * strength * (1.0f - 0.65f * shade);
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
    float2 gTunnelFocus;
    float2 gImpactScreen;
    float gLensLeak;
    float gLensAge;
    float gTunnelShade;
    float gAux15;
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
        EvaluateRailFilmBurn(uv, gAux4, gAux6, gTunnelFocus, gImpactScreen, gAux7,
                             sceneUv, chromaticOffset, tint, visibility, burnLight);
        if (visibility <= 0.0f) return float4(0, 0, 0, 1);
        float3 presentation = gPostColor.Sample(gSampler, sceneUv).rgb;
        presentation.r = gPostColor.Sample(gSampler, saturate(sceneUv + chromaticOffset)).r;
        presentation.b = gPostColor.Sample(gSampler, saturate(sceneUv - chromaticOffset)).b;
        float3 lensLight = EvaluateRailLensLeak(uv, gImpactScreen, gTunnelFocus,
                                               gLensLeak, gLensAge, gTunnelShade);
        return float4((presentation * tint + burnLight + lensLight) * visibility, 1.0f);
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
