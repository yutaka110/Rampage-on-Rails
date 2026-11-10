// Seeded billows, with dense shaded centres and broken translucent edges.
#include "RailEnergyShield.hlsli"
struct PixelInput {
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
    float4 color : COLOR0;
};
float Hash(float2 p) { return frac(sin(dot(p,float2(127.1f,311.7f)))*43758.5453f); }
float Noise(float2 p) {
    float2 cell=floor(p), f=frac(p); f=f*f*(3.0f-2.0f*f);
    return lerp(lerp(Hash(cell),Hash(cell+float2(1,0)),f.x),
        lerp(Hash(cell+float2(0,1)),Hash(cell+float2(1,1)),f.x),f.y);
}
float Fbm(float2 p) {
    return Noise(p)*0.5f+Noise(p*2.07f+7.1f)*0.28f+Noise(p*4.13f+17.3f)*0.15f+Noise(p*8.3f)*0.07f;
}
float4 main(PixelInput input) : SV_TARGET0 {
    if(input.texcoord.x>=3200.0f && input.texcoord.x<=3201.0f) {
        float2 p=float2((input.texcoord.x-3200.5f)*2,frac(input.texcoord.y*.5f)*4-1);
        float packed=floor(input.texcoord.y*.5f);
        float phase=fmod(packed,32)/31,seed=floor(packed/32)*6.7f;
        float y=.5f-.5f*p.y;
        float2 flow=float2(p.x*4.2f,y*20-phase*6)+seed;
        float turbulence=Fbm(flow),fine=Noise(flow*2.7f+4.5f);
        float center=.10f*sin(y*27.0f+seed)+.14f*(Noise(float2(y*8,phase*2)+seed)-.5f);
        float edge=abs(p.x-center)+.48f*(turbulence-.5f);
        float cover=(1-smoothstep(.22f,.90f,edge))*(1-smoothstep(.76f,1,y));
        float rim=smoothstep(.49f,.83f,turbulence)*fine;
        float3 color=lerp(input.color.rgb,float3(.26f,.032f,.015f),rim*(1-phase)*.72f);
        return float4(color,cover*(.42f+.70f*turbulence)*input.color.a);
    }
    if(input.texcoord.x>=3000 && input.texcoord.x<3002)return RenderRailEnergyShieldBacking(input.texcoord,input.color);
    if(input.texcoord.x>=3100 && input.texcoord.x<=3101) {
        float2 p=float2((input.texcoord.x-3100.5f)*2,input.texcoord.y*2-1);
        float body=max(abs(p.x)-.24f,abs(p.y-.06f)-.76f);
        float pod=max(abs(abs(p.x)-.54f)-.23f,abs(p.y+.11f)-.45f);
        float crossbeam=max(abs(p.x)-.67f,abs(p.y+.09f)-.14f);
        float edge=min(body,min(pod,crossbeam));
        float softness=max(.13f,fwidth(edge));
        return float4(input.color.rgb,input.color.a*(1-smoothstep(-softness,softness,edge)));
    }
    if(input.texcoord.x>=4000.0f) {
        float2 p=float2((input.texcoord.x-4000.5f)*2,input.texcoord.y*2-1);
        float edge=max(abs(p.x)*0.82f+abs(p.y)*0.36f,abs(p.y)*0.94f)+0.12f*(p.x+p.y);
        float facet=step(p.y,p.x*0.45f-0.12f);
        float bevel=exp(-pow(edge-0.78f,2)*150)*0.6f;
        float3 color=input.color.rgb*(0.55f+facet*0.80f)+float3(0.35f,0.29f,0.19f)*bevel;
        return float4(color,(1-smoothstep(0.84f,1.0f,edge))*input.color.a);
    }
    if(input.texcoord.x>=2100.0f && input.texcoord.x<2400.0f) {
        // Weapon-specific solid bodies retain the exact spherical depth and
        // interception silhouette. Saturated edges survive pale hull/sky.
        float mode=floor((input.texcoord.x-2000.0f)/100.0f);
        float baseX=2000.0f+mode*100.0f;
        float2 p=float2((input.texcoord.x-baseX-0.5f)*2,frac(input.texcoord.y*0.5f)*4-1);
        float r=length(p),aa=max(fwidth(r),.008f);
        float packed=floor(input.texcoord.y*.5f);
        float clock=fmod(packed,64)/64*6.2831853f,seed=floor(packed/64)*1.713f;
        float coverage=1-smoothstep(1-aa,1+aa,r);
        if(mode<1.5f) {
            // Stable yellow-white seed with an orange shoulder; no rotating
            // dark channels or large halo obscuring the next rapid round.
            float hot=exp(-dot(p,p)*12);
            float shoulder=exp(-dot(p,p)*3);
            float3 color=input.color.rgb*(.60f+.50f*shoulder);
            color=lerp(color,float3(3.1f,2.65f,1.35f),hot);
            return float4(color,coverage*input.color.a);
        }
        float angle=atan2(p.y,p.x),cracked=step(2.5f,mode);
        float grain=Noise(p*4+float2(cos(clock),sin(clock))+seed);
        float core=exp(-dot(p,p)*lerp(22,12,cracked));
        float band=exp(-pow((r-.81f)/.065f,2));
        float plates=.65f+.35f*smoothstep(.10f,.32f,abs(sin(angle*3+seed)));
        float crack=exp(-pow(p.x+.17f*sin(p.y*7+seed)-.06f,2)*650)*cracked;
        crack*=smoothstep(.2f,.8f,r);
        float pulse=.94f+.06f*sin(clock*2+seed);
        float3 color=input.color.rgb*(.36f+.34f*(1-r)+band*plates*.65f);
        color+=input.color.rgb*grain*.12f;
        color=lerp(color,float3(3.1f,2.65f,3.05f)*pulse,saturate(core+crack*.80f));
        // The damaged shell loses complete bands, exposing bright fissures.
        float gaps=lerp(1,smoothstep(.15f,.55f,sin(angle*3+seed)),cracked);
        color+=input.color.rgb*band*plates*gaps*.35f;
        return float4(color,coverage*input.color.a);
    }
    if(input.texcoord.x>=2400.0f && input.texcoord.x<=2401.0f) {
        float packed=floor(input.texcoord.y*.5f);
        float clock=fmod(packed,64.0f)/64.0f*6.2831853f;
        float seed=floor(packed/64.0f)*2.713f;
        float2 p=float2((input.texcoord.x-2400.5f)*2,frac(input.texcoord.y*.5f)*4-1);
        float2 drift=float2(cos(clock),sin(clock));
        float broad=Noise(p*3.4f+drift*.7f+seed);
        float fine=Noise(p*9.0f-drift*1.1f+seed);
        float r=length(p),angle=atan2(p.y,p.x);
        float shape=r+.12f*sin(angle*5+clock*2+seed)+.13f*(broad-.5f);
        float aa=max(fwidth(shape),.014f);
        float coverage=1-smoothstep(.82f-aa,.94f+aa,shape);
        float seam=p.y+.24f*sin(p.x*5+clock*2)+.30f*(broad-.5f);
        float hot=exp(-seam*seam*19)*(1-smoothstep(.26f,.91f,shape));
        float shell=exp(-pow((shape-.73f)*9,2))*(.45f+.55f*fine);
        float3 plasma=input.color.rgb*(.32f+1.10f*broad+1.3f*shell);
        plasma+=float3(2.15f,.24f,.18f)*hot*(.64f+.36f*fine);
        float pulse=floor(clock*2.55f);
        float arcRadius=.70f+.12f*(Noise(float2(angle*5.0f,pulse+seed))-.5f);
        float arcWidth=max(.021f,fwidth(r)*.72f);
        float arc=exp(-pow((r-arcRadius)/arcWidth,2));
        float breaks=smoothstep(.03f,.62f,sin(angle*3+seed+pulse*.74f));
        plasma+=float3(2.2f,.13f,.12f)*arc*breaks;
        return float4(plasma,coverage*input.color.a);
    }
    if(input.texcoord.x>=2000.0f) {
        // Dense plasma, not a lit plastic sphere. Broad dark channels and a
        // torn hot seam remain visible at the actual 14-20px target size.
        float2 p=float2((input.texcoord.x-2000.5f)*2.0f,frac(input.texcoord.y*0.5f)*4.0f-1.0f);
        float r=length(p);
        float packed=floor(input.texcoord.y*0.5f);
        float clock=fmod(packed,64.0f)/64.0f*6.2831853f;
        float seed=floor(packed/64.0f)*1.713f;
        float angle=seed*0.7f+0.14f*sin(clock);
        float2 q=float2(p.x*cos(angle)-p.y*sin(angle),p.x*sin(angle)+p.y*cos(angle));
        float2 drift=float2(cos(clock+seed),sin(clock+seed));
        float coarse=Noise(q*2.6f+drift*0.65f+seed);
        float fine=Noise(q*5.7f-drift*0.45f+seed*1.3f);
        float swirl=q.y+0.24f*sin(q.x*3.8f+clock+seed)+(coarse-0.5f)*0.24f;
        float ribbon=exp(-swirl*swirl*28.0f)*(1-smoothstep(0.50f,0.95f,abs(q.x)));
        float density=saturate(coarse*0.95f+fine*0.30f+(1-r)*0.16f);
        float3 color=lerp(float3(0.14f,0.012f,0.003f),input.color.rgb*(0.55f+1.25f*density),smoothstep(0.23f,0.80f,density));
        color+=float3(1.2f,0.42f,0.025f)*ribbon*(0.3f+0.7f*coarse);
        // Small ragged slit instead of the former perfectly round white dot.
        float hot=exp(-swirl*swirl*210.0f)*(1-smoothstep(0.16f,0.48f,abs(q.x)));
        hot*=smoothstep(0.30f,0.60f,coarse+fine*0.25f);
        color=lerp(color,float3(3.5f,2.5f,0.95f),hot);
        // A red density edge anchors the aimable body on bright backgrounds.
        // The additive pass supplies broken flowing arcs over this edge.
        float rim=smoothstep(0.82f,0.98f,r);
        color=lerp(color,float3(0.09f,0.010f,0.003f),rim*0.83f);
        float aa=max(fwidth(r)*1.15f,0.012f);
        float coverage=1-smoothstep(1-aa,1+aa,r);
        return float4(color,input.color.a*coverage);
    }
    // Even UV offsets carry a stable puff seed without another vertex stream.
    float seed=floor(input.texcoord.x*0.5f)*2.0f;
    float2 uv=float2(input.texcoord.x-seed,input.texcoord.y);
    float2 p=uv*2.0f-1.0f;
    // Negative offsets mark angular grains using the same depth-tested pass.
    if(seed<0.0f) {
        if(seed<=-10.0f) {
            // Irregular, non-emissive ground grit. Random dimensions and
            // orientation come from fixed world geometry, not screen drift.
            float2 offset=float2(abs(seed)*.37f,abs(seed)*.71f);
            float2 warped=p+float2(Noise(p*3.1f+offset),Noise(p*3.7f+offset+17))*.32f-.16f;
            float edge=length(warped*float2(1.12f,.94f))+.20f*(Noise(p*5+offset)-.5f);
            float coverage=1-smoothstep(.30f,.95f,edge);
            return float4(input.color.rgb*(.65f+.25f*Noise(p*4+offset)),input.color.a*coverage);
        }
        if(seed<-3.0f) {
            float edge=length(p*float2(1.12f,0.92f));
            float coverage=1.0f-smoothstep(0.25f,0.95f,edge);
            return float4(input.color.rgb*(0.76f+0.24f*(1.0f-uv.y)),input.color.a*coverage);
        }
        float edge=max(abs(p.x),abs(p.y))+0.18f*(p.x+p.y);
        float coverage=1.0f-smoothstep(0.65f,1.0f,edge);
        return float4(input.color.rgb*(0.72f+0.30f*(1.0f-uv.y)),input.color.a*coverage);
    }
    float2 offset=float2(seed*1.37f,seed*0.73f);
    if(seed>=1000.0f) {
        // Original layered dust shading: irregular rolling lobes, granular
        // edges and self-shadowed centres, instead of an expanding flat disc.
        float2 flow=p+float2(0.23f*Noise(p*3.7f+offset),0.18f*Noise(p*4.1f+offset+13.0f))-0.10f;
        float coarse=Fbm(flow*3.1f+offset);
        float fine=Noise(flow*16.0f+offset*0.7f);
        float edge=length(flow)+0.34f*(coarse-0.5f)+0.06f*(fine-0.5f);
        float coverage=1.0f-smoothstep(0.38f,1.04f,edge);
        float density=coverage*(0.55f+1.85f*coarse);
        float alpha=1.0f-exp(-density*input.color.a*2.0f);
        float lighting=0.52f+0.28f*(1.0f-uv.y)+0.25f*coarse;
        lighting*=1.0f-0.16f*coverage*coarse;
        return float4(input.color.rgb*lighting,alpha);
    }
    float2 warped=p+0.38f*float2(Noise(p*3.0f+offset),Noise(p*3.0f+offset+19.0f))-0.19f;
    float density=Fbm(warped*3.2f+offset);
    float edge=length(warped)+0.40f*(density-0.5f);
    float coverage=1.0f-smoothstep(0.36f,1.02f,edge);
    if(seed>=700.0f && seed<800.0f) {
        // Fault soot is thin and non-luminous. Fade the complete quad boundary
        // as well as the noisy lobe so overlapping puffs never reveal cards.
        float border=(1-smoothstep(0.72f,0.98f,abs(p.x)))*(1-smoothstep(0.72f,0.98f,abs(p.y)));
        float alpha=1-exp(-coverage*border*(0.45f+1.25f*density)*input.color.a*1.6f);
        float lighting=0.55f+0.25f*(1-uv.y)+0.20f*density;
        return float4(input.color.rgb*lighting,alpha);
    }
    float opticalDepth=coverage*(0.40f+2.0f*density)*input.color.a;
    float alpha=1.0f-exp(-opticalDepth*2.1f);
    float lighting=0.46f+0.32f*(1.0f-uv.y)+0.52f*density;
    return float4(input.color.rgb*lighting,alpha);
}
