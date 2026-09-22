#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Assets/RenderPipelineAsset.h>
#include <Engine/Core/Rendering/Assets/MaterialAsset.h>
#include <Engine/Core/Rendering/Meshes/GPUResource/MeshResourceTypes.h>
#include <Engine/Core/Rendering/Meshes/GPUResource/MeshShaderSharedTypes.h>
#include <Engine/Core/Rendering/Core/RenderingFeatureTypes.h>

// c++
#include <span>

namespace Engine {
	class RenderAssetLibrary;
	enum class MeshRenderFlags : uint32_t;
	struct ResolvedRenderView;
	struct SubMeshMaterial;
}

namespace Engine::RaytracingSceneGeometryUtility {

	// 描画Flagを変換する
	uint32_t ToRaytracingRenderFlags(Engine::MeshRenderFlags flags);
	// Cull設定を変換する
	D3D12_RAYTRACING_INSTANCE_FLAGS ToRaytracingCullFlags(
		const D3D12_RASTERIZER_DESC& rasterizer);
	// PrimitiveのPipelineを選択する
	const Engine::PipelineVariantDesc* ResolvePrimitivePipelineVariant(
		Engine::RenderAssetLibrary& assetLibrary,
		const Engine::MaterialAsset& material,
		Engine::MaterialSurfaceMode surfaceMode,
		const Engine::GraphicsRuntimeFeatures& runtimeFeatures);
	// Geometry配置を識別する
	uint64_t ComputeGeometryLayoutHash(
		std::span<const Engine::SubMeshMaterial> subMeshes,
		uint32_t geometryCount);
	// Material内容を識別する
	uint64_t ComputeSceneMaterialHash(
		std::span<const Engine::MeshSubMeshShaderData> subMeshes);
	// TLASの再構築条件を判定する
	bool RequiresTLASRebuildForTraceQuality(
		size_t instanceCount, uint32_t changedInstanceCount);
	// 行列の最大Scaleを求める
	float GetMatrixMaxScale(const Engine::Matrix4x4& matrix);
	// 包含する球を更新する
	void EncapsulateSphere(const Engine::Vector3& sourceCenter,
		float sourceRadius, Engine::Vector3& center, float& radius);
	// MeshのWorld Boundsを求める
	void CalculateMeshWorldBounds(
		const Engine::MeshGPUResource& meshResource,
		std::span<const Engine::SubMeshMaterial> subMeshes,
		const Engine::Matrix4x4& worldMatrix,
		Engine::Vector3& outCenter, float& outRadius);
	// LOD範囲を選択する
	const Engine::MeshLODRange& ResolveRaytracingLODRange(
		const Engine::SubMeshDesc& subMesh, uint32_t lodIndex);
	// 画面上の大きさからLODを選択する
	uint32_t ResolveMeshLOD(
		const Engine::GraphicsRuntimeFeatures& features,
		const Engine::ResolvedRenderView* cullingView,
		const Engine::Vector3& center, float radius);
	// LODのView条件を識別する
	uint64_t ComputeLODViewHash(
		const Engine::GraphicsRuntimeFeatures& features,
		const Engine::ResolvedRenderView* cullingView);
}
