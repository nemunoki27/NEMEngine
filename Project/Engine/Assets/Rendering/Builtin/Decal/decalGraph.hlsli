#include "../Common/surfaceGraphMath.hlsli"

// DecalGraphOpと同じ命令順をRasterとReflectionで使用する
bool EvaluateDecalGraph(DecalGPUData decal, HexTilingData hex, float2 footprint, float3 worldPosition,
	float3 originalNormal, float angle, SamplerState samplerState, out DecalSample result) {

	const float2 uv = hex.uv;
	result = (DecalSample)0;
	result.normal = originalNormal;
	result.surface = float2(0, 0.5f);
	float4 color = 1;
	float opacity = 1;
	float4 values[64];
	values[0] = 0;
	values[1] = 1;
	values[2] = float4(uv, 0, 1);
	values[3] = 0.5f;
	[loop] for (uint i = 0; i < min(decal.graphCount, 60u); ++i) {
		uint at = decal.graphOffset + 6u * i;
		uint op = gDecalReceiverIndices[at];
		uint inputs = gDecalReceiverIndices[at + 1];
		float4 v = asfloat(uint4(gDecalReceiverIndices[at + 2], gDecalReceiverIndices[at + 3],
			gDecalReceiverIndices[at + 4], gDecalReceiverIndices[at + 5]));
		float4 a = values[inputs & 63u];
		float4 b = values[(inputs >> 8) & 63u];
		float4 c = values[(inputs >> 16) & 63u];
		float4 d = values[(inputs >> 24) & 63u];
		switch (op) {
		case 0: break; // Literal
		case 1: break; // Texture
		case 2: v = float4(uv,0,1); break; // UV
		case 3: v = float4(worldPosition,1); break; // WorldPosition
		case 4: v = float4(originalNormal,1); break; // WorldNormal
		case 5: { // TextureSample
			HexTilingData sampleHex = hex;
			if (((inputs >> 8) & 63u) != 2u) sampleHex = BuildHexTiling(b.xy, decal.hexTiling);
			v = SampleDecalTexture(asuint(a.x), samplerState, sampleHex, footprint, v, (inputs & 0x80u) != 0u);
			break;
		}
		case 8: v = float4(normalize(a.xyz*2-1),1); break; // NormalUnpack
		default: v = SurfaceGraphMath(op,a,b,c,d,v); break;
		case 40: // Output
			switch (uint(v.x)) {
			case 0: color = a; break;
			case 1: result.normal = DecalNormalToWorld(decal, originalNormal, a.xyz); break;
			case 2: result.surface.x = saturate(a.x); break;
			case 3: result.surface.y = saturate(a.x); break;
			case 4: result.emissive = max(0, a.rgb); break;
			case 5: opacity = a.x; break;
			}
			break;
		}
		values[i + 4] = v;
	}
	result.color = max(0, color.rgb * decal.baseColor.rgb);
	result.alpha = saturate(color.a * opacity * angle * decal.surface.w * decal.baseColor.a);
	return result.alpha > 0;
}
