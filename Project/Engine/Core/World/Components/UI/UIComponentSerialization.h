#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Identity/UUID.h>

namespace Engine::UIComponentSerialization {

	// エンティティ参照を読み込む
	inline UUID ReadEntityReference(const nlohmann::json& value) {

		std::string localFileID;
		if (value.is_object()) {
			localFileID = value.value("localFileId", std::string{});
		} else if (value.is_string()) {
			localFileID = value.get<std::string>();
		}
		return localFileID.empty() ? UUID{} : FromString16Hex(localFileID);
	}

	// シーンまたはプレファブ内のエンティティ参照を読み込む
	inline UUID ReadEntityReference(const nlohmann::json& in, const char* key) {

		const auto it = in.find(key);
		if (it == in.end()) {
			return {};
		}
		return ReadEntityReference(*it);
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
