#pragma once

//============================================================================
//	include
//============================================================================
#include "LightGPUTypes.h"
#include <Engine/Core/Rendering/Renderer/Lighting/FrameLightBatch.h>

#include <vector>

namespace Engine {

	//============================================================================
	//	ViewLightData class
	//	View別のCPUライトとCluster索引を保持する
	//============================================================================
	class ViewLightData {
		friend class ViewLightBufferSet;
	public:
		//========================================================================
		//	public Methods
		//========================================================================
		void Update(const PerViewLightSet& lightSet);
		void Reset();
	private:
		//========================================================================
		//	private Methods
		//========================================================================
		//--------- structure ----------------------------------------------------

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

		// 毎フレーム再利用するCPU側配列
		std::vector<DirectionalLightGPU> directionalScratch_{};
		std::vector<PointLightGPU> pointScratch_{};
		std::vector<RectLightGPU> rectScratch_{};
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
		bool cacheValid_ = false;

		//--------- functions ----------------------------------------------------

		// カメラ空間を分割してライト索引を構築する
		void BuildClusters(const PerViewLightSet& lightSet);
	};
}
