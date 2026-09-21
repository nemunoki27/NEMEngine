#include "UIComponentSerialization.h"

//============================================================================
//	UIComponentSerialization classMethods
//============================================================================

namespace Engine::UIComponentSerialization {

	UUID ReadEntityReference(const nlohmann::json& value) {

		std::string localFileID;
		if (value.is_object()) {
			localFileID = value.value("localFileId", std::string{});
		} else if (value.is_string()) {
			localFileID = value.get<std::string>();
		}
		return localFileID.empty() ? UUID{} : FromString16Hex(localFileID);
	}

	UUID ReadEntityReference(const nlohmann::json& in, const char* key) {

		const auto it = in.find(key);
		if (it == in.end()) {
			return {};
		}
		return ReadEntityReference(*it);
	}

	nlohmann::json WriteEntityReference(UUID localFileID) {

		return {
			{ "kind",localFileID ? "Scene" : "Null" },
			{ "sourceAsset","" },
			{ "localFileId",localFileID ? ToString(localFileID) : "" }
		};
	}
}
