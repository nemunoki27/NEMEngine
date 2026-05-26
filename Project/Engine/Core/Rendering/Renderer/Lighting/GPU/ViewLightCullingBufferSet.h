#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Renderer/Lighting/GPU/LightCullingGPUTypes.h>
#include <Engine/Core/Rendering/Renderer/Lighting/FrameLightBatch.h>
#include <Engine/Core/Rendering/Renderer/Backends/Common/ViewConstantBuffer.h>
#include <Engine/Core/Rendering/Renderer/Views/RenderViewTypes.h>
#include <Engine/Core/Rendering/RHI/DirectX12/Buffers/RenderBufferRegistry.h>
#include <Engine/Core/Rendering/RHI/DirectX12/Buffers/D3D12RWStructuredBuffer.h>

namespace Engine {

	// front
	class GraphicsCore;

	//============================================================================
	//	ViewLightCullingBufferSet class
	//	1つの描画ビューに対するライトカリングのためのGPUバッファセット
	//============================================================================
	class ViewLightCullingBufferSet {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		ViewLightCullingBufferSet() = default;
		~ViewLightCullingBufferSet() = default;

		// 初期化
		void Init(GraphicsCore& graphicsCore);

		// CPUのライト情報を基にGPU用のライトカリングデータを生成してアップロード
		void Upload(const ResolvedRenderView& view, const PerViewLightSet& lightSet, uint32_t lightCullingMode);

		// 解放
		void Release();

		// 登録クラスへ公開
		void RegisterTo(RenderBufferRegistry& registry) const;

		//--------- accessor -----------------------------------------------------

		// 初期化されているか
		bool IsInitialized() const { return initialized_; }

		// Clustered Forwardの初期実装は既存Forward+と同じXYタイルサイズを使う
		static constexpr uint32_t kTileSizeX = 16;
		static constexpr uint32_t kTileSizeY = 16;
		static constexpr uint32_t kMaxLocalLightsPerTile = 64;
		static constexpr uint32_t kClusterCountZ = 16;
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		// バッファ
		ViewConstantBuffer<LightCullingParamsGPU> params_{ "LightCullingParams" };
		StructuredRWBuffer<TileLightGridEntryGPU> tileLightGrid_{ "gTileLightGrid" };
		StructuredRWBuffer<uint32_t> tileLightIndexList_{ "gTileLightIndexList" };

		// タイル数
		uint32_t tileCountX_ = 0;
		uint32_t tileCountY_ = 0;
		uint32_t totalTileCount_ = 0;
		uint32_t totalClusterCount_ = 0;
		uint32_t totalIndexCount_ = 0;

		// 初期化フラグ
		bool initialized_ = false;
	};
}
