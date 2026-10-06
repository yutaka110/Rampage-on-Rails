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
float4 main(PixelInput input) : SV_TARGET0 {
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
