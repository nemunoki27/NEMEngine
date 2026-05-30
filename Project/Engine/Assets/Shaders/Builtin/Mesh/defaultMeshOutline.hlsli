//============================================================================
//	背面法アウトライン 共通定義
//============================================================================

#include "defaultMesh.hlsli"

//============================================================================
//	定数
//============================================================================

static const uint OUTLINE_EXPANSION_NORMAL_DIRECTION = 0u;
static const uint OUTLINE_EXPANSION_POSITION_SCALING = 1u;
static const uint OUTLINE_WIDTH_MODEL_UNITS = 0u;
static const uint OUTLINE_WIDTH_SCREEN_PIXELS = 1u;
static const uint MESH_OUTLINE_FLAG_USE_BAKED_NORMAL = 1u << 0;
static const uint MESH_OUTLINE_FLAG_USE_OUTLINE_SAMPLER = 1u << 1;

//============================================================================
//	resources
//============================================================================

SamplerState gOutlineSampler : register(s0);

struct MeshOutlineGPUData {

	float4 color;

	float width;
	float cameraZOffset;
	uint expansionMode;
	uint widthMode;

	uint bakedNormalTextureIndex;
	uint outlineSamplerTextureIndex;
	uint flags;
	uint _pad0;
};
// 既存割り当て(t0-t5,t9 space1)と衝突しない番号を使用する
StructuredBuffer<MeshOutlineGPUData> gMeshOutlines : register(t7, space1);

struct OutlineVertexOutput {

	float4 position : SV_Position;
	nointerpolation float4 color : COLOR0;
};

//============================================================================
//	functions
//============================================================================

// Outline Samplerによる部位別アウトライン幅の乗数を取得する。VS/MSではSampleLevelを使う
float SampleOutlineWidthMultiplier(MeshOutlineGPUData outline, float2 uv) {

	if ((outline.flags & MESH_OUTLINE_FLAG_USE_OUTLINE_SAMPLER) == 0u ||
		outline.outlineSamplerTextureIndex == 0xFFFFFFFFu) {
		return 1.0f;
	}

	Texture2D<float4> tex = ResourceDescriptorHeap[
		NonUniformResourceIndex(outline.outlineSamplerTextureIndex)];
	return saturate(tex.SampleLevel(gOutlineSampler, uv, 0.0f).r);
}

// Baked Normal Textureがあればそれを、なければ頂点法線を返す。object/local空間normalを想定する
float3 ResolveOutlineLocalNormal(MeshOutlineGPUData outline, MeshVertex vertex) {

	if ((outline.flags & MESH_OUTLINE_FLAG_USE_BAKED_NORMAL) == 0u ||
		outline.bakedNormalTextureIndex == 0xFFFFFFFFu) {
		return normalize(vertex.normal);
	}

	Texture2D<float4> tex = ResourceDescriptorHeap[
		NonUniformResourceIndex(outline.bakedNormalTextureIndex)];
	float3 encoded = tex.SampleLevel(gOutlineSampler, vertex.uv, 0.0f).xyz;
	float3 normal = encoded * 2.0f - 1.0f;
	return normalize(normal);
}

// Camera Z Offset。カメラからワールド頂点へ向かう方向へ押し込む
float3 ApplyOutlineCameraZOffset(float3 worldPos, float cameraZOffset) {

	float3 fromCamera = worldPos - renderCameraPos;
	float len = length(fromCamera);
	if (len <= 0.00001f || abs(cameraZOffset) <= 0.00001f) {
		return worldPos;
	}
	return worldPos + fromCamera / len * cameraZOffset;
}

// ScreenPixels膨張。clip/NDC上でXY offsetを加えて画面上の線幅を一定にする
float4 ApplyScreenPixelOutlineOffset(float3 worldPos, float3 worldDirection, float widthPixels) {

	float4 clip = mul(float4(worldPos, 1.0f), viewProjection);
	float4 dirClip = mul(float4(worldPos + worldDirection, 1.0f), viewProjection);

	if (abs(clip.w) <= 0.00001f || abs(dirClip.w) <= 0.00001f) {
		return clip;
	}

	float2 ndc = clip.xy / clip.w;
	float2 dirNdc = dirClip.xy / dirClip.w;
	float2 projectedDir = dirNdc - ndc;
	float projectedLen = length(projectedDir);
	if (projectedLen <= 0.00001f) {
		return clip;
	}

	float2 safeViewSize = max(viewSize, float2(1.0f, 1.0f));
	// 非一様なviewSizeでもX/Y別々にピクセル変換できるよう、ピクセル->NDC係数を軸別に持つ
	float2 pixelToNdc = 2.0f / safeViewSize;
	float2 ndcOffset = normalize(projectedDir) * widthPixels * pixelToNdc;
	clip.xy += ndcOffset * clip.w;
	return clip;
}

// VS/MS共通の膨張頂点生成。両経路で完全に同じロジックを使う
OutlineVertexOutput BuildOutlineVertex(uint instanceID, uint localSubMeshIndex,
	MeshVertex vertex, float4x4 worldMatrix) {

	MeshInstance instance = gMeshInstances[instanceID];
	MeshOutlineGPUData outline = gMeshOutlines[instance.outlineDataIndex];
	SubMeshShaderData subMesh = GetInstanceSubMesh(instanceID, localSubMeshIndex);

	float widthMultiplier = SampleOutlineWidthMultiplier(outline, vertex.uv);
	float3 localNormal = ResolveOutlineLocalNormal(outline, vertex);

	float4 localPos = vertex.position;
	float3 localDirection = localNormal;

	// Position Scalingはピボットからの放射方向へ押し出す
	if (outline.expansionMode == OUTLINE_EXPANSION_POSITION_SCALING) {
		float3 fromPivot = localPos.xyz - subMesh.sourcePivot;
		if (length(fromPivot) > 0.00001f) {
			localDirection = normalize(fromPivot);
		}
	}

	// ModelUnitsはモデル空間で頂点を膨張させる
	if (outline.widthMode == OUTLINE_WIDTH_MODEL_UNITS) {
		localPos.xyz += localDirection * outline.width * widthMultiplier;
	}

	float3 worldPos = mul(localPos, worldMatrix).xyz;
	worldPos = ApplyOutlineCameraZOffset(worldPos, outline.cameraZOffset);

	OutlineVertexOutput output;
	if (outline.widthMode == OUTLINE_WIDTH_SCREEN_PIXELS) {

		float3 worldDirection = normalize(mul(localDirection, (float3x3)worldMatrix));
		output.position = ApplyScreenPixelOutlineOffset(
			worldPos, worldDirection, outline.width * widthMultiplier);
	} else {

		output.position = mul(float4(worldPos, 1.0f), viewProjection);
	}
	output.color = outline.color;
	return output;
}
