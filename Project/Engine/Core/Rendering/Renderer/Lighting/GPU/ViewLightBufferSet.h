#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Renderer/Lighting/GPU/LightGPUTypes.h>
#include <Engine/Core/Rendering/Renderer/Lighting/FrameLightBatch.h>
#include <Engine/Core/Rendering/DxObject/Buffers/RenderBufferRegistry.h>
#include <Engine/Core/Rendering/Renderer/Backends/Common/StructuredInstanceBuffer.h>
#include <Engine/Core/Rendering/Renderer/Backends/Common/ViewConstantBuffer.h>

// c++
#include <array>
#include <string>
#include <vector>

namespace Engine {

	// front
	class GraphicsCore;

	//============================================================================
	//	ViewLightBufferSet class
	//	1ビュー分のライトをGPUへ渡すためのバッファセット
	//============================================================================
	class ViewLightBufferSet {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		ViewLightBufferSet() = default;
		~ViewLightBufferSet() = default;

		// 初期化
		void Init(GraphicsCore& graphicsCore);

		// CPU側のライト集合をGPU用に変換してアップロード
		void Upload(const PerViewLightSet& lightSet);

		// 解放
		void Release();

		// 登録クラスへ公開
		void RegisterTo(RenderBufferRegistry& registry) const;

		//--------- accessor -----------------------------------------------------

		// 初期化されているか
		bool IsInitialized() const { return initialized_; }
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		struct ClusterBoundsScratch {

			uint32_t minX = 0;
			uint32_t maxX = 0;
			uint32_t minY = 0;
			uint32_t maxY = 0;
			uint32_t minZ = 0;
			uint32_t maxZ = 0;
			uint32_t lightIndex = 0;
		};

		//--------- variables ----------------------------------------------------

		// バッファ
		ViewConstantBuffer<LightCountsGPU> lightCounts_{ "LightCounts" };
		StructuredInstanceBuffer<DirectionalLightGPU> directionalLights_{ "gDirectionalLights" };
		StructuredInstanceBuffer<PointLightGPU> pointLights_{ "gPointLights" };
		StructuredInstanceBuffer<SpotLightGPU> spotLights_{ "gSpotLights" };
		ViewConstantBuffer<LightClusterConstantsGPU> clusterConstants_{ "LightClusterConstants" };
		StructuredInstanceBuffer<LightClusterHeaderGPU> clusterHeaders_{ "gLightClusterHeaders" };
		StructuredInstanceBuffer<uint32_t> clusterLightIndices_{ "gLightClusterIndices" };

		// 毎フレーム再利用するCPU側配列
		std::vector<DirectionalLightGPU> directionalScratch_{};
		std::vector<PointLightGPU> pointScratch_{};
		std::vector<SpotLightGPU> spotScratch_{};
		std::vector<LightClusterHeaderGPU> clusterHeaderScratch_{};
		std::vector<uint32_t> clusterLightIndexScratch_{};
		std::vector<uint32_t> clusterCountScratch_{};
		std::vector<uint32_t> clusterCursorScratch_{};
		std::vector<ClusterBoundsScratch> clusterBoundsScratch_{};
		std::vector<uint32_t> clusterWorkerCursorScratch_{};
		std::vector<uint32_t> clusterWorkerIndicesScratch_{};
		LightCountsGPU lightCountsScratch_{};
		LightClusterConstantsGPU clusterConstantsScratch_{};

		// ライト集合とカメラが同じ間はCPUクラスタを再構築せずフレーム別GPUバッファだけ更新する
		uint64_t cachedLightRevision_ = 0;
		uint64_t cachedViewHash_ = 0;
		uint64_t dataSerial_ = 0;
		std::array<uint64_t, kGraphicsFrameContextCount> uploadedSerials_{};
		bool cacheValid_ = false;

		// 初期化フラグ
		bool initialized_ = false;

		//--------- functions ----------------------------------------------------
		
		// CPU側のライト構造体をGPU用の構造体に変換
		static DirectionalLightGPU ToGPU(const DirectionalLightItem& item);
		static PointLightGPU ToGPU(const PointLightItem& item);
		static SpotLightGPU ToGPU(const SpotLightItem& item);
		// 確定済みCPUデータを現在のフレームスロットへ転送する
		void UploadCachedBuffers();
		// カメラ空間をタイルと対数深度へ分割してローカルライト索引を構築する
		void BuildClusters(const PerViewLightSet& lightSet);
	};
}

