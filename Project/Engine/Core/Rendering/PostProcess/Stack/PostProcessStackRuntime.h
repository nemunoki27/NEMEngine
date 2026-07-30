#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/AssetTypes.h>
#include <Engine/Core/Rendering/Assets/MaterialAsset.h>
#include <Engine/Core/Rendering/PostProcess/Stack/PostProcessAnchor.h>

// c++
#include <string>
#include <vector>
#include <unordered_map>

namespace Engine {

	//============================================================================
	//	PostProcessStackRuntime structures
	//	ランタイム実行用にフラット化されたPostProcessStackの情報
	//============================================================================
	// 1パスのランタイム実行データ
	struct PostProcessStackRuntimePass {

		UUID id{};
		std::string name;
		bool enabled = true;
		AssetID material{};
		MaterialPassKind passKind = MaterialPassKind::PostProcess;
		PostProcessAnchor anchor = PostProcessAnchor::AfterMaskedUI;
		UUID sourcePass{};
		bool graphOutput = false;
		uint32_t targetMask = 0u;
		MaterialParameterSet parameterOverrides;
		std::unordered_map<std::string, AssetID> textureGuids;
		std::unordered_map<std::string, std::string> renderTargetInputs;
		std::unordered_map<std::string, UUID> passInputs;
		std::unordered_map<std::string, PipelineStaticSamplerSettings> samplerOverrides;
	};

	struct PostProcessGraphPlanNode {

		const PostProcessStackRuntimePass* pass = nullptr;
		UUID sourcePass{};
	};

	struct PostProcessGraphPlan {

		std::vector<PostProcessGraphPlanNode> nodes;
		UUID outputPass{};
		std::string diagnostic;

		bool IsValid() const {
			return diagnostic.empty() &&
				(!nodes.empty() || !outputPass);
		}
	};

	// ランタイム実行用のPostProcessStack全体データ
	struct PostProcessStackRuntime {

		std::vector<PostProcessStackRuntimePass> passes;

		// 指定アンカーに割り当てられた有効なパスが1つ以上あるか
		bool HasEnabledPassesForAnchor(PostProcessAnchor anchor) const;
		// 依存関係を検証し、実行可能な順序へ変換する
		PostProcessGraphPlan BuildGraphPlan(
			PostProcessAnchor anchor) const;
	};
} // Engine
