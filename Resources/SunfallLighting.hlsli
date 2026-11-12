#ifndef GE3_SUNFALL_LIGHTING
#define GE3_SUNFALL_LIGHTING

// One linear-light palette for sky, surface bounce, reflections and dust.
float3 SunfallZenith(float3 sunColor) {
    return lerp(float3(0.48f,0.66f,0.94f),float3(0.64f,0.76f,0.96f),saturate(sunColor.b));
}
float3 SunfallHorizon(float3 sunColor,float sunIntensity) {
    return lerp(float3(1.0f,0.92f,0.76f),sunColor,0.30f)*(1.0f+0.18f*min(sunIntensity,3.5f));
}
float3 SunfallBackdropHorizon(float3 sunColor) {
    // Background radiance is a composition value, separate from the brighter
    // irradiance used to light rock and metal. Keep room for pale enemies.
    return lerp(float3(.46f,.56f,.66f),float3(.62f,.54f,.42f),saturate(1-sunColor.b)*.32f);
}
float3 SunfallSkyRadiance(float3 direction,float3 toSun,float3 sunColor,float sunIntensity) {
    float facing=saturate(dot(direction,toSun));
    float horizon=exp(-abs(direction.y)*3.8f);
    float3 sky=lerp(SunfallZenith(sunColor)*.55f,SunfallBackdropHorizon(sunColor),horizon);
    float halo=pow(facing,10.0f)*0.12f+pow(facing,85.0f)*0.20f;
    float disk=pow(facing,1800.0f)*2.2f;
    return sky+lerp(sunColor,float3(1.0f,0.96f,0.85f),0.40f)*(halo+disk)*min(sunIntensity,3.5f);
}
float3 SunfallCourseZenith(float3 palette) {
    return max(float3(.055f,.075f,.10f),saturate(palette)*1.36f);
}
float3 SunfallCourseHorizon(float3 palette,float3 sunColor) {
    float3 aerial=SunfallCourseZenith(palette)*.70f+float3(.32f,.36f,.38f);
    return lerp(SunfallBackdropHorizon(sunColor),aerial,.78f);
}
float SunfallCloudHash(float2 cell) {
    float3 p=frac(float3(cell.xyx)*.1031f);
    p+=dot(p,p.yzx+33.33f);
    return frac((p.x+p.y)*p.z);
}
float SunfallCloudNoise(float2 position) {
    float2 cell=floor(position),fraction=frac(position);
    fraction=fraction*fraction*(3-2*fraction);
    return lerp(lerp(SunfallCloudHash(cell),SunfallCloudHash(cell+float2(1,0)),fraction.x),
        lerp(SunfallCloudHash(cell+float2(0,1)),SunfallCloudHash(cell+1),fraction.x),fraction.y);
}
float3 SunfallCourseSky(float3 direction,float3 toSun,float3 sunColor,float sunIntensity,float3 palette) {
    const float elevation=max(direction.y,0);
    const float horizon=exp(-elevation*4.8f);
    const float3 zenith=SunfallCourseZenith(palette);
    const float3 air=SunfallCourseHorizon(palette,sunColor);
    float3 sky=lerp(zenith,air,horizon);

    // Project distant cloud cover in world direction. The pattern stays put
    // when the camera banks or reverses for the pursuit.
    const float2 cloudPoint=direction.xz/(elevation+.24f)*float2(2.9f,4.5f)+float2(6.4f,9.1f);
    const float vapor=.61f*SunfallCloudNoise(cloudPoint)+.27f*SunfallCloudNoise(cloudPoint*2.04f+4.2f)+
        .12f*SunfallCloudNoise(cloudPoint*4.08f+8.3f);
    const float cloud=smoothstep(.51f,.72f,vapor)*smoothstep(.035f,.20f,elevation);
    const float3 cloudLight=lerp(air*1.12f,lerp(float3(.89f,.92f,1.0f),sunColor,.28f),.65f);
    sky=lerp(sky,cloudLight,cloud*.62f);
    const float facing=saturate(dot(direction,toSun));
    const float glare=pow(facing,12)*.15f+pow(facing,90)*.22f+pow(facing,1800)*2.2f;
    return sky+lerp(sunColor,float3(1,.96f,.85f),.40f)*glare*min(sunIntensity,3.5f);
}
float3 SunfallSkyIrradiance(float3 normal,float3 toSun,float3 sunColor,float sunIntensity) {
    float up=saturate(normal.y*0.5f+0.5f);
    float3 sky=lerp(SunfallHorizon(sunColor,sunIntensity)*0.58f,SunfallZenith(sunColor),up);
    return sky*(0.55f+0.45f*up)+sunColor*0.07f*saturate(dot(normal,toSun)*0.5f+0.5f);
}
float SunfallDistanceHaze(float depth,float outdoor) {
    // Keep the combat space clear; reserve the stronger veil for the horizon.
    float middle=smoothstep(120.0f,420.0f,depth)*0.14f;
    float farLayer=(1-exp(-max(depth-420.0f,0.0f)*0.0009f))*0.34f;
    return min(middle+farLayer,0.42f)*outdoor;
}
#endif
