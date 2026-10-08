#include "RaytracingTLASState.h"

//============================================================================
//	include
//============================================================================
#include "RaytracingSceneGeometryUtility.h"
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/DxObject/Core/DxCommand.h>
#include <Engine/Core/Foundation/Time/FrameProfiler.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>

// c++
#include <bit>

//============================================================================
//	RaytracingTLASState internal
//============================================================================
using namespace Engine::RaytracingSceneGeometryUtility;

namespace {

	// 連続refitの上限
	constexpr uint32_t kMaxConsecutiveTLASRefits = 240;
}

void Engine::RaytracingTLASState::ResetState() {

	// 初回構築の状態へ戻す
	firstTLASBuild_ = true;
	tlasInstanceHash_ = 0;
	consecutiveTLASRefitCount_ = 0;
}

void Engine::RaytracingTLASState::RecordInstances(std::span<const RaytracingTLASInstance> instances) {

	// 次回の差分比較に使う配置を記録
	tlasInstanceHash_ = ComputeTLASInstanceHash(instances);
}

void Engine::RaytracingTLASState::BuildORUpdate(GraphicsCore& graphicsCore, const std::vector<RaytracingTLASInstance>& tlasInstances,
			const std::vector<RaytracingTLASInstance>& previousInstances, uint32_t previousCount, bool requireTLASRebuild, bool blasContentsChanged) {

	auto& dxObject = graphicsCore.GetDXObject();
	BuildORUpdate(dxObject.GetDevice(), dxObject.GetDxCommand()->GetCommandList(), dxObject.GetResourceRetirement(),
		tlasInstances, previousInstances, previousCount, requireTLASRebuild, blasContentsChanged);
}

void Engine::RaytracingTLASState::BuildORUpdate(ID3D12Device8* device, ID3D12GraphicsCommandList6* commandList,
	GraphicsResourceRetirement& retirement, const std::vector<RaytracingTLASInstance>& tlasInstances,
	const std::vector<RaytracingTLASInstance>& previousInstances, uint32_t previousCount, bool requireTLASRebuild, bool blasContentsChanged) {

	const uint64_t tlasInstanceHash =
		ComputeTLASInstanceHash(tlasInstances);
	// 前回の配置と比較して変更数を数える
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
		RequiresTLASRebuildForTraceQuality(tlasInstances.size(), changedInstanceCount);
	if (firstTLASBuild_ || !tlas_.IsBuilt() ||
		requireTLASRebuild || instanceCountChanged ||
		rebuildForTraceQuality) {

		// 形状や配置の変更に合わせて再構築
		tlas_.SetRetirementQueue(retirement);
		tlas_.Build(device, commandList, tlasInstances, true);
		consecutiveTLASRefitCount_ = 0;
		firstTLASBuild_ = false;
		FrameProfiler::GetInstance().AddTLASBuild();
	} else if (blasContentsChanged || tlasInstanceHash_ != tlasInstanceHash) {

		// BLASのアドレスが同じでも境界を更新する
		RefitORRebuild(commandList, tlasInstances, false);
	} else {

		FrameProfiler::GetInstance().AddTLASSkip();
	}
	tlasInstanceHash_ = tlasInstanceHash;

}

uint64_t Engine::RaytracingTLASState::ComputeTLASInstanceHash(
	std::span<const RaytracingTLASInstance> instances) {

	// BLASと描画条件を同じ順で集約
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

				Algorithm::HashCombine(hash, std::bit_cast<uint32_t>(instance.worldMatrix.m[row][column]));
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
	RefitORRebuild(commandList, instances, forceRebuild);
}

void Engine::RaytracingTLASState::RefitORRebuild(ID3D12GraphicsCommandList6* commandList,
	const std::vector<RaytracingTLASInstance>& instances, bool forceRebuild) {

	if (forceRebuild || kMaxConsecutiveTLASRefits <=
		consecutiveTLASRefitCount_ + 1) {

		// 連続更新で劣化した探索構造を再構築
		tlas_.Rebuild(commandList, instances);
		consecutiveTLASRefitCount_ = 0;
		FrameProfiler::GetInstance().AddTLASBuild();
		return;
	}

	// 配置の差分だけを更新
	tlas_.Update(commandList, instances);
	++consecutiveTLASRefitCount_;
	FrameProfiler::GetInstance().AddTLASRefit();
}
