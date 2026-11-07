#include "../../Engine/Assets/Rendering/Builtin/Atmosphere/transparentFog.hlsli"

Texture2D<float4> gInputs : register(t0);
RWTexture2D<float4> gResult : register(u0);
cbuffer ValidationConstants : register(b1) {
	uint mode;
	uint count;
	float baseHeight;
	float startHeight;
};

[numthreads(8, 1, 1)]
void main(uint3 index : SV_DispatchThreadID) {

	if (index.x >= count) return;
	float4 input = gInputs.Load(int3(index.xy, 0));
	if (mode == 0u) {
		float depth = IntegrateHeightFog(input.x, input.y, baseHeight, startHeight, input.z, input.w);
		gResult[index.xy] = float4(depth, exp(-min(depth, 80.0f)), 0, 1);
	} else {
		gResult[index.xy] = BlendTransparentFog(input, float4(0.1f, 0.2f, 0.3f, 0.25f), index.x);
	}
}
