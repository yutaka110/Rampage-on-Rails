// Four circular arcs with round luminous ends. Fold all quadrants onto one
// quarter and find the closest point on its arc, leaving the cardinal gaps open.
float4 RailGatlingRing(float2 uv,float4 energy) {
    float2 p=float2(uv.x-46.5f,uv.y-.5f)*2.0f;
    float radius=length(p);
    if(radius<.53f || radius>.98f)return 0;
    float2 quadrant=abs(p);
    float angle=clamp(atan2(quadrant.y,quadrant.x),.18f,1.570796327f-.18f);
    float2 closest=float2(cos(angle),sin(angle))*.68f;
    float d=length(quadrant-closest);
    float aa=max(fwidth(radius),.001f);
    float core=1.0f-smoothstep(.010f,.022f+aa,d);
    float sheath=exp2(-d*d*2000.0f);
    float halo=exp2(-d*d*130.0f);
    float cutoff=smoothstep(.53f,.57f,radius)*(1.0f-smoothstep(.90f,.98f,radius));
    float lightAlpha=saturate(halo*.30f+sheath*.62f+core*.8f)*cutoff*energy.a;
    float3 color=lerp(energy.rgb,float3(1.12f,1.20f,1.24f),core);
    // A continuous faint separation edge supports pale hulls without a thick
    // polygonal black outline. Composite it under the light in straight alpha.
    float separation=exp2(-d*d*950.0f)*.25f*cutoff*energy.a;
    float alpha=lightAlpha+separation*(1.0f-lightAlpha);
    color=(color*lightAlpha+float3(.015f,.035f,.045f)*separation*(1.0f-lightAlpha))/max(alpha,.00001f);
    return float4(color,alpha);
}
