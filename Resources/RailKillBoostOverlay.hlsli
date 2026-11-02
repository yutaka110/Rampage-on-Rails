// Procedural light around the display perimeter. The middle 68% of each
// dimension stays fully clear, including the reticle and the combat lane.
float4 RailKillBoostOverlay(float2 atlasUv,float4 state) {
    float2 p=(atlasUv-float2(48,0))*2-1;
    float edge=max(abs(p.x),abs(p.y));
    if(edge<=.68f)return float4(0,0,0,0);
    float strength=saturate(state.y),ignition=saturate(state.z),age=max(0,state.x);
    float rim=smoothstep(.68f,1.0f,edge);
    float angle=atan2(p.y,p.x);
    float flow=.5f+.5f*sin(angle*11+age*1.9f+sin(angle*5-age*.8f));
    flow=flow*flow;
    float filament=pow(saturate(.5f+.5f*sin(angle*23+edge*19-age*3.2f)),12);
    float alpha=rim*rim*strength*(.035f+.055f*flow+.045f*filament+.025f*ignition);
    float3 color=lerp(float3(.12f,1.0f,.48f),float3(.20f,.88f,1.0f),flow);
    return float4(color,min(.16f,alpha));
}
