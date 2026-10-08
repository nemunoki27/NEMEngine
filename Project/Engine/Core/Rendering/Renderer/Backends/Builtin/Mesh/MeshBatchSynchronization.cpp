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
