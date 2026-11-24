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
    if (input.texcoord.x >= 136.0f && input.texcoord.x <= 137.0f) {
        const float along = input.texcoord.x - 136.0f;
        const float across = abs(input.texcoord.y - .5f) * 2.0f;
        const float profile = across / lerp(.37f, 1.0f, along);
        const float core = exp2(-profile * profile * 12.0f);
        const float halo = exp2(-profile * profile * 3.5f) * .31f;
        const float ends = smoothstep(0.0f, .25f, along) * (1.0f - smoothstep(.85f, 1.0f, along));
        const float head = lerp(.16f, .91f, smoothstep(.24f, .90f, along));
        const float edge = 1.0f - smoothstep(.69f, 1.0f, across);
        const float3 color = lerp(input.color.rgb, float3(4.7f, 6.1f, 8.65f), core * head);
        return float4(color, (core * head + halo) * ends * edge * input.color.a);
    }
    if (input.texcoord.x >= 134.0f && input.texcoord.x <= 135.0f) {
        float2 p;
        float phase, seed;
        ImpactCoordinates(input.texcoord, 134.0f, p, phase, seed);
        float radius = length(p);
        float bearing = atan2(p.y, p.x + .0001f) + seed;
        float grain = ImpactNoise(p * 8.0f + seed);
        float warp = grain * .46f + radius * 1.15f;
        float fracture = abs(sin(bearing * 3.5f + warp));
        float lineWidth = max(.040f, min(fwidth(fracture), .23f));
        float arms = (1.0f - smoothstep(.035f, .055f + lineWidth, fracture)) *
            (1.0f - smoothstep(.24f, .84f, radius + grain * .12f));
        float front = abs(radius + (grain - .5f) * .145f - lerp(.14f, .83f, phase));
        float crest = (1.0f - smoothstep(.014f, .082f, front)) * smoothstep(.34f, .76f, grain);
        float core = exp2(-radius * radius * 24.0f) * .63f;
        float edge = 1.0f - smoothstep(.79f, .98f, radius);
        return float4(lerp(input.color.rgb, float3(3.7f, 5.9f, 8.2f), saturate(core + .49f * arms)),
            (core + arms * .58f + crest * .71f) * edge * input.color.a);
    }
    if (input.texcoord.x >= 132.0f && input.texcoord.x <= 133.0f) {
        float2 p;
        float phase, seed;
        ImpactCoordinates(input.texcoord, 132.0f, p, phase, seed);
        float height = saturate(.5f - .5f * p.y);
        float upperFade = smoothstep(.30f, .96f, height);
        float curl = sin(height * 8.7f - phase * 5.8f + seed) * smoothstep(.05f, .63f, height);
        float2 field = float2(p.x * 4.1f + seed, height * 5.2f - phase * 4.4f);
        float eddy = ImpactNoise(field * .67f + float2(13.8f, 3.4f)) - .5f;
        float center = curl * .32f + eddy * (.19f + .24f * height);
        float extent = lerp(.16f, .40f, height);
        float coreDist = (p.x - center) / extent;
        float core = exp2(-2.4f * coreDist * coreDist);
        // The higher stream peels away from the core and breaks into fine wisps.
        float split = sin(height * 6.5f - phase * 5.8f + seed + 1.9f) * (.12f + .38f * height);
        float strandDist = (p.x - center - split) / (.10f + .16f * height);
        float strand = exp2(-2.9f * strandDist * strandDist);
        float silhouettes = core + strand * smoothstep(.17f, .61f, height) * .84f;

        field.x += curl * .88f + eddy * 1.41f;
        float noise = ImpactNoise(field) * .52f + ImpactNoise(field * 2.0f + 7.3f) * .31f +
            ImpactNoise(field * 4.8f + 2.7f) * .17f;
        float breakup = smoothstep(.24f + height * .145f, .66f + height * .09f, noise);
        float edge = smoothstep(0.0f, .13f, height) * (1.0f - smoothstep(.82f, 1.0f, height)) *
            (1.0f - smoothstep(.75f, .99f, abs(p.x)));
        float density = silhouettes * breakup * edge * lerp(.72f, .20f, upperFade) * (1.0f - phase * .30f);
        float3 tint = lerp(float3(.46f, .77f, .98f), float3(.39f, .62f, .89f), upperFade);
        return float4(input.color.rgb * tint, density * input.color.a);
    }
    if (input.texcoord.x >= 128.0f && input.texcoord.x <= 129.0f) {
        const float2 p = float2(input.texcoord.x - 128.5f, input.texcoord.y - .5f) * 2.0f;
        const float distance = abs(p.x + p.y * .19f) * 1.30f + abs(p.y - .10f) * 1.13f;
        const float antialias = min(fwidth(distance), .38f);
        const float face = 1.0f - smoothstep(.78f, .92f + antialias, distance);
        const float glint = 1.0f - smoothstep(.055f, .29f, abs(p.x + p.y * .32f));
        const float facet = smoothstep(-.12f, .09f, p.x + p.y * .30f);
        const float halo = (1.0f - smoothstep(.74f, 1.18f, distance)) * .18f;
        const float3 color = input.color.rgb * (.54f + .46f * facet) + float3(1.72f, 1.85f, 2.0f) * glint;
        return float4(color, max(face, halo) * input.color.a);
    }
    if (input.texcoord.x >= 130.0f && input.texcoord.x <= 131.0f) {
        const float2 p = float2(input.texcoord.x - 130.5f, input.texcoord.y - .5f) * 2.0f;
        const float radiusSquared = dot(p, p);
        const float core = exp(-radiusSquared * 42.0f);
        const float halo = exp(-radiusSquared * 5.9f) * .53f;
        const float rays = exp(-abs(p.y) * 42.0f - abs(p.x) * 2.5f) * .33f;
        const float edge = 1.0f - smoothstep(.63f, 1.0f, radiusSquared);
        return float4(lerp(input.color.rgb, float3(7.4f, 9.6f, 13.2f), core),
            (core + halo + rays) * edge * input.color.a);
    }
    if (input.texcoord.x >= 124.0f && input.texcoord.x <= 125.0f) {
        float2 p;
        float phase, seed;
        ImpactCoordinates(input.texcoord, 124.0f, p, phase, seed);
        float angle = phase * 6.2831853f;
        float2 drift = float2(cos(angle), sin(angle));
        float2 field = p * float2(4.2f, 2.8f) + seed;
        float turbulence = ImpactNoise(field + drift * .72f) * .58f +
            ImpactNoise(field * 2.3f - drift * .52f) * .29f + ImpactNoise(field * 5.4f + drift) * .13f;
        float envelope = 1.0f - smoothstep(.21f, 1.0f, length(p));
        float wisps = smoothstep(.32f, .78f, turbulence) * envelope * envelope;
        return float4(input.color.rgb * lerp(.48f, 1.18f, turbulence), wisps * input.color.a);
    }
    if (input.texcoord.x >= 126.0f && input.texcoord.x <= 127.0f) {
        float2 p = abs(float2(input.texcoord.x - 126.5f, input.texcoord.y - .5f)) * 2.0f;
        float shape = p.x * 1.22f + p.y * .90f;
        float core = 1.0f - smoothstep(.12f, .56f, shape);
        float halo = (1.0f - smoothstep(.38f, 1.0f, length(p))) * .28f;
        return float4(lerp(input.color.rgb, float3(2.5f, 4.1f, 6.0f), core),
            (core + halo) * input.color.a);
    }
    if (input.texcoord.x >= 120.0f && input.texcoord.x <= 121.0f) {
        // The shock front expands in world space, exposing larger gaps as it cools.
        float2 p;
        float phase, seed;
        ImpactCoordinates(input.texcoord, 120.0f, p, phase, seed);
        const float radius = length(p);
        const float2 radial = p / max(radius, .0001f);
        const float angle = atan2(radial.y, radial.x + .0001f);
        const float roughness = ImpactNoise(radial * 8.0f + seed);
        const float distance = radius - (.79f + .028f * sin(angle * 5.0f + seed)
            + lerp(.028f, .082f, phase) * (roughness - .5f));
        const float erosion = .69f * sin(angle * 4.0f + seed)
            + .34f * sin(angle * 9.0f - seed) + .38f * (roughness - .5f);
        const float threshold = lerp(-.54f, .44f, phase);
        const float arcs = smoothstep(threshold, threshold + .20f, erosion);
        const float width = lerp(.048f, .015f, phase) + min(.063f, fwidth(distance) * .63f);
        const float edge = 1.0f - smoothstep(width * .25f, width * 1.20f, abs(distance));
        const float shoulder = (1.0f - smoothstep(width, width * 2.05f, abs(distance))) * .22f;
        const float3 color = input.color.rgb * (.80f + .35f * roughness + .18f * edge);
        return float4(color, (edge + shoulder) * arcs * input.color.a);
    }
    if (input.texcoord.x >= 122.0f && input.texcoord.x <= 123.0f) {
        // A tapered streak with a bright tip and a saturated wake.
        const float head = saturate(input.texcoord.x - 122.0f);
        const float cross = abs(input.texcoord.y * 2.0f - 1.0f);
        const float profile = cross / lerp(.19f, .79f, sqrt(head));
        const float core = exp(-profile * profile * 11.0f);
        const float halo = exp(-profile * profile * 2.8f) * .32f;
        const float taper = smoothstep(0.0f, .42f, head) * (1.0f - smoothstep(.79f, 1.0f, head));
        const float3 color = input.color.rgb * 1.35f + (.58f * core * head);
        return float4(color, (core * .80f + halo) * taper * input.color.a);
    }
    if(input.texcoord.x>=118.0f && input.texcoord.x<=119.0f) {
        // Red rims border the alpha-blended dark channel. Only the moving
        // leader and the impact glint carry white heat.
        const float u=input.texcoord.x-118.0f;
        const float v=abs(input.texcoord.y*2.0f-1.0f);
        const float aa=max(fwidth(v)*.55f,.045f);
        const float rim=1.0f-smoothstep(.055f,.14f+aa,abs(v-.48f));
        const float shoulder=exp(-pow((v-.48f)/.25f,2.0f))*.19f;
        const float hollow=smoothstep(.16f,.32f,v);
        const float end=smoothstep(0.0f,.055f,u)*(1.0f-smoothstep(.945f,1.0f,u));
        return float4(input.color.rgb,(rim+shoulder)*hollow*end*input.color.a);
    }
    const bool cartArcTip=input.texcoord.x>=116.0f && input.texcoord.x<=117.0f;
    if(cartArcTip || (input.texcoord.x>=114.0f && input.texcoord.x<=115.0f)) {
        const float u=input.texcoord.x-(cartArcTip?116.0f:114.0f);
        const float v=abs(input.texcoord.y*2.0f-1.0f);
        const float aa=max(fwidth(v)*.55f,.055f);
        const float core=1.0f-smoothstep(.10f,.22f+aa,v);
        const float halo=exp(-v*v*6.6f)*(1.0f-smoothstep(.75f,1.0f,v));
        const float end=smoothstep(0.0f,.055f,u)*(1.0f-smoothstep(.945f,1.0f,u));
        return float4(input.color.rgb,(core*.84f+halo*(cartArcTip?.12f:.31f))*end*input.color.a);
    }
    const bool violetDischarge=input.texcoord.x>=112.0f && input.texcoord.x<=113.0f;
    if(violetDischarge || (input.texcoord.x>=106.0f && input.texcoord.x<=107.0f)) {
        // The hollow discharge front has a hot edge and a thin shoulder.
        float2 p=float2(input.texcoord.x-(violetDischarge?112.5f:106.5f),input.texcoord.y-.5f)*2.0f;
        float angle=atan2(p.y,p.x);
        float r=length(p)-.016f*sin(angle*5.0f+.9f);
        float band=abs(r-.79f);
        float width=max(.032f,min(.105f,fwidth(r)*.65f));
        float crest=1-smoothstep(width*.27f,width*1.15f,band);
        float shoulder=(1-smoothstep(width,width*2.18f,band))*.29f;
        float arcs=.62f+.38f*smoothstep(-.54f,.63f,sin(angle*3.0f-.5f)+.24f*sin(angle*7.0f));
        float3 hotEdge=lerp(input.color.rgb,violetDischarge?float3(3.30f,1.37f,5.40f):float3(4.55f,1.71f,.28f),crest*.71f);
        return float4(hotEdge,(crest+shoulder)*arcs*input.color.a);
    }
    if(input.texcoord.x>=104.0f && input.texcoord.x<=105.0f) {
        // A small photographic glint pinned to the hot nose.
        float2 p=float2(input.texcoord.x-104.5f,input.texcoord.y-.5f)*2.0f;
        float horiz=exp(-abs(p.x)*3.9f-p.y*p.y*150.0f);
        float vert=exp(-abs(p.y)*5.5f-p.x*p.x*190.0f);
        float star=(horiz+vert*.60f)*(1-smoothstep(.70f,1.0f,length(p)));
        float3 tint=lerp(input.color.rgb,float3(5.3f,4.48f,3.27f),exp(-dot(p,p)*28.0f));
        return float4(tint,saturate(star)*input.color.a);
    }
    if(input.texcoord.x>=102.0f && input.texcoord.x<=103.0f) {
        // Curved, tapered pieces retain the colour of the orbiting shell.
        float2 p=float2(input.texcoord.x-102.5f,input.texcoord.y-.5f)*2.0f;
        float curve=p.y+.38f*p.x*p.x+.13f*p.x;
        float halves=1-smoothstep(.42f,1.0f,abs(p.x));
        float edge=abs(curve)/(.22f+halves*.29f);
        float width=max(.065f,min(.40f,fwidth(edge)*.60f));
        float chip=(1-smoothstep(.72f-width,.86f+width,edge))*halves;
        float fracture=exp(-pow((curve+.16f)/.18f,2.0f));
        float3 color=lerp(input.color.rgb*.57f,float3(4.8f,2.45f,.59f),fracture);
        return float4(color,chip*input.color.a);
    }
    const bool violetShell=input.texcoord.x>=110.0f && input.texcoord.x<=111.0f;
    if(violetShell || (input.texcoord.x>=100.0f && input.texcoord.x<=101.0f)) {
        // Shade the visible hemisphere with the shot's world heading in camera space.
        float packed=floor(input.texcoord.y*.5f);
        float clock=fmod(packed,64.0f)*(6.2831853f/64.0f);
        float heat=saturate(floor(packed/64.0f)/63.0f);
        float2 p=float2((input.texcoord.x-(violetShell?110.5f:100.5f))*2.0f,1.0f-frac(input.texcoord.y*.5f)*4.0f);
        float r=length(p);
        float aa=min(.14f,fwidth(r)*.65f);
        float body=1-smoothstep(.66f-aa,.72f+aa,r);
        float2 sphereUV=p/.71f;
        float3 normal=float3(sphereUV,sqrt(saturate(1-dot(sphereUV,sphereUV))));
        float heading=dot(normal,input.color.rgb);
        float ahead=smoothstep(-.52f,.88f,heading);
        float rim=smoothstep(.43f,.70f,r)*(.60f+.40f*ahead);
        float2 hotOffset=p-input.color.xy*.27f;
        float core=exp(-dot(hotOffset,hotOffset)*15.0f)*(1-smoothstep(.45f,.68f,r));
        float angle=atan2(p.y,p.x);
        float orbit=angle-clock;
        float sweepingArc=smoothstep(-.46f,.70f,sin(orbit*2.0f+.34f*sin(orbit*5.0f)));
        float flicker=.5f+.5f*sin(orbit*7.0f+clock*2.0f);
        float flameEdge=.69f+sweepingArc*(.075f+.071f*flicker);
        float flameDistance=abs(r-flameEdge);
        float crest=1-smoothstep(.024f,.084f+aa*.43f,flameDistance);
        float flame=crest*sweepingArc*(.69f+.31f*flicker)*smoothstep(.46f,.66f,r);
        float wake=exp(-flameDistance*flameDistance*59.0f)*sweepingArc*.30f;
        wake*=1-smoothstep(.88f,.99f,r);
        float3 shoulder=violetShell?
            lerp(float3(.42f,.035f,.99f),float3(1.05f,.23f,2.18f),ahead):
            lerp(float3(.96f,.18f,.017f),float3(2.08f,.66f,.073f),ahead);
        shoulder+=(violetShell?float3(.79f,.19f,1.38f):float3(1.08f,.40f,.052f))*rim;
        float whiteHot=smoothstep(.24f,.86f,heat);
        float3 white=violetShell?lerp(float3(.13f,.022f,.23f),float3(4.68f,4.16f,5.20f),whiteHot):
            lerp(float3(.18f,.009f,.004f),float3(5.3f,5.02f,4.5f),whiteHot);
        float3 shell=lerp(shoulder*lerp(.26f,1.0f,heat),white,core);
        float3 fire=violetShell?
            lerp(float3(1.35f,.13f,3.30f),float3(2.84f,.78f,4.72f),flame):
            lerp(float3(2.92f,.37f,.035f),float3(4.22f,1.68f,.26f),flame);
        float fireBlend=saturate(flame*.83f+wake*.42f)*(1-core);
        float halo=exp(-r*r*4.8f)*.39f*(1-smoothstep(.76f,1.0f,r));
        return float4(lerp(shell,fire,fireBlend),saturate(body+halo+(flame+wake)*.75f)*input.color.a);
    }
    if(input.texcoord.x>=98.0f && input.texcoord.x<=99.0f) {
        // A single shock front with a clear interior. World geometry owns its expansion.
        float2 p=float2(input.texcoord.x-98.5f,input.texcoord.y-.5f)*2.0f;
        float angle=atan2(p.y,p.x);
        float r=length(p)-.014f*sin(angle*5.0f+.7f);
        float width=max(.026f,min(.12f,fwidth(r)*.72f));
        float distance=abs(r-.79f);
        float band=1-smoothstep(width*.3f,width*1.2f,distance);
        float fringe=(1-smoothstep(width,width*2.35f,distance))*.23f;
        float sector=.74f+.26f*cos(angle*2.0f-.54f);
        return float4(input.color.rgb,(band+fringe)*sector*input.color.a);
    }
    const bool violetWake=input.texcoord.x>=108.0f && input.texcoord.x<=109.0f;
    if(violetWake || (input.texcoord.x>=96.0f && input.texcoord.x<=97.0f)) {
        // Anchor the wake at the shell. The thin end wavers and erodes into air.
        float clock=fmod(floor(input.texcoord.y*.5f),64.0f)*(6.2831853f/64.0f);
        float along=input.texcoord.x-(violetWake?108.0f:96.0f);
        float across=frac(input.texcoord.y*.5f)*4.0f-1.0f;
        float loose=1-smoothstep(.27f,.87f,along);
        across-=loose*(.24f*sin(along*16.0f+clock)+.09f*sin(along*25.0f+clock*2.0f));
        float width=lerp(.115f,.89f,pow(along,.85f));
        float v=across/width;
        float filament=exp(-v*v*6.8f);
        float glow=exp(-v*v*2.3f)*.30f;
        float ragged=.79f+.21f*sin(along*23.0f+clock*2.0f);
        float ends=smoothstep(.025f,.66f,along)*(1-smoothstep(.90f,1.0f,along))*lerp(1.0f,ragged,loose);
        float twist=.38f*sin(along*12.0f+clock*2.0f);
        float strand=exp(-pow((v-twist)*5.4f,2.0f));
        strand*=smoothstep(.16f,.77f,.5f+.5f*sin(along*20.0f+clock*3.0f));
        float3 hot=violetWake?float3(2.55f,1.0f,4.72f):float3(4.0f,2.15f,.70f);
        float3 color=lerp(input.color.rgb,hot,saturate(filament*along*.52f+strand*.51f));
        return float4(color,(filament+glow+strand*.30f)*ends*input.color.a);
    }
    if(input.texcoord.x>=94.0f && input.texcoord.x<=95.0f) {
        // One thin front, with feathered edges and a transparent centre.
        float2 p=(float2(input.texcoord.x-94.0f,input.texcoord.y)*2.0f)-1.0f;
        float radius=length(p);
        float distance=abs(radius-.81f);
        float width=max(.025f,fwidth(radius)*.66f);
        float rim=1-smoothstep(width*.35f,width*1.35f,distance);
        float fringe=(1-smoothstep(width,width*2.5f,distance))*.23f;
        float sector=.83f+.17f*cos(4.0f*atan2(p.y,p.x));
        return float4(input.color.rgb,(rim+fringe)*sector*input.color.a);
    }
    if(input.texcoord.x>=90.0f && input.texcoord.x<=91.0f) {
        float2 p;float phase,seed;
        ImpactCoordinates(input.texcoord,90,p,phase,seed);
        float r=length(p),a=atan2(p.y,p.x);
        float ring=exp(-pow((r-lerp(.70f,.56f,phase))/max(.035f,fwidth(r)),2));
        float teeth=smoothstep(.08f,.28f,cos(a*12+seed));
        float field=exp(-r*r*4.0f)*.23f;
        return float4(input.color.rgb,(ring*(.42f+.58f*teeth)+field)*input.color.a);
    }
    if(input.texcoord.x>=92.0f && input.texcoord.x<=93.0f) {
        float2 p;float phase,seed;
        ImpactCoordinates(input.texcoord,92,p,phase,seed);
        float y=.5f-.5f*p.y,clock=phase*16.0f;
        float bend=.075f*sin(y*30.0f+seed)+.047f*sin(y*104.0f-clock*2.7f);
        bend+=(ImpactNoise(float2(y*29.0f,clock*.37f)+seed)-.5f)*.16f;
        float x=p.x-bend;
        float aa=max(.016f,fwidth(x)*.56f);
        float core=exp(-pow(x/aa,2));
        float arc=exp(-pow((x-.14f*sin(y*61+clock))/max(.018f,aa*.67f),2));
        arc*=smoothstep(.33f,.68f,ImpactNoise(float2(y*14,clock*.6f)+seed+5));
        float heat=ImpactNoise(float2(p.x*9.0f,y*30-clock*1.65f)+seed);
        float fringe=exp(-x*x*7.0f)*smoothstep(.32f,.83f,heat);
        float glow=exp(-x*x*21.0f);
        float tip=1-smoothstep(.78f,1,y);
        float3 tint=lerp(float3(4.3f,.035f,.061f),float3(6.8f,1.18f,.47f),fringe*.68f);
        tint=lerp(tint,float3(8.0f,3.85f,3.18f),saturate(core+arc*.42f));
        return float4(tint,(core*.91f+arc*.50f+glow*.27f+fringe*.65f)*tip*input.color.a);
    }
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
    if(input.texcoord.x>=70.0f && input.texcoord.x<=71.0f) {
        // The red front advances in the same direction as the charge packets.
        float u=input.texcoord.x-70.0f;
        float y=frac(input.texcoord.y*.5f)*4.0f-1.0f;
        float tick=fmod(floor(input.texcoord.y*.5f),64.0f);
        float clock=tick/64.0f;
        float seed=input.color.g;
        float heat=smoothstep(u+.01f,u+.14f,input.color.r);
        float leading=1.0f-smoothstep(.035f,.15f,abs(input.color.r-u-.064f));
        float packet=pow(.5f+.5f*cos(6.2831853f*(3.0f*u-clock)),5.0f);
        float jitter=ImpactNoise(float2(u*26.0f,floor(tick*.52f))+seed*7.11f)-.5f;
        float seam=y-.13f*sin(u*28.0f-clock*6.2831853f)-jitter*(.62f*heat+.30f*leading);
        float hot=exp(-seam*seam*22.0f);
        float glow=exp(-y*y*4.6f);
        float ends=smoothstep(0.0f,.045f,u)*(1.0f-smoothstep(.94f,1.0f,u));
        float3 plasma=lerp(float3(.24f,2.2f,4.8f),float3(6.6f,.34f,.073f),heat);
        float3 white=lerp(float3(5.5f,5.8f,6.2f),float3(7.8f,3.6f,2.55f),heat);
        float3 color=lerp(plasma,white,hot*saturate(packet*.64f+leading*.48f));
        float density=glow*lerp(.30f,.39f,input.color.b)+hot*(.28f+.66f*packet+.32f*leading);
        return float4(color,density*ends*input.color.a);
    }
    if(input.texcoord.x>=68.0f && input.texcoord.x<=69.0f) {
        float u=input.texcoord.x-68.0f;
        float v=input.texcoord.y*2.0f-1.0f;
        float profile=exp(-v*v*8.0f)*(1.0f-smoothstep(.80f,1.0f,abs(v)));
        return float4(input.color.rgb,profile*(1.0f-smoothstep(.97f,1.0f,u))*input.color.a);
    }
    if(input.texcoord.x>=66.0f && input.texcoord.x<=67.0f) {
        float u=input.texcoord.x-66.0f;
        float y=frac(input.texcoord.y*.5f)*4.0f-1.0f;
        float clock=fmod(floor(input.texcoord.y*.5f),64.0f)/64.0f;
        float packet=pow(.5f+.5f*cos(6.2831853f*(3.0f*u-clock)),5.0f);
        float seam=y-.15f*sin(31.0f*u-6.2831853f*clock);
        float hot=exp(-seam*seam*18.0f);
        float glow=exp(-y*y*4.6f);
        float ends=smoothstep(.0f,.05f,u)*(1.0f-smoothstep(.94f,1.0f,u));
        return float4(lerp(input.color.rgb,float3(5.5f,5.8f,6.2f),hot*packet*.76f),
            (glow*.33f+hot*(.25f+.68f*packet))*ends*input.color.a);
    }
    if(input.texcoord.x>=72.0f && input.texcoord.x<=73.0f) {
        // One cross-section for every segment of the curved ion wake.
        float v=2.0f*input.texcoord.y-1.0f;
        float core=exp(-v*v*22.0f);
        float halo=exp(-v*v*3.8f)*(1.0f-smoothstep(.72f,1.0f,abs(v)));
        return float4(lerp(input.color.rgb,float3(1.18f,2.1f,4.6f),core),
            (core*.78f+halo*.39f)*input.color.a);
    }
    if(input.texcoord.x>=74.0f && input.texcoord.x<=75.0f) {
        // A compact laser bolt with a tight white core and a cyan shoulder.
        float along=input.texcoord.x-74.0f;
        float across=input.texcoord.y*2.0f-1.0f;
        float width=lerp(.50f,1.0f,smoothstep(.05f,.78f,along));
        float tip=(max(0.0f,along-.86f)/.14f);
        float radiusSquared=across*across/(width*width)+tip*tip;
        float core=exp(-radiusSquared*14.0f);
        float shoulder=exp(-radiusSquared*3.5f)*.50f;
        float envelope=smoothstep(0.0f,.22f,along)*(1.0f-smoothstep(.68f,1.0f,radiusSquared));
        float3 color=lerp(input.color.rgb,float3(4.25f,5.75f,6.5f),core);
        return float4(color,saturate(core+shoulder)*envelope*input.color.a);
    }
    if(input.texcoord.x>=76.0f && input.texcoord.x<=77.0f) {
        float2 p; float phase,seed;
        ImpactCoordinates(input.texcoord,76.0f,p,phase,seed);
        float r=length(p),angle=atan2(p.y,p.x);
        float2 flow=p*5.2f+float2(seed,-seed*.73f)-float2(0,phase*1.3f);
        float noise=ImpactNoise(flow);
        float edge=.68f+.10f*sin(angle*5.0f+seed+phase*1.8f)+.07f*sin(angle*8.0f-seed);
        edge+=(noise-.5f)*.19f;
        float band=(r-edge)/(.24f-phase*.11f);
        float shell=exp(-band*band);
        float tongues=smoothstep(.12f,.78f,noise+.18f*sin(angle*3.0f+seed));
        float streak=abs(ImpactNoise(flow*2.2f+phase*2.0f)-.51f);
        float filaments=(1-smoothstep(.018f,.082f,streak))*shell*tongues;
        float core=exp(-r*r*11.0f)*(1-smoothstep(.07f,.39f,phase));
        float glow=exp(-r*r*3.0f)*(.19f+.37f*noise)*(1-phase);
        float3 green=lerp(float3(.10f,2.35f,1.09f),float3(1.46f,4.4f,2.67f),filaments);
        return float4(lerp(green,float3(5.1f,5.3f,3.9f),saturate(core*1.7f)),
            saturate(shell*tongues*.83f+filaments*.33f+core+glow)*
            (1-smoothstep(.86f,1.0f,r))*input.color.a);
    }
    if(input.texcoord.x>63.5f)return RenderRailEnergyShield(input.texcoord,input.color);
    if(input.texcoord.x>=50.0f && input.texcoord.x<=51.0f) {
        float u=input.texcoord.x-50.0f;
        float v=frac(input.texcoord.y*.5f)*4.0f-1.0f;
        float clock=fmod(floor(input.texcoord.y*.5f),64.0f)/64.0f*6.2831853f;
        float turbulence=.08f*sin(62.0f*u-clock)+.046f*sin(131.0f*u-clock*2.0f);
        float radius=abs(v-turbulence);
        float cloud=ImpactNoise(float2(110.0f*u,9.0f*v)+float2(cos(clock),sin(clock))*2.5f);
        float flare=exp(-radius*radius*8.8f);
        float shell=exp(-radius*radius*3.3f)*(.28f+.72f*cloud);
        float fibres=pow(.5f+.5f*sin(v*27.0f+u*89.0f-clock*2.0f),6.0f);
        float edge=(1.0f-smoothstep(.68f,1.0f,abs(v)))*(1.0f-smoothstep(.93f,1.0f,u));
        float3 hot=input.color.rgb+float3(1.6f,.46f,.18f)*flare*fibres;
        return float4(hot,(flare*.60f+shell*.58f)*edge*input.color.a);
    }
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
        // Short laser pulse with a bright core and a saturated green edge.
        float2 p=float2(input.texcoord.x-26.5f,input.texcoord.y-0.5f)*2.0f;
        float end=max(0.0f,abs(p.x)-0.83f)/0.17f;
        float d2=end*end+p.y*p.y;
        float edge=1.0f-smoothstep(.53f,1.0f,sqrt(d2));
        float core=exp(-d2*16.0f);
        float3 highlight=input.color.rgb*float3(4.0f,1.16f,2.1f);
        return float4(lerp(input.color.rgb,highlight,core),edge*input.color.a);
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
