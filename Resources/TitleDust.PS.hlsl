// Seeded billows, with dense shaded centres and broken translucent edges.
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
    // Even UV offsets carry a stable puff seed without another vertex stream.
    float seed=floor(input.texcoord.x*0.5f)*2.0f;
    float2 uv=float2(input.texcoord.x-seed,input.texcoord.y);
    float2 p=uv*2.0f-1.0f;
    // Negative offsets mark angular grains using the same depth-tested pass.
    if(seed<0.0f) {
        float edge=max(abs(p.x),abs(p.y))+0.18f*(p.x+p.y);
        float coverage=1.0f-smoothstep(0.65f,1.0f,edge);
        return float4(input.color.rgb*(0.72f+0.30f*(1.0f-uv.y)),input.color.a*coverage);
    }
    float2 offset=float2(seed*1.37f,seed*0.73f);
    float2 warped=p+0.38f*float2(Noise(p*3.0f+offset),Noise(p*3.0f+offset+19.0f))-0.19f;
    float density=Fbm(warped*3.2f+offset);
    float edge=length(warped)+0.40f*(density-0.5f);
    float coverage=1.0f-smoothstep(0.36f,1.02f,edge);
    float opticalDepth=coverage*(0.40f+2.0f*density)*input.color.a;
    float alpha=1.0f-exp(-opticalDepth*2.1f);
    float lighting=0.46f+0.32f*(1.0f-uv.y)+0.52f*density;
    return float4(input.color.rgb*lighting,alpha);
}
