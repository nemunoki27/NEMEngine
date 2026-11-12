#include "EditorInspectionTarget.h"

using namespace Engine;

void Engine::to_json(nlohmann::json& out, const EditorInspectionTarget& target) {

	out = {{"providerID", target.providerID}, {"asset", ToAssetReferenceJson(target.asset)},
		{"primary", ToString(target.primary)}, {"secondary", ToString(target.secondary)}};
}

void Engine::from_json(const nlohmann::json& in, EditorInspectionTarget& target) {

	target = {};
	if (!in.is_object()) return;
	const auto text = [&](const char* key) {
		const auto value = in.find(key);
		return value != in.end() && value->is_string() ? value->get<std::string>() : std::string{};
	};
	target.providerID = text("providerID");
	target.asset = TryParseAssetGUID32Hex(text("asset")).value_or(AssetID{});
	target.primary = TryParseUUID16Hex(text("primary")).value_or(UUID{});
	target.secondary = TryParseUUID16Hex(text("secondary")).value_or(UUID{});
}
