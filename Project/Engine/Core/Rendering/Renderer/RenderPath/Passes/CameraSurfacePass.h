#pragma once

//============================================================================
//	include
//============================================================================
#include "RenderFeaturePass.h"
#include <Engine/Core/Rendering/RenderFeatures/RenderFeatureProfileRuntime.h>

namespace Engine {

	//============================================================================
	//	CameraSurfacePass class
	//	Cameraの反射と遮蔽を固定位置で実行する
	//============================================================================
	class CameraSurfacePass : public IRenderPass {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		CameraSurfacePass(const RenderPipelineDeps& deps, bool ambientOcclusion);
		// 使用していないCameraの実行状態を回収する
		void RetainViews(const std::unordered_set<std::string>& activeViews) override;
		// Camera設定から固定パスを実行する
		void Execute(GraphicsCore& graphicsCore, const RenderPassPhaseBuckets& passBuckets, SceneExecutionContext& context) override;

		//--------- accessor -----------------------------------------------------

		RenderPathPassKind GetKind() const override { return RenderPathPassKind::RenderFeature; }
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- structure ----------------------------------------------------

		struct State {

			RenderFeatureProfileRuntime runtime{};
			nlohmann::json settings{};
			uint64_t generation = 0;
		};

		//--------- variables ----------------------------------------------------

		bool ambientOcclusion_ = false;
		RenderFeaturePass executor_;
		std::unordered_map<std::string, State> states_{};
	};
}
