//============================================================================
//	resources
//============================================================================

cbuffer SkinningConstants : register(b0) {

	uint vertexCount;
	uint boneCount;
	uint skinnedInstanceCount;
	uint _pad0;
}
struct MeshVertex {

	float3 normal;
	float2 uv;
	float4 position;
};
struct VertexInfluence {

	float4 weights;
	int4 jointIndices;
};
struct WellForGPU {

	float4x4 skeletonSpaceMatrix;
	float4x4 skeletonSpaceInverseTransposeMatrix;
};
StructuredBuffer<MeshVertex> gInputVertices : register(t0);
StructuredBuffer<VertexInfluence> gVertexInfluences : register(t1);
StructuredBuffer<WellForGPU> gSkinningPalette : register(t2);
RWStructuredBuffer<MeshVertex> gSkinnedVertices : register(u0);

//============================================================================
//	functions
//============================================================================

float4 SkinPosition(float4 position, VertexInfluence influence, uint paletteOffset) {

	float4 outPos = 0.0f.xxxx;
	float totalWeight = 0.0f;
	[unroll]
	for (uint i = 0; i < 4; ++i) {

		float w = influence.weights[i];
		int jointIndex = influence.jointIndices[i];
		if (w <= 0.0f || jointIndex < 0) {
			continue;
		}
		outPos += mul(position, gSkinningPalette[paletteOffset + (uint) jointIndex].skeletonSpaceMatrix) * w;
		totalWeight += w;
	}

	// ウェイトが無い頂点はそのまま返す
	if (totalWeight <= 0.0f) {
		return position;
	}
	outPos /= totalWeight;
	outPos.w = 1.0f;
	return outPos;
}

float3 SkinNormal(float3 normal, VertexInfluence influence, uint paletteOffset) {

	float3 outNormal = 0.0f.xxx;
	float totalWeight = 0.0f;
	[unroll]
	for (uint i = 0; i < 4; ++i) {

		float w = influence.weights[i];
		int jointIndex = influence.jointIndices[i];
		if (w <= 0.0f || jointIndex < 0) {
			continue;
		}
		outNormal += mul(normal, (float3x3) gSkinningPalette[paletteOffset + (uint) jointIndex].skeletonSpaceInverseTransposeMatrix) * w;
		totalWeight += w;
	}

	// ウェイト無しなら元法線
	if (totalWeight <= 0.0f) {
		return normalize(normal);
	}
	return normalize(outNormal / totalWeight);
}

//============================================================================
//	main
//============================================================================
[numthreads(256, 1, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID, uint3 groupID : SV_GroupID) {

	uint vertexIndex = dispatchThreadID.x;
	uint skinnedInstanceIndex = groupID.y;

	// 範囲外アクセスを防止
	if (vertexCount <= vertexIndex || skinnedInstanceCount <= skinnedInstanceIndex) {
		return;
	}

	// 頂点とスキニング情報を読み込む
	MeshVertex input = gInputVertices[vertexIndex];
	VertexInfluence influence = gVertexInfluences[vertexIndex];
	
	// スキニングパレットのオフセットを計算
	uint paletteOffset = skinnedInstanceIndex * boneCount;

	MeshVertex output = input;
	
	// 頂点のスキニング処理
	output.position = SkinPosition(input.position, influence, paletteOffset);
	output.normal = SkinNormal(input.normal, influence, paletteOffset);

	// スキニング後の頂点を出力
	uint outputIndex = skinnedInstanceIndex * vertexCount + vertexIndex;
	gSkinnedVertices[outputIndex] = output;
}