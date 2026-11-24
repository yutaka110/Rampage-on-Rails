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

float3 RailBounceIrradiance(float3 worldPosition, float3 normal) {
    [branch] if (railBounceControl.x == 0.0f) return 0.0f;
    float3 irradiance = 0.0f;
    float coverage = 0.0f;
    [loop] for (uint i = 0; i < (uint)railBounceControl.x; ++i) {
        RailBouncePatch patch = railBouncePatches[i];
        float3 delta = patch.positionRadius.xyz - worldPosition;
        float distanceSquared = dot(delta, delta);
        float radiusSquared = max(1.0f, patch.positionRadius.w * patch.positionRadius.w);
        if (distanceSquared >= radiusSquared) continue;
        float edge = saturate(1.0f - distanceSquared / radiusSquared);
        float3 toPatch = delta * rsqrt(max(distanceSquared, 0.01f));
        // Crystal patches are point emitters and carry the crystal root in normal.xyz.
        float emitter = patch.normal.w > .5f ? rcp(1.0f + distanceSquared * .0075f) :
            saturate(dot(patch.normal.xyz, -toPatch));
        float receiver = saturate(dot(normal, toPatch));
        float weight = edge * edge * emitter;
        irradiance += patch.radiance.rgb * weight * receiver;
        coverage += weight;
    }
    return irradiance * (railBounceControl.y / max(1.0f, coverage));
}

float CrystalSurgeAt(float3 worldPosition) {
    float flash = 0.0f;
    [loop] for (uint i = 0; i < (uint)railBounceControl.x; ++i) {
        RailBouncePatch patch = railBouncePatches[i];
        if (patch.normal.w < .5f || patch.radiance.w < 1.0f) continue;
        float age = patch.radiance.w - 1.0f;
        float reach = patch.positionRadius.w * .31f;
        float cover = 1.0f - smoothstep(reach * .43f, reach, length(worldPosition.xz - patch.normal.xz));
        float pulse = smoothstep(0.0f, .055f, age) * (1.0f - smoothstep(.10f, .43f, age));
        flash = max(flash, pulse * cover);
    }
    return flash;
}
#endif
