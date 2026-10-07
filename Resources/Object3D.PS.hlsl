#include "Object3d.hlsli"

// ------------------------------------------------------------
// Material / Light
// ------------------------------------------------------------
struct Material
{
    float32_t4 color;
    int32_t enableLighting;
    float32_t4x4 uvTransform;
    float shininess;
    float environmentCoefficient;
    int32_t specularMode;
    float pad_; // Mode 8: gameplay/title exposure ratio; otherwise padding.
};

struct DirectionalLight
{
    float4 color; // light color
    float3 direction; // light direction (unit vector)
    float intensity; // brightness
};

struct PointLight
{
    float4 color; // ライトの色
    float3 position; // ライト位置
    float intensity; // 輝度
    float radius; // 影響半径（最大距離）
    float decay; // 減衰の指数（大きいほど急激）
    float2 pad_; // 16byte alignment（重要）
};

struct SpotLight
{
    float4 color;
    float3 position;
    float intensity;
    float3 direction; // スポットが向く方向（ワールド、単位ベクトル）
    float distance; // 最大到達距離
    float decay; // 減衰指数
    float cosAngle; // 内側円錐のcos（例: cos(pi/3)）
    float pad_; // 16byte alignment
};

ConstantBuffer<SpotLight> gSpotLight : register(b4);



ConstantBuffer<Material> gMaterial : register(b0);
ConstantBuffer<DirectionalLight> gDirectionalLight : register(b1);

// ★追加：PointLight は b3（資料どおり）
ConstantBuffer<PointLight> gPointLight : register(b3);

Texture2D<float4> gTexture : register(t0);
TextureCube<float4> gEnvironmentTexture : register(t1);
SamplerState gSampler : register(s0);

// kept for your project (not used here)
Texture2D<float4> gReceivedTex : register(t4);

// motion mask (optional)
Texture2D<float4> motionMaskTex : register(t2);

// ------------------------------------------------------------
// Output
// ------------------------------------------------------------
struct PixelShaderOutput
{
    float4 color : SV_TARGET0;
};

// ------------------------------------------------------------
// Safe normalize (prevents NaN when vector length is near zero)
// ------------------------------------------------------------
static float3 SafeNormalize(float3 v)
{
    float len2 = dot(v, v);
    if (len2 < 1e-8f)
    {
        return float3(0.0f, 0.0f, 1.0f);
    }
    return v * rsqrt(len2);
}

static float EvaluateSpecular(float3 N, float3 L, float3 V, float shininess, int specularMode)
{
    // Authored matte stone/paint: keep diffuse lighting, without washing out
    // fracture marks under the camera-facing spot light. 0/1 retain Phong/Blinn.
    if (specularMode >= 2) return 0.0f;
    float power = max(shininess, 1.0f);
    if (specularMode == 0)
    {
        float3 R = reflect(-L, N);
        return pow(saturate(dot(R, V)), power);
    }

    float3 H = SafeNormalize(L + V);
    return pow(saturate(dot(N, H)), power);
}

#include "TitleLighting.hlsli"

// Mode 10 uses the existing normal SRV (t4); alpha carries linear roughness.
// Other models continue to use their existing material modes and shader path.
static float3 DroneNormal(VertexShaderOutput input, float3 sampledNormal)
{
    float3 N = SafeNormalize(input.normal);
    float3 dx = ddx(input.worldPosition), dy = ddy(input.worldPosition);
    float2 ux = ddx(input.texcoord), uy = ddy(input.texcoord);
    float determinant = ux.x*uy.y-ux.y*uy.x;
    if (abs(determinant)<1e-8f) return N;
    float3 T = (dx*uy.y-dy*ux.y)/determinant;
    float3 B = (dy*ux.x-dx*uy.x)/determinant;
    T = SafeNormalize(T-N*dot(N,T));
    float handedness = dot(cross(N,T),B)<0 ? -1.0f : 1.0f;
    B = cross(N,T)*handedness;
    float3 mapped = sampledNormal*2.0f-1.0f;
    return SafeNormalize(T*mapped.x+B*mapped.y+N*mapped.z);
}

static float3 DroneDirect(float3 base, float metal, float rough, float3 N, float3 V, float3 L, float3 radiance)
{
    float nv = max(saturate(dot(N,V)),0.001f), nl = saturate(dot(N,L));
    float3 H = SafeNormalize(V+L);
    float nh = saturate(dot(N,H)), vh = saturate(dot(V,H));
    float a = rough*rough, a2 = a*a;
    float denominator = nh*nh*(a2-1.0f)+1.0f;
    float D = a2/max(3.14159265f*denominator*denominator,0.0001f);
    float k = (rough+1.0f)*(rough+1.0f)*0.125f;
    float G = nv/(nv*(1.0f-k)+k)*nl/(nl*(1.0f-k)+k);
    float3 F = lerp(float3(0.04f,0.04f,0.04f),base,metal);
    F += (1.0f-F)*pow(1.0f-vh,5.0f);
    return ((1.0f-F)*(1.0f-metal)*base/3.14159265f + D*G*F/max(4.0f*nv*max(nl,0.001f),0.001f))*radiance*nl;
}

static float CartSurfaceNoise(float2 p)
{
    float2 cell=floor(p), f=frac(p);
    f=f*f*(3.0f-2.0f*f);
    float4 h=float4(dot(cell,float2(127.1f,311.7f)),
        dot(cell+float2(1,0),float2(127.1f,311.7f)),
        dot(cell+float2(0,1),float2(127.1f,311.7f)),
        dot(cell+float2(1,1),float2(127.1f,311.7f)));
    h=frac(sin(h)*43758.5453f);
    return lerp(lerp(h.x,h.y,f.x),lerp(h.z,h.w,f.x),f.y);
}

// ------------------------------------------------------------
// PS Main
// ------------------------------------------------------------
PixelShaderOutput main(VertexShaderOutput input)
{
    PixelShaderOutput output;

    float2 uv = input.texcoord;

    float4 texColor = gTexture.Sample(gSampler, uv);
    float4 maskColor = motionMaskTex.Sample(gSampler, uv);

    float3 baseRgb = texColor.rgb * gMaterial.color.rgb;
    float baseA = texColor.a * gMaterial.color.a;

    if (gMaterial.specularMode == 11) {
        float3 core = baseRgb*max(gMaterial.pad_,1.0f);
        if (titleSubject.w > 0.5f) core = TitleAtmosphere(core,input.worldPosition);
        output.color = float4(core,baseA);
        return output;
    }
    if (gMaterial.specularMode == 10) {
        float4 surface = gReceivedTex.Sample(gSampler,uv);
        float3 N = DroneNormal(input,surface.rgb);
        float3 V = SafeNormalize(cameraWorldPosition-input.worldPosition);
        float rough = clamp(surface.a,0.30f,0.90f);
        float painted = saturate((texColor.r-texColor.b)*4.0f);
        float metal = lerp(saturate(gMaterial.environmentCoefficient),0.10f,painted);
        float3 lit = DroneDirect(baseRgb,metal,rough,N,V,SafeNormalize(-gDirectionalLight.direction),
            gDirectionalLight.color.rgb*gDirectionalLight.intensity);
        float3 toPoint = gPointLight.position-input.worldPosition;
        float pointAtten = pow(saturate(1.0f-length(toPoint)/max(gPointLight.radius,0.001f)),gPointLight.decay);
        lit += DroneDirect(baseRgb,metal,rough,N,V,SafeNormalize(toPoint),gPointLight.color.rgb*gPointLight.intensity*pointAtten);
        float3 toSpot = gSpotLight.position-input.worldPosition;
        float spotAtten = pow(saturate(1.0f-length(toSpot)/max(gSpotLight.distance,0.001f)),gSpotLight.decay);
        spotAtten *= saturate((dot(-SafeNormalize(toSpot),SafeNormalize(gSpotLight.direction))-gSpotLight.cosAngle)/max(1.0f-gSpotLight.cosAngle,0.0001f));
        lit += DroneDirect(baseRgb,metal,rough,N,V,SafeNormalize(toSpot),gSpotLight.color.rgb*gSpotLight.intensity*spotAtten);
        float3 reflected = gEnvironmentTexture.SampleLevel(gSampler,reflect(-V,N),rough*4.0f).rgb;
        float3 f0 = lerp(float3(0.04f,0.04f,0.04f),baseRgb,metal);
        float rim = pow(1.0f-saturate(abs(dot(N,V))),3.0f);
        if (titleSubject.w > 0.5f) {
            lit += reflected*f0*(1.0f-rough*0.55f) + baseRgb*0.38f;
            lit += float3(0.18f,0.20f,0.22f)*rim;
        } else {
            // Cool upper-side key separates the metal from warm cave rock.
            // Keep the mapped GGX response so opposing faces stay distinct.
            float3 right=SafeNormalize(cross(float3(0,1,0),V));
            float3 fill=SafeNormalize(V+right*0.70f+float3(0,0.85f,0));
            lit += DroneDirect(baseRgb,metal,rough,N,V,fill,float3(4.2f,4.9f,5.8f));
            // A broad soft frontal source reveals the sensor housing and shield
            // faces at gameplay distance, rather than lighting only the bevels.
            float3 front=SafeNormalize(V+right*0.20f+float3(0,0.25f,0));
            lit += DroneDirect(baseRgb,metal,max(rough,0.55f),N,V,front,float3(2.2f,2.4f,2.7f));
            float3 bounce=SafeNormalize(V*0.45f-right*0.65f-float3(0,0.40f,0));
            lit += DroneDirect(baseRgb,metal,rough,N,V,bounce,float3(0.90f,0.73f,0.57f));

            // Broad fill and edge reflection use the geometric normal: normal
            // map scratches must not become a noisy glowing outline.
            float3 shapeN=SafeNormalize(input.normal);
            float skyFacing=saturate(shapeN.y*0.5f+0.5f);
            float ambient=lerp(0.15f,0.34f,skyFacing);
            lit += baseRgb*ambient + reflected*f0*(0.55f-rough*0.12f);
            lit += f0*float3(0.18f,0.22f,0.28f)*lerp(0.50f,1.0f,skyFacing);
            float3 edgeLight=SafeNormalize(-V*0.45f+right*0.90f+float3(0,0.70f,0));
            float edge=pow(1.0f-saturate(abs(dot(shapeN,V))),2.8f);
            edge *= smoothstep(-0.20f,0.65f,dot(shapeN,edgeLight));
            lit += float3(0.20f,0.30f,0.40f)*edge*lerp(0.65f,1.0f,skyFacing);
        }
        lit = lerp(lit,float3(1.4f,1.15f,0.80f),saturate(gMaterial.pad_)*0.85f);
        if (titleSubject.w > 0.5f) lit = TitleAtmosphere(lit,input.worldPosition);
        output.color = float4(lit,baseA);
        return output;
    }

    // The opening gameplay palette anchors the title, while actual surface
    // normals and GGX roughness produce distinct lit and shaded faces.
    if (gMaterial.specularMode == 8 || gMaterial.specularMode == 13) {
        float3 N = SafeNormalize(input.normal);
        float3 L = SafeNormalize(-gDirectionalLight.direction);
        float3 V = SafeNormalize(cameraWorldPosition-input.worldPosition);
        bool wheel = gMaterial.specularMode == 13;
        float3 surfaceBase = wheel ? lerp(baseRgb,float3(0.42f,0.43f,0.44f),0.55f) : baseRgb;
        float rough = clamp(gMaterial.shininess,0.35f,0.85f);
        float metal = saturate(gMaterial.environmentCoefficient);
        if (!wheel) {
            // Object-authored UVs keep weathering attached during suspension
            // and laps. U+2 identifies the actual chamfer strips/corners.
            bool bevel = input.texcoord.x > 1.5f;
            float2 uv = input.texcoord-float2(bevel ? 2.0f : 0.0f,0.0f);
            float mottling = CartSurfaceNoise(uv*float2(7.0f,5.0f)+3.7f);
            float grit = CartSurfaceNoise(uv*float2(46.0f,31.0f));
            float worn = (bevel ? 1.0f : 0.0f)*
                smoothstep(0.63f,0.86f,CartSurfaceNoise(uv*float2(13,7)+11.0f));
            float2 scratchesUV=uv*float2(4.0f,18.0f);
            float scratchLine=abs(frac(scratchesUV.y+CartSurfaceNoise(float2(floor(scratchesUV.x),2.0f))*7.0f)-0.5f);
            float aa=max(fwidth(scratchesUV.y),0.018f);
            float scratches=(1.0f-smoothstep(0.018f,0.018f+aa,scratchLine))*
                smoothstep(0.68f,0.88f,CartSurfaceNoise(scratchesUV*float2(1,0.11f)))*
                (1.0f-smoothstep(0.25f,0.90f,aa));
            float dust=(1.0f-smoothstep(0.55f,1.70f,input.worldPosition.y-titleSubject.y))*
                lerp(0.35f,0.75f,mottling);
            float exposed=saturate(worn*0.28f+scratches*0.12f)*(1.0f-dust*0.45f);
            float gritFilter=1.0f-smoothstep(0.35f,1.0f,max(fwidth(uv.x)*46.0f,fwidth(uv.y)*31.0f));
            surfaceBase *= 0.97f+mottling*0.05f+(grit-0.5f)*0.03f*gritFilter;
            // Chipped paint exposes subdued iron; dusty lower panels are
            // matte, with no emissive outline painted onto the rim.
            surfaceBase=lerp(surfaceBase,float3(0.30f,0.28f,0.25f),exposed);
            surfaceBase=lerp(surfaceBase,surfaceBase*float3(0.84f,0.78f,0.69f),dust*0.50f);
            metal=lerp(metal,0.70f,exposed);
            rough=clamp(rough+(mottling-0.5f)*0.08f+dust*0.13f-exposed*0.16f-(bevel?0.05f:0.0f),0.35f,0.85f);
        }
        float3 paint = DroneDirect(surfaceBase,metal,rough,N,V,L,
            gDirectionalLight.color.rgb*gDirectionalLight.intensity);
        float hemisphere = lerp(0.48f,1.0f,saturate(N.y*0.5f+0.5f));
        float cavity = wheel ? 0.80f : lerp(0.62f,1.0f,
            smoothstep(0.60f,2.8f,input.worldPosition.y-titleSubject.y));
        float3 sky = TitleSkyIrradiance()*hemisphere*cavity;
        float3 f0 = lerp(float3(0.04f,0.04f,0.04f),surfaceBase,metal);
        float3 reflected = gEnvironmentTexture.SampleLevel(gSampler,reflect(-V,N),rough*4.0f).rgb;
        paint += surfaceBase*sky*0.42f*(1.0f-metal) + f0*(sky*0.24f+reflected*0.16f);
        if(!wheel) paint *= max(gMaterial.pad_,0.0001f);
        if (titleSubject.w > 0.5f) paint = TitleAtmosphere(paint,input.worldPosition);
        output.color = float4(paint,baseA);
        return output;
    }

    // Title-only warm key / cool sky fill. The camera flag isolates this rig
    // from gameplay materials. Landscape/caves use the terrain PBR pipeline.
    if (titleSubject.w > 0.5f && gMaterial.enableLighting != 0 && gMaterial.specularMode != 7) {
        float3 N = SafeNormalize(input.normal);
        float3 L = SafeNormalize(-gDirectionalLight.direction);
        float diffuse = saturate(dot(N,L));
        float3 skyFill = TitleSkyIrradiance()*0.370f*lerp(0.65f,1.0f,saturate(N.y*0.5f+0.5f));
        float3 key = gDirectionalLight.color.rgb*gDirectionalLight.intensity*diffuse*0.82f;
        float contactShadow = gMaterial.specularMode == 12 ? TitleGroundShadow(input.worldPosition,N) : 0.0f;
        skyFill *= 1.0f-contactShadow*0.72f;
        key *= 1.0f-contactShadow;
        float3 lit = baseRgb*(skyFill+key);
        if (gMaterial.specularMode != 6) {
            float3 V = SafeNormalize(cameraWorldPosition-input.worldPosition);
            float rim = pow(1.0f-saturate(dot(N,V)),3.0f)*smoothstep(-0.25f,0.65f,dot(N,L));
            float specular = EvaluateSpecular(N,L,V,max(gMaterial.shininess,24.0f),
                gMaterial.specularMode == 12 ? 1 : gMaterial.specularMode);
            lit += gDirectionalLight.color.rgb*(rim*0.10f+specular*0.10f)*(1.0f-contactShadow);
        }
        output.color = float4(TitleAtmosphere(lit,input.worldPosition),baseA);
        return output;
    }

    float3 outRgb = baseRgb;

    if (gMaterial.enableLighting != 0)
    {
        float3 N = SafeNormalize(input.normal);

        // V = pixel -> camera
        float3 V = SafeNormalize(cameraWorldPosition - input.worldPosition);

        //========================================================
        // 1) DirectionalLight 分
        //========================================================
        float3 Ld = SafeNormalize(-gDirectionalLight.direction); // pixel -> light
        float3 dirLightColor = gDirectionalLight.color.rgb * gDirectionalLight.intensity;

        // Diffuse (Half-Lambert)
        float NdotLd = saturate(dot(N, Ld));
        float halfLambertD = pow(NdotLd * 0.5f + 0.5f, 2.0f);

        float3 diffuseD =
            baseRgb *
            dirLightColor *
            halfLambertD;

        float specPowD = EvaluateSpecular(N, Ld, V, gMaterial.shininess, gMaterial.specularMode);

        float3 specularD =
            dirLightColor *
            specPowD;

       //========================================================
// 2) PointLight 分 ★距離減衰あり（逆二乗）
//========================================================
        float3 toPL = gPointLight.position - input.worldPosition;

        float dist = length(toPL);

// 半径が0だと割れちゃうので保険
        float r = max(gPointLight.radius, 0.001f);

// 資料の形：pow(saturate(-dist/r + 1), decay) = pow(saturate(1 - dist/r), decay)
        float factor = pow(saturate(1.0f - dist / r), gPointLight.decay);


// Lp = pixel -> pointLight（入射光方向）
        float3 Lp = SafeNormalize(toPL);

// 色 = pointLight の色 * intensity * 減衰
        float3 pointLightColor = gPointLight.color.rgb * gPointLight.intensity * factor;


// Diffuse (Half-Lambert)
        float NdotLp = saturate(dot(N, Lp));
        float halfLambertP = pow(NdotLp * 0.5f + 0.5f, 2.0f);

        float3 diffuseP =
    baseRgb *
    pointLightColor *
    halfLambertP;

        float specPowP = EvaluateSpecular(N, Lp, V, gMaterial.shininess, gMaterial.specularMode);

        float3 specularP =
    pointLightColor *
    specPowP;

       //========================================================
// 2.5) SpotLight 分（距離減衰 + Falloff）※向き整理版
//========================================================
        float3 toSurface = input.worldPosition - gSpotLight.position; // light -> pixel
        float distS = length(toSurface);

// light->pixel（フォールオフ用）
        float3 Ls_out = SafeNormalize(toSurface);

// pixel->light（拡散・鏡面用）
        float3 Ls = -Ls_out;

// 距離減衰：0で最大、distanceで0
        float maxDist = max(gSpotLight.distance, 0.001f);
        float atten = pow(saturate(1.0f - distS / maxDist), gSpotLight.decay);

// Falloff（角度減衰）
// direction は「スポットが向く方向」（lightから照らす向き）とする
        float3 Sd = SafeNormalize(gSpotLight.direction);
        float cosA = dot(Ls_out, Sd); // 中心軸だと1

// cosAngleが1に近いと分母が0になるので保険
        float denom = max(1.0f - gSpotLight.cosAngle, 1e-4f);
        float falloff = saturate((cosA - gSpotLight.cosAngle) / denom);

// ライト色（距離減衰＋角度減衰込み）
        float3 spotColor = gSpotLight.color.rgb * gSpotLight.intensity * atten * falloff;

// Diffuse（Half-Lambert）
        float NdotLs = saturate(dot(N, Ls));
        float halfLambertS = pow(NdotLs * 0.5f + 0.5f, 2.0f);
        float3 diffuseS = baseRgb * spotColor * halfLambertS;

// Specular（Blinn）
        float specPowS = EvaluateSpecular(N, Ls, V, gMaterial.shininess, gMaterial.specularMode);
        float3 specularS = spotColor * specPowS;



        //========================================================
        // 3) 最終合成：全部足す（資料どおり）
        //========================================================
        outRgb = (diffuseD + specularD) + (diffuseP + specularP) + (diffuseS + specularS);


    }

    if (gMaterial.environmentCoefficient > 0.0f)
    {
        float3 N = SafeNormalize(input.normal);
        float3 cameraToPosition = SafeNormalize(input.worldPosition - cameraWorldPosition);
        float3 reflectedVector = reflect(cameraToPosition, N);
        float3 environmentColor = gEnvironmentTexture.Sample(gSampler, reflectedVector).rgb;
        outRgb += environmentColor * gMaterial.environmentCoefficient;
    }

    // Enemy presentation modes: retain volume in shadow and separate the rim
    // from rock. These modes never alter depth or collision geometry.
    if (gMaterial.specularMode == 3 || gMaterial.specularMode == 4)
    {
        float3 N = SafeNormalize(input.normal);
        float3 V = SafeNormalize(cameraWorldPosition - input.worldPosition);
        float rim = pow(1.0f - saturate(abs(dot(N, V))), 2.5f);
        outRgb = max(outRgb, baseRgb * 0.62f + float3(0.06f, 0.035f, 0.02f));
        outRgb += float3(1.0f, 0.48f, 0.12f) * rim * 0.85f;
        // A confirmed hit flashes even on a dark texture, not just its specular.
        if (gMaterial.specularMode == 4)
            outRgb = lerp(outRgb, float3(1.4f, 1.3f, 1.1f), 0.88f);
    }
    // Small turret muzzle cores use their own light colour, independent of
    // the sphere's texture or scene lighting. The owning attack gates them.
    if (gMaterial.specularMode == 5)
        outRgb = gMaterial.color.rgb * 1.6f;
    output.color = float4(outRgb, baseA);
    return output;
}
