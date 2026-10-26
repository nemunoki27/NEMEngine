#include "particleGPUDraw.hlsli"
#include "../Common/particleSurface.hlsli"

Texture2D<float4> baseColorTexture : register(t0, space2);

float4 main(ParticleGPUVertexOutput input) : SV_TARGET0 {
	float4 color = baseColorTexture.Sample(gSampler, input.uv) * input.color;
	clip(color.a - input.alphaReference);
	color.a *= ParticleSoftFade(input.position, input.worldPosition);
	float3 N = ParticleSurfaceNormal(input.uv, input.normal, input.tangent, input.worldPosition);
	color.rgb = ParticleSurfaceLighting(color.rgb, input.worldPosition, N,
		particleSurfaceParameters.z, particleSurfaceParameters.w);
	color.rgb += input.emissive.rgb * input.emissive.w;
	return PrepareParticleGPUBlend(color, input.blendMode);
}
