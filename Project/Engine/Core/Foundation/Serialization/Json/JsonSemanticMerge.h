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
	struct JsonMergeConflict {

		std::string path;
		nlohmann::json base;
		nlohmann::json ours;
		nlohmann::json theirs;
	};

	struct JsonMergeResult {

		nlohmann::json merged;
		std::vector<JsonMergeConflict> conflicts;

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

		static JsonMergeResult Merge(const nlohmann::json& base,
			const nlohmann::json& ours, const nlohmann::json& theirs);
	};
} // Engine
