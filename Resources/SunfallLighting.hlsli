#ifndef GE3_SUNFALL_LIGHTING
#define GE3_SUNFALL_LIGHTING

// One linear-light palette for sky, surface bounce, reflections and dust.
float3 SunfallZenith(float3 sunColor) {
    return lerp(float3(0.48f,0.66f,0.94f),float3(0.64f,0.76f,0.96f),saturate(sunColor.b));
}
float3 SunfallHorizon(float3 sunColor,float sunIntensity) {
    return lerp(float3(1.0f,0.92f,0.76f),sunColor,0.30f)*(1.0f+0.18f*min(sunIntensity,3.5f));
}
float3 SunfallSkyRadiance(float3 direction,float3 toSun,float3 sunColor,float sunIntensity) {
    float facing=saturate(dot(direction,toSun));
    float horizon=exp(-abs(direction.y)*3.8f);
    float3 sky=lerp(SunfallZenith(sunColor),SunfallHorizon(sunColor,sunIntensity),horizon);
    float halo=pow(facing,10.0f)*0.32f+pow(facing,85.0f)*0.55f;
    float disk=pow(facing,1800.0f)*2.2f;
    return sky+lerp(sunColor,float3(1.0f,0.96f,0.85f),0.40f)*(halo+disk)*min(sunIntensity,3.5f);
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
