#include "MaterialPaintComponent.h"

#include <algorithm>

void Engine::to_json(nlohmann::json& out, const MaterialPaintComponent& component) {

	out = {
		{"paintAtlas", ToAssetReferenceJson(component.atlas)}, {"maskTexture", ToAssetReferenceJson(component.mask)},
		{"geometryKey", component.geometryKey}, {"resolution", component.resolution},
		{"projectionFromLocal", component.projectionFromLocal.m},
		{"layers", nlohmann::json::array()}, {"baseHeightTextures", nlohmann::json::array()},
	};
	for (const auto& layer : component.layers) {
		out["layers"].push_back({{"paintMaterial", ToAssetReferenceJson(layer.material)}, {"channel", layer.channel},
			{"visible", layer.visible}, {"heightBlend", layer.heightBlend}, {"heightContrast", layer.heightContrast}});
	}
	for (const auto texture : component.baseHeightTextures) out["baseHeightTextures"].push_back(ToAssetReferenceJson(texture));
}

void Engine::from_json(const nlohmann::json& in, MaterialPaintComponent& component) {

	MaterialPaintComponent loaded;
	loaded.atlas = ParseAssetID(in, "paintAtlas");
	loaded.mask = ParseAssetID(in, "maskTexture");
	loaded.geometryKey = in.value("geometryKey", std::string{});
	const auto size = in.value("resolution", 1024u);
	loaded.resolution = size == 512 || size == 2048 ? size : 1024u;
	if (in.contains("projectionFromLocal")) {
		const auto rows = in.at("projectionFromLocal").get<std::array<std::array<float, 4>, 4>>();
		for (size_t row = 0; row < rows.size(); ++row) std::copy(rows[row].begin(), rows[row].end(), loaded.projectionFromLocal.m[row]);
	}
	uint32_t used = 0;
	for (const auto& item : in.value("layers", nlohmann::json::array())) {
		MaterialPaintLayer layer;
		layer.channel = item.value("channel", 4u);
		if (4 <= layer.channel || (used & (1u << layer.channel))) continue;
		used |= 1u << layer.channel;
		layer.material = ParseAssetID(item, "paintMaterial");
		layer.visible = item.value("visible", true);
		layer.heightBlend = item.value("heightBlend", false);
		layer.heightContrast = std::clamp(item.value("heightContrast", 1.0f), 0.0f, 32.0f);
		loaded.layers.push_back(layer);
	}
	for (const auto& id : in.value("baseHeightTextures", nlohmann::json::array())) {
		loaded.baseHeightTextures.push_back(TryParseAssetGUID32Hex(id.get<std::string>()).value_or(AssetID{}));
	}
	component = std::move(loaded);
}
