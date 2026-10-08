#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Renderer/RenderPath/IRenderPass.h>
#include <Engine/Core/Rendering/Renderer/RenderTargets/MultiRenderTarget.h>
#include <Engine/Core/Foundation/Math/Matrix4x4.h>
#include <Engine/Core/Foundation/Math/Vector4.h>

// c++
#include <array>
#include <memory>

namespace Engine {

	class GraphicsCore;
	struct RenderPipelineDeps;

	// Lightingへ渡す平行光源の影情報
	struct DirectionalShadowMapState {

		static constexpr uint32_t kCascadeCount = 4;

		std::array<Matrix4x4, kCascadeCount> viewProjections{};
		Vector4 cascadeSplits = Vector4(0.0f, 0.0f, 0.0f, 0.0f);
		Vector4 depthRanges = Vector4(1.0f, 1.0f, 1.0f, 1.0f);
		uint32_t lightIndex = UINT32_MAX;
	};

	//============================================================================
	//	DirectionalShadowMapRenderer class
	//	平行光源のShadow Map生成と描画資源を所有する
	//============================================================================
	class DirectionalShadowMapRenderer {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		// Viewごとの遮蔽物をCascadeへ描画する
		bool Render(GraphicsCore& graphicsCore, const RenderPassPhaseBuckets& passBuckets, SceneExecutionContext& context,
			const RenderPipelineDeps& deps);

		//--------- accessor -----------------------------------------------------

		const DirectionalShadowMapState& GetState() const { return state_; }
		// Lightingで参照する深度画像を取得
		DepthTexture2D* GetDepthTexture(uint32_t cascade);

	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		std::array<std::unique_ptr<MultiRenderTarget>, DirectionalShadowMapState::kCascadeCount> shadowMaps_{};
		DirectionalShadowMapState state_;
	};
}
