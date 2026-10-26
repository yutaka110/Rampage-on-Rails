// Depth-tested world ribbons and readable projectile heads. Impact flashes retain their own profile.
struct PixelInput {
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
    float4 color : COLOR0;
};
float ImpactNoise(float2 p) {
    float2 cell=floor(p),f=frac(p);f=f*f*(3.0f-2.0f*f);
    float4 n=frac(sin(float4(dot(cell,float2(127.1f,311.7f)),
        dot(cell+float2(1,0),float2(127.1f,311.7f)),
        dot(cell+float2(0,1),float2(127.1f,311.7f)),
        dot(cell+float2(1,1),float2(127.1f,311.7f))))*43758.5453f);
    return lerp(lerp(n.x,n.y,f.x),lerp(n.z,n.w,f.x),f.y);
}
// UV.y packs a 32-step dissolve clock and a deterministic shape variant.
// Geometry and simulation own time; the shader never reads wall-clock time.
void ImpactCoordinates(float2 uv,float baseX,out float2 p,out float phase,out float seed) {
    float packed=floor(uv.y*0.5f);
    phase=fmod(packed,32.0f)/31.0f;
    seed=floor(packed/32.0f)*2.713f;
    p=float2((uv.x-baseX-0.5f)*2.0f,frac(uv.y*0.5f)*4.0f-1.0f);
}
float4 main(PixelInput input) : SV_TARGET0 {
    if(input.texcoord.x>41.5f) {
        // Thin world-space near-ground streak, with no round projectile head.
        float u=saturate(input.texcoord.x-42),v=input.texcoord.y*2-1;
        float ends=smoothstep(0,0.18f,u)*(1-smoothstep(0.72f,1,u));
        return float4(input.color.rgb,exp(-v*v*7)*ends*input.color.a);
    }
    if(input.texcoord.x>39.5f) {
        // Original turbulent underbody plume: anchored hot root, longitudinal
        // fibers and a broken fading tail. Simulation time pauses with play.
        float u=saturate(input.texcoord.x-40);
        float across=frac(input.texcoord.y*0.5f)*4-1;
        float clock=fmod(floor(input.texcoord.y*0.5f),64)/64*6.2831853f;
        float wave=sin(u*19-clock*4)*0.075f+sin(u*31+clock*3)*0.035f;
        float noise=ImpactNoise(float2(u*17+cos(clock)*3,across*4+sin(clock)*2));
        // The head is finite-width and luminous all the way to the metal
        // lip. Only the tail breaks up; the root must never dissolve away.
        float width=lerp(0.85f,0.52f,u);
        float edge=exp(-pow((across+wave)/width,2)*4);
        float core=exp(-pow((across-wave*0.5f)/width,2)*38);
        float tail=smoothstep(0,0.30f,u);
        float diamonds=0.80f+0.20f*pow(0.5f+0.5f*cos(u*35-clock*2),4);
        float root=exp(-pow((u-0.88f)/0.17f,2));
        float fibers=lerp(0.55f+0.45f*noise,1.0f,smoothstep(0.70f,1.0f,u));
        float3 color=lerp(input.color.rgb,float3(2.8f,3.0f,3.1f),core*(0.28f+u*0.55f));
        return float4(color,(edge*0.52f+core*(0.48f+root*0.20f))*tail*fibers*diamonds*input.color.a);
    }
    if(input.texcoord.x>37.5f) {
        // A concentrated spear of light with ragged secondary filaments.
        // Tapered ends and empty shoulders keep the enemy silhouette visible.
        float along=(input.texcoord.x-38.5f)*2,across=(input.texcoord.y-0.5f)*2;
        float taper=pow(saturate(1-abs(along)),0.55f);
        float width=0.08f+0.92f*taper;
        float core=exp(-pow(across/width,2)*24);
        float fringe=ImpactNoise(float2(along*15,across*4)+11.3f);
        float fibers=exp(-pow((abs(across)-0.32f*width)/width,2)*75)*fringe*0.22f;
        float halo=exp(-pow(across/width,2)*3.5f)*0.17f;
        float ends=1-smoothstep(0.84f,1,abs(along));
        return float4(lerp(input.color.rgb,float3(3.8f,4.0f,4.2f),core*0.88f),
            (core+halo+fibers)*ends*taper*input.color.a);
    }
    if(input.texcoord.x>35.5f) {
        // A torn, oblique shock sheet: unequal fronts with negative space,
        // followed by erosion. Avoid a complete HUD-like circular ring.
        float2 p;float phase,seed;
        ImpactCoordinates(input.texcoord,36.0f,p,phase,seed);
        float noise=ImpactNoise(p*5+seed+phase*0.8f);
        float2 q=float2(p.x,p.y+0.10f*sin(p.x*5+seed));
        float r=length(q*float2(0.94f,1.25f)),angle=atan2(q.y,q.x);
        float frontRadius=0.18f+0.60f*sqrt(phase)+0.055f*sin(angle*3+seed);
        float width=max(0.025f+0.055f*(1-phase),fwidth(r)*0.8f);
        float front=exp(-pow((r-frontRadius)/width,2));
        float broken=smoothstep(0.15f,0.67f,noise+0.16f*sin(angle*2+seed));
        float wake=exp(-pow((r-frontRadius+0.08f)/0.16f,2))*0.23f;
        float spine=exp(-q.y*q.y*180)*(1-smoothstep(0.25f,0.88f,abs(q.x)))*(1-phase)*0.28f;
        float alpha=((front+wake)*broken+spine)*(1-smoothstep(0.90f,1.0f,r))*input.color.a;
        float3 color=lerp(input.color.rgb,float3(2.8f,3.2f,3.5f),front*broken*0.65f);
        return float4(color,alpha);
    }
    if(input.texcoord.x>33.5f) {
        float2 p=float2((input.texcoord.x-34.5f)*2.0f,frac(input.texcoord.y*0.5f)*4.0f-1.0f);
        float packed=floor(input.texcoord.y*0.5f);
        float clock=fmod(packed,64.0f)/64.0f*6.2831853f,seed=floor(packed/64.0f)*1.713f;
        float r=length(p),angle=atan2(p.y,p.x);
        float noise=ImpactNoise(p*4+float2(cos(clock),sin(clock))+seed);
        float radius=0.73f+0.07f*sin(angle*3+clock+seed)+0.025f*sin(angle*7-clock);
        float width=max(0.035f+0.022f*noise,fwidth(r)*0.75f);
        float filament=exp(-(r-radius)*(r-radius)/(width*width));
        float broken=smoothstep(-0.12f,0.64f,sin(angle*2+clock+seed)+0.22f*sin(angle*5-seed));
        float shoulder=exp(-(r-radius)*(r-radius)*80.0f)*0.18f;
        float opacity=(filament+shoulder)*broken*(1-smoothstep(0.88f,1.0f,r));
        return float4(lerp(input.color.rgb,float3(2.4f,1.15f,0.20f),filament*0.40f),opacity*input.color.a);
    }
    if(input.texcoord.x>31.5f) {
        float along=saturate(input.texcoord.x-32.0f),across=input.texcoord.y*2-1;
        float width=lerp(0.08f,0.85f,along);
        float edge=exp(-across*across/(width*width)*4.5f);
        float fade=smoothstep(0.0f,0.30f,along)*(1-smoothstep(0.88f,1.0f,along));
        return float4(input.color.rgb,edge*fade*input.color.a);
    }
    if(input.texcoord.x>29.5f) {
        // Internal core rupture: a white-hot seed expands into turbulent,
        // cooling lobes. Geometry supplies staggered births and world drift.
        float2 p;float phase,seed;
        ImpactCoordinates(input.texcoord,30.0f,p,phase,seed);
        float2 flow=p*3.9f+float2(seed,seed*0.61f)-float2(0,phase*1.6f);
        float coarse=ImpactNoise(flow);
        float fine=ImpactNoise(flow*2.7f+seed+phase*2.3f);
        float angle=atan2(p.y,p.x);
        float radius=length(p)+0.15f*sin(angle*5+seed)+0.07f*cos(angle*9-seed);
        float field=radius+(coarse-0.5f)*0.28f+(fine-0.5f)*0.10f;
        float silhouette=1-smoothstep(0.57f,0.95f,field);
        float density=coarse*0.60f+fine*0.20f+(1-radius)*0.50f;
        float erosion=smoothstep(0.25f,1.0f,phase)*0.94f;
        float matter=smoothstep(erosion-0.12f,erosion+0.16f,density);
        float heat=saturate((1-radius)*0.65f+coarse*0.40f-phase*0.85f);
        float3 color=lerp(float3(1.35f,0.065f,0.008f),float3(3.8f,1.05f,0.09f),smoothstep(0.05f,0.65f,heat));
        float hot=(1-smoothstep(0.12f,0.52f,radius))*(1-smoothstep(0.08f,0.32f,phase));
        color=lerp(color,float3(4.4f,3.7f,2.6f),hot);
        return float4(color*input.color.rgb,silhouette*matter*input.color.a);
    }
    if(input.texcoord.x>27.5f) {
        float along=(input.texcoord.x-28.5f)*2.0f;
        float across=(input.texcoord.y-0.5f)*2.0f;
        float taper=sqrt(saturate(1.0f-along*along));
        float width=0.06f+0.94f*taper;
        float core=exp(-across*across/(width*width)*14.0f);
        float shoulder=exp(-across*across/(width*width)*3.6f)*0.28f;
        float ends=1-smoothstep(0.82f,1.0f,abs(along));
        float3 color=lerp(input.color.rgb,float3(4.0f,3.8f,3.4f),core);
        return float4(color,(core+shoulder)*ends*taper*input.color.a);
    }
    if(input.texcoord.x>25.5f) {
        // Primary rapid-fire dash: a rounded capsule with a white-blue core.
        float2 p=float2(input.texcoord.x-26.5f,input.texcoord.y-0.5f)*2.0f;
        float end=max(0.0f,abs(p.x)-0.72f)/0.28f;
        float d2=end*end+p.y*p.y;
        float edge=1.0f-smoothstep(0.60f,1.0f,sqrt(d2));
        float core=exp(-d2*7.0f);
        return float4(lerp(input.color.rgb,float3(2.5f,2.9f,3.0f),core*0.65f),edge*input.color.a);
    }
    if(input.texcoord.x>23.5f) {
        // Small faceted energy fragments, with a sharp centre and soft edges.
        float2 p=float2(input.texcoord.x-24.5f,input.texcoord.y-0.5f)*2.0f;
        float diamond=abs(p.x)*0.70f+abs(p.y);
        float edge=1-smoothstep(0.55f,1.0f,diamond);
        float core=exp(-dot(p,p)*7.0f);
        return float4(lerp(input.color.rgb,float3(3.6f,3.8f,4.0f),core),edge*input.color.a);
    }
    if(input.texcoord.x>21.5f) {
        float2 p;float phase,seed;
        ImpactCoordinates(input.texcoord,22.0f,p,phase,seed);
        float r=length(p),angle=atan2(p.y,p.x);
        float noise=ImpactNoise(p*5.0f+float2(seed,seed*0.43f));
        float radius=0.22f+0.64f*pow(phase,0.55f);
        radius+=0.035f*sin(angle*5+seed)+0.018f*sin(angle*11-seed);
        float width=lerp(0.075f,0.018f,phase);
        float front=exp(-(r-radius)*(r-radius)/(width*width));
        float gaps=smoothstep(0.30f,0.62f,noise);
        float alpha=front*gaps*(1-smoothstep(0.93f,1.0f,r))*input.color.a;
        return float4(input.color.rgb,alpha);
    }
    if(input.texcoord.x>19.5f) {
        // A lumpy, eroding impact volume rather than a smooth circular glow.
        float2 p;float phase,seed;
        ImpactCoordinates(input.texcoord,20.0f,p,phase,seed);
        float angle=atan2(p.y,p.x);
        float noise=ImpactNoise(p*4.8f+float2(seed,seed*0.73f));
        float fine=ImpactNoise(p*12.0f+seed+phase*1.4f);
        float lobe=0.64f+0.10f*sin(angle*5+seed)+0.06f*cos(angle*9+seed*1.9f);
        float radial=length(p)/lobe;
        float field=radial+(noise-0.5f)*0.24f+fine*0.10f;
        float silhouette=1-smoothstep(0.70f,1.0f,field);
        float erosion=smoothstep(0.28f,1.0f,phase)*1.10f;
        float matter=noise*0.65f+(1-radial)*0.75f;
        float dissolved=smoothstep(erosion-0.14f,erosion+0.10f,matter);
        float core=(1-smoothstep(0.12f,0.60f,radial))*(1-phase);
        float3 color=lerp(input.color.rgb*(0.75f+0.35f*fine),float3(4.8f,4.9f,5.0f),core);
        return float4(color,silhouette*dissolved*input.color.a);
    }
    if(input.texcoord.x>17.5f) {
        // A sharp tapered core-break cut, with a restrained cyan shoulder.
        float along=(input.texcoord.x-18.0f)*2.0f-1.0f;
        float across=input.texcoord.y*2.0f-1.0f;
        float taper=pow(saturate(1.0f-abs(along)),0.58f);
        float width=0.10f+0.90f*taper;
        float core=exp(-across*across/(width*width)*18.0f);
        float halo=exp(-across*across/(width*width)*4.0f)*0.34f;
        float ends=1.0f-smoothstep(0.86f,1.0f,abs(along));
        float3 color=lerp(input.color.rgb,float3(3.0f,3.1f,3.2f),core);
        return float4(color,(core+halo)*ends*taper*input.color.a);
    }
    if(input.texcoord.x>15.5f) {
        float2 p=float2(input.texcoord.x-16.5f,input.texcoord.y-0.5f)*2.0f;
        float glow=exp(-dot(p,p)*4.0f)*(1-smoothstep(0.65f,1.0f,length(p)));
        return float4(input.color.rgb,glow*input.color.a);
    }
    if(input.texcoord.x>13.5f) {
        float across=input.texcoord.y*2.0f-1.0f;
        float glow=exp(-across*across*6.0f)*(1-smoothstep(0.75f,1.0f,abs(across)));
        return float4(input.color.rgb,glow*input.color.a);
    }
    if(input.texcoord.x>11.5f) {
        float along=input.texcoord.x-12.0f;
        float across=input.texcoord.y*2.0f-1.0f;
        float ends=smoothstep(0.0f,0.18f,along)*(1.0f-smoothstep(0.70f,1.0f,along));
        return float4(input.color.rgb,exp(-across*across*5.0f)*ends*input.color.a);
    }
    if(input.texcoord.x>9.5f) {
        float along=input.texcoord.x-10.0f;
        float across=abs(input.texcoord.y*2.0f-1.0f);
        float pulse=smoothstep(0.0f,0.12f,along)*(1.0f-smoothstep(0.55f,1.0f,along));
        float core=exp(-across*across*12.0f);
        float edge=1.0f-smoothstep(0.75f,1.0f,across);
        return float4(lerp(input.color.rgb,float3(2.2f,1.7f,0.90f),core),
            pulse*(0.35f+0.65f*core)*edge*input.color.a);
    }
    if(input.texcoord.x>7.5f) {
        float2 p=float2(input.texcoord.x-8.5f,input.texcoord.y-0.5f)*2.0f;
        float edge=1.0f-smoothstep(0.65f,1.0f,abs(p.x));
        float glow=exp(-p.y*p.y*5.0f)*edge;
        return float4(input.color.rgb,glow*input.color.a);
    }
    if(input.texcoord.x>5.5f) {
        float2 p=float2(input.texcoord.x-6.5f,input.texcoord.y-0.5f)*2.0f;
        float noise=ImpactNoise(p*5.1f+13.7f);
        float radius=length(p)+0.12f*(noise-0.5f);
        float glow=exp(-radius*radius*3.2f)*(1.0f-smoothstep(0.65f,1.0f,radius));
        return float4(input.color.rgb*(0.70f+noise*0.55f),glow*input.color.a);
    }
    if(input.texcoord.x>3.5f) {
        float2 p=float2(input.texcoord.x-4.5f,input.texcoord.y-0.5f)*2.0f;
        float radius=length(p);
        float core=exp(-dot(p,p)*5.5f);
        float halo=exp(-dot(p,p)*2.2f)*(1.0f-smoothstep(0.72f,1.0f,radius));
        float3 color=lerp(input.color.rgb*1.2f,float3(1.7f,1.35f,0.90f),core);
        return float4(color,(halo*0.70f+core*0.65f)*input.color.a);
    }
    if(input.texcoord.x>1.5f) {
        float2 p=float2(input.texcoord.x-2.5f,input.texcoord.y-0.5f)*2.0f;
        float core=exp(-dot(p,p)*12.0f);
        float glow=exp(-dot(p,p)*4.0f)*(1.0f-smoothstep(0.6f,1.0f,length(p)));
        return float4(lerp(input.color.rgb,float3(1.9f,1.55f,1.1f),core),glow*input.color.a);
    }
    float width=lerp(1.0f,0.18f,smoothstep(0.12f,1.0f,input.texcoord.x));
    float across=abs(input.texcoord.y*2.0f-1.0f)/width;
    float head=smoothstep(0.0f,0.065f,input.texcoord.x);
    float tail=pow(saturate(1.0f-input.texcoord.x),0.55f);
    float halo=exp(-across*across*3.5f)*(1.0f-smoothstep(0.65f,1.0f,across));
    float core=exp(-across*across*10.0f);
    float alpha=head*tail*(halo*0.65f+core*0.88f)*input.color.a;
    float3 color=lerp(input.color.rgb,float3(1.4f,1.20f,0.85f),core*0.9f);
    return float4(color,alpha);
}
