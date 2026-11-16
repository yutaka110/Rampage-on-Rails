float RailGatlingWaveBand(float offset,float width) {
    float scaled=offset/width;
    return exp2(-scaled*scaled);
}

float RailGatlingWrappedTurns(float turns) {
    return frac(turns+.5f)-.5f;
}

// The atlas vertex supplies phase, direction in turns, layer and opacity.
// Layers are idle arcs (0), contact fronts (1) and the completion wave (2).
float4 RailGatlingRing(float2 uv,float4 material) {
    float2 p=float2(uv.x-46.5f,uv.y-.5f)*2.0f;
    float radius=length(p);
    float aa=max(fwidth(radius),.001f);
    float phase=material.x;
    float turns=atan2(p.y,p.x+1e-6f)*.1591549431f;
    float angularDistance=abs(RailGatlingWrappedTurns(turns-material.y));
    if(material.z>1.5f) {
        float front=lerp(.45f,.90f,1.0f-(1.0f-phase)*(1.0f-phase));
        float offset=radius-front;
        float core=1.0f-smoothstep(.010f,.025f+aa,abs(offset));
        float wake=exp2(-abs(offset)*lerp(16.0f,38.0f,step(0.0f,offset)));
        float heading=.65f+.35f*exp2(-angularDistance*angularDistance*32.0f);
        float fade=(1.0f-smoothstep(.12f,.98f,phase))*smoothstep(.37f,.43f,radius);
        float3 color=lerp(float3(.10f,.71f,1.0f),float3(1.04f,1.14f,1.19f),core);
        return float4(color,(core*.75f+wake*.21f)*heading*fade*material.a);
    }

    if(radius<.53f || radius>.98f)return 0;
    // The animated edge starts outside the fixed sight and fits inside its progress track.
    float outerMask=smoothstep(.69f,.723f,radius)*(1.0f-smoothstep(.81f,.85f,radius));
    if(material.z>.5f) {
        float front=angularDistance-.31f*phase;
        float spike=RailGatlingWaveBand(front,.024f);
        float wake=RailGatlingWaveBand(front+.047f,.061f);
        float fade=1.0f-smoothstep(.20f,1.0f,phase);
        float waveRadius=.752f+(.063f*spike-.016f*wake)*fade;
        float offset=radius-waveRadius;
        float filament=RailGatlingWaveBand(offset,max(.014f,aa*.62f));
        float glow=RailGatlingWaveBand(offset,.048f);
        float light=fade*(.78f*spike+.28f*wake);
        float3 color=lerp(float3(.11f,.75f,1.0f),float3(1.04f,1.14f,1.22f),filament*spike);
        return float4(color,(filament*.66f+glow*.21f)*saturate(light)*outerMask*material.a);
    }

    // Fold the four fixed arcs into one quadrant for rounded ends and open gaps.
    float2 quadrant=abs(p);
    float angle=clamp(atan2(quadrant.y,quadrant.x),.18f,1.570796327f-.18f);
    float2 closest=float2(cos(angle),sin(angle))*.68f;
    float d=length(quadrant-closest);
    float core=1.0f-smoothstep(.010f,.022f+aa,d);
    float sheath=exp2(-d*d*2000.0f);
    float cutoff=smoothstep(.53f,.57f,radius)*(1.0f-smoothstep(.90f,.98f,radius));
    float sightAlpha=saturate(core*.68f+sheath*.27f)*cutoff;
    float3 sightColor=lerp(float3(.19f,.72f,.92f),float3(.88f,.99f,1.06f),core);

    // A broad crest, a shallow trailing trough and a smaller crest travel around the sight.
    float travel=turns-phase;
    float swell=RailGatlingWaveBand(RailGatlingWrappedTurns(travel),.108f);
    float trough=RailGatlingWaveBand(RailGatlingWrappedTurns(travel+.15f),.084f);
    float echo=RailGatlingWaveBand(RailGatlingWrappedTurns(travel-.47f),.115f);
    float offset=radius-(.752f+.052f*swell-.023f*trough+.027f*echo);
    float filament=RailGatlingWaveBand(offset,max(.016f,aa*.58f));
    float glow=RailGatlingWaveBand(offset,.042f);
    float waveLight=.38f+.62f*swell+.28f*echo;
    float waveAlpha=(filament*.51f+glow*.14f)*waveLight*outerMask;
    float3 waveColor=lerp(float3(.13f,.73f,1.0f),float3(.73f,.96f,1.09f),filament*waveLight);

    // Put the fixed sight above the travelling glow and its dark contrast edge.
    float separation=exp2(-d*d*950.0f)*.25f*cutoff;
    float waveWeight=waveAlpha*(1.0f-sightAlpha);
    float edgeWeight=separation*(1.0f-sightAlpha)*(1.0f-waveAlpha);
    float alpha=sightAlpha+waveWeight+edgeWeight;
    float3 color=(sightColor*sightAlpha+waveColor*waveWeight+float3(.015f,.035f,.045f)*edgeWeight)/max(alpha,.00001f);
    return float4(color,alpha*material.a);
}

// Opening intensity, signed contact intensity (left/right), and counter window visibility.
float4 RailBatteryCounterArc(float2 uv,float4 material) {
    float2 p=(uv-float2(52.5f,.5f))*2.0f;
    float radius=length(p);
    float aa=max(fwidth(radius),.001f);
    if(radius<.54f || radius>.94f)return 0;
    float2 side=float2(abs(p.x),p.y);
    float angle=atan2(side.y,side.x);
    float end=clamp(angle,-1.02f,1.02f);
    float d=length(side-.72f*float2(cos(end),sin(end)));
    float contact=saturate(p.x<0 ? -material.y : material.y);
    float visibility=max(material.z,contact);
    float flash=max(material.x,contact);
    float core=RailGatlingWaveBand(d,max(.013f,aa*.55f));
    float halo=RailGatlingWaveBand(d,.043f);
    float taper=1.0f-.31f*smoothstep(.73f,1.03f,abs(angle));
    float rim=core*(.88f+.12f*flash)+halo*(.13f+.22f*flash);
    float lightAlpha=saturate(rim)*taper*visibility;
    float3 gold=lerp(float3(.96f,.68f,.28f),float3(1.0f,.89f,.60f),core);
    float3 struckGold=lerp(gold,float3(1.10f,.94f,.58f),contact);
    float3 color=lerp(struckGold,float3(1.10f,1.04f,.90f),material.x);
    float edgeAlpha=RailGatlingWaveBand(d,.067f)*.55f*taper*visibility*(1.0f-lightAlpha);
    float alpha=lightAlpha+edgeAlpha;
    color=(color*lightAlpha+float3(.055f,.038f,.017f)*edgeAlpha)/max(alpha,.00001f);
    return float4(color,alpha);
}
