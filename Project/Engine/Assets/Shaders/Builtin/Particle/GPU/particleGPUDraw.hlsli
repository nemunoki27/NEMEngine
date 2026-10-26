#ifndef NEM_PARTICLE_GPU_DRAW
#define NEM_PARTICLE_GPU_DRAW

struct ParticleGPUVertexOutput {
	float4 position : SV_POSITION;
	float2 uv : TEXCOORD0;
	float4 color : COLOR0;
	nointerpolation float4 emissive : TEXCOORD1;
	nointerpolation float alphaReference : TEXCOORD2;
	nointerpolation uint blendMode : TEXCOORD3;
	float3 worldPosition : TEXCOORD4;
	float3 normal : TEXCOORD5;
	float4 tangent : TEXCOORD6;
	nointerpolation float4 velocityAge : TEXCOORD7;
	nointerpolation float2 lifetimeRandom : TEXCOORD8;
	nointerpolation float4 particleColor : TEXCOORD9;
};

float4 PrepareParticleGPUBlend(float4 color, uint mode) {
	float alpha = saturate(color.a);
	if (mode == 3u) color.rgb = lerp(1.0f.xxx, color.rgb, alpha);
	else if (mode == 4u || mode == 5u) color.rgb *= alpha;
	return color;
}

#endif
