#ifndef GE3_CAVE_LIGHTING
#define GE3_CAVE_LIGHTING

// Camera constants are declared by TitleLighting.hlsli before this include.
float GameplayCaveWeightAtDistance(float along) {
    if (caveRange.w < 0.5f) return 0.0f;
    float t = saturate((along - caveRange.x) / max(caveRange.y, 1.0f));
    float outdoor = t * t * (3.0f - 2.0f * t) * saturate(caveRange.z);
    return 1.0f - smoothstep(0.10f, 0.75f, outdoor);
}

float GameplayCaveWeight(float3 worldPosition) {
    return GameplayCaveWeightAtDistance(caveRailOrigin.w +
        dot(worldPosition - caveRailOrigin.xyz, caveRailForward.xyz));
}

float3 CaveOpeningIrradiance(float3 worldPosition, float3 normal, out float proximity) {
    float amount = 0.0f;
    proximity = 0.0f;
    [unroll] for (uint i = 0; i < 4; ++i) {
        float3 delta = caveOpenings[i].xyz - worldPosition;
        float radius = caveOpenings[i].w;
        float distanceSquared = dot(delta, delta);
        float falloff = radius > 0.0f ? exp2(-2.0f * distanceSquared / max(radius * radius, 1.0f)) : 0.0f;
        float facing = saturate(dot(normal, delta * rsqrt(max(distanceSquared, 0.01f))));
        float diffuseFloor = radius > 64.0f ? 0.35f : 0.22f;
        amount += falloff * lerp(diffuseFloor, 1.0f, facing);
        proximity = max(proximity, falloff);
    }
    // Warm, diffuse bounce from openings, with a finite energy budget.
    return float3(0.90f, 0.70f, 0.45f) * min(amount, 1.15f);
}
#endif
