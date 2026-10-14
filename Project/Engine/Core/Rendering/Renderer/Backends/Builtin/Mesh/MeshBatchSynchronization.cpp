#include "MeshBatchResources.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/DxObject/Common/DxUtils.h>
#include <Engine/Core/Rendering/Renderer/Backends/Common/BackendDrawCommon.h>
#include <Engine/Core/Rendering/Renderer/Backends/Common/RenderBillboardUtility.h>
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Mesh/MeshDrawPathCommon.h>
#include <Engine/Core/Rendering/Textures/RuntimeTextureResolver.h>
#include <Engine/Core/Rendering/Materials/MaterialParameterBufferBuilder.h>
#include <Engine/Core/Rendering/DxObject/Descriptors/DxShaderResourceView.h>
#include <Engine/Core/Rendering/Meshes/GPUResource/MeshResourceTypes.h>
#include <Engine/Core/Rendering/Meshes/Utility/MeshNormalMatrixUtility.h>
#include <Engine/Core/World/Components/Rendering/MeshRendererComponent.h>
#include <Engine/Core/World/Components/Rendering/InvertedHullOutlineComponent.h>
#include <Engine/Core/World/Components/Animation/SkinnedAnimationComponent.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/Foundation/Diagnostics/Assert.h>
#include <Engine/Core/Foundation/Time/FrameProfiler.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>

// c++
#include <cmath>
#include <unordered_map>
#include <variant>

//============================================================================
//	MeshBatchResources classMethods
//============================================================================
bool Engine::MeshBatchResources::MatchesBatch(const RenderSceneBatch& batch,
	std::span<const RenderItem* const> items, const MeshGPUResource& gpuMesh) const {

	// CPU側の構成と更新世代を照合
	return batchIdentity_.Matches(batch, items, gpuMesh);
}

bool Engine::MeshBatchResources::RefreshSkinningPose(const RenderSceneBatch& batch,
	std::span<const RenderItem* const> items, const MeshGPUResource& gpuMesh) {

	if (!gpuMesh.isSkinned || !skinning_) return false;
	uint64_t poseHash = 1469598103934665603ull;
	Algorithm::HashCombine(poseHash, gpuMesh.reloadGeneration);
	uint32_t skinnedCount = 0;
	for (const RenderItem* item : items) {
		if (!item || !batch.GetPayload<MeshRenderPayload>(*item) || !item->world) continue;
		const ECSWorld& world = *item->world;
		const auto* runtime = TryGetSkinnedAnimationRuntime(world, item->entity);
		if (!runtime || !runtime->initialized || runtime->palette.size() != gpuMesh.boneCount) continue;
		uint32_t vertexOffset = 0;
		if (!FindSkinnedVertexOffset(item->world, item->entity, vertexOffset) ||
			vertexOffset != skinnedCount * gpuMesh.vertexCount) return false;
		Algorithm::HashCombine(poseHash, static_cast<uint64_t>(item->entity.index));
		Algorithm::HashCombine(poseHash, static_cast<uint64_t>(item->entity.generation));
		Algorithm::HashCombine(poseHash, runtime->poseGeneration);
		++skinnedCount;
	}
	if (skinnedCount != skinnedInstanceCount_) return false;
	Algorithm::HashCombine(poseHash, skinnedCount);
	if (poseHash == currentSkinningPoseHash_) return true;
	// 新しいポーズだけを公開し、前の頂点結果を失効させる
	paletteCandidateScratch_.clear();
	paletteCandidateScratch_.reserve(paletteScratch_.size());
	for (const RenderItem* item : items) {
		if (!item || !batch.GetPayload<MeshRenderPayload>(*item) || !item->world) continue;
		const ECSWorld& world = *item->world;
		const auto* runtime = TryGetSkinnedAnimationRuntime(world, item->entity);
		if (!runtime || !runtime->initialized || runtime->palette.size() != gpuMesh.boneCount) continue;
		paletteCandidateScratch_.insert(paletteCandidateScratch_.end(),
			runtime->palette.begin(), runtime->palette.end());
	}
	paletteScratch_.swap(paletteCandidateScratch_);
	currentSkinningPoseHash_ = poseHash;
	skinningDispatched_ = false;
	return true;
}

uint32_t Engine::MeshBatchResources::RefreshMaterialColors() {

	FrameProfiler::ScopedSample total(FrameProfiler::Category::MeshBatchUpload);
	FrameProfiler::ScopedSample build(FrameProfiler::Category::MeshMaterialBuild);

	uint32_t changed = 0;
	for (size_t i = 0; i < batchIdentity_.cachedInstances_.size(); ++i) {
		auto& cached = batchIdentity_.cachedInstances_[i];
		if (!cached.world || !cached.worldLifetime->IsAlive() || !cached.world->IsAlive(cached.entity)) {
			continue;
		}
		const uint64_t revision = cached.world->GetMeshColorRevision(cached.entity);
		if (revision == cached.colorRevision) {
			continue;
		}
		const auto subMeshes = GetMeshSubMeshes(*cached.world, cached.entity);
		const auto& instance = meshScratch_[i];
		for (uint32_t local = 0; local < instance.subMeshCount; ++local) {
			const uint32_t source = cached.subMeshIndex == kAllMeshSubMeshes ? local : cached.subMeshIndex;
			if (source >= subMeshes.size()) {
				continue;
			}
			const auto* value = subMeshes[source].materialInstance.Find(MaterialParameterIDs::BaseColor);
			if (!value) {
				continue;
			}
			const uint32_t destination = instance.subMeshDataOffset + local;
			subMeshParamScratch_[destination].Set(MaterialParameterIDs::BaseColor,
				MaterialParameterNames::BaseColor, MaterialParameterSemantic::BaseColor, *value);
			subMeshParamGenerations_[destination] = ++parameterGeneration_;
		}
		cached.colorRevision = revision;
		++changed;
	}
	if (changed) {
		FrameProfiler::GetInstance().AddMeshUpdate(0, 0, 1, 0, changed);
	}
	return changed;
}

void Engine::MeshBatchResources::RefreshBatchTransforms(std::span<const RenderItem* const> items) {

	FrameProfiler::ScopedSample total(FrameProfiler::Category::MeshBatchUpload);
	FrameProfiler::ScopedSample build(FrameProfiler::Category::MeshBatchBuild);

	uint32_t changed = 0;
	for (size_t i = 0; i < items.size(); ++i) {
		auto& instance = meshScratch_[i];
		const auto& item = *items[i];
		if (instance.worldMatrix == item.worldMatrix) {
			continue;
		}
		instance.worldMatrix = item.worldMatrix;
		instance.previousWorldMatrix = item.previousWorldMatrix;
		instance.motionFrameSerial = item.motionFrameSerial;
		const auto normal = BuildSafeMeshNormalMatrix(instance.worldMatrix);
		instance.normalMatrix = normal.matrix;
		instance.orientationSign = normal.orientationSign;
		meshData_.MarkDirtyRange(static_cast<uint32_t>(i), 1);
		++changed;
	}
	if (changed) {
		FrameProfiler::GetInstance().AddMeshUpdate(0, 1, 0, 0, changed);
	}
}
