#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Core/GraphicsFrameResourceArray.h>
#include <Engine/Core/Rendering/Renderer/RenderPath/IRenderPass.h>
#include <Engine/Core/Rendering/Pipelines/PipelineState.h>
#include <Engine/Core/Rendering/Pipelines/Bind/PipelineBindingCache.h>
#include <Engine/Core/Rendering/Pipelines/Bind/RegistryAutoBindTable.h>
#include <Engine/Core/Rendering/Renderer/Lighting/SkyboxIrradianceMap.h>
#include <Engine/Core/Rendering/Renderer/Lighting/DirectionalShadowMapRenderer.h>
#include <Engine/Core/Rendering/Renderer/Lighting/InlineShadowRenderer.h>
#include <Engine/Core/Rendering/Renderer/RenderTargets/MultiRenderTarget.h>
#include <Engine/Core/Rendering/DxObject/Buffers/DxConstantBuffer.h>
#include <Engine/Core/Rendering/Renderer/Backends/Common/ViewConstantBuffer.h>
#include <Engine/Core/Rendering/Core/GraphicsFrameContext.h>
#include <Engine/Core/Foundation/Math/Matrix4x4.h>
#include <Engine/Core/Foundation/Math/Vector3.h>
#include <Engine/Core/Foundation/Math/Vector4.h>
#include <Engine/Core/Foundation/Math/Color.h>

// c++
#include <memory>
#include <array>
#include <vector>
#include <unordered_map>

namespace Engine {

	// front
	class GraphicsCore;
	class RenderTexture2D;
	class RenderFeatureProfileRuntime;
	struct RenderPipelineDeps;

	//============================================================================
	//	LightingPass class
	//	GBufferを入力に全ライトをSceneColorFinalへ書くライティングパス
	//============================================================================
	class LightingPass : public IRenderPass {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		explicit LightingPass(const RenderPipelineDeps& deps);
		~LightingPass() override = default;

		void Execute(
			GraphicsCore& graphicsCore, const RenderPassPhaseBuckets& passBuckets, SceneExecutionContext& context) override;
		// 使用中のViewだけ影の履歴を残す
		void RetainViews(const std::unordered_set<std::string>& viewKeys) override;

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
			Matrix4x4 viewMatrix = Matrix4x4::Identity();
			Matrix4x4 viewProjectionMatrix = Matrix4x4::Identity();

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

			uint32_t softShadowSampleCount = 4;
			uint32_t shadowMapAvailable = 0;
			uint32_t shadowMapLightIndex = UINT32_MAX;
			uint32_t reflectionFeatureActive = 0;

			float ambientOcclusionIntensity = 0.0f;
			float reflectionIntensity = 1.0f;
			uint32_t filteredShadowAvailable = 0;
			float _surfacePad1 = 0.0f;

			std::array<Matrix4x4, 4> shadowViewProjections{};
			Vector4 shadowCascadeSplits = Vector4(0.0f, 0.0f, 0.0f, 0.0f);
			Vector4 shadowDepthRanges = Vector4(1.0f, 1.0f, 1.0f, 1.0f);
			uint32_t environmentSpecularIndex = UINT32_MAX;
			uint32_t environmentBRDFIndex = UINT32_MAX;
			uint32_t physicalEnvironmentEnabled = 0;
			uint32_t _environmentPad = 0;
		};
		struct DirectionalShadowConstants {

			std::array<Matrix4x4, 4> viewProjections{};
			Vector4 cascadeSplits{};
			uint32_t available = 0;
			uint32_t lightIndex = UINT32_MAX;
			float depthBias = 0.00002f;
			float padding = 0.0f;
		};

		struct ShadowRequirementKey {

			const RenderFeatureProfileRuntime* runtime = nullptr;
			uint64_t profile = 0;
			uint64_t world = 0;
			uint64_t overrides = 0;
			uint64_t material = 0;
			uint64_t pipeline = 0;
			uint32_t features = 0;
			bool operator==(const ShadowRequirementKey&) const = default;
		};
		struct ShadowRequirement {

			ShadowRequirementKey key;
			bool required = false;
			bool valid = false;
		};
		//--------- variables ----------------------------------------------------

		std::unordered_map<std::string, ShadowRequirement> shadowRequirements_;

		// シャドウ無しPSOと、TLASによる平行光源シャドウ付きPSO
		std::unique_ptr<PipelineState> pipeline_;
		std::unique_ptr<PipelineState> pipelineShadowed_;
		bool initialized_ = false;
		// shadow版PSOが構築できたか、inlineRT非対応環境では作れないので分けて持つ
		bool shadowedAvailable_ = false;

		// 同一フレームでビューごとに複数回描いても定数が上書きされないようプールで持つ
		GraphicsFrameResourceArray<std::vector<std::unique_ptr<DxConstBuffer<LightingConstants>>>>
			constantBuffers_{};
		std::array<uint32_t, kGraphicsFrameContextCount> constantBufferIndices_{};
		std::array<uint64_t, kGraphicsFrameContextCount> constantBufferFrameSerials_{};

		// GBufferの各SRVと定数のスロット、ライトバッファはregistryAutoBindTableが名前で解決する
		PipelineBindingCache bindCache_{};
		PipelineBindingCache::SlotID albedoSlot_ = PipelineBindingCache::kInvalidSlot;
		PipelineBindingCache::SlotID normalSlot_ = PipelineBindingCache::kInvalidSlot;
		PipelineBindingCache::SlotID worldPosSlot_ = PipelineBindingCache::kInvalidSlot;
		PipelineBindingCache::SlotID materialSlot_ = PipelineBindingCache::kInvalidSlot;
		PipelineBindingCache::SlotID emissiveSlot_ = PipelineBindingCache::kInvalidSlot;
		PipelineBindingCache::SlotID flagsSlot_ = PipelineBindingCache::kInvalidSlot;
		PipelineBindingCache::SlotID ambientOcclusionSlot_ = PipelineBindingCache::kInvalidSlot;
		PipelineBindingCache::SlotID constantsSlot_ = PipelineBindingCache::kInvalidSlot;
		std::array<PipelineBindingCache::SlotID, 4> shadowMapSlots_{};

		// ライトバッファをレジストリから自動バインドする
		RegistryAutoBindTable registryAutoBindTable_{};

		// skyboxのcubemapから作る拡散IBL用の放射照度cubemap
		SkyboxIrradianceMap irradianceMap_{};
		const RenderPipelineDeps& deps_;
		DirectionalShadowMapRenderer directionalShadows_;
		InlineShadowRenderer inlineShadows_;
		ViewConstantBuffer<DirectionalShadowConstants> directionalShadowConstants_{"DirectionalShadowConstants"};

		//--------- functions ----------------------------------------------------

		// 初回描画時にパイプラインを生成する
		void EnsurePipeline(GraphicsCore& graphicsCore, DXGI_FORMAT colorFormat);
		// 実行するShaderの影入力から必要なCascadeを判定する
		bool RequiresDirectionalShadowMap(GraphicsCore& graphicsCore, const SceneExecutionContext& context);
		// 同じViewの影を後段の散乱計算へ公開する
		void PublishDirectionalShadows(GraphicsCore& graphicsCore, SceneExecutionContext& context, bool available);
		// フレーム内で再利用する定数バッファを確保する
		DxConstBuffer<LightingConstants>& AllocateConstantBuffer(GraphicsCore& graphicsCore);
		// GBufferのSRVを対応スロットへバインドする
		void BindGBufferSRV(
			ID3D12GraphicsCommandList* commandList, PipelineBindingCache::SlotID slot, RenderTexture2D* texture);
	};
} // Engine
