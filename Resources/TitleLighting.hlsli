#ifndef GE3_TITLE_LIGHTING
#define GE3_TITLE_LIGHTING

// Shared b2 layout matches CameraForGPU; the first 16 bytes are the camera ABI.
cbuffer Camera : register(b2)
{
    float3 cameraWorldPosition;
    float padCam;
    float4 titleSubject;
    float4 titleForward;
    float4 titleHaze;
    float4 titleWheelContacts[4];
    float4 titleLightSweep; // sheet offset/intensity, rail glint distance/intensity
    float4 titleRailImpact; // fixed world contact and transient radiance
    float4 sunfallSky; // enabled, outdoor blend, sky fill, reserved
    float4 caveOpenings[4]; // world position and diffuse influence radius
    float4 caveRailOrigin; // world position and course distance
    float4 caveRailForward;
    float4 caveRange; // outdoor start, transition length, outdoor strength, enabled
}

static float TitleCaveExposure()
{
    return titleSubject.w>0.5f ? lerp(1.0f,0.42f,saturate(titleForward.w)) : 1.0f;
}
static float3 TitleImpactDirection(float3 worldPosition)
{
    float3 delta=titleRailImpact.xyz+float3(0,0.30f,0)-worldPosition;
    return delta/max(length(delta),0.001f);
}
static float3 TitleImpactRadiance(float3 worldPosition)
{
    float3 delta=titleRailImpact.xyz-worldPosition;
    return float3(5.0f,2.4f,0.72f)*titleRailImpact.w/(1.0f+dot(delta,delta)*0.22f);
}

static float3 TitleSweepDirection()
{
    float3 forward = normalize(titleForward.xyz);
    float3 right = float3(forward.z,0,-forward.x);
    return normalize(-right*0.65f-forward*0.35f+float3(0,0.8f,0));
}
static float TitleSweepAmount(float3 worldPosition)
{
    if(titleSubject.w<0.5f || titleLightSweep.y<=0.0f) return 0.0f;
    float3 forward=normalize(titleForward.xyz);
    float3 right=float3(forward.z,0,-forward.x);
    float3 delta=worldPosition-titleSubject.xyz;
    float along=dot(delta,forward);
    float sheet=(dot(delta,right)+0.28f*along-0.80f*delta.y-titleLightSweep.x)/1.4f;
    return titleLightSweep.y*exp(-sheet*sheet)*exp(-along*along/200.0f);
}
static float TitleRailGlint(float3 worldPosition,float3 normal)
{
    if(titleLightSweep.w<=0.0f) return 0.0f;
    float3 forward=normalize(titleForward.xyz);
    float3 right=float3(forward.z,0,-forward.x);
    float3 delta=worldPosition-titleSubject.xyz;
    float lane=abs(dot(delta,right));
    float head=1.0f-smoothstep(0.10f,0.17f,abs(lane-1.9f));
    float along=(dot(delta,forward)-titleLightSweep.z-2.0f)/2.5f;
    return head*smoothstep(0.55f,0.95f,normal.y)*exp(-along*along)*titleLightSweep.w;
}

// Diffuse fill follows the opening course hue without changing its luminance.
// Partial desaturation keeps shaded ground readable and avoids orange shadows.
static float3 TitleSkyIrradiance()
{
    float3 hue = gDirectionalLight.color.rgb;
    hue /= max(dot(hue,float3(0.2126f,0.7152f,0.0722f)),0.001f);
    return lerp(float3(1.0f,1.0f,1.0f),hue,0.55f);
}

static float TitleGroundShadow(float3 worldPosition, float3 normal)
{
    float receiver = smoothstep(0.65f,0.95f,normal.y)*
        (1.0f-smoothstep(0.38f,0.70f,abs(worldPosition.y-titleSubject.y)));
    float2 forward = normalize(titleForward.xz);
    float2 right = float2(forward.y,-forward.x);
    float2 delta = worldPosition.xz-titleSubject.xz;
    float2 footprint = float2(dot(delta,right)/1.85f,dot(delta,forward)/3.25f);
    float contact = exp(-2.0f*dot(footprint,footprint))*0.46f;
    float2 castOffset = gDirectionalLight.direction.xz*(1.7f/max(-gDirectionalLight.direction.y,0.25f));
    float2 castDelta = delta-castOffset;
    float2 castFootprint = float2(dot(castDelta,right)/2.1f,dot(castDelta,forward)/3.3f);
    float cast = (1.0f-smoothstep(0.50f,1.15f,length(castFootprint)))*0.30f;
    float wheelContact = 0.0f;
    [unroll] for (int i=0;i<4;++i) {
        float2 wheelDelta = worldPosition.xz-titleWheelContacts[i].xz;
        float height = abs(worldPosition.y-titleWheelContacts[i].y);
        float softness = lerp(1.0f,1.7f,saturate(height/0.45f));
        float2 wheelFootprint = float2(dot(wheelDelta,right)/0.26f,dot(wheelDelta,forward)/0.44f)/softness;
        float softContact = exp(-dot(wheelFootprint,wheelFootprint))*0.66f;
        float2 coreFootprint = float2(dot(wheelDelta,right)/0.12f,dot(wheelDelta,forward)/0.20f);
        float hardContact = exp(-dot(coreFootprint,coreFootprint))*0.84f*
            (1.0f-smoothstep(0.035f,0.22f,height));
        wheelContact = max(wheelContact,max(softContact,hardContact)*titleWheelContacts[i].w);
    }
    return min(0.86f,max(wheelContact,contact+cast))*receiver;
}

static float3 TitleAtmosphere(float3 color, float3 worldPosition)
{
    float haze = smoothstep(48.0f,260.0f,length(cameraWorldPosition-worldPosition))*0.90f;
    return lerp(color,titleHaze.rgb,haze*(1.0f-titleForward.w*0.80f));
}
#endif
