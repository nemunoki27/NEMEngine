#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/RenderFeatures/RenderFeatureProfile.h>

// c++
#include <string>
#include <vector>

namespace Engine {

	//============================================================================
	//	RenderFeatureProfileRuntime structures
	//============================================================================
	// 実行計画に含まれるPassと省略時の主入力
	struct RenderFeaturePlanNode {

		const RenderFeaturePassSettings* pass = nullptr;
		RenderFeatureOutputReference source{};
	};

	// Anchor単位で依存順に並べた実行計画
	struct RenderFeatureExecutionPlan {

		std::vector<RenderFeaturePlanNode> nodes{};
		RenderFeatureOutputReference sceneColorOutput{};
		std::string diagnostic{};

		bool IsValid() const {

			return diagnostic.empty();
		}
	};

	// Profileを検証して実行順へ変換する
	class RenderFeatureProfileRuntime {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		RenderFeatureProfileRuntime() = default;
		~RenderFeatureProfileRuntime() = default;

		void Rebuild(const RenderFeatureProfileAsset& profile);
		RenderFeatureExecutionPlan BuildPlan(
			RenderFeatureAnchor anchor) const;

		//--------- accessor -----------------------------------------------------

		const RenderFeatureProfileAsset& GetProfile() const {

			return profile_;
		}
		const std::string& GetDiagnostic() const { return diagnostic_; }

	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		RenderFeatureProfileAsset profile_{};
		std::string diagnostic_{};

		// Profile全体のIDと入出力参照を検証する
		bool ValidateProfile();
	};
} // Engine
