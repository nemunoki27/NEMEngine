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
	class BuiltinTextureLibrary;
	class RaytracingSceneBuilder;

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
		void Upload(const PerViewLightSet& lightSet, const RaytracingSceneBuilder* scene = nullptr);

		// 解放
		void Release();

		// 登録クラスへ公開
		void RegisterTo(RenderBufferRegistry& registry) const;

		//--------- accessor -----------------------------------------------------

		// 初期化されているか
		bool IsInitialized() const { return initialized_; }
		uint64_t GetShadowFilterRevision() const { return shadowFilterRevision_; }
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
		StructuredInstanceBuffer<uint32_t> shadowFilters_{ "gLightShadowFilters" };
		std::vector<uint32_t> shadowFilterData_;
		std::vector<uint32_t> shadowFilterScratch_;
		uint64_t shadowFilterRevision_ = 0;

		ViewLightData data_;
		BuiltinTextureLibrary* builtinTextures_ = nullptr;
		uint32_t rectLTCMatrixIndex_ = UINT32_MAX;
		uint32_t rectLTCAmplitudeIndex_ = UINT32_MAX;
		std::array<uint64_t, kGraphicsFrameContextCount> uploadedSerials_{};
		GraphicsFrameResourceRegistration frameRegistration_{};

		// 初期化フラグ
		bool initialized_ = false;

		//--------- functions ----------------------------------------------------
		
		// 確定済みCPUデータを現在のフレームスロットへ転送する
		void UploadCachedBuffers();
		// 現在のTLASに対応する除外一覧を作る
		void UpdateShadowFilters(const PerViewLightSet& lightSet, const RaytracingSceneBuilder* scene);
	};
}
