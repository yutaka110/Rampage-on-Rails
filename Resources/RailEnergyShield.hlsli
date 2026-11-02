// Alpha pass draws a dark keyline; the additive pass supplies localized light.
float4 RenderRailEnergyShieldBacking(float2 uv,float4 color) {
    float2 p=float2((uv.x-3000.5f)*2,uv.y*2-1);
    float d=max(abs(p.y),abs(p.x)+.28f*abs(p.y));
    float rim=exp(-pow((d-.955f)/max(.062f,fwidth(d)*1.65f),2));
    return float4(color.rgb,(.075f+rim*.80f)*color.a);
}
// Reserved UV bands: membrane, emitters, local impacts, fracture, guard, splinters.
float4 RenderRailEnergyShield(float2 uv,float4 color) {
    float band=floor((uv.x-64.0f)*.5f)*2.0f+64.0f;
    float2 p=float2((uv.x-band-.5f)*2,frac(uv.y*.5f)*4-1);
    float packed=floor(uv.y*.5f);
    if(band==76) {
        float along=saturate(uv.x-76),across=uv.y*2-1;
        float ends=smoothstep(0,.18f,along)*(1-smoothstep(.65f,1,along));
        return float4(color.rgb,exp(-across*across*3.5f)*ends*color.a);
    }
    if(band==72) {
        float phase=fmod(packed,32)/31;
        float2 origin=color.rg*2-1;
        float r=length((p-origin)*float2(.92f,1));
        float radius=.08f+phase*2.8f;
        float wave=exp(-pow((r-radius)/max(.085f,fwidth(r)*1.3f),2));
        float d=max(abs(p.y),abs(p.x)+.28f*abs(p.y));
        float rim=exp(-pow((d-.975f)/max(.025f,fwidth(d)),2));
        float flash=exp(-r*r*30)*exp(-phase*28);
        float rimPulse=smoothstep(.02f,.12f,phase)*(1-smoothstep(.28f,.72f,phase));
        return float4(lerp(float3(.10f,.85f,1.5f),float3(2.4f,2.8f,3),flash),
            (wave*.65f+rim*rimPulse*.72f+flash*.85f)*color.a);
    }
    if(band==66) {
        return float4(color.rgb,exp(-p.x*p.x*2)*(1-smoothstep(.80f,1,abs(p.y)))*color.a);
    }
    if(band==68) {
        float phase=fmod(packed,32)/31;
        float r=length(p),radius=.16f+.64f*sqrt(phase);
        float width=max(.045f,fwidth(r));
        float ring=exp(-pow((r-radius)/width,2));
        float flash=exp(-r*r*28)*exp(-phase*19);
        float broken=.70f+.30f*sin(atan2(p.y,p.x)*5+phase*8);
        return float4(lerp(color.rgb,float3(2.3f,2.7f,3),flash),
            (ring*broken*.8f+flash)*(1-smoothstep(.88f,1,r))*color.a);
    }
    float d=max(abs(p.y),abs(p.x)+.28f*abs(p.y));
    float rim=exp(-pow((d-.975f)/max(.025f,fwidth(d)*.90f),2));
    float2 q=p*float2(5.4f,4.3f),tile=float2(1,1.7320508f);
    float2 a=frac(q/tile)*tile-.5f*tile;
    float2 b=frac((q-.5f*tile)/tile)*tile-.5f*tile;
    float2 cell=dot(a,a)<dot(b,b)?a:b;
    float hex=max(abs(cell.x),dot(abs(cell),float2(.5f,.8660254f)));
    float gridLine=1-smoothstep(.012f,.012f+max(.012f,fwidth(hex)),abs(hex-.5f));
    float clock=fmod(packed,64)/64*6.2831853f;
    float scan=exp(-pow(p.y-.75f*sin(clock),2)*100)*.018f;
    float opacity=.018f+gridLine*.028f+rim*.92f+scan;
    if(band==70) {
        float phase=fmod(packed,32)/31;
        float noise=frac(sin(dot(floor(p*19),float2(127.1f,311.7f)))*43758.5453f);
        opacity=(rim*.85f+gridLine*.28f+.10f)*smoothstep(phase-.08f,phase+.08f,noise);
        opacity+=exp(-phase*32)*.55f;
    }
    return float4(color.rgb*(1+rim*.45f),opacity*color.a);
}
