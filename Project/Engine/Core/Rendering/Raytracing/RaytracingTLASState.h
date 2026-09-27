#pragma once

//============================================================================
//	include
//============================================================================
#include "AccelerationStructure/TopLevelAccelerationStructure.h"

namespace Engine {

	class GraphicsCore;

	//============================================================================
	//	RaytracingTLASState class
	//	TLASの構築状態と更新条件を所有する
	//============================================================================
	class RaytracingTLASState {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		// Scene初期化時の更新履歴を戻す
		void ResetState();
		// 配置の差分に応じて構築方法を決める
		void BuildORUpdate(GraphicsCore& graphicsCore, const std::vector<RaytracingTLASInstance>& tlasInstances,
			const std::vector<RaytracingTLASInstance>& previousInstances, uint32_t previousCount, bool requireTlasRebuild, bool blasContentsChanged);
		// GPU接続を指定して同じ更新判定を使用する
		void BuildORUpdate(ID3D12Device8* device, ID3D12GraphicsCommandList6* commandList, GraphicsResourceRetirement& retirement,
			const std::vector<RaytracingTLASInstance>& tlasInstances, const std::vector<RaytracingTLASInstance>& previousInstances,
			uint32_t previousCount, bool requireTlasRebuild, bool blasContentsChanged);
		// 連続更新の上限で再構築する
		void RefitORRebuild(GraphicsCore& graphicsCore, const std::vector<RaytracingTLASInstance>& instances, bool forceRebuild);
		// 比較用の配置を記録する
		void RecordInstances(std::span<const RaytracingTLASInstance> instances);

		bool IsBuilt() const { return tlas_.IsBuilt(); }
		ID3D12Resource* GetResource() const { return tlas_.GetResource(); }
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		TopLevelAccelerationStructure tlas_{};
		bool firstTLASBuild_ = true;
		uint64_t tlasInstanceHash_ = 0;
		uint32_t consecutiveTLASRefitCount_ = 0;

		// 配置の比較用ハッシュを計算する
		static uint64_t ComputeTLASInstanceHash(std::span<const RaytracingTLASInstance> instances);
		void RefitORRebuild(ID3D12GraphicsCommandList6* commandList,
			const std::vector<RaytracingTLASInstance>& instances, bool forceRebuild);
	};
}
