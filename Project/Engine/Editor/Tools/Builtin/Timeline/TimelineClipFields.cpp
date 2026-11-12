#include "TimelineClipFields.h"

using namespace Engine;

ValueEditResult TimelineClipFields::Bool(const char* label, nlohmann::json& data, const char* key, bool fallback) {

	bool value = data.value(key, fallback);
	const auto edit = MyGUI::CheckboxField(label, value);
	if (edit.valueChanged) data[key] = value;
	return edit;
}

ValueEditResult TimelineClipFields::Number(const char* label, nlohmann::json& data, const char* key,
	float fallback, float minimum, float maximum) {

	float value = data.value(key, fallback);
	const auto edit = MyGUI::DragFloat(label, value, {.dragSpeed = 0.01f, .minValue = minimum, .maxValue = maximum});
	if (edit.valueChanged) data[key] = value;
	return edit;
}

ValueEditResult TimelineClipFields::Integer(const char* label, nlohmann::json& data, const char* key, int fallback) {

	int value = data.value(key, fallback);
	const auto edit = MyGUI::DragInt(label, value);
	if (edit.valueChanged) data[key] = value;
	return edit;
}

ValueEditResult TimelineClipFields::Text(const char* label, nlohmann::json& data, const char* key) {

	std::string value = data.value(key, "");
	const auto edit = MyGUI::InputText(label, value);
	if (edit.valueChanged) data[key] = value;
	return edit;
}

ValueEditResult TimelineClipFields::Vector(const char* label, nlohmann::json& data, const char* key, Vector3 fallback) {

	Vector3 value = data.contains(key) ? Vector3::FromJson(data.at(key)) : fallback;
	const auto edit = MyGUI::DragVector3(label, value);
	if (edit.valueChanged) data[key] = value.ToJson();
	return edit;
}

ValueEditResult TimelineClipFields::Asset(const char* label, nlohmann::json& data, const char* key,
	AssetDatabase* database, std::initializer_list<AssetType> types) {

	AssetID value = ParseAssetID(data, key);
	const auto edit = MyGUI::AssetReferenceField(label, value, database, types);
	if (edit.valueChanged) data[key] = ToAssetReferenceJson(value);
	return edit;
}

ValueEditResult TimelineClipFields::Binding(const char* label, nlohmann::json& data, const char* key, const TimelineAsset& asset) {

	const std::string name = data.value(key, "");
	UUID selected = FromString16Hex(name);
	if (!selected) for (const auto& role : asset.bindings) if (role.name == name) selected = role.id;
	const auto edit = TimelineEditorFields::Binding(label, selected, asset);
	if (edit.valueChanged) data[key] = selected ? ToString(selected) : std::string{};
	return edit;
}
