#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Renderer/RenderPath/IRenderPass.h>
#include <Engine/Core/Rendering/Pipelines/PipelineState.h>
#include <Engine/Core/Rendering/Pipelines/Bind/PipelineBindingCache.h>
#include <Engine/Core/Rendering/Pipelines/Bind/RegistryAutoBindTable.h>
#include <Engine/Core/Rendering/Renderer/Lighting/SkyboxIrradianceMap.h>
#include <Engine/Core/Rendering/DxObject/Buffers/DxConstantBuffer.h>
#include <Engine/Core/Foundation/Math/Matrix4x4.h>
#include <Engine/Core/Foundation/Math/Vector3.h>
#include <Engine/Core/Foundation/Math/Color.h>

// c++
#include <memory>
#include <vector>

namespace Engine {

	// front
	class GraphicsCore;
	class RenderTexture2D;

	//============================================================================
	//	LightingPass class
	//	GBufferを入力に全ライトをSceneColorFinalへ書くライティングパス
	//============================================================================
	class LightingPass :
		public IRenderPass {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		LightingPass();
		~LightingPass() override = default;

		void Execute(GraphicsCore& graphicsCore, const RenderPassPhaseBuckets& passBuckets,
			SceneExecutionContext& context) override;

		//--------- accessor -----------------------------------------------------

		RenderPathPassKind GetKind() const override { return RenderPathPassKind::Lighting; }
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- structure ----------------------------------------------------

		// b1へ渡すライティング定数、背景復元と環境光に使う
		struct LightingConstants {

			Vector3 cameraPos = Vector3::AnyInit(0.0f);
			float ambientIntensity = 0.03f;

			Matrix4x4 inverseViewProjection = Matrix4x4::Identity();

			Color4 skyboxColor = Color4::White();

			uint32_t skyboxCubemapIndex = 0xFFFFFFFF;
			uint32_t hasSkybox = 0;
			uint32_t viewportWidth = 0;
			uint32_t viewportHeight = 0;

			// 平行光源シャドウのレイ設定、mainShadowed側でのみ使う
			float shadowNormalBias = 0.05f;
			float shadowMaxDistance = 100000.0f;
			// skyboxから畳み込んだ放射照度cubemap、無い場合は無効値
			uint32_t irradianceCubemapIndex = 0xFFFFFFFF;
			// 拡散IBL環境光の強さ
			float iblIntensity = 1.0f;
		};

		//--------- variables ----------------------------------------------------

		// シャドウ無しPSOと、TLASによる平行光源シャドウ付きPSO
		PipelineState pipeline_{};
		PipelineState pipelineShadowed_{};
		bool initialized_ = false;
		// shadow版PSOが構築できたか、inlineRT非対応環境では作れないので分けて持つ
		bool shadowedAvailable_ = false;

		// 同一フレームでビューごとに複数回描いても定数が上書きされないようプールで持つ
		std::vector<std::unique_ptr<DxConstBuffer<LightingConstants>>> constantBuffers_{};
		uint32_t constantBufferIndex_ = 0;

		// GBufferの各SRVと定数のスロット、ライトバッファはregistryAutoBindTableが名前で解決する
		PipelineBindingCache bindCache_{};
		PipelineBindingCache::SlotID albedoSlot_ = PipelineBindingCache::kInvalidSlot;
		PipelineBindingCache::SlotID normalSlot_ = PipelineBindingCache::kInvalidSlot;
		PipelineBindingCache::SlotID worldPosSlot_ = PipelineBindingCache::kInvalidSlot;
		PipelineBindingCache::SlotID materialSlot_ = PipelineBindingCache::kInvalidSlot;
		PipelineBindingCache::SlotID emissiveSlot_ = PipelineBindingCache::kInvalidSlot;
		PipelineBindingCache::SlotID flagsSlot_ = PipelineBindingCache::kInvalidSlot;
		PipelineBindingCache::SlotID constantsSlot_ = PipelineBindingCache::kInvalidSlot;

		// ライトバッファをレジストリから自動バインドする
		RegistryAutoBindTable registryAutoBindTable_{};

		// skyboxのcubemapから作る拡散IBL用の放射照度cubemap
		SkyboxIrradianceMap irradianceMap_{};

		//--------- functions ----------------------------------------------------

		// 初回描画時にパイプラインを生成する
		void EnsurePipeline(GraphicsCore& graphicsCore, DXGI_FORMAT colorFormat);
		// フレーム内で再利用する定数バッファを確保する
		DxConstBuffer<LightingConstants>& AllocateConstantBuffer(GraphicsCore& graphicsCore);
		// GBufferのSRVを対応スロットへバインドする
		void BindGBufferSRV(ID3D12GraphicsCommandList* commandList, PipelineBindingCache::SlotID slot, RenderTexture2D* texture);
	};
} // Engine
