Texture2D<float4> gSceneColor : register(t0);
Texture2D<float4> gVfxAccumulation : register(t1);
Texture2D<float4> gPostColor : register(t2);
SamplerState gSampler : register(s0);

// Temporary sand-collapse preview. Restore true to bring back the outgoing
// burn, including its irregular edge, warm light and optical distortion.
static const bool kRailFilmBurnEnabled = false;

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
    if (!kRailFilmBurnEnabled) {
        // Keep sand and smoke readable until the final short world handoff.
        // Fully opaque progress still conceals the scene swap as before.
        visibility = 1.0f - smoothstep(0.65f, 1.0f, progress);
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
    float gBloomIntensity; // Presentation mode: continuous sand-cloud age.
    float gDistortionIntensity;
    float gGlowIntensity;
    float gPostIntensity;
    float gAux4;
    float gAux5;
    float gAux6;
    float gAux7; // 0 outgoing fallback, 1 incoming fallback, 2/3 sand cover/clear.
    float2 gTunnelFocus;
    float2 gImpactScreen;
    float gLensLeak;
    float gLensAge;
    float gTunnelShade;
    float gCollapseDust;
};

struct PSInput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

float SandSpecks(float2 uv, float age, float2 scale, float2 wind) {
    float2 grid=uv*scale+wind*age;
    float2 cell=floor(grid);
    float2 jitter=float2(FilmBurnHash(cell),FilmBurnHash(cell+17.0f))*0.50f+0.25f;
    float2 delta=(frac(grid)-jitter)*float2(1.0f,1.4f);
    float grain=1.0f-smoothstep(0.04f,0.14f,length(delta));
    return grain*step(0.76f,FilmBurnHash(cell+43.0f));
}

float3 RotateCollapseRock(float3 p, float3 axis, float sine, float cosine) {
    return p*cosine+cross(axis,p)*sine+axis*dot(axis,p)*(1.0f-cosine);
}

// Actual convex solid intersections in camera space, not textured flat quads.
// A sphere rejection bounds the work before testing the fourteen rock faces.
bool TraceCollapseRock(float3 ray, float3 center, float3 scale, float seed, float tumble,
                       float fadeProgress, inout float nearest, out float3 stone, out float opacity) {
    stone=0.0f;opacity=0.0f;
    float radius=length(scale);
    float along=dot(ray,center);
    if(along<=0.0f || along*along-dot(center,center)+radius*radius<0.0f) return false;
    float3 axis=normalize(float3(0.35f+seed,0.8f,0.55f-seed*0.3f));
    float sine,cosine;
    sincos(tumble,sine,cosine);
    float3 origin=RotateCollapseRock(-center,axis,-sine,cosine)/scale;
    float3 direction=RotateCollapseRock(ray,axis,-sine,cosine)/scale;
    float nearHit=0.0f,farHit=10000.0f;
    float3 faceNormal=0.0f;
    [loop] for(uint face=0;face<14;++face) {
        float3 normal;
        float plane;
        if(face<6) {
            normal=0.0f;
            normal[face/2]=face%2==0 ? 1.0f : -1.0f;
            plane=0.88f+0.12f*frac(seed*11.7f+float(face)*0.371f);
        } else {
            uint corner=face-6;
            normal=float3((corner&1u)!=0 ? 1.0f : -1.0f,
                (corner&2u)!=0 ? 1.0f : -1.0f,(corner&4u)!=0 ? 1.0f : -1.0f)*0.57735027f;
            // Unequal fracture cuts break the repeated bevelled-block shape.
            normal=normalize(normal+0.12f*float3(
                frac(seed*13.7f+float(face)*0.41f)-0.5f,
                frac(seed*19.3f+float(face)*0.37f)-0.5f,
                frac(seed*23.1f+float(face)*0.29f)-0.5f));
            plane=1.08f+0.32f*frac(seed*17.3f+float(face)*0.293f);
        }
        float denominator=dot(normal,direction);
        float planeDistance=plane-dot(normal,origin);
        if(abs(denominator)<0.00001f) {
            if(planeDistance<0.0f) return false;
        } else {
            float hit=planeDistance/denominator;
            if(denominator<0.0f) {
                if(hit>nearHit) {nearHit=hit;faceNormal=normal;}
            } else farHit=min(farHit,hit);
        }
        if(nearHit>farHit) return false;
    }
    if(nearHit<=0.0f || nearHit>=nearest) return false;
    // Camera-up defines a descending veil of dust. Keep the solid surface
    // present; this value controls smoke absorption, not see-through geometry.
    float3 localUp=RotateCollapseRock(float3(0,1,0),axis,-sine,cosine);
    float halfHeight=max(dot(abs(localUp),scale),0.001f);
    float down=saturate((center.y+halfHeight-ray.y*nearHit)/(2.0f*halfHeight));
    float fadeEdge=lerp(-0.18f,1.18f,saturate(fadeProgress));
    opacity=smoothstep(fadeEdge-0.16f,fadeEdge+0.16f,down);
    nearest=nearHit;
    float3 normal=normalize(RotateCollapseRock(faceNormal/scale,axis,sine,cosine));
    float3 localHit=origin+direction*nearHit;
    float relief=FilmBurnNoise(localHit.xy*9.3f+seed*23.0f)*0.34f+
        FilmBurnNoise(localHit.yz*13.7f+19.0f)*0.33f+
        FilmBurnNoise(localHit.zx*18.1f+seed*31.0f)*0.33f;
    float diffuse=saturate(dot(normal,normalize(float3(-0.45f,0.80f,-0.65f))));
    float3 fill=float3(0.27f,0.23f,0.18f)+saturate(normal.y)*float3(0.09f,0.075f,0.055f);
    float rim=pow(1.0f-saturate(dot(normal,-ray)),3.0f)*0.025f;
    float3 albedo=lerp(float3(0.39f,0.29f,0.19f),float3(0.53f,0.41f,0.28f),seed);
    float3 key=diffuse*float3(0.76f,0.63f,0.47f);
    stone=albedo*(fill+key)*(0.82f+0.26f*relief)+rim*float3(0.8f,0.65f,0.44f);
    return true;
}

bool LateCollapseRocks(float2 uv, float age, out float3 stone, out float mist, out float opacity) {
    stone=0.0f;mist=0.0f;opacity=0.0f;
    // Keep the last falling pieces briefly visible after the dense veil clears.
    // Matches the bounded rock tail in HasRailFilmBurnPresentation().
    if(age<0.85f || age>3.30f) return false;
    uint width,height;
    gPostColor.GetDimensions(width,height);
    float aspect=float(width)/max(float(height),1.0f);
    float3 ray=normalize(float3((uv.x-0.5f)*2.0f*aspect*0.3650285f,
        (0.5f-uv.y)*2.0f*0.3650285f,1.0f));
    float nearest=10000.0f;
    bool hit=false;
    [loop] for(uint i=0;i<16;++i) {
        float seed=FilmBurnHash(float2(float(i)+7.0f,91.4f));
        // Stratified lanes guarantee edge-to-edge coverage. Four uneven waves
        // distribute rocks vertically instead of packing them into one row.
        uint laneIndex=(i*5u)%16u;
        // Spread emission, keeping gravity and each fragment's fall speed intact.
        float birth=0.85f+float(i/4u)*0.36f+float(i%4u)*0.035f+seed*0.07f;
        float flight=age-birth;
        if(flight<0.0f) continue;
        float depthSeed=FilmBurnHash(float2(float(i),37.1f));
        uint depthLayer=i%4u;
        float depth=depthLayer==0u ? 3.5f+1.3f*depthSeed :
            depthLayer==3u ? 9.0f+3.0f*depthSeed : 6.0f+2.0f*depthSeed;
        float lane=(float(laneIndex)+0.18f+0.64f*FilmBurnHash(float2(float(i),15.7f)))/16.0f;
        float size=depthLayer==0u ? 0.34f+seed*0.16f : 0.23f+seed*0.18f;
        float height=depth*0.3650285f+1.25f+seed*0.75f;
        float speed=depthLayer==3u ? 7.6f+seed*1.3f : 9.0f+seed*1.6f;
        float exitTime=(sqrt(speed*speed+19.6f*(height+depth*0.3650285f+size*1.8f))-speed)/9.8f;
        if(flight>exitTime+0.12f) continue;
        float3 center=float3((lane-0.5f)*2.0f*aspect*0.3650285f*depth+(seed-0.5f)*flight*0.45f,
            height-speed*flight-4.9f*flight*flight,depth-flight*0.35f);
        float3 scale=size*float3(0.72f+0.45f*FilmBurnHash(float2(float(i),21.3f)),
            0.58f+0.60f*FilmBurnHash(float2(float(i),48.7f)),
            0.65f+0.50f*FilmBurnHash(float2(float(i),62.1f)));
        float tumble=seed*6.2831853f+flight*(3.5f+seed*3.0f);
        float3 candidate;
        float candidateOpacity;
        float fade=smoothstep(exitTime*0.52f,exitTime*0.95f,flight);
        if(TraceCollapseRock(ray,center,scale,seed,tumble,fade,nearest,candidate,candidateOpacity)) {
            stone=candidate;mist=nearest;opacity=candidateOpacity;hit=true;
        }
        // Irregularly shed zero, one or two chips, with independent release
        // times and directions. Avoid identical paired satellites on every rock.
        uint chipCount=(uint)(FilmBurnHash(float2(float(i),73.9f))*3.0f);
        [unroll] for(uint chip=0;chip<2;++chip) {
            if(chip>=chipCount) continue;
            float chipSeed=FilmBurnHash(float2(float(i)*3.0f+float(chip),107.3f));
            float chipAge=flight-(0.12f+chipSeed*0.20f);
            if(chipAge<=0.0f) continue;
            float angle=chipSeed*6.2831853f;
            float3 offset=float3(cos(angle)*scale.x*0.8f,sin(angle)*scale.y*0.7f,(chipSeed-0.5f)*scale.z);
            float3 velocity=float3(cos(angle)*(0.65f+chipSeed),-0.3f-chipSeed*0.7f,sin(angle)*0.35f);
            float3 chipCenter=center+offset+velocity*chipAge+float3(0,-2.2f*chipAge*chipAge,0);
            float3 chipScale=scale*(0.10f+chipSeed*0.15f)*float3(0.80f,1.0f,0.85f);
            if(TraceCollapseRock(ray,chipCenter,chipScale,chipSeed,
                tumble+(chipSeed-0.5f)*chipAge*10.0f,fade,nearest,candidate,candidateOpacity)) {
                stone=candidate;mist=nearest;opacity=candidateOpacity;hit=true;
            }
        }
    }
    return hit;
}

float4 PresentSandTransition(float2 uv) {
    float progress=saturate(gAux4);
    float age=max(gBloomIntensity,0.0f);
    bool incoming=gAux7>2.5f;
    // Advect downward continuously rather than mostly enlarging a fixed veil.
    float2 drift=float2(age*0.32f,-age*0.70f);
    float2 flow=uv*float2(5.0f,3.5f)+drift;
    float2 warp=float2(FilmBurnNoise(flow+17.0f),FilmBurnNoise(flow+43.0f))-0.5f;
    float coarse=FilmBurnNoise(flow+warp*0.85f);
    float middle=FilmBurnNoise(flow*2.13f+19.0f+warp*0.4f+float2(age*0.05f,age*0.15f));
    float fine=FilmBurnNoise(flow*4.27f+53.0f+float2(-age*0.06f,-age*0.15f));
    float cloud=coarse*0.58f+middle*0.29f+fine*0.13f;
    // Overhead powder and the two ground shoulders merge into unequal
    // rolling fronts. No oval hole, burning edge or separate black fade.
    float shoulders=min(uv.x,1.0f-uv.x)*2.0f;
    float arrival=0.10f+uv.y*0.21f+shoulders*0.15f+(coarse-0.5f)*0.17f;
    float front=smoothstep(arrival,arrival+0.30f,progress);
    if(incoming) {
        // Dust keeps falling after the swap: clear from ceiling to floor,
        // instead of reversing the outgoing front back toward the ceiling.
        float clearArrival=0.04f+uv.y*0.49f+shoulders*0.07f+(coarse-0.5f)*0.075f;
        front=1.0f-smoothstep(clearArrival,clearArrival+0.30f,1.0f-progress);
    }
    float airborne=saturate(gCollapseDust)*(0.68f+0.32f*cloud)*(incoming ? front : 1.0f);
    // Keep the moving cart visible through the building cloud until the last
    // dense surge. A long, almost opaque brown hold breaks the collapse flow.
    float opticalDepth=-log(max(1.0f-airborne,0.001f))+front*front*(1.25f+1.0f*cloud);
    // Leave a thin moving powder field while the last fragments fall through
    // the incoming scene. Its extinction reaches zero with the rock tail.
    float settlingMist=incoming ? 0.10f*(0.30f+0.70f*cloud)*
        (1.0f-smoothstep(2.50f,3.30f,age)) : 0.0f;
    opticalDepth+=settlingMist;
    float transmittance=exp(-opticalDepth);
    // This exact endpoint conceals the swap even in thin peripheral pockets.
    // The fog pattern uses one continuous clock across both scenes.
    transmittance*=incoming ? 1.0f-smoothstep(0.80f,0.98f,front) :
        1.0f-smoothstep(0.80f,0.90f,progress);
    float selfShadow=saturate(coarse*0.7f+middle*0.3f);
    float illumination=saturate(0.16f+0.72f*selfShadow+0.14f*(1.0f-uv.y));
    float3 dustColor=lerp(float3(0.15f,0.10f,0.064f),float3(0.45f,0.325f,0.205f),illumination);
    dustColor*=0.92f+0.16f*fine;
    // Sampling drift is the inverse of on-screen motion. Negative grid Y
    // makes specks fall, agreeing with the world grains and powder flow.
    float specks=SandSpecks(uv,age,float2(180.0f,110.0f),float2(8.0f,-30.0f))*0.025f+
        SandSpecks(uv,age,float2(310.0f,190.0f),float2(13.0f,-48.0f))*0.015f;
    dustColor+=float3(1.0f,0.73f,0.42f)*specks;
    float3 scene=0.0f;
    if(transmittance>0.0001f) {
        float2 scatterOffset=float2(0.0016f,0.0009f)*saturate(1.0f-transmittance);
        scene=(gPostColor.Sample(gSampler,uv).rgb*2.0f+
            gPostColor.Sample(gSampler,saturate(uv+scatterOffset)).rgb+
            gPostColor.Sample(gSampler,saturate(uv-scatterOffset)).rgb)*0.25f;
        scene+=EvaluateRailLensLeak(uv,gImpactScreen,gTunnelFocus,gLensLeak,gLensAge,gTunnelShade);
    }
    float3 result=scene*transmittance+dustColor*(1.0f-transmittance);
    float3 stone;
    float rockMist;
    float rockOpacity;
    if(LateCollapseRocks(uv,age,stone,rockMist,rockOpacity)) {
        // One dust field scatters light from the cave and the falling solids.
        // Distance softens far silhouettes and facet contrast; a descending
        // local veil hides the top without making the stone transparent.
        float storm=saturate(gCollapseDust+front*0.55f);
        float pathDepth=max(rockMist-1.4f,0.0f);
        float fogDepth=(0.045f+0.19f*storm*(0.60f+0.40f*cloud))*pathDepth;
        float upperVeil=-log(max(rockOpacity,0.001f))*(0.85f+middle*0.30f);
        float rockTransmission=exp(-fogDepth-upperVeil);
        float3 rockColor=lerp(dustColor,stone,rockTransmission);
        // Let solid fragments outlast the screen-covering dust. Their upper
        // veil dissolves into powder, then exposes the moving gameplay scene.
        float rockTail=(1.0f-smoothstep(3.00f,3.30f,age))*
            smoothstep(0.02f,0.28f,rockOpacity);
        result=lerp(result,rockColor,incoming ? max(front,rockTail) : 1.0f);
    }
    return float4(result,1.0f);
}

float4 main(PSInput input) : SV_TARGET
{
    float2 uv = saturate(input.uv);
    // Boost uses this same shader in both the Release and editor viewport.
    // Mode -2 is a pure copy; -1 blurs display colour before HUD/VFX overlays.
    if (gAux5 < -1.5f) return gSceneColor.Sample(gSampler,uv);
    if (gAux5 < -0.5f) {
        uint width,height;
        gSceneColor.GetDimensions(width,height);
        float2 size=float2(width,height),texel=1.0f/size;
        float4 original=gSceneColor.Sample(gSampler,uv);
        float edge=max(abs(uv.x-0.5f),abs(uv.y-0.5f))*2.0f;
        float peripheral=smoothstep(0.46f,0.94f,edge);
        float strength=saturate(gAux4);
        float shock=saturate(gAux6);
        if(peripheral*strength<0.001f && shock<0.001f) return original;
        // Preserve the exact subject silhouettes and a two-pixel guard band.
        float protect=gVfxAccumulation.Sample(gSampler,uv).r;
        protect=max(protect,gVfxAccumulation.Sample(gSampler,uv+float2(texel.x*2,0)).r);
        protect=max(protect,gVfxAccumulation.Sample(gSampler,uv-float2(texel.x*2,0)).r);
        protect=max(protect,gVfxAccumulation.Sample(gSampler,uv+float2(0,texel.y*2)).r);
        protect=max(protect,gVfxAccumulation.Sample(gSampler,uv-float2(0,texel.y*2)).r);
        float3 color=original.rgb;
        if(protect<=0.01f && peripheral*strength>0.001f) {
            float2 radial=(uv-gTunnelFocus)*size;
            float radialLength=length(radial);
            // Wider shutter at full speed, still zero at the aim center.
            // Keep the same eight scene taps as the original boost pass.
            float shutter=min((26.0f+shock*8.0f)*height/1080.0f,radialLength*0.10f)*strength*peripheral;
            float2 direction=radial/max(radialLength,0.001f)*texel;
            float3 sum=original.rgb;
            float weights=1.0f;
            [unroll] for(int i=1;i<8;++i) {
                float2 sampleUv=saturate(uv-direction*shutter*(float(i)/7.0f));
                // Reject protected taps too; an enemy leaves no blurred copy.
                float accepted=1.0f-step(0.01f,gVfxAccumulation.Sample(gSampler,sampleUv).r);
                float weight=accepted*(1.0f-float(i)*0.055f);
                sum+=gSceneColor.Sample(gSampler,sampleUv).rgb*weight;
                weights+=weight;
            }
            color=lerp(original.rgb,sum/weights,0.90f*peripheral);
        }
        if(shock>0.001f) {
            // A neutral display-space lift, after tone mapping: black stays
            // black and highlights roll off instead of clipping to white.
            // HUD and shootable world-projectile overlays are drawn later.
            float response=lerp(1.0f,0.25f,step(0.01f,protect));
            color+=color*(1.0f-saturate(color))*(0.22f*shock*response);
            float aspect=float(width)/max(float(height),1.0f);
            float2 source0=(uv-gImpactScreen)*float2(aspect*5.0f,8.0f);
            float2 source1=(uv-float2(gLensLeak,gLensAge))*float2(aspect*5.0f,8.0f);
            // Just a thin lens bloom at the actual roots; the world plume and
            // surface reflections supply the sustained energy and colour.
            float halo=exp(-dot(source0,source0))*saturate(gTunnelShade)+
                exp(-dot(source1,source1))*saturate(gCollapseDust);
            color+=(1.0f-saturate(color))*float3(0.045f,0.14f,0.19f)*halo*shock*response;
        }
        return float4(color,original.a);
    }

    // Mode 1 presents the already composed scene + HUD exactly once. Do not
    // apply the normal luminance fallback or add VFX again during blackout.
    if (gAux5 > 0.5f) {
        if(gAux7>1.5f) return PresentSandTransition(uv);
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
        // Near-camera airborne sand scatters warm light into a moving
        // veil. It adds no second wipe; visibility remains owned by the burn.
        if(gCollapseDust>0.0f) {
            float2 drift=float2(gLensAge*0.26f,-gLensAge*0.38f);
            float2 flow=uv*float2(5.0f,3.5f)+drift;
            float2 warp=float2(FilmBurnNoise(flow+17.0f),FilmBurnNoise(flow+43.0f))-0.5f;
            float cloud=FilmBurnNoise(flow+warp*0.7f)*0.65f+FilmBurnNoise(flow*2.13f+19.0f)*0.35f;
            float grit=FilmBurnNoise(uv*float2(140.0f,90.0f)+gLensAge*0.65f);
            float haze=saturate(gCollapseDust)*(0.68f+0.32f*cloud);
            float2 scatterOffset=float2(0.0016f,0.0009f)*gCollapseDust;
            float3 scattered=(presentation*2.0f+gPostColor.Sample(gSampler,saturate(sceneUv+scatterOffset)).rgb+
                gPostColor.Sample(gSampler,saturate(sceneUv-scatterOffset)).rgb)*0.25f;
            float3 dustColor=lerp(float3(0.27f,0.18f,0.10f),float3(0.50f,0.36f,0.22f),cloud);
            presentation=lerp(scattered,dustColor,haze);
            presentation+=gCollapseDust*(grit-0.5f)*0.022f;
        }
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
