#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Graphics/Render/Light/GPU/LightGpuTypes.h>
#include <Engine/Core/Graphics/Render/Light/FrameLightBatch.h>
#include <Engine/Core/Graphics/GPUBuffer/RenderBufferRegistry.h>
#include <Engine/Core/Graphics/Render/Backend/Common/StructuredInstanceBuffer.h>
#include <Engine/Core/Graphics/Render/Backend/Common/ViewConstantBuffer.h>

// c++
#include <string>

namespace Engine {

	// front
	class GraphicsCore;

	//============================================================================
	//	ViewLightBufferSet class
	//	1ビュー分のライトをGPUへ渡すためのバッファセット
	//============================================================================
	class ViewLightBufferSet {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

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
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		// バッファ
		ViewConstantBuffer<LightCountsGPU> lightCounts_{ "LightCounts" };
		StructuredInstanceBuffer<DirectionalLightGPU> directionalLights_{ "gDirectionalLights" };
		StructuredInstanceBuffer<PointLightGPU> pointLights_{ "gPointLights" };
		StructuredInstanceBuffer<SpotLightGPU> spotLights_{ "gSpotLights" };

		// 毎フレーム再利用するCPU側配列
		std::vector<DirectionalLightGPU> directionalScratch_{};
		std::vector<PointLightGPU> pointScratch_{};
		std::vector<SpotLightGPU> spotScratch_{};

		// 初期化フラグ
		bool initialized_ = false;

		//--------- functions ----------------------------------------------------
		
		// CPU側のライト構造体をGPU用の構造体に変換
		static DirectionalLightGPU ToGPU(const DirectionalLightItem& item);
		static PointLightGPU ToGPU(const PointLightItem& item);
		static SpotLightGPU ToGPU(const SpotLightItem& item);
	};
}