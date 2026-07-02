#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Pipelines/PipelineState.h>
#include <Engine/Core/Rendering/Pipelines/Bind/PipelineBindingCache.h>
#include <Engine/Core/Rendering/DxObject/Buffers/DxConstantBuffer.h>
#include <Engine/Core/Rendering/DxObject/Common/ComPtr.h>
#include <Engine/Core/Assets/AssetTypes.h>

namespace Engine {

	// front
	class GraphicsCore;

	//============================================================================
	//	SkyboxIrradianceMap class
	//	skyboxのcubemapを畳み込んで拡散IBL用の放射照度cubemapを作るクラス
	//============================================================================
	class SkyboxIrradianceMap {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		SkyboxIrradianceMap();
		~SkyboxIrradianceMap() = default;

		// 元cubemapが変わった時だけ畳み込みを実行して放射照度cubemapを更新する
		void Update(GraphicsCore& graphicsCore, AssetID sourceAssetID, uint32_t sourceSRVIndex);

		//--------- accessor -----------------------------------------------------

		// 畳み込み済みならSRVインデックス、未構築ならUINT32_MAXを返す
		uint32_t GetSRVIndex() const { return built_ ? srvIndex_ : UINT32_MAX; }
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- structure ----------------------------------------------------

		// b0へ渡す畳み込み定数
		struct IrradianceConstants {

			uint32_t sourceCubemapIndex = 0xFFFFFFFF;
			uint32_t faceSize = 0;
			uint32_t _pad[2] = { 0, 0 };
		};

		//--------- variables ----------------------------------------------------

		// 放射照度cubemapの1面のサイズ
		static constexpr uint32_t kFaceSize = 32;

		// 畳み込みコンピュートパイプライン
		PipelineState pipeline_{};
		bool initialized_ = false;

		// 放射照度cubemap
		ComPtr<ID3D12Resource> cubemap_{};
		uint32_t srvIndex_ = UINT32_MAX;
		uint32_t uavIndex_ = UINT32_MAX;
		D3D12_RESOURCE_STATES cubemapState_ = D3D12_RESOURCE_STATE_COMMON;

		// 畳み込み定数バッファ
		DxConstBuffer<IrradianceConstants> constants_{};

		// 構築済みの元cubemap、変化した時だけ再畳み込みする
		AssetID builtAssetID_{};
		uint32_t builtSRVIndex_ = UINT32_MAX;
		bool built_ = false;

		// 定数と出力UAVのスロット
		PipelineBindingCache bindCache_{};
		PipelineBindingCache::SlotID constantsSlot_ = PipelineBindingCache::kInvalidSlot;
		PipelineBindingCache::SlotID outputUAVSlot_ = PipelineBindingCache::kInvalidSlot;

		//--------- functions ----------------------------------------------------

		// 初回更新時にパイプラインとcubemapリソースを生成する
		void EnsureResources(GraphicsCore& graphicsCore);
	};
} // Engine