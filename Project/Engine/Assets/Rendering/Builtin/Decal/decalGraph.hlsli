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
		case 6: v = a[min(uint(v.x),3u)].xxxx; break; // Split
		case 7: v = float4(a.x,b.x,c.x,d.x); break; // Combine
		case 8: v = float4(normalize(a.xyz*2-1),1); break; // NormalUnpack
		case 9: v = float4(a.xy*b.xy+c.xy,0,1); break; // TilingAndOffset
		case 10: v = a+b; break; // Add
		case 11: v = a-b; break; // Subtract
		case 12: v = a*b; break; // Multiply
		case 13: v = a/b; break; // Divide
		case 14: v = pow(a,b); break; // Power
		case 15: v = min(a,b); break; // Minimum
		case 16: v = max(a,b); break; // Maximum
		case 17: v = 1-a; break; // OneMinus
		case 18: v = lerp(a,b,c); break; // Lerp
		case 19: v = saturate(a); break; // Saturate
		case 20: v = sin(a); break; // Sine
		case 21: v = cos(a); break; // Cosine
		case 22: v = abs(a); break; // Absolute
		case 23: v = floor(a); break; // Floor
		case 24: v = frac(a); break; // Fraction
		case 25: v = sqrt(a); break; // SquareRoot
		case 26: v = -a; break; // Negate
		case 27: v = step(a,b); break; // Step
		case 28: v = smoothstep(a,b,c); break; // Smoothstep
		case 29: v = clamp(a,b,c); break; // Clamp
		case 30: v = a.x != 0 ? b : c; break; // Branch
		case 31: v = ceil(a); break; // Ceiling
		case 32: v = round(a); break; // Round
		case 33: v = sign(a); break; // Sign
		case 34: v = trunc(a); break; // Truncate
		case 35: v = tan(a); break; // Tangent
		case 36: v = rcp(a); break; // Reciprocal
		case 37: v = v.x == 1 ? exp2(a) : (v.x == 2 ? pow(10.0f, a) : exp(a)); break; // Exponential
		case 38: v = v.x == 1 ? log2(a) : (v.x == 2 ? log10(a) : log(a)); break; // Log
		case 39: v = fmod(a,b); break; // Modulo
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
