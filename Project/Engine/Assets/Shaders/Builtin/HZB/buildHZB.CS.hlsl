//============================================================================
//	resources
//============================================================================

cbuffer HZBBuildConstants : register(b0) {

	uint sourceWidth;
	uint sourceHeight;
	uint destWidth;
	uint destHeight;
};

Texture2D<float> gSourceDepth : register(t0);
RWTexture2D<float> gDestHZB : register(u0);

//============================================================================
//	main
//============================================================================
[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID) {

	if (dispatchThreadID.x >= destWidth || dispatchThreadID.y >= destHeight) {
		return;
	}

	uint2 dst = dispatchThreadID.xy;
	uint2 src0 = min(dst * 2u, uint2(sourceWidth - 1u, sourceHeight - 1u));
	uint2 src1 = min(src0 + uint2(1u, 0u), uint2(sourceWidth - 1u, sourceHeight - 1u));
	uint2 src2 = min(src0 + uint2(0u, 1u), uint2(sourceWidth - 1u, sourceHeight - 1u));
	uint2 src3 = min(src0 + uint2(1u, 1u), uint2(sourceWidth - 1u, sourceHeight - 1u));

	if (sourceWidth == destWidth && sourceHeight == destHeight) {
		gDestHZB[dst] = gSourceDepth.Load(int3(dst, 0));
		return;
	}

	float z0 = gSourceDepth.Load(int3(src0, 0));
	float z1 = gSourceDepth.Load(int3(src1, 0));
	float z2 = gSourceDepth.Load(int3(src2, 0));
	float z3 = gSourceDepth.Load(int3(src3, 0));
	gDestHZB[dst] = max(max(z0, z1), max(z2, z3));
}
