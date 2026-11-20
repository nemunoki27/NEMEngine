#include "../Common/particle.hlsli"

struct BeamParameters {

	float4 startColor;
	float4 targetColor;
	float4 emissiveColor;
	float emissiveIntensity;
};

StructuredBuffer<BeamParameters> gParticleCustomParameters : register(t1, space1);
Texture2D<float4> baseColorTexture : register(t0, space2);
SamplerState gSampler : register(s0);

float4 main(VSOutput input) : SV_TARGET0 {

	ParticleMaterialData material = GetParticleMaterial(input);
	BeamParameters beam = gParticleCustomParameters[input.particleIndex];
	float2 uv = TransformParticleUV(input.texcoord, material);
	float4 color = lerp(beam.startColor, beam.targetColor, saturate(input.texcoord.x)) * baseColorTexture.Sample(gSampler, uv);
	if (color.a <= 0.0f) discard;
	color.rgb += beam.emissiveColor.rgb * beam.emissiveIntensity;
	return PrepareParticleBlendColor(color, material, input.position);
}
