//============================================================================
//	include
//============================================================================
#include "../Common/particle.hlsli"

struct ParticleCustomParameters {

	float emissionStrength;
	float maskPower;
	float2 distortionScale;
	float4 emissionColor;
};

StructuredBuffer<ParticleCustomParameters> gParticleCustomParameters : register(t1, space1);
Texture2D<float4> baseColorTexture : register(t0, space2);
Texture2D<float4> maskTexture : register(t1, space2);
Texture2D<float4> distortionTexture : register(t2, space2);
SamplerState gSampler : register(s0);

//============================================================================
//	output
//============================================================================
struct PSOutput {

	float4 color : SV_TARGET0;
};

//============================================================================
//	main
//============================================================================
PSOutput main(VSOutput input) {

	ParticleMaterialData material = GetParticleMaterial(input);
	const float2 baseUV = TransformParticleUV(input.texcoord, material);
	ParticleCustomParameters custom = (ParticleCustomParameters)0;
	if (input.particleIndex != 0xffffffffu) {
		custom = gParticleCustomParameters[input.particleIndex];
	}

	const float2 distortion = distortionTexture.Sample(gSampler,
		baseUV * custom.distortionScale).rg * 2.0f - 1.0f;
	const float2 uv = baseUV + distortion * 0.02f;
	const float4 textureColor = baseColorTexture.Sample(gSampler, uv);
	const float mask = pow(saturate(maskTexture.Sample(gSampler, uv).r), max(custom.maskPower, 0.0001f));

	float4 result = textureColor * input.vertexColor * material.materialColor;
	result.rgb += custom.emissionColor.rgb * custom.emissionStrength * mask;
	result.rgb += material.emissive.rgb * material.emissive.w;
	result.a *= mask;
	clip(result.a - material.materialParams.x);

	PSOutput output;
	output.color = result;
	return output;
}
