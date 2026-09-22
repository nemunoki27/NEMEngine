#include "RaytracingSceneBuilder.h"

//============================================================================
//	include
//============================================================================
#include "RaytracingSceneGeometryUtility.h"
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/Core/GraphicsFrameContext.h>
#include <Engine/Core/Rendering/Assets/RenderAssetLibrary.h>
#include <Engine/Core/Rendering/Materials/MaterialResolver.h>
#include <Engine/Core/Rendering/Materials/MaterialParameter.h>
#include <Engine/Core/Rendering/Renderer/Pipeline/RenderPipelineRunner.h>
#include <Engine/Core/Rendering/Renderer/Backends/Common/RenderBillboardUtility.h>
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Mesh/MeshDrawPathCommon.h>
#include <Engine/Core/World/Scene/Runtime/SceneInstanceManager.h>
#include <Engine/Core/World/Components/Rendering/MeshRendererComponent.h>
#include <Engine/Core/World/Components/Rendering/PrimitiveRendererComponent.h>
#include <Engine/Core/Rendering/Primitive/PrimitiveGeometryManager.h>
#include <Engine/Core/Rendering/Primitive/PrimitiveMeshGenerator.h>
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Mesh/MeshRenderBackend.h>

#include <Engine/Core/Rendering/Textures/RuntimeTextureResolver.h>
#include <Engine/Core/Rendering/Meshes/Utility/MeshNormalMatrixUtility.h>
#include <Engine/Core/Foundation/Time/FrameProfiler.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>

// c++
#include <bit>
#include <cmath>
#include <cstddef>
#include <memory>
#include <span>
#include <unordered_set>
#include <variant>

//============================================================================
//	RaytracingSceneBuilder internal
//============================================================================
using namespace Engine::RaytracingSceneGeometryUtility;

void Engine::RaytracingSceneBuilder::BuildPrimitiveInstances(
	std::span<const CollectedPrimitiveInstance> scenePrimitives, SceneBuildWork& work) {

	for (const CollectedPrimitiveInstance& src : scenePrimitives) {

		const PrimitiveRendererComponent& renderer = *src.renderer;
		AssetID materialID = work.materialResolver.ResolveORDefault(
			work.assetDatabase, src.material, DefaultMaterialSlot::Primitive);
		const MaterialAsset* material = work.assetLibrary.LoadMaterial(materialID);
		if (!material) {
			continue;
		}
		const PipelineVariantDesc* pipelineVariant =
			ResolvePrimitivePipelineVariant(
				work.assetLibrary, *material, src.surfaceMode, work.runtimeFeatures);
		// 半透明パスがないMaterialは通常描画と同じくPrimitive既定Materialへ戻す
		if (!pipelineVariant &&
			src.surfaceMode == MaterialSurfaceMode::Transparent) {

			materialID = work.materialResolver.ResolveORDefault(
				work.assetDatabase, AssetID{}, DefaultMaterialSlot::Primitive);
			material = work.assetLibrary.LoadMaterial(materialID);
			pipelineVariant = material ?
				ResolvePrimitivePipelineVariant(
					work.assetLibrary, *material, src.surfaceMode, work.runtimeFeatures) :
				nullptr;
		}
		if (!material || !pipelineVariant) {
			continue;
		}
		++work.blasGeometryCount;

		PrimitiveGeometry* geometry = work.primitiveGeometryManager->GetOrCreate(work.graphicsCore, src.geometryHash, renderer);
		if (!geometry) {
			continue;
		}
		// BLASを共有ジオメトリから作る、初めて作ったフレームだけTLASを完全再構築する
		const bool wasBuilt = geometry->blasBuilt;
		if (!work.primitiveGeometryManager->EnsureBLAS(work.device, work.commandList, *geometry)) {
			continue;
		}
		if (!wasBuilt) {
			FrameProfiler::GetInstance().AddBLASBuild(1);
			work.requireTlasRebuild = true;
		} else {

			FrameProfiler::GetInstance().AddBLASSkip(1);
		}

		const uint32_t subMeshDataIndex = static_cast<uint32_t>(result_.sceneSubMeshScratch_.size());

		const MeshSubMeshShaderData subMeshData = materialResolver_.BuildPrimitiveSubMeshData(
			work.graphicsCore, work.assetDatabase, *material,
			src.materialInstance, src.uvMatrix);
		result_.sceneSubMeshScratch_.emplace_back(subMeshData);

		RaytracingInstanceShaderData instanceShaderData{};
		instanceShaderData.vertexDescriptorIndex = geometry->vertexBuffer.srvIndex;
		instanceShaderData.indexDescriptorIndex = geometry->indexSRV.srvIndex;
		instanceShaderData.vertexOffset = 0;
		instanceShaderData.geometryDataOffset =
			static_cast<uint32_t>(result_.sceneGeometryScratch_.size());
		instanceShaderData.renderFlags = ToRaytracingRenderFlags(
			renderer.renderFlags);
		const uint32_t shaderInstanceIndex = static_cast<uint32_t>(result_.sceneInstanceScratch_.size());
		result_.sceneInstanceScratch_.emplace_back(instanceShaderData);

		const uint32_t pickRecordIndex =
			static_cast<uint32_t>(result_.scenePickRecords_.size());
		MeshSubMeshPickRecord pickRecord{};
		pickRecord.entity = src.entity;
		pickRecord.subMeshIndex = 0;
		result_.scenePickRecords_.emplace_back(pickRecord);
		result_.scenePickRecordOffsets_.emplace_back(pickRecordIndex);

		RaytracingGeometryShaderData geometryData{};
		geometryData.subMeshDataIndex = subMeshDataIndex;
		geometryData.pickRecordIndex = pickRecordIndex;
		result_.sceneGeometryScratch_.emplace_back(geometryData);

		RaytracingTLASInstance instance{};
		instance.blas = geometry->blas.GetResource();
		instance.instanceID = shaderInstanceIndex;
		instance.hitGroupIndex = 0;
		// CastShadow/CastReflectionに応じて影レイと反射レイの当たり判定を分ける
		instance.mask = kRaytracingMaskAlwaysHit;
		if (src.castShadows) {
			instance.mask |= kRaytracingMaskShadowCaster;
		}
		if (HasMeshRenderFlag(renderer.renderFlags, MeshRenderFlags::CastReflection)) {
			instance.mask |= kRaytracingMaskReflectionCaster;
		}
		instance.flags = ToRaytracingCullFlags(
			pipelineVariant->rasterizer);
		instance.worldMatrix = src.worldMatrix;
		work.tlasInstances.emplace_back(instance);
		work.tlasEntityKeys.emplace_back(
			SceneEntityKey{
				.world = src.world,
				.entity = src.entity,
			});
		work.meshLODRecordIndices.emplace_back(UINT32_MAX);
	}
}
