// Depth-tested world ribbons and readable projectile heads. Impact flashes retain their own profile.
#include "RailEnergyShield.hlsli"
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
float2 FangOrbitArc(float2 p,float tilt,float clock,float seed) {
    float c=cos(tilt),s=sin(tilt);
    float2 q=float2(dot(p,float2(c,s)),dot(p,float2(-s,c)));
    q.y*=1.40f;
    float angle=atan2(q.y,q.x),r=length(q);
    float tick=floor(clock*3.0f+seed);
    float2 orbit=float2(cos(angle),sin(angle));
    float radius=.71f+.16f*(ImpactNoise(orbit*7.0f+tick*.71f+seed)-.5f);
    float distance=r-radius;
    float width=max(.031f,fwidth(distance)*.72f);
    float head=cos(angle-clock*2.0f+seed);
    float segment=smoothstep(-.38f,.54f,head)*
        (.42f+.58f*smoothstep(-.55f,.32f,sin(angle*6+tick)));
    float core=exp(-pow(distance/width,2))*segment;
    float halo=exp(-pow(distance/.105f,2))*segment;
    float branch=exp(-pow((distance-.11f*sin(angle*8+seed))/width,2))*
        smoothstep(.62f,.96f,head);
    return float2(core+branch*.45f,halo);
}

float4 main(PixelInput input) : SV_TARGET0 {
    if(input.texcoord.x>57.5f && input.texcoord.x<59.5f) {
        // White-hot coherent core surrounded by a turbulent crimson sheath.
        float u=saturate(input.texcoord.x-58);
        float v=frac(input.texcoord.y*.5f)*4-1;
        float phase=fmod(floor(input.texcoord.y*.5f),32)/31;
        float noise=ImpactNoise(float2(u*43-phase*9,v*8+phase*3));
        float core=exp(-v*v*100);
        float sheath=exp(-v*v*5.5f)*(.55f+.45f*noise);
        float filaments=exp(-pow(abs(v)-.26f-.06f*sin(u*57-phase*17),2)*180)*noise;
        float ends=smoothstep(0,.012f,u)*(1-smoothstep(.94f,1,u));
        float3 color=lerp(input.color.rgb,float3(7.0f,6.8f,6.2f),smoothstep(.06f,.70f,core));
        return float4(color,saturate(core+sheath*.64f+filaments*.28f)*ends*input.color.a);
    }
    if(input.texcoord.x>=60.0f && input.texcoord.x<=61.0f) {
        float2 p;float phase,seed;
        ImpactCoordinates(input.texcoord,60,p,phase,seed);
        float radius=length(p);
        float noise=ImpactNoise(p*7+phase*3);
        float halo=exp(-radius*radius*5)*(.65f+.35f*noise);
        float core=exp(-radius*radius*75);
        float rays=(exp(-p.y*p.y*420)*exp(-p.x*p.x*5)+
            exp(-p.x*p.x*500)*exp(-p.y*p.y*8))*.24f;
        float edge=1-smoothstep(.72f,1,radius);
        return float4(lerp(input.color.rgb,float3(7.2f,7.0f,6.5f),core),
            saturate(halo*.6f+core+rays)*edge*input.color.a);
    }
    if(input.texcoord.x>=62.0f && input.texcoord.x<=63.0f) {
        float2 p;float phase,seed;
        ImpactCoordinates(input.texcoord,62,p,phase,seed);
        float r=length(p),angle=atan2(p.y,p.x);
        float front=.15f+.68f*sqrt(phase);
        float width=max(.018f+.055f*(1-phase),fwidth(r));
        float ring=exp(-pow((r-front)/width,2));
        float fragments=.35f+.65f*ImpactNoise(p*11+seed);
        float spokes=pow(saturate(sin(angle*13+seed)),18)*exp(-pow((r-front)/.19f,2))*.35f;
        return float4(input.color.rgb,(ring*fragments+spokes)*(1-smoothstep(.90f,1,r))*input.color.a);
    }
    if(input.texcoord.x>63.5f)return RenderRailEnergyShield(input.texcoord,input.color);
    if(input.texcoord.x>=56.0f && input.texcoord.x<=57.0f) {
        float2 p=float2((input.texcoord.x-56.5f)*2,frac(input.texcoord.y*.5f)*4-1);
        float packed=floor(input.texcoord.y*.5f);
        float clock=fmod(packed,64.0f)/64.0f*6.2831853f;
        float seed=floor(packed/64.0f)*1.73f;
        float2 arc=FangOrbitArc(p,clock+seed,clock,seed);
        arc+=FangOrbitArc(p,-clock*.5f+seed+1.42f,-clock,seed+3.5f)*.72f;
        float3 color=lerp(input.color.rgb,float3(3.1f,.54f,.47f),saturate(arc.x));
        return float4(color,saturate(arc.x*.79f+arc.y*.31f)*input.color.a);
    }
    if(input.texcoord.x>=54.0f && input.texcoord.x<=55.0f) {
        float along=input.texcoord.x-54.0f, across=frac(input.texcoord.y*.5f)*4-1;
        float packed=floor(input.texcoord.y*.5f);
        float phase=fmod(packed,32.0f)/31.0f,seed=floor(packed/32.0f)*3.17f;
        float displacement=(ImpactNoise(float2(along*11,seed+phase*6))-.5f)*.90f;
        displacement*=sin(along*3.14159265f);
        float d=across-displacement;
        float core=exp(-d*d*95),halo=exp(-d*d*10)*.23f;
        float branch=d+.43f*sin(along*8+seed);
        float fork=exp(-branch*branch*180)*smoothstep(.12f,.35f,along)*(1-smoothstep(.64f,.88f,along));
        // A gap opens in the arc as the shot pulls free from the muzzle.
        float gap=smoothstep(.35f,.72f,phase)*.22f;
        float intact=smoothstep(gap,gap+.04f,abs(along-.5f));
        float ends=smoothstep(0,.045f,along)*(1-smoothstep(.94f,1,along));
        float3 color=lerp(input.color.rgb,float3(4.6f,.82f,.67f),core*.8f);
        return float4(color,(core+halo+fork*.57f)*ends*lerp(1,intact,smoothstep(.35f,.50f,phase))*input.color.a);
    }
    if(input.texcoord.x>51.5f && input.texcoord.x<53.5f) {
        // Soft rising vent vapor. Geometry carries the paused gameplay age;
        // this shader has no independent clock and no projectile-shaped head.
        float2 p=float2((input.texcoord.x-52.5f)*2,input.texcoord.y*2-1);
        float width=.48f+.14f*(1-p.y);
        float flow=ImpactNoise(p*float2(4,3)+8.7f);
        float edge=exp(-pow((p.x+.10f*sin(p.y*5))/width,2)*3.5f);
        float ends=(1-smoothstep(.55f,1,abs(p.y)));
        return float4(input.color.rgb,edge*ends*(.45f+.55f*flow)*input.color.a);
    }
    if(input.texcoord.x>45.5f && input.texcoord.x<49.5f) {
        bool cracked=input.texcoord.x>47.5f;
        float baseX=cracked?48:46;
        float2 p=float2((input.texcoord.x-baseX-.5f)*2,frac(input.texcoord.y*.5f)*4-1);
        float packed=floor(input.texcoord.y*.5f),clock=fmod(packed,64)/64*6.2831853f;
        float seed=floor(packed/64)*1.713f,r=length(p),angle=atan2(p.y,p.x);
        float radius=.76f+.025f*sin(angle*3+clock+seed);
        float width=max(.025f,fwidth(r));
        float arc=exp(-pow((r-radius)/width,2));
        float gaps=smoothstep(-.25f,.55f,sin(angle*3+clock*.4f+seed));
        if(cracked)gaps*=smoothstep(-.1f,.6f,sin(angle*5-seed));
        float crack=cracked?exp(-pow(p.x+.14f*sin(p.y*8+seed),2)*500)*smoothstep(.25f,.7f,r):0;
        float a=(arc*gaps+crack*.65f)*(1-smoothstep(.9f,1,r))*input.color.a;
        return float4(input.color.rgb+float3(.20f,.12f,.20f)*(arc+crack),a);
    }
    if(input.texcoord.x>=44.0f && input.texcoord.x<=45.0f) {
        // Contact spark. R and G carry energy and heat; alpha carries age fade.
        float along = input.texcoord.x - 44.0f;
        float across = input.texcoord.y * 2.0f - 1.0f;
        float width = lerp(0.07f, 0.9f, sqrt(along)) * (1.0f - 0.55f * smoothstep(0.87f, 1.0f, along));
        float transverse = across / width;
        float core = exp(-transverse * transverse * 11.0f);
        float halo = exp(-transverse * transverse * 2.8f) * 0.24f;
        float tip = core * smoothstep(0.5f, 0.92f, along) * input.color.g;
        float3 amber = lerp(float3(1.8f, 0.23f, 0.024f), float3(3.3f, 1.28f, 0.18f), input.color.g);
        float3 color = lerp(amber, float3(5.2f, 4.45f, 2.55f), tip) * input.color.r;
        float ends = smoothstep(0.0f, 0.52f, along) * (1.0f - smoothstep(0.91f, 1.0f, along));
        return float4(color, (core + halo) * ends * input.color.a);
    }
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
        // u=1 is the aperture; increasing phase must carry both filaments
        // toward u=0 (the tail), rather than sending one back into the nozzle.
        float wave=sin(u*23+clock*5)*0.055f+sin(u*37+clock*7)*0.025f;
        float noise=ImpactNoise(float2(u*17+cos(clock)*3,across*4+sin(clock)*2));
        // The head is finite-width and luminous all the way to the metal
        // lip. Only the tail breaks up; the root must never dissolve away.
        float width=lerp(0.85f,0.52f,u);
        float edge=exp(-pow((across+wave)/width,2)*4);
        float core=exp(-pow((across-wave*0.5f)/width,2)*38);
        float tail=smoothstep(0,0.30f,u);
        float diamonds=0.80f+0.20f*pow(0.5f+0.5f*cos(u*35+clock*6),4);
        float root=exp(-pow((u-0.88f)/0.17f,2));
        float flowing=.5f+.5f*sin(u*51+clock*9+across*3);
        float fibers=lerp(0.55f+0.30f*noise+0.15f*flowing,1.0f,smoothstep(0.70f,1.0f,u));
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
