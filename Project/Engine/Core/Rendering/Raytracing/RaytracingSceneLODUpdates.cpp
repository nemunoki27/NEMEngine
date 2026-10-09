#include "RaytracingSceneBuilder.h"

//============================================================================
//	include
//============================================================================
#include "RaytracingSceneGeometryUtility.h"
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Mesh/MeshRenderBackend.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>

// c++
#include <algorithm>

using namespace Engine::RaytracingSceneGeometryUtility;

//============================================================================
//	RaytracingSceneBuilder classMethods
//============================================================================
uint32_t Engine::RaytracingSceneBuilder::UpdateCachedLODSelections(MeshRenderBackend* meshBackend,
	const GraphicsRuntimeFeatures& runtimeFeatures, const ResolvedRenderView* lodView, bool& lodResourceMissing) {

	uint32_t changedCount = 0;
	if (!meshBackend) {
		return changedCount;
	}
	// 各Viewの距離に合わせて構築済みLODを選択
	for (CachedMeshLODInstance& record : cachedMeshLODInstances_) {

		const uint32_t lodIndex = ResolveMeshLOD(runtimeFeatures, lodView, record.worldBoundsCenter, record.worldBoundsRadius);
		if (lodIndex == record.lodIndex) {
			continue;
		}

		const MeshGPUResource* meshResource = meshBackend->FindMeshResource(record.meshAssetID);
		if (!meshResource || record.tlasInstanceIndex >= cachedTLASInstances_.size()) {
			lodResourceMissing = true;
			continue;
		}

		// 不足するBLASは通常の再構築へ戻す
		ID3D12Resource* blasResource = nullptr;
		if (record.usesInstanceBLAS) {

			StaticInstanceBLASKey key{};
			key.globalIllumination = globalIlluminationScene_;
			key.world = record.world;
			key.worldLifetime = record.world->GetLifetime();
			key.entity = record.entity;
			key.meshAssetID = record.meshAssetID;
			key.reloadGeneration = record.reloadGeneration;
			auto blasIt = blasCache_->staticInstanceBLASes_.find(key);
			if (blasIt == blasCache_->staticInstanceBLASes_.end() ||
				blasIt->second.lodGeometryLayoutHashes[lodIndex] != record.geometryLayoutHash ||
				!blasIt->second.lodBLASes[lodIndex].IsBuilt()) {
				lodResourceMissing = true;
				continue;
			}
			blasResource = blasIt->second.lodBLASes[lodIndex].GetResource();
		} else {

			BLASKey key{};
			key.meshAssetID = record.meshAssetID;
			key.reloadGeneration = record.reloadGeneration;
			key.lodIndex = lodIndex;
			key.geometryLayoutHash = record.geometryLayoutHash;
			auto blasIt = blasCache_->blases_.find(key);
			if (blasIt == blasCache_->blases_.end() || !blasIt->second.IsBuilt()) {
				lodResourceMissing = true;
				continue;
			}
			blasResource = blasIt->second.GetResource();
		}

		// BLASとShader側のGeometry番号を同じLODへ同期
		cachedTLASInstances_[record.tlasInstanceIndex].blas = blasResource;
		const size_t offset = record.geometryDataOffset;
		const size_t geometryCount = record.geometrySubMeshIndices.size();
		if (result_.sceneGeometryScratch_.size() < offset ||
			result_.sceneGeometryScratch_.size() - offset < geometryCount) {
			lodResourceMissing = true;
			continue;
		}
		const auto geometries =
			std::span<RaytracingGeometryShaderData>(result_.sceneGeometryScratch_).subspan(offset, geometryCount);
		if (!UpdateGeometryLODOffsets(meshResource->subMeshes, record.geometrySubMeshIndices, lodIndex, geometries)) {
			lodResourceMissing = true;
			continue;
		}
		record.lodIndex = lodIndex;
		++changedCount;
	}
	return changedCount;
}
