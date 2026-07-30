//============================================================================
//	resources
//============================================================================
cbuffer PostProcessParameters : register(b1) {

	uint targetMask;
	float3 _padding;
};

Texture2D<float4> gSourceColor : register(t0);
Texture2D<float4> gEffectColor : register(t1);
Texture2D<uint> gSourceFlags : register(t2);
RWTexture2D<float4> gDestColor : register(u0);

//============================================================================
//	main
//============================================================================
[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID) {

	uint2 size;
	gDestColor.GetDimensions(size.x, size.y);
	if (any(dispatchThreadID.xy >= size)) {
		return;
	}

	static const uint kRenderingLayerMaskShift = 8u;
	static const uint kRenderingLayerMaskBits = 0x00FFFFFFu;
	const uint renderingLayerMask =
		(gSourceFlags[dispatchThreadID.xy] >>
			kRenderingLayerMaskShift) &
		kRenderingLayerMaskBits;
	const bool selected =
		(renderingLayerMask & targetMask) != 0u;
	gDestColor[dispatchThreadID.xy] =
		selected ?
		gEffectColor[dispatchThreadID.xy] :
		gSourceColor[dispatchThreadID.xy];
}
