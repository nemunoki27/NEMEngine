#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/AssetTypes.h>

// c++
#include <string>
// json
#include <json.hpp>

namespace Engine {

	// 永続IDでツールの選択内容を解決する
	struct EditorInspectionTarget {

		std::string providerID;
		AssetID asset{};
		UUID primary{}, secondary{};

		bool IsSet() const { return !providerID.empty() && static_cast<bool>(asset); }
		bool operator==(const EditorInspectionTarget&) const = default;
	};

	void to_json(nlohmann::json& out, const EditorInspectionTarget& target);
	void from_json(const nlohmann::json& in, EditorInspectionTarget& target);
}
