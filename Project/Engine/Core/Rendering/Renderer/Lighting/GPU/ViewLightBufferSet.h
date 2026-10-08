#pragma once

//============================================================================
//	include
//============================================================================
#include "ViewLightData.h"
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

		//--------- variables ----------------------------------------------------

		// バッファ
		ViewConstantBuffer<LightCountsGPU> lightCounts_{ "LightCounts" };
		StructuredInstanceBuffer<DirectionalLightGPU> directionalLights_{ "gDirectionalLights" };
		StructuredInstanceBuffer<PointLightGPU> pointLights_{ "gPointLights" };
		StructuredInstanceBuffer<RectLightGPU> rectLights_{ "gRectLights" };
		StructuredInstanceBuffer<SpotLightGPU> spotLights_{ "gSpotLights" };
		ViewConstantBuffer<LightClusterConstantsGPU> clusterConstants_{ "LightClusterConstants" };
		StructuredInstanceBuffer<LightClusterHeaderGPU> clusterHeaders_{ "gLightClusterHeaders" };
		StructuredInstanceBuffer<uint32_t> clusterLightIndices_{ "gLightClusterIndices" };

		ViewLightData data_;
		std::array<uint64_t, kGraphicsFrameContextCount> uploadedSerials_{};

		// 初期化フラグ
		bool initialized_ = false;

		//--------- functions ----------------------------------------------------
		
		// 確定済みCPUデータを現在のフレームスロットへ転送する
		void UploadCachedBuffers();
	};
}
