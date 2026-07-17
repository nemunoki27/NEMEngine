#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Identity/UUID.h>

namespace Engine::UIComponentSerialization {

	// シーンまたはプレファブ内のエンティティ参照を読み込む
	inline UUID ReadEntityReference(const nlohmann::json& in, const char* key) {

		const auto it = in.find(key);
		if (it == in.end()) {
			return {};
		}
		std::string localFileID;
		if (it->is_object()) {
			localFileID = it->value("localFileId", std::string{});
		} else if (it->is_string()) {
			localFileID = it->get<std::string>();
		}
		return localFileID.empty() ? UUID{} : FromString16Hex(localFileID);
	}

	// プレファブ参照リマップが扱える形式でエンティティ参照を書き込む
	inline nlohmann::json WriteEntityReference(UUID localFileID) {

		return {
			{ "kind",localFileID ? "Scene" : "Null" },
			{ "sourceAsset","" },
			{ "localFileId",localFileID ? ToString(localFileID) : "" }
		};
	}
}
