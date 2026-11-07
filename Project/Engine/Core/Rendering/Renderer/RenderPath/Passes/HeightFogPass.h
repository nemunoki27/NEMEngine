#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Renderer/RenderPath/IRenderPass.h>
#include <Engine/Core/Rendering/Renderer/Backends/Common/ViewConstantBuffer.h>
#include <Engine/Core/Rendering/Pipelines/PipelineState.h>
#include <Engine/Core/Rendering/Pipelines/Bind/PipelineBindingCache.h>
#include <Engine/Core/Foundation/Math/Matrix4x4.h>
#include <Engine/Core/Foundation/Math/Vector4.h>
#include <Engine/Core/Rendering/Renderer/Lighting/HeightFogConstants.h>

namespace Engine {

	//============================================================================
	//	HeightFogPass class
	//	高さ霧を合成し透明描画へ同じ設定を公開する
	//============================================================================
	class HeightFogPass : public IRenderPass {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		HeightFogPass();
		// 大気と高さ霧を合成する
		void Execute(GraphicsCore& graphics, const RenderPassPhaseBuckets& buckets, SceneExecutionContext& context) override;

		//--------- accessor -----------------------------------------------------

		RenderPathPassKind GetKind() const override { return RenderPathPassKind::HeightFog; }
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		ViewConstantBuffer<HeightFogConstants> constants_{"HeightFogConstants"};
		std::unique_ptr<PipelineState> pipeline_;
		DXGI_FORMAT format_ = DXGI_FORMAT_UNKNOWN;
		PipelineBindingCache bindings_;
		PipelineBindingCache::SlotID constantSlot_;
		PipelineBindingCache::SlotID depthSlot_;

		//--------- functions ----------------------------------------------------

		// 出力形式に対応する合成パイプラインを用意する
		bool EnsurePipeline(GraphicsCore& graphics, DXGI_FORMAT format);
	};
}
