#pragma once

//============================================================================
//	include
//============================================================================
// c++
#include <string>
#include <vector>
// json
#include <json.hpp>

namespace Engine {

	//============================================================================
	//	JsonSemanticMerge structures
	//============================================================================
	// 競合位置と変更前後の三つの値
	struct JsonMergeConflict {

		std::string path;
		nlohmann::json base;
		nlohmann::json ours;
		nlohmann::json theirs;
	};

	// 統合した文書と競合一覧
	struct JsonMergeResult {

		nlohmann::json merged;
		std::vector<JsonMergeConflict> conflicts;

		// 競合なしで統合できたか確認する
		bool Succeeded() const { return conflicts.empty(); }
	};

	//============================================================================
	//	JsonSemanticMerge class
	//	安定IDを持つJSON配列を要素単位で三方向マージするクラス
	//============================================================================
	class JsonSemanticMerge {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		JsonSemanticMerge() = delete;
		~JsonSemanticMerge() = delete;

		// 共通の元文書から双方の変更を統合する
		static JsonMergeResult Merge(const nlohmann::json& base, const nlohmann::json& ours, const nlohmann::json& theirs);
	};
} // Engine
