#include "RaytracingTLASState.h"

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

namespace { constexpr uint32_t kMaxConsecutiveTLASRefits = 240; }

void Engine::RaytracingTLASState::ResetState() {

	firstTLASBuild_ = true;
	tlasInstanceHash_ = 0;
	consecutiveTLASRefitCount_ = 0;
}

void Engine::RaytracingTLASState::RecordInstances(std::span<const RaytracingTLASInstance> instances) {

	tlasInstanceHash_ = ComputeTLASInstanceHash(instances);
}

void Engine::RaytracingTLASState::BuildORUpdate(GraphicsCore& graphicsCore, const std::vector<RaytracingTLASInstance>& tlasInstances,
			const std::vector<RaytracingTLASInstance>& previousInstances, uint32_t previousCount, bool requireTlasRebuild) {

	auto* device = graphicsCore.GetDXObject().GetDevice();
	auto* commandList = graphicsCore.GetDXObject().GetDxCommand()->GetCommandList();
	const uint64_t tlasInstanceHash =
		ComputeTLASInstanceHash(tlasInstances);
	uint32_t changedInstanceCount = 0;
	if (previousInstances.size() == tlasInstances.size()) {
		for (size_t index = 0;
			index < tlasInstances.size(); ++index) {

			const RaytracingTLASInstance& previous =
				previousInstances[index];
			const RaytracingTLASInstance& current =
				tlasInstances[index];
			if (previous.blas != current.blas ||
				previous.instanceID != current.instanceID ||
				previous.hitGroupIndex != current.hitGroupIndex ||
				previous.mask != current.mask ||
				previous.flags != current.flags ||
				previous.worldMatrix != current.worldMatrix) {
				++changedInstanceCount;
			}
		}
	}
	const bool instanceCountChanged =
		tlas_.IsBuilt() &&
		previousCount != tlasInstances.size();
	const bool rebuildForTraceQuality =
		RequiresTLASRebuildForTraceQuality(
			tlasInstances.size(), changedInstanceCount);
	if (firstTLASBuild_ || !tlas_.IsBuilt() ||
		requireTlasRebuild || instanceCountChanged ||
		rebuildForTraceQuality) {

		tlas_.Build(device, commandList, tlasInstances, true);
		consecutiveTLASRefitCount_ = 0;
		firstTLASBuild_ = false;
		FrameProfiler::GetInstance().AddTLASBuild();
	} else if (tlasInstanceHash_ != tlasInstanceHash) {

		RefitORRebuild(graphicsCore, tlasInstances, false);
	} else {

		FrameProfiler::GetInstance().AddTLASSkip();
	}
	tlasInstanceHash_ = tlasInstanceHash;

}

uint64_t Engine::RaytracingTLASState::ComputeTLASInstanceHash(
	std::span<const RaytracingTLASInstance> instances) {

	uint64_t hash = 1469598103934665603ull;
	Algorithm::HashCombine(hash,
		static_cast<uint64_t>(instances.size()));
	for (const RaytracingTLASInstance& instance : instances) {

		Algorithm::HashCombine(hash, instance.blas ?
			instance.blas->GetGPUVirtualAddress() : 0);
		Algorithm::HashCombine(hash, instance.instanceID);
		Algorithm::HashCombine(hash, instance.hitGroupIndex);
		Algorithm::HashCombine(hash, instance.mask);
		Algorithm::HashCombine(hash,
			static_cast<uint32_t>(instance.flags));
		for (uint32_t row = 0; row < 4; ++row) {
			for (uint32_t column = 0; column < 4; ++column) {

				Algorithm::HashCombine(hash, std::bit_cast<uint32_t>(
					instance.worldMatrix.m[row][column]));
			}
		}
	}
	return hash;
}

void Engine::RaytracingTLASState::RefitORRebuild(
	GraphicsCore& graphicsCore,
	const std::vector<RaytracingTLASInstance>& instances,
	bool forceRebuild) {

	ID3D12GraphicsCommandList6* commandList =
		graphicsCore.GetDXObject().GetDxCommand()->GetCommandList();
	if (forceRebuild || kMaxConsecutiveTLASRefits <=
		consecutiveTLASRefitCount_ + 1) {

		tlas_.Rebuild(commandList, instances);
		consecutiveTLASRefitCount_ = 0;
		FrameProfiler::GetInstance().AddTLASBuild();
		return;
	}

	tlas_.Update(commandList, instances);
	++consecutiveTLASRefitCount_;
	FrameProfiler::GetInstance().AddTLASRefit();
}
