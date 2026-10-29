// Emissive material for the existing warning strokes. No time or geometry
// changes here: gameplay still owns charge, acquisition and fire timing.
float4 RailChargeStroke(float2 uv, float4 energy) {
    float along=saturate(uv.x-44.0f);
    float across=uv.y*2.0f-1.0f;
    float ends=smoothstep(0.0f,0.09f,along)*(1.0f-smoothstep(0.91f,1.0f,along));
    float halo=exp(-across*across*5.5f);
    float sheath=exp(-across*across*36.0f);
    float core=exp(-across*across*360.0f);
    // The thin hot center is yellow-white, surrounded by saturated orange
    // and a wide transparent red shoulder. No dark keyline or flat border.
    float3 hot=max(energy.rgb*1.35f,float3(1.35f,1.06f,0.48f));
    float3 color=lerp(energy.rgb*float3(0.90f,0.75f,0.65f),energy.rgb,sheath);
    color=lerp(color,hot,core);
    float alpha=saturate(halo*0.20f+sheath*0.76f+core*0.74f)*ends*energy.a;
    return float4(color,alpha);
}
