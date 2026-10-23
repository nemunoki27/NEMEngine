#include "particleGPUDraw.hlsli"

Texture2D<float4> baseColorTexture : register(t0, space2);
SamplerState gSampler : register(s0);

float4 main(ParticleGPUVertexOutput input) : SV_TARGET0 {
	float4 color = baseColorTexture.Sample(gSampler, input.uv) * input.color;
	clip(color.a - input.alphaReference);
	color.rgb += input.emissive.rgb * input.emissive.w;
	return PrepareParticleGPUBlend(color, input.blendMode);
}
