#include "Object3d.hlsli"

struct Material
{
    float32_t4 color;
    int32_t enableLighting;
    float detailNormalStrength;
    float cavityAoStrength;
    float skyFillStrength;
    float32_t4x4 uvTransform;
    float shininess;
    float environmentCoefficient;
    int32_t specularMode;
    float rimLightStrength;
    float microDetailStrength;
    float useDetailCache;
    float detailCacheScale;
    float detailTileWorldSize;
    float detailNearScale;
    float detailFarScale;
    float detailDistanceBlend;
    float useDetailNormalMap;
    float detailNormalMapStrength;
    float detailHybridBlend;
    float invertDetailNormalY;
    float terrainDebugViewMode;
    float strataBreakupStrength;
    float floorSandShadowStrength;
    float backlightRimBoost;
};

struct DirectionalLight
{
    float4 color;
    float3 direction;
    float intensity;
};

struct PointLight
{
    float4 color;
    float3 position;
    float intensity;
    float radius;
    float decay;
    float2 pad_;
};

struct SpotLight
{
    float4 color;
    float3 position;
    float intensity;
    float3 direction;
    float distance;
    float decay;
    float cosAngle;
    float pad_;
};

struct CascadeShadowData
{
    float4x4 lightViewProjection[4];
    float4 cascadeSplits;
    float4 parameters;
};

struct TerrainPbrLayerConstants
{
    float4 baseColorTintAndNormalStrength;
    float4 surfaceParameters;
    float4 scaleParameters;
};

struct TerrainPbrLibraryConstants
{
    TerrainPbrLayerConstants layers[4]; // 0..2 gameplay; 3 title Ground054.
    float4 blendParameters;
};

ConstantBuffer<Material> gMaterial : register(b0);
ConstantBuffer<DirectionalLight> gDirectionalLight : register(b1);
ConstantBuffer<PointLight> gPointLight : register(b3);
ConstantBuffer<SpotLight> gSpotLight : register(b4);
ConstantBuffer<CascadeShadowData> gCascadeShadow : register(b5);
ConstantBuffer<TerrainPbrLibraryConstants> gTerrainPbr : register(b8);
#include "TitleLighting.hlsli"

Texture2DArray<float4> gTerrainBaseColorArray : register(t0);
TextureCube<float4> gEnvironmentTexture : register(t1);
Texture2DArray<float4> gTerrainDetailNormalMap : register(t2);
Texture2DArray<float4> gTerrainDetailCache : register(t4);
Texture2DArray<float4> gTerrainPbrNormalArray : register(t5);
Texture2DArray<float4> gTerrainPbrOrmArray : register(t6);
Texture2DArray<float4> gTerrainPbrHeightArray : register(t7);
Texture2D<float> gCascadeShadow0 : register(t11);
Texture2D<float> gCascadeShadow1 : register(t12);
Texture2D<float> gCascadeShadow2 : register(t13);
Texture2D<float> gCascadeShadow3 : register(t14);
SamplerState gSampler : register(s0);

struct PixelShaderOutput
{
    float4 color : SV_TARGET0;
};

static float3 SafeNormalize(float3 v)
{
    float len2 = dot(v, v);
    if (len2 < 1e-8f)
    {
        return float3(0.0f, 1.0f, 0.0f);
    }
    return v * rsqrt(len2);
}

static float Pow2(float v)
{
    return v * v;
}

static float Pow4(float v)
{
    float v2 = v * v;
    return v2 * v2;
}

static float Pow8(float v)
{
    float v4 = Pow4(v);
    return v4 * v4;
}

static float Pow16(float v)
{
    float v8 = Pow8(v);
    return v8 * v8;
}

static float Pow32(float v)
{
    float v16 = Pow16(v);
    return v16 * v16;
}

static float Pow64(float v)
{
    float v32 = Pow32(v);
    return v32 * v32;
}

static float Pow128(float v)
{
    float v64 = Pow64(v);
    return v64 * v64;
}

static const float kPi = 3.14159265359f;

static float DistributionGgx(float nDotH, float roughness)
{
    float alpha = max(roughness * roughness, 0.0025f);
    float alpha2 = alpha * alpha;
    float denominator = nDotH * nDotH * (alpha2 - 1.0f) + 1.0f;
    return alpha2 / max(kPi * denominator * denominator, 1.0e-5f);
}

static float GeometrySchlickGgx(float nDotDirection, float roughness)
{
    float r = roughness + 1.0f;
    float k = (r * r) * 0.125f;
    return nDotDirection /
        max(nDotDirection * (1.0f - k) + k, 1.0e-5f);
}

static float GeometrySmith(
    float nDotV,
    float nDotL,
    float roughness)
{
    return
        GeometrySchlickGgx(nDotV, roughness) *
        GeometrySchlickGgx(nDotL, roughness);
}

static float3 FresnelSchlick(float cosTheta, float3 f0)
{
    float oneMinusCos = 1.0f - saturate(cosTheta);
    return f0 + (1.0f - f0) * Pow4(oneMinusCos) * oneMinusCos;
}

static float3 FresnelSchlickRoughness(
    float cosTheta,
    float3 f0,
    float roughness)
{
    float oneMinusCos = 1.0f - saturate(cosTheta);
    float3 grazing = max(1.0f - roughness, f0);
    return f0 + (grazing - f0) * Pow4(oneMinusCos) * oneMinusCos;
}

// Epic's analytic split-sum approximation. It removes the need for a BRDF
// lookup texture while retaining the GGX roughness/view-angle response.
static float2 EnvironmentBrdfApprox(float roughness, float nDotV)
{
    const float4 c0 = float4(-1.0f, -0.0275f, -0.572f, 0.022f);
    const float4 c1 = float4(1.0f, 0.0425f, 1.04f, -0.04f);
    float4 r = roughness * c0 + c1;
    float a004 = min(r.x * r.x, exp2(-9.28f * nDotV)) * r.x + r.y;
    return float2(-1.04f, 1.04f) * a004 + r.zw;
}

static float3 EvaluateEnvironmentIbl(
    float3 normal,
    float3 viewDir,
    float3 baseColor,
    float metallic,
    float roughness,
    float ao,
    float skyFillStrength)
{
    uint environmentWidth = 1u;
    uint environmentHeight = 1u;
    uint environmentMipCount = 1u;
    gEnvironmentTexture.GetDimensions(
        0u,
        environmentWidth,
        environmentHeight,
        environmentMipCount);
    float maxEnvironmentLod = max((float)environmentMipCount - 1.0f, 0.0f);

    float nDotV = saturate(dot(normal, viewDir));
    float3 f0 = lerp(float3(0.04f, 0.04f, 0.04f), baseColor, metallic);
    float3 fresnel =
        FresnelSchlickRoughness(nDotV, f0, roughness);
    float3 diffuseWeight = (1.0f - fresnel) * (1.0f - metallic);

    // The smallest cubemap mips act as the low-frequency irradiance source.
    float diffuseLod = maxEnvironmentLod;
    float3 irradiance =
        gEnvironmentTexture.SampleLevel(gSampler, normal, diffuseLod).rgb;
    float3 diffuseIbl = irradiance * baseColor * diffuseWeight;

    float3 reflected = reflect(-viewDir, normal);
    float specularLod = roughness * maxEnvironmentLod;
    float3 prefilteredEnvironment =
        gEnvironmentTexture.SampleLevel(gSampler, reflected, specularLod).rgb;
    float2 environmentBrdf = EnvironmentBrdfApprox(roughness, nDotV);
    float3 specularIbl =
        prefilteredEnvironment *
        (f0 * environmentBrdf.x + environmentBrdf.y);

    float multiBounceAo = ao + (1.0f - ao) * saturate(baseColor * 0.35f);
    float intensity = lerp(0.32f, 1.0f, saturate(skyFillStrength));
    return (diffuseIbl * multiBounceAo + specularIbl * ao) * intensity;
}

static float3 TriplanarBlendWeights(float3 normal)
{
    float3 n = abs(normal);
    float3 n2 = n * n;
    float3 n4 = n2 * n2;
    return n4 / max(n4.x + n4.y + n4.z, 0.0001f);
}

static float Hash31(float3 p)
{
    p = frac(p * 0.1031f);
    p += dot(p, p.yzx + 33.33f);
    return frac((p.x + p.y) * p.z);
}

static float Hash21(float2 p)
{
    float3 p3 = frac(float3(p.x, p.y, p.x) * 0.1031f);
    p3 += dot(p3, p3.yzx + 33.33f);
    return frac((p3.x + p3.y) * p3.z);
}

static float ValueNoise(float3 p)
{
    float3 i = floor(p);
    float3 f = frac(p);
    f = f * f * (3.0f - 2.0f * f);

    float n000 = Hash31(i + float3(0.0f, 0.0f, 0.0f));
    float n100 = Hash31(i + float3(1.0f, 0.0f, 0.0f));
    float n010 = Hash31(i + float3(0.0f, 1.0f, 0.0f));
    float n110 = Hash31(i + float3(1.0f, 1.0f, 0.0f));
    float n001 = Hash31(i + float3(0.0f, 0.0f, 1.0f));
    float n101 = Hash31(i + float3(1.0f, 0.0f, 1.0f));
    float n011 = Hash31(i + float3(0.0f, 1.0f, 1.0f));
    float n111 = Hash31(i + float3(1.0f, 1.0f, 1.0f));

    float nx00 = lerp(n000, n100, f.x);
    float nx10 = lerp(n010, n110, f.x);
    float nx01 = lerp(n001, n101, f.x);
    float nx11 = lerp(n011, n111, f.x);
    float nxy0 = lerp(nx00, nx10, f.y);
    float nxy1 = lerp(nx01, nx11, f.y);
    return lerp(nxy0, nxy1, f.z);
}

static float TerrainNoise(float3 worldPosition)
{
    float n0 = ValueNoise(worldPosition * 0.045f);
    float n1 = ValueNoise(worldPosition * 0.115f + 13.7f);
    float n2 = ValueNoise(worldPosition * 0.23f + 41.2f);
    return n0 * 0.55f + n1 * 0.32f + n2 * 0.13f;
}

static float StrataMask(float3 worldPosition, float3 normal, float strataBreakupStrength)
{
    float slope = 1.0f - saturate(abs(normal.y));
    float erosionWarp =
        ValueNoise(float3(worldPosition.x * 0.035f, worldPosition.y * 0.19f, worldPosition.z * 0.045f) + 91.0f) * 0.42f;
    float diagonalWarp =
        ValueNoise(float3(
            worldPosition.x * 0.026f + worldPosition.y * 0.018f,
            worldPosition.y * 0.072f,
            worldPosition.z * 0.031f - worldPosition.y * 0.013f) + 187.0f) * 0.62f;
    float warpedHeight =
        worldPosition.y * 0.21f +
        TerrainNoise(worldPosition * 0.5f) * 0.65f +
        erosionWarp +
        diagonalWarp * saturate(strataBreakupStrength);
    float band = abs(frac(warpedHeight) - 0.5f);
    float thinLine = smoothstep(0.055f, 0.0f, band);
    float broadLine = smoothstep(0.18f, 0.0f, band) * 0.35f;
    float strataLine = (thinLine * 0.72f + broadLine) * (0.25f + slope * 0.75f);

    float longGap =
        ValueNoise(float3(worldPosition.x * 0.030f, worldPosition.y * 0.28f, worldPosition.z * 0.034f) + 293.0f);
    float verticalFracture =
        ValueNoise(float3(worldPosition.x * 0.087f + worldPosition.z * 0.018f, worldPosition.y * 0.020f, worldPosition.z * 0.082f) + 347.0f);
    float diagonalCutCoord =
        worldPosition.x * 0.034f +
        worldPosition.z * 0.027f +
        worldPosition.y * 0.118f +
        TerrainNoise(worldPosition * 0.24f + 31.0f) * 0.58f;
    float diagonalCut = smoothstep(0.075f, 0.0f, abs(frac(diagonalCutCoord) - 0.5f));
    float chunkedGap =
        smoothstep(0.58f, 0.92f, longGap) * 0.70f +
        smoothstep(0.66f, 0.96f, verticalFracture) * 0.55f +
        diagonalCut * 0.46f;
    float breakup = saturate(chunkedGap * saturate(strataBreakupStrength) * (0.28f + slope * 0.92f));
    return strataLine * (1.0f - breakup);
}

static float ErosionCrackMask(float3 worldPosition, float3 normal)
{
    float wall = saturate(1.0f - abs(normal.y));
    float verticalColumn =
        ValueNoise(float3(worldPosition.x * 0.060f, worldPosition.y * 0.018f, worldPosition.z * 0.070f) + 151.0f);
    float fineSplit =
        ValueNoise(float3(worldPosition.x * 0.180f, worldPosition.y * 0.052f, worldPosition.z * 0.210f) + 277.0f);
    float ledgeLayer = abs(frac(worldPosition.y * 0.145f + TerrainNoise(worldPosition * 0.42f) * 0.34f) - 0.5f);
    float verticalCrack = smoothstep(0.76f, 0.96f, verticalColumn) * smoothstep(0.42f, 0.90f, fineSplit);
    float brokenLedge = smoothstep(0.115f, 0.0f, ledgeLayer) * smoothstep(0.42f, 0.90f, wall);
    return saturate(verticalCrack * wall * 0.72f + brokenLedge * 0.46f);
}

static float MicroGrain(float3 worldPosition)
{
    float g0 = ValueNoise(worldPosition * 1.65f + 311.0f);
    float g1 = ValueNoise(worldPosition * 3.25f + 719.0f);
    float g2 = ValueNoise(worldPosition * 6.40f + 1103.0f);
    float g3 = ValueNoise(worldPosition * 11.50f + 1901.0f);
    return saturate(g0 * 0.34f + g1 * 0.30f + g2 * 0.22f + g3 * 0.14f);
}

static float MicroVerticalCracks(float3 worldPosition, float3 normal)
{
    float wall = saturate(1.0f - abs(normal.y));
    float column =
        ValueNoise(float3(worldPosition.x * 0.34f, worldPosition.y * 0.055f, worldPosition.z * 0.36f) + 541.0f);
    float split =
        ValueNoise(float3(worldPosition.x * 0.82f, worldPosition.y * 0.11f, worldPosition.z * 0.78f) + 887.0f);
    float hairline =
        ValueNoise(float3(worldPosition.x * 1.74f, worldPosition.y * 0.20f, worldPosition.z * 1.58f) + 1297.0f);
    float vein = smoothstep(0.77f, 0.97f, column) * smoothstep(0.34f, 0.86f, split);
    float fineVein = smoothstep(0.82f, 0.985f, split) * smoothstep(0.58f, 0.94f, hairline);
    float broken = smoothstep(0.70f, 0.96f, 1.0f - abs(frac(worldPosition.y * 0.38f + split * 0.42f) - 0.5f) * 2.0f);
    return saturate((vein * 0.76f + fineVein * 0.42f + vein * broken * 0.42f) * wall);
}

static float ChippedStrataEdge(float3 worldPosition, float3 normal)
{
    float wall = saturate(1.0f - abs(normal.y));
    float layer = abs(frac(worldPosition.y * 0.31f + TerrainNoise(worldPosition * 0.78f) * 0.52f) - 0.5f);
    float edge = smoothstep(0.094f, 0.0f, layer);
    float sharpEdge = smoothstep(0.030f, 0.0f, layer);
    float breakup = ValueNoise(worldPosition * 1.18f + 421.0f);
    float fineBreakup = ValueNoise(worldPosition * 4.3f + 1721.0f);
    float fractureGate = smoothstep(0.31f, 0.90f, breakup * 0.64f + fineBreakup * 0.36f);
    return saturate((edge * 0.78f + sharpEdge * 0.42f) * wall * fractureGate);
}

static float DetailHeight(float3 worldPosition, float microDetailStrength)
{
    float broad = TerrainNoise(worldPosition);
    float mid = ValueNoise(worldPosition * 0.36f + 19.0f);
    float fine = ValueNoise(worldPosition * 0.92f + 73.0f);
    float extraFine = ValueNoise(worldPosition * 2.45f + 509.0f);
    float layer = abs(frac(worldPosition.y * 0.19f + broad * 0.42f) - 0.5f);
    float ledge = smoothstep(0.18f, 0.0f, layer);
    float erosion = ErosionCrackMask(worldPosition, float3(0.0f, 0.0f, 1.0f));
    float chip = ChippedStrataEdge(worldPosition, float3(0.0f, 0.0f, 1.0f));
    float micro =
        (MicroGrain(worldPosition) - 0.5f) * 0.42f +
        (extraFine - 0.5f) * 0.16f +
        MicroVerticalCracks(worldPosition, float3(0.0f, 0.0f, 1.0f)) * 0.46f +
        chip * 0.24f;
    return broad * 0.56f + mid * 0.23f + fine * 0.11f + ledge * 0.22f + erosion * 0.36f + micro * saturate(microDetailStrength);
}

static float3 PerturbRockNormal(float3 worldPosition, float3 normal, float strength, float microDetailStrength)
{
    float wall = saturate(1.0f - abs(normal.y));
    strength = saturate(strength * lerp(0.62f, 0.95f, wall));
    if (strength <= 0.001f)
    {
        return normal;
    }

    float3 reference = abs(normal.y) < 0.82f ? float3(0.0f, 1.0f, 0.0f) : float3(1.0f, 0.0f, 0.0f);
    float3 tangent = SafeNormalize(cross(reference, normal));
    float3 bitangent = SafeNormalize(cross(normal, tangent));
    float sampleStep = 1.35f;
    float dhT =
        DetailHeight(worldPosition + tangent * sampleStep, microDetailStrength) -
        DetailHeight(worldPosition - tangent * sampleStep, microDetailStrength);
    float dhB =
        DetailHeight(worldPosition + bitangent * sampleStep, microDetailStrength) -
        DetailHeight(worldPosition - bitangent * sampleStep, microDetailStrength);

    float microStep = lerp(0.72f, 0.38f, saturate(microDetailStrength));
    float mdhT =
        MicroGrain(worldPosition + tangent * microStep) -
        MicroGrain(worldPosition - tangent * microStep);
    float mdhB =
        MicroGrain(worldPosition + bitangent * microStep) -
        MicroGrain(worldPosition - bitangent * microStep);
    float microNormalStrength = saturate(microDetailStrength * lerp(0.30f, 0.58f, wall));

    return SafeNormalize(
        normal -
        tangent * (dhT * strength + mdhT * microNormalStrength) -
        bitangent * (dhB * strength + mdhB * microNormalStrength));
}

static float RockCavity(float3 worldPosition, float3 normal, float strata)
{
    float fineLow = 1.0f - TerrainNoise(worldPosition * 2.7f);
    float cracks = smoothstep(0.58f, 0.94f, fineLow);
    float verticalCrease = smoothstep(0.38f, 0.95f, 1.0f - abs(normal.y));
    float erosion = ErosionCrackMask(worldPosition, normal);
    float grainPit = smoothstep(0.66f, 0.96f, 1.0f - MicroGrain(worldPosition * 1.25f + 211.0f));
    return saturate(strata * 0.72f + cracks * 0.30f + verticalCrease * cracks * 0.26f + erosion * 0.56f + grainPit * verticalCrease * 0.18f);
}

static float WallDetailMask(float3 normal)
{
    return smoothstep(0.16f, 0.82f, 1.0f - abs(normal.y));
}

static float HighFrequencyRockDetail(float3 worldPosition, float3 normal, float strata, float microDetailStrength)
{
    float wall = WallDetailMask(normal);
    float grain = smoothstep(0.50f, 0.93f, MicroGrain(worldPosition * 1.35f + 733.0f));
    float cracks = MicroVerticalCracks(worldPosition * 1.08f + 41.0f, normal);
    float chips = ChippedStrataEdge(worldPosition * 1.06f + 17.0f, normal);
    float freckle = smoothstep(0.76f, 0.99f, ValueNoise(worldPosition * 7.8f + 2411.0f));
    float detail = chips * 0.58f + cracks * 0.48f + grain * 0.22f + freckle * 0.16f + strata * 0.18f;
    return saturate(detail * wall * saturate(microDetailStrength));
}

static float FloorSandShadow(float3 worldPosition, float3 normal)
{
    float floorMask = smoothstep(0.42f, 0.86f, normal.y);
    float2 p = worldPosition.xz;
    float broad =
        ValueNoise(float3(p.x * 0.020f + p.y * 0.010f, 0.0f, p.y * 0.026f) + 2501.0f);
    float streakCoord =
        p.x * 0.030f +
        p.y * 0.010f +
        ValueNoise(float3(p.x * 0.018f, 0.0f, p.y * 0.022f) + 2609.0f) * 1.15f;
    float streak = smoothstep(0.30f, 0.82f, 1.0f - abs(frac(streakCoord) - 0.5f) * 2.0f);
    float dune =
        ValueNoise(float3(p.x * 0.090f, 0.0f, p.y * 0.038f) + 2707.0f) * 0.55f +
        ValueNoise(float3(p.x * 0.210f, 0.0f, p.y * 0.075f) + 2801.0f) * 0.45f;
    float broken = smoothstep(0.34f, 0.92f, broad * 0.42f + dune * 0.58f);
    return saturate(floorMask * (streak * 0.48f + broken * 0.52f));
}

static float2 DecodeTerrainSurfaceAttributes(inout float2 uv)
{
    float rawY = uv.y;
    if (rawY < 16.0f)
    {
        return float2(0.0f, 0.5f);
    }

    float packed = floor(rawY) - 16.0f;
    uv.y = frac(rawY);
    float contactBucket = fmod(packed, 16.0f);
    float variationBucket = floor(packed / 16.0f);
    return float2(saturate(contactBucket / 15.0f), saturate(variationBucket / 15.0f));
}

static float Smooth01(float v)
{
    return v * v * (3.0f - 2.0f * v);
}

static float DetailNearWeight(float3 worldPosition, float distanceBlend)
{
    float cameraDistance = distance(cameraWorldPosition, worldPosition);
    float farDistance = max(distanceBlend, 1.0f);
    float nearDistance = farDistance * 0.32f;
    return 1.0f - smoothstep(nearDistance, farDistance, cameraDistance);
}

static float4 SampleTerrainDetailCacheLayer(float2 uv, float2 tile)
{
    float layerHash = Hash21(tile + 17.0f);
    float layer = floor(layerHash * 4.0f);
    float2 offset = float2(Hash21(tile + 31.0f), Hash21(tile + 73.0f));
    float2 stretch = lerp(float2(0.92f, 1.12f), float2(1.17f, 0.88f), Hash21(tile + 109.0f));
    float2 seededUv = uv * stretch + offset * 0.87f + layer * 0.071f;
    return gTerrainDetailCache.Sample(gSampler, float3(seededUv, layer));
}

static float3 DecodeDetailNormalMap(float4 sampleValue)
{
    float3 n = float3(sampleValue.rg * 2.0f - 1.0f, sampleValue.b * 2.0f - 1.0f);
    n.z = max(n.z, 0.08f);
    return SafeNormalize(n);
}

struct TerrainPbrSurface
{
    float4 baseColor;
    float3 mappedNormal;
    float ao;
    float roughness;
    float metallic;
    float normalStrength;
    float detailNormalStrength;
    float wetness;
};

static float4 SampleTerrainPbrBaseColor(
    float3 worldPosition,
    float3 normal,
    uint layerIndex)
{
    float scale = 1.0f / max(gTerrainPbr.layers[layerIndex].scaleParameters.x, 0.25f);
    float3 blend = TriplanarBlendWeights(normal);
    float layer = (float)layerIndex;
    float4 sx = gTerrainBaseColorArray.Sample(gSampler, float3(worldPosition.zy * scale, layer));
    float4 sy = gTerrainBaseColorArray.Sample(gSampler, float3(worldPosition.xz * scale, layer));
    float4 sz = gTerrainBaseColorArray.Sample(gSampler, float3(worldPosition.xy * scale, layer));
    return sx * blend.x + sy * blend.y + sz * blend.z;
}

static float3 SampleTerrainPbrMappedNormal(
    float3 worldPosition,
    float3 normal,
    uint layerIndex)
{
    float scale = 1.0f / max(gTerrainPbr.layers[layerIndex].scaleParameters.x, 0.25f);
    float3 blend = TriplanarBlendWeights(normal);
    float layer = (float)layerIndex;
    float3 sx = DecodeDetailNormalMap(
        gTerrainPbrNormalArray.Sample(gSampler, float3(worldPosition.zy * scale, layer)));
    float3 sy = DecodeDetailNormalMap(
        gTerrainPbrNormalArray.Sample(gSampler, float3(worldPosition.xz * scale, layer)));
    float3 sz = DecodeDetailNormalMap(
        gTerrainPbrNormalArray.Sample(gSampler, float3(worldPosition.xy * scale, layer)));
    return SafeNormalize(sx * blend.x + sy * blend.y + sz * blend.z);
}

static float3 SampleTerrainPbrOrm(
    float3 worldPosition,
    float3 normal,
    uint layerIndex)
{
    float scale = 1.0f / max(gTerrainPbr.layers[layerIndex].scaleParameters.x, 0.25f);
    float3 blend = TriplanarBlendWeights(normal);
    float layer = (float)layerIndex;
    float3 sx = gTerrainPbrOrmArray.Sample(gSampler, float3(worldPosition.zy * scale, layer)).rgb;
    float3 sy = gTerrainPbrOrmArray.Sample(gSampler, float3(worldPosition.xz * scale, layer)).rgb;
    float3 sz = gTerrainPbrOrmArray.Sample(gSampler, float3(worldPosition.xy * scale, layer)).rgb;
    return sx * blend.x + sy * blend.y + sz * blend.z;
}

static float SampleTerrainPbrHeight(
    float3 worldPosition,
    float3 normal,
    uint layerIndex)
{
    float scale = 1.0f / max(gTerrainPbr.layers[layerIndex].scaleParameters.x, 0.25f);
    float3 blend = TriplanarBlendWeights(normal);
    float layer = (float)layerIndex;
    float sx = gTerrainPbrHeightArray.Sample(gSampler, float3(worldPosition.zy * scale, layer)).r;
    float sy = gTerrainPbrHeightArray.Sample(gSampler, float3(worldPosition.xz * scale, layer)).r;
    float sz = gTerrainPbrHeightArray.Sample(gSampler, float3(worldPosition.xy * scale, layer)).r;
    return dot(float3(sx, sy, sz), blend);
}

static TerrainPbrSurface SampleTerrainPbrSurface(
    float3 worldPosition,
    float3 normal)
{
    TerrainPbrSurface surface;
    float floorWeight = smoothstep(
        gTerrainPbr.blendParameters.x,
        gTerrainPbr.blendParameters.y,
        normal.y);
    float wetNoise = 1.0f - TerrainNoise(worldPosition * 0.31f + 43.0f);
    float wetWeight = (1.0f - floorWeight) * smoothstep(0.46f, 0.82f, wetNoise) * 0.78f;
    float4 weights = float4(
        max(1.0f - floorWeight - wetWeight, 0.001f),
        max(wetWeight, 0.001f),
        max(floorWeight, 0.001f), 0.0f);
    if (titleSubject.w > 0.5f && gMaterial.specularMode == 9)
        weights = float4(0.0f,0.0f,0.0f,1.0f);

    float4 heights;
    [unroll]
    for (uint layerIndex = 0u; layerIndex < 4u; ++layerIndex)
    {
        if (weights[layerIndex] <= 0.0f) continue;
        heights[layerIndex] = SampleTerrainPbrHeight(worldPosition, normal, layerIndex);
        float heightScale = gTerrainPbr.layers[layerIndex].surfaceParameters.w;
        float heightBias =
            (heights[layerIndex] - 0.5f) *
            gTerrainPbr.blendParameters.z *
            heightScale *
            24.0f;
        weights[layerIndex] *= exp2(heightBias);
    }
    weights /= max(weights.x + weights.y + weights.z + weights.w, 0.0001f);

    surface.baseColor = 0.0f;
    surface.mappedNormal = 0.0f;
    surface.ao = 0.0f;
    surface.roughness = 0.0f;
    surface.metallic = 0.0f;
    surface.normalStrength = 0.0f;
    surface.detailNormalStrength = 0.0f;
    surface.wetness = 0.0f;
    [unroll]
    for (uint materialIndex = 0u; materialIndex < 4u; ++materialIndex)
    {
        float weight = weights[materialIndex];
        if (weight <= 0.0f) continue;
        TerrainPbrLayerConstants material = gTerrainPbr.layers[materialIndex];
        float4 baseColor =
            SampleTerrainPbrBaseColor(worldPosition, normal, materialIndex);
        baseColor.rgb *= material.baseColorTintAndNormalStrength.rgb;
        float macroNoise =
            TerrainNoise(worldPosition * 0.035f + (float)materialIndex * 29.0f);
        baseColor.rgb *= lerp(
            1.0f,
            0.82f + macroNoise * 0.36f,
            saturate(material.scaleParameters.z));
        float3 orm = SampleTerrainPbrOrm(worldPosition, normal, materialIndex);
        float layerAo = lerp(
            1.0f,
            orm.r,
            saturate(material.surfaceParameters.z));
        float layerRoughness = saturate(
            orm.g * material.surfaceParameters.x +
            material.surfaceParameters.y);

        surface.baseColor += baseColor * weight;
        surface.mappedNormal +=
            SampleTerrainPbrMappedNormal(worldPosition, normal, materialIndex) * weight;
        surface.ao += layerAo * weight;
        surface.roughness += layerRoughness * weight;
        surface.metallic += orm.b * weight;
        surface.normalStrength += material.baseColorTintAndNormalStrength.w * weight;
        surface.detailNormalStrength += material.scaleParameters.y * weight;
        surface.wetness += material.scaleParameters.w * weight;
    }
    surface.mappedNormal = SafeNormalize(surface.mappedNormal);
    return surface;
}

static float3 SampleTerrainDetailNormalMapLayer(float2 uv, float2 tile)
{
    float layerHash = Hash21(tile + 17.0f);
    float layer = floor(layerHash * 4.0f);
    float2 offset = float2(Hash21(tile + 31.0f), Hash21(tile + 73.0f));
    float2 stretch = lerp(float2(0.92f, 1.12f), float2(1.17f, 0.88f), Hash21(tile + 109.0f));
    float2 seededUv = uv * stretch + offset * 0.87f + layer * 0.071f;
    float3 n = DecodeDetailNormalMap(gTerrainDetailNormalMap.Sample(gSampler, float3(seededUv, layer)));
    n.y *= lerp(1.0f, -1.0f, saturate(gMaterial.invertDetailNormalY));
    return n;
}

static float4 SampleTerrainDetailCacheTiled(float2 uv, float2 worldTileCoord)
{
    float2 baseTile = floor(worldTileCoord);
    float2 f = frac(worldTileCoord);
    f = float2(Smooth01(f.x), Smooth01(f.y));

    float4 c00 = SampleTerrainDetailCacheLayer(uv, baseTile);
    float4 c10 = SampleTerrainDetailCacheLayer(uv, baseTile + float2(1.0f, 0.0f));
    float4 c01 = SampleTerrainDetailCacheLayer(uv, baseTile + float2(0.0f, 1.0f));
    float4 c11 = SampleTerrainDetailCacheLayer(uv, baseTile + float2(1.0f, 1.0f));
    return lerp(lerp(c00, c10, f.x), lerp(c01, c11, f.x), f.y);
}

static float3 SampleTerrainDetailNormalMapTiled(float2 uv, float2 worldTileCoord)
{
    float2 baseTile = floor(worldTileCoord);
    float2 f = frac(worldTileCoord);
    f = float2(Smooth01(f.x), Smooth01(f.y));

    float3 c00 = SampleTerrainDetailNormalMapLayer(uv, baseTile);
    float3 c10 = SampleTerrainDetailNormalMapLayer(uv, baseTile + float2(1.0f, 0.0f));
    float3 c01 = SampleTerrainDetailNormalMapLayer(uv, baseTile + float2(0.0f, 1.0f));
    float3 c11 = SampleTerrainDetailNormalMapLayer(uv, baseTile + float2(1.0f, 1.0f));
    return SafeNormalize(lerp(lerp(c00, c10, f.x), lerp(c01, c11, f.x), f.y));
}

static float4 SampleTerrainDetailCache(float3 worldPosition, float3 normal, float detailCacheScale, float detailTileWorldSize)
{
    float scale = max(detailCacheScale, 0.01f) * 0.055f;
    float tileWorldSize = max(detailTileWorldSize, 1.0f);
    float2 worldTileCoord = worldPosition.xz / tileWorldSize;
    float3 blend = TriplanarBlendWeights(normal);
    float2 uvX = worldPosition.zy * scale;
    float2 uvY = worldPosition.xz * scale;
    float2 uvZ = worldPosition.xy * scale;
    float4 sx = SampleTerrainDetailCacheTiled(uvX, worldTileCoord + float2(11.0f, 0.0f));
    float4 sy = SampleTerrainDetailCacheTiled(uvY, worldTileCoord);
    float4 sz = SampleTerrainDetailCacheTiled(uvZ, worldTileCoord + float2(0.0f, 11.0f));
    return sx * blend.x + sy * blend.y + sz * blend.z;
}

static float3 SampleTerrainDetailNormalMap(float3 worldPosition, float3 normal, float detailMapScale, float detailTileWorldSize)
{
    float scale = max(detailMapScale, 0.01f) * 0.055f;
    float tileWorldSize = max(detailTileWorldSize, 1.0f);
    float2 worldTileCoord = worldPosition.xz / tileWorldSize;
    float3 blend = TriplanarBlendWeights(normal);
    float2 uvX = worldPosition.zy * scale;
    float2 uvY = worldPosition.xz * scale;
    float2 uvZ = worldPosition.xy * scale;
    float3 sx = SampleTerrainDetailNormalMapTiled(uvX, worldTileCoord + float2(11.0f, 0.0f));
    float3 sy = SampleTerrainDetailNormalMapTiled(uvY, worldTileCoord);
    float3 sz = SampleTerrainDetailNormalMapTiled(uvZ, worldTileCoord + float2(0.0f, 11.0f));
    return SafeNormalize(sx * blend.x + sy * blend.y + sz * blend.z);
}

static float CachedDetailHeight(float3 worldPosition, float3 normal, float detailCacheScale, float detailTileWorldSize)
{
    float4 cache = SampleTerrainDetailCache(worldPosition, normal, detailCacheScale, detailTileWorldSize);
    return (cache.r - 0.5f) * 0.52f + cache.g * 0.36f + cache.b * 0.24f + cache.a * 0.28f;
}

static float3 PerturbCachedDetailNormal(
    float3 worldPosition,
    float3 normal,
    float microDetailStrength,
    float detailCacheScale,
    float detailTileWorldSize,
    float detailNearScale,
    float detailFarScale,
    float detailDistanceBlend,
    float useDetailCache)
{
    float strength = saturate(microDetailStrength * useDetailCache * 0.55f);
    if (strength <= 0.001f)
    {
        return normal;
    }

    float3 reference = abs(normal.y) < 0.82f ? float3(0.0f, 1.0f, 0.0f) : float3(1.0f, 0.0f, 0.0f);
    float3 tangent = SafeNormalize(cross(reference, normal));
    float3 bitangent = SafeNormalize(cross(normal, tangent));
    float nearWeight = DetailNearWeight(worldPosition, detailDistanceBlend);
    float nearScale = max(detailNearScale, 0.01f);
    float farScale = max(detailFarScale, 0.01f);
    float nearStep = 0.42f;
    float farStep = 1.28f;
    float sampleScale = detailCacheScale * lerp(farScale, nearScale, nearWeight);
    float sampleStep = lerp(farStep, nearStep, nearWeight);
    float dhT =
        CachedDetailHeight(worldPosition + tangent * sampleStep, normal, sampleScale, detailTileWorldSize) -
        CachedDetailHeight(worldPosition - tangent * sampleStep, normal, sampleScale, detailTileWorldSize);
    float dhB =
        CachedDetailHeight(worldPosition + bitangent * sampleStep, normal, sampleScale, detailTileWorldSize) -
        CachedDetailHeight(worldPosition - bitangent * sampleStep, normal, sampleScale, detailTileWorldSize);
    float distanceStrength = lerp(0.58f, 1.0f, nearWeight);
    return SafeNormalize(normal - tangent * dhT * strength * distanceStrength - bitangent * dhB * strength * distanceStrength);
}

static float3 PerturbMappedDetailNormal(
    float3 worldPosition,
    float3 normal,
    float microDetailStrength,
    float detailCacheScale,
    float detailTileWorldSize,
    float detailNearScale,
    float detailFarScale,
    float detailDistanceBlend,
    float normalMapStrength,
    float useDetailNormalMap)
{
    float strength = saturate(microDetailStrength * normalMapStrength * useDetailNormalMap);
    if (strength <= 0.001f)
    {
        return normal;
    }

    float3 reference = abs(normal.y) < 0.82f ? float3(0.0f, 1.0f, 0.0f) : float3(1.0f, 0.0f, 0.0f);
    float3 tangent = SafeNormalize(cross(reference, normal));
    float3 bitangent = SafeNormalize(cross(normal, tangent));
    float nearWeight = DetailNearWeight(worldPosition, detailDistanceBlend);
    float sampleScale = detailCacheScale * lerp(max(detailFarScale, 0.01f), max(detailNearScale, 0.01f), nearWeight);
    float3 mapped = SampleTerrainDetailNormalMap(worldPosition, normal, sampleScale, detailTileWorldSize);
    float2 detailSlope = mapped.xy;
    float distanceStrength = lerp(0.42f, 1.0f, nearWeight);
    return SafeNormalize(normal + tangent * detailSlope.x * strength * distanceStrength + bitangent * detailSlope.y * strength * distanceStrength);
}

static uint SelectCascade(float3 worldPosition)
{
    float cameraDistance = distance(cameraWorldPosition, worldPosition);
    if (cameraDistance < gCascadeShadow.cascadeSplits.x)
    {
        return 0u;
    }
    if (cameraDistance < gCascadeShadow.cascadeSplits.y)
    {
        return 1u;
    }
    if (cameraDistance < gCascadeShadow.cascadeSplits.z)
    {
        return 2u;
    }
    return 3u;
}

static float SampleCascadeDepth(uint cascadeIndex, float2 uv)
{
    if (cascadeIndex == 0u)
    {
        return gCascadeShadow0.SampleLevel(gSampler, uv, 0.0f);
    }
    if (cascadeIndex == 1u)
    {
        return gCascadeShadow1.SampleLevel(gSampler, uv, 0.0f);
    }
    if (cascadeIndex == 2u)
    {
        return gCascadeShadow2.SampleLevel(gSampler, uv, 0.0f);
    }
    return gCascadeShadow3.SampleLevel(gSampler, uv, 0.0f);
}

static float SampleCascadeShadow(float3 worldPosition, float3 normal, float3 lightDir)
{
    if (gCascadeShadow.parameters.z < 0.5f)
    {
        return 1.0f;
    }

    uint cascadeIndex = SelectCascade(worldPosition);
    float4 lightClip = mul(float4(worldPosition, 1.0f), gCascadeShadow.lightViewProjection[cascadeIndex]);
    if (abs(lightClip.w) < 1.0e-5f)
    {
        return 1.0f;
    }

    float3 ndc = lightClip.xyz / lightClip.w;
    float2 uv = ndc.xy * float2(0.5f, -0.5f) + 0.5f;
    if (uv.x <= 0.001f || uv.x >= 0.999f ||
        uv.y <= 0.001f || uv.y >= 0.999f ||
        ndc.z <= 0.0f || ndc.z >= 1.0f)
    {
        return 1.0f;
    }

    float texel = gCascadeShadow.parameters.w;
    float bias = gCascadeShadow.parameters.x;
    bias += (1.0f - saturate(dot(normal, lightDir))) * 0.0025f;

    float visibility = 0.0f;
    [unroll]
    for (int y = -1; y <= 1; ++y)
    {
        [unroll]
        for (int x = -1; x <= 1; ++x)
        {
            float2 sampleUv = clamp(uv + float2((float)x, (float)y) * texel, 0.001f, 0.999f);
            float shadowDepth = SampleCascadeDepth(cascadeIndex, sampleUv);
            visibility += (ndc.z - bias <= shadowDepth) ? 1.0f : 0.0f;
        }
    }
    visibility *= (1.0f / 9.0f);
    return lerp(1.0f, visibility, saturate(gCascadeShadow.parameters.y));
}

PixelShaderOutput main(VertexShaderOutput input)
{
    PixelShaderOutput output;

    float3 normal = SafeNormalize(input.normal);
    float2 terrainUv = input.texcoord;
    float2 surfaceAttributes = DecodeTerrainSurfaceAttributes(terrainUv);
    // Authored title OBJ UVs carry ordinary coordinates / cave depth, whereas
    // streamed gameplay terrain packs contact AO and rock variation into UVs.
    bool titleLandscape = titleSubject.w > 0.5f &&
        (gMaterial.specularMode == 6 || gMaterial.specularMode == 7 || gMaterial.specularMode == 9);
    float contactAo = titleLandscape ? 0.0f : surfaceAttributes.x;
    float rockVariation = titleLandscape ? 0.5f : surfaceAttributes.y;
    TerrainPbrSurface pbrSurface =
        SampleTerrainPbrSurface(input.worldPosition, normal);
    float4 texColor = pbrSurface.baseColor;

    float noiseAmount = saturate(gMaterial.color.a);
    float strataAmount = saturate(gMaterial.environmentCoefficient);
    float specularStrength = saturate(gMaterial.shininess);
    float rimStrength = saturate(gMaterial.rimLightStrength * 0.5f);
    float detailNormalStrength = clamp(gMaterial.detailNormalStrength, 0.0f, 2.0f);
    float microDetailStrength = clamp(gMaterial.microDetailStrength, 0.0f, 2.0f);
    float useDetailCache = saturate(gMaterial.useDetailCache);
    float detailCacheScale = max(gMaterial.detailCacheScale, 0.01f);
    float detailTileWorldSize = max(gMaterial.detailTileWorldSize, 1.0f);
    float detailNearScale = max(gMaterial.detailNearScale, 0.01f);
    float detailFarScale = max(gMaterial.detailFarScale, 0.01f);
    float detailDistanceBlend = max(gMaterial.detailDistanceBlend, 1.0f);
    float useDetailNormalMap = saturate(gMaterial.useDetailNormalMap);
    float detailNormalMapStrength = clamp(gMaterial.detailNormalMapStrength, 0.0f, 2.0f);
    float detailHybridBlend = saturate(gMaterial.detailHybridBlend);
    float cavityAoStrength = saturate(gMaterial.cavityAoStrength);
    float skyFillStrength = saturate(gMaterial.skyFillStrength);
    float floorSandShadowStrength = saturate(gMaterial.floorSandShadowStrength);
    float backlightRimBoost = saturate(gMaterial.backlightRimBoost);
    detailNormalStrength *= lerp(0.72f, 1.28f, saturate(pbrSurface.detailNormalStrength));
    specularStrength *= lerp(1.18f, 0.20f, saturate(pbrSurface.roughness));
    {
        float3 reference =
            abs(normal.y) < 0.82f
                ? float3(0.0f, 1.0f, 0.0f)
                : float3(1.0f, 0.0f, 0.0f);
        float3 tangent = SafeNormalize(cross(reference, normal));
        float3 bitangent = SafeNormalize(cross(normal, tangent));
        float2 pbrSlope = pbrSurface.mappedNormal.xy;
        normal = SafeNormalize(
            normal +
            tangent * pbrSlope.x * pbrSurface.normalStrength +
            bitangent * pbrSlope.y * pbrSurface.normalStrength);
    }
    float cameraDistance = distance(cameraWorldPosition, input.worldPosition);
    float wallMaskBeforeDetail = WallDetailMask(normal);
    float distanceAir = smoothstep(180.0f, 760.0f, cameraDistance);
    float encodedFarWall = smoothstep(0.54f, 0.86f, contactAo);
    float distantWallAtmosphere = saturate(distanceAir * encodedFarWall * wallMaskBeforeDetail);
    detailNormalStrength *= lerp(1.0f, 0.22f, distantWallAtmosphere);
    microDetailStrength *= lerp(1.0f, 0.16f, distantWallAtmosphere);
    strataAmount *= lerp(1.0f, 0.48f, distantWallAtmosphere);
    specularStrength *= lerp(1.0f, 0.24f, distantWallAtmosphere);
    rimStrength *= lerp(1.0f, 0.52f, distantWallAtmosphere);
    if (gMaterial.terrainDebugViewMode > 0.5f)
    {
        float debugNearWeight = DetailNearWeight(input.worldPosition, detailDistanceBlend);
        float3 debugNearNormal = SampleTerrainDetailNormalMap(
            input.worldPosition,
            normal,
            detailCacheScale * detailNearScale,
            detailTileWorldSize);
        float3 debugFarNormal = SampleTerrainDetailNormalMap(
            input.worldPosition,
            normal,
            detailCacheScale * detailFarScale,
            detailTileWorldSize);
        float3 debugNormal = SafeNormalize(lerp(debugFarNormal, debugNearNormal, debugNearWeight));
        output.color = float4(debugNormal * 0.5f + 0.5f, 1.0f);
        return output;
    }

    float noise = TerrainNoise(input.worldPosition);
    float strataBreakupStrength = saturate(gMaterial.strataBreakupStrength);
    float strata = StrataMask(input.worldPosition, normal, strataBreakupStrength) * strataAmount;
    float proceduralNormalStrength = detailNormalStrength *
        (1.0f - saturate(useDetailCache * 0.70f + useDetailNormalMap * detailHybridBlend * 0.52f));
    normal = PerturbRockNormal(input.worldPosition, normal, proceduralNormalStrength, microDetailStrength);
    float cacheNormalUse = useDetailCache * lerp(1.0f, 0.55f, detailHybridBlend * useDetailNormalMap);
    normal = PerturbCachedDetailNormal(
        input.worldPosition,
        normal,
        microDetailStrength,
        detailCacheScale,
        detailTileWorldSize,
        detailNearScale,
        detailFarScale,
        detailDistanceBlend,
        cacheNormalUse);
    normal = PerturbMappedDetailNormal(
        input.worldPosition,
        normal,
        microDetailStrength,
        detailCacheScale,
        detailTileWorldSize,
        detailNearScale,
        detailFarScale,
        detailDistanceBlend,
        detailNormalMapStrength * lerp(0.35f, 1.0f, detailHybridBlend),
        useDetailNormalMap);
    strata = StrataMask(input.worldPosition, normal, strataBreakupStrength) * strataAmount;
    float nearDetailWeight = DetailNearWeight(input.worldPosition, detailDistanceBlend);
    float detailSampleScale = detailCacheScale * lerp(detailFarScale, detailNearScale, nearDetailWeight);
    float4 detailCache = SampleTerrainDetailCache(input.worldPosition, normal, detailSampleScale, detailTileWorldSize);
    float erosionCracks = ErosionCrackMask(input.worldPosition, normal) * saturate(detailNormalStrength * 0.75f + cavityAoStrength * 0.35f);
    float proceduralCracks = 0.0f;
    float proceduralChipped = 0.0f;
    float proceduralGrain = 0.0f;
    float proceduralHighFrequency = 0.0f;
    float proceduralCavity = 0.0f;
    [branch]
    if (useDetailCache < 0.999f)
    {
        proceduralCracks = MicroVerticalCracks(input.worldPosition, normal);
        proceduralChipped = ChippedStrataEdge(input.worldPosition, normal);
        proceduralGrain = MicroGrain(input.worldPosition);
        proceduralCavity = RockCavity(input.worldPosition, normal, strata);
        proceduralHighFrequency = HighFrequencyRockDetail(input.worldPosition, normal, strata, microDetailStrength);
    }
    float microCracks = lerp(proceduralCracks, detailCache.g, useDetailCache) * microDetailStrength;
    float chippedEdges = lerp(proceduralChipped, detailCache.b, useDetailCache) * microDetailStrength;
    float dryGrain = lerp(proceduralGrain, detailCache.r, useDetailCache) * microDetailStrength;
    float wallDetailMask = WallDetailMask(normal);
    float cachedCavity = saturate(detailCache.a * microDetailStrength * 0.62f + strata * 0.36f + erosionCracks * 0.34f);
    float cavity = lerp(proceduralCavity, cachedCavity, useDetailCache);
    float cachedHighFrequency = saturate(
        (detailCache.b * 0.58f + detailCache.g * 0.48f + detailCache.r * 0.22f + strata * 0.18f) *
        wallDetailMask *
        saturate(microDetailStrength));
    float highFrequencyDetail = lerp(proceduralHighFrequency, cachedHighFrequency, useDetailCache);
    float ao = lerp(
        1.0f,
        0.42f,
        saturate((cavity + microCracks * 0.55f + chippedEdges * 0.48f + highFrequencyDetail * 0.40f) * cavityAoStrength));
    ao *= pbrSurface.ao;
    float rootContact = saturate(contactAo * (0.75f + cavityAoStrength * 0.95f));
    ao *= lerp(1.0f, 0.20f, rootContact);

    float3 sandstone = texColor.rgb * gMaterial.color.rgb;
    float3 warmHigh = float3(1.15f, 0.92f, 0.62f);
    float3 coolLow = float3(0.55f, 0.38f, 0.25f);
    float3 rockColor = lerp(coolLow, warmHigh, saturate(noise * 0.9f + 0.12f));
    rockColor *= sandstone;
    rockColor = lerp(sandstone, rockColor, noiseAmount);
    float variationSigned = rockVariation - 0.5f;
    float3 variationTint = lerp(float3(0.72f, 0.67f, 0.60f), float3(1.16f, 0.98f, 0.78f), rockVariation);
    float variationAmount = saturate(abs(variationSigned) * 2.0f);
    rockColor *= lerp(float3(1.0f, 1.0f, 1.0f), variationTint, variationAmount * 0.42f);
    rockColor *= 0.92f + rockVariation * 0.18f;
    rockColor = lerp(rockColor, rockColor * float3(0.58f, 0.45f, 0.34f), strata);
    rockColor = lerp(rockColor, rockColor * float3(0.46f, 0.34f, 0.24f), saturate(erosionCracks * 0.65f));
    rockColor = lerp(
        rockColor,
        rockColor * float3(0.34f, 0.26f, 0.20f),
        saturate((microCracks * 0.58f + chippedEdges * 0.54f + highFrequencyDetail * 0.68f) * (0.55f + wallDetailMask * 0.45f)));
    float chippedHighlight = saturate((chippedEdges * 0.34f + highFrequencyDetail * 0.42f) * wallDetailMask);
    rockColor += sandstone * float3(0.13f, 0.10f, 0.07f) * chippedHighlight * saturate(microDetailStrength * 0.75f);
    rockColor *= lerp(1.0f, 0.82f + dryGrain * 0.28f + highFrequencyDetail * 0.08f, saturate(microDetailStrength * noiseAmount));
    rockColor = lerp(rockColor, rockColor * float3(0.38f, 0.29f, 0.22f), rootContact * 0.34f);
    float floorSandShadow = FloorSandShadow(input.worldPosition, normal) * floorSandShadowStrength;
    rockColor = lerp(
        rockColor,
        rockColor * float3(0.58f, 0.47f, 0.34f),
        floorSandShadow * (0.44f + dryGrain * 0.18f));
    float wetDetailSignal = saturate(
        cavity * 0.30f +
        microCracks * 0.28f +
        chippedEdges * 0.34f +
        highFrequencyDetail * 0.54f);
    float wallWetRidgeMask = wallDetailMask * smoothstep(0.46f, 0.92f, wetDetailSignal);
    float floorWaterFilmMask = smoothstep(0.50f, 0.94f, normal.y) *
        smoothstep(0.42f, 0.94f, floorSandShadow * 0.74f + dryGrain * 0.34f + rootContact * 0.16f);
    wallWetRidgeMask *= 1.0f - distantWallAtmosphere * 0.70f;
    floorWaterFilmMask *= 1.0f - distantWallAtmosphere * 0.55f;
    float wetCanyonMask = saturate(
        wallWetRidgeMask +
        floorWaterFilmMask * 0.72f +
        pbrSurface.wetness * 0.18f);
    float wetDarkenMask = saturate(wallWetRidgeMask * 0.74f + floorWaterFilmMask * 0.24f);
    float3 wetStoneTint = lerp(float3(0.42f, 0.38f, 0.34f), float3(0.55f, 0.53f, 0.47f), rockVariation);
    rockColor = lerp(rockColor, rockColor * wetStoneTint, wetDarkenMask * 0.32f);
    float3 distantAirColor = lerp(float3(0.58f, 0.61f, 0.61f), float3(0.86f, 0.88f, 0.85f), distanceAir);
    rockColor = lerp(rockColor, distantAirColor * (0.70f + rockVariation * 0.08f), distantWallAtmosphere * 0.42f);
    ao = lerp(ao, 0.82f, distantWallAtmosphere * 0.48f);

    if (gMaterial.enableLighting == 0)
    {
        output.color = float4(saturate(rockColor * ao), 1.0f);
        return output;
    }

    float3 viewDir = SafeNormalize(cameraWorldPosition - input.worldPosition);
    float3 lightDir = SafeNormalize(-gDirectionalLight.direction);
    float3 halfDir = SafeNormalize(lightDir + viewDir);
    float nDotV = max(saturate(dot(normal, viewDir)), 0.001f);
    float nDotL = saturate(dot(normal, lightDir));
    float nDotH = saturate(dot(normal, halfDir));
    float vDotH = saturate(dot(viewDir, halfDir));
    float titleShadow = titleLandscape ? TitleGroundShadow(input.worldPosition,SafeNormalize(input.normal)) : 0.0f;
    float shadowVisibility = titleLandscape ? 1.0f-titleShadow :
        SampleCascadeShadow(input.worldPosition, normal, lightDir);

    float wetSurface = saturate(
        wetCanyonMask * 0.78f +
        pbrSurface.wetness * 0.12f);
    float roughness = clamp(
        lerp(pbrSurface.roughness, pbrSurface.roughness * 0.34f, wetSurface),
        0.045f,
        1.0f);
    roughness = lerp(
        roughness,
        min(roughness + 0.10f, 1.0f),
        saturate(dryGrain * 0.18f + distantWallAtmosphere * 0.24f));
    float metallic = saturate(pbrSurface.metallic);
    float3 f0 = lerp(
        float3(0.04f, 0.04f, 0.04f),
        rockColor,
        metallic);
    float distribution = DistributionGgx(nDotH, roughness);
    float geometry = GeometrySmith(nDotV, nDotL, roughness);
    float3 fresnel = FresnelSchlick(vDotH, f0);
    float3 specular = distribution * geometry * fresnel /
        max(4.0f * nDotV * nDotL, 1.0e-4f);
    specular *= lerp(0.55f, 1.0f, specularStrength);

    float3 diffuseWeight = (1.0f - fresnel) * (1.0f - metallic);
    float3 diffuseBrdf = diffuseWeight * rockColor / kPi;
    float3 sunRadiance =
        gDirectionalLight.color.rgb * gDirectionalLight.intensity;
    float3 directLighting =
        (diffuseBrdf + specular) *
        sunRadiance *
        nDotL *
        shadowVisibility;

    float3 environmentLighting = EvaluateEnvironmentIbl(
        normal,
        viewDir,
        rockColor,
        metallic,
        roughness,
        ao,
        skyFillStrength);
    if (titleLandscape) {
        // The title uses a clear-colour sky instead of the gameplay skybox.
        // Supply its diffuse sky irradiance alongside the shared specular IBL
        // so physically shaded ground remains readable beneath the cart.
        float skyVisibility = lerp(0.65f,1.0f,saturate(normal.y*0.5f+0.5f));
        environmentLighting += rockColor*ao*TitleSkyIrradiance()*0.962f*skyFillStrength*skyVisibility;
        environmentLighting *= 1.0f-titleShadow*0.65f;
    }
    float3 lit = directLighting + environmentLighting;
    float3 canyonAirLight = lerp(float3(0.42f, 0.46f, 0.47f), float3(0.76f, 0.78f, 0.76f), distanceAir);
    lit = lerp(lit, canyonAirLight, distantWallAtmosphere * 0.26f);

    float silhouetteBase = 1.0f - saturate(dot(normal, viewDir));
    float silhouette = Pow2(silhouetteBase) * lerp(1.0f, silhouetteBase, 0.35f);
    float backLightBase = saturate(dot(-viewDir, lightDir));
    float backLight = Pow2(backLightBase) * backLightBase;
    float grazingBase = saturate(dot(normal, lightDir)) * 0.5f + 0.5f;
    float grazingLight = Pow2(grazingBase) * grazingBase;
    float wallRimMask = smoothstep(0.18f, 0.74f, 1.0f - abs(normal.y));
    float edgeOnlyRim = silhouette * lerp(1.0f, silhouette, 0.35f) * wallRimMask;
    float boostedBackLight = backLight * (0.76f + backlightRimBoost * 1.05f * edgeOnlyRim);
    float rim = silhouette * (0.24f + boostedBackLight + grazingLight * (0.22f + backlightRimBoost * 0.16f * wallRimMask)) * rimStrength;
    rim *= lerp(1.0f, 1.34f, edgeOnlyRim * backlightRimBoost);
    rim *= 1.0f + highFrequencyDetail * wallRimMask * 0.28f;
    float3 rimTint = lerp(float3(1.08f, 0.94f, 0.78f), float3(0.72f, 0.84f, 1.02f), saturate(wetCanyonMask * 0.72f + backlightRimBoost * 0.18f));
    lit += gDirectionalLight.color.rgb * rimTint * rim * gDirectionalLight.intensity;

    if (titleLandscape) {
        if (gMaterial.specularMode == 7) {
            float recess = saturate(input.texcoord.x)*smoothstep(0.0f,38.0f,max(0.0f,1.0f-input.texcoord.y));
            lit *= lerp(1.0f,0.16f,recess);
        } else {
            lit = TitleAtmosphere(lit,input.worldPosition);
        }
    }
    output.color = float4(saturate(lit), texColor.a);
    return output;
}
