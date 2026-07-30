#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Renderer/Backends/Core/IRenderBackend.h>
#include <Engine/Core/Rendering/Pipelines/Bind/PipelineBindingCache.h>
#include <Engine/Core/Rendering/Pipelines/Bind/RegistryAutoBindTable.h>
#include <Engine/Core/Rendering/Materials/MaterialParameterBinder.h>
#include <Engine/Core/Rendering/Materials/MaterialParameterLayout.h>
#include <Engine/Core/Rendering/PostProcess/PostProcessConstantBufferAllocator.h>
#include <Engine/Core/Rendering/DxObject/Common/DxTypes.h>

// c++
#include <unordered_map>
#include <string>

namespace Engine {

	// front
	class PipelineState;
	struct MaterialParameterValue;

	//============================================================================
	//	BuiltinRenderBackendBase class
	//	Builtinバックエンドが共通で使うバインドキャッシュとマテリアルバインドをまとめる
	//	各backendのDrawBatch/BeginFrameから共通処理をヘルパーとして呼ぶ
	//============================================================================
	class BuiltinRenderBackendBase :
		public IRenderBackend {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		BuiltinRenderBackendBase() {

			viewCBVSlot_ = perDrawBindCache_.AddSlot("ViewConstants", ShaderBindingKind::CBV);
			materialParamsCBVSlot_ = perDrawBindCache_.AddSlot(MaterialParameterCBuffer::kSurface, ShaderBindingKind::CBV);
		}
		~BuiltinRenderBackendBase() override = default;

		// 既定は基本条件でのバッチ判定、特殊なbackendだけoverrideする
		bool CanBatch(const RenderItem& first, const RenderItem& next, const GraphicsRuntimeFeatures& features) const override;
	protected:
		//========================================================================
		//	protected Methods
		//========================================================================

		// フレーム開始で共通に必要なリセット、各backendのBeginFrameから呼ぶ
		void BeginFrameCommon();

		// レジストリのオートバインドとパイプラインのスロット同期をまとめて行う
		void SyncAndBindRegistry(const PipelineState& pipelineState, const RenderDrawContext& context,
			ID3D12GraphicsCommandList* commandList);

		// reflection駆動のマテリアルパラメータとテクスチャをバインドする、宣言しないBuiltinは無回帰
		void BindMaterial(const RenderDrawContext& context, const PipelineState& pipelineState,
			const MaterialAsset& material,
			const MaterialParameterSet* overrides,
			ID3D12GraphicsCommandList* commandList);

		//--------- variables ----------------------------------------------------

		RegistryAutoBindTable registryAutoBindTable_{};
		PipelineBindingCache perDrawBindCache_{};
		MaterialParameterBinder materialParamBinder_{};
		// 描画ごとに別のCBV領域を切り出すアロケータ
		PostProcessConstantBufferAllocator constantBufferAllocator_{};

		PipelineBindingCache::SlotID viewCBVSlot_ = PipelineBindingCache::kInvalidSlot;
		PipelineBindingCache::SlotID materialParamsCBVSlot_ = PipelineBindingCache::kInvalidSlot;
	};
} // Engine
