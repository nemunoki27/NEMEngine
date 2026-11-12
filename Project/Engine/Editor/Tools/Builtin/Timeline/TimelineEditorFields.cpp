#include "TimelineEditorFields.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Animation/Timeline/TimelineBuiltinConfig.h>

// c++
#include <algorithm>
#include <cmath>
#include <iterator>
#include <type_traits>

using namespace Engine;

std::string_view Engine::TimelineEditorFields::BindingLabel(std::string_view name) {

	if (name == "Actor") return "アクター";
	if (name == "Camera") return "カメラ";
	if (name == "Audio") return "音声";
	if (name == "Hitbox") return "衝突判定";
	if (name == "Effect") return "エフェクト";
	return name;
}

namespace {

	constexpr const char* kTrackNames[]{
		"グループ", "骨アニメーション", "移動・回転", "パーティクル", "Prefabの発生", "衝突判定",
		"カメラ", "音声", "プロパティ", "有効状態", "時間倍率", "イベント", "受付区間", "子Timeline", "C#独自クリップ"
	};
	constexpr ImU32 kTrackColors[]{
		IM_COL32(35, 37, 42, 255), IM_COL32(47, 25, 91, 255), IM_COL32(18, 60, 34, 255),
		IM_COL32(15, 53, 83, 255), IM_COL32(12, 65, 71, 255), IM_COL32(80, 20, 23, 255),
		IM_COL32(20, 43, 82, 255), IM_COL32(76, 53, 17, 255), IM_COL32(43, 30, 80, 255),
		IM_COL32(28, 62, 43, 255), IM_COL32(73, 35, 18, 255), IM_COL32(73, 39, 17, 255),
		IM_COL32(60, 34, 73, 255), IM_COL32(25, 50, 64, 255), IM_COL32(40, 49, 65, 255)
	};
}

const char* TimelineEditorFields::TrackLabel(TimelineTrackKind kind) {

	return kTrackNames[(std::min)(static_cast<size_t>(kind), std::size(kTrackNames) - 1)];
}

ImU32 TimelineEditorFields::TrackColor(TimelineTrackKind kind) {

	return kTrackColors[(std::min)(static_cast<size_t>(kind), std::size(kTrackColors) - 1)];
}

const char* TimelineEditorFields::RequiredComponent(TimelineTrackKind kind) {

	const auto name = GetTimelineTrackRequiredComponent(kind);
	return name.empty() ? "" : name.data();
}

ValueEditResult TimelineEditorFields::Select(const char* label, int& selected, const char* const* names, int count) {

	ValueEditResult result;
	if (!MyGUI::BeginPropertyRow(label)) return result;
	ImGui::PushID(label);
	result.valueChanged = ImGui::Combo("##Value", &selected, names, count);
	result.editFinished = result.valueChanged;
	result.anyItemActive = ImGui::IsItemActive();
	ImGui::PopID();
	MyGUI::EndPropertyRow();
	return result;
}

ValueEditResult TimelineEditorFields::Time(const char* label, double& seconds, double frameRate, bool frames, double minimum) {

	const double scale = frames && std::isfinite(frameRate) && frameRate > 0.0 ? frameRate : 1.0;
	float value = static_cast<float>(seconds * scale);
	auto edit = MyGUI::DragFloat(label, value, {.dragSpeed = frames ? 1.0f : 0.01f,
		.minValue = static_cast<float>(minimum * scale), .maxValue = 10000000.0f});
	if (edit.valueChanged) {
		const double next = (frames ? std::round(value) : value) / scale;
		if (std::isfinite(next)) seconds = (std::clamp)(next, minimum, 10000000.0 / scale);
	}
	return edit;
}

ValueEditResult TimelineEditorFields::Binding(const char* label, UUID& selected, const TimelineAsset& asset, bool allowNone) {

	std::vector<std::string> labels;
	std::vector<const char*> names;
	if (allowNone) labels.emplace_back("未設定");
	int index = 0;
	for (size_t i = 0; i < asset.bindings.size(); ++i) {
		labels.emplace_back(BindingLabel(asset.bindings[i].name));
		if (asset.bindings[i].id == selected) index = static_cast<int>(i) + (allowNone ? 1 : 0);
	}
	for (const auto& text : labels) names.push_back(text.c_str());
	if (names.empty()) { MyGUI::Text(label, "割り当て先を追加してください"); return {}; }
	const auto result = Select(label, index, names.data(), static_cast<int>(names.size()));
	if (result.valueChanged) selected = allowNone && index == 0 ? UUID{} : asset.bindings[index - (allowNone ? 1 : 0)].id;
	return result;
}

ValueEditResult TimelineEditorFields::Parameter(const char* label, UUID& selected, const TimelineAsset& asset) {

	std::vector<const char*> names;
	int index = 0;
	for (size_t i = 0; i < asset.parameters.size(); ++i) {
		names.push_back(asset.parameters[i].name.c_str());
		if (asset.parameters[i].id == selected) index = static_cast<int>(i);
	}
	if (names.empty()) { MyGUI::Text(label, "パラメーターを追加してください"); return {}; }
	auto result = Select(label, index, names.data(), static_cast<int>(names.size()));
	if (!selected) { result.valueChanged = true; result.editFinished = true; }
	if (result.valueChanged) selected = asset.parameters[index].id;
	return result;
}

ValueEditResult TimelineEditorFields::ParameterValue(const char* label, TimelineParameterValue& value, bool chooseType) {

	ValueEditResult result;
	if (chooseType) {
		const char* names[]{"bool", "int", "float", "Vector3", "string"};
		int type = static_cast<int>(value.index());
		const auto edit = Select("型", type, names, static_cast<int>(std::size(names)));
		result.Merge(edit);
		if (edit.valueChanged) {
			switch (type) {
			case 0: value = false; break;
			case 1: value = int32_t{0}; break;
			case 2: value = 0.0f; break;
			case 3: value = Vector3{}; break;
			case 4: value = std::string{}; break;
			}
		}
	}
	result.Merge(std::visit([&](auto& typed) -> ValueEditResult {
		using T = std::decay_t<decltype(typed)>;
		if constexpr (std::is_same_v<T, bool>) return MyGUI::CheckboxField(label, typed);
		else if constexpr (std::is_same_v<T, int32_t>) return MyGUI::DragInt(label, typed);
		else if constexpr (std::is_same_v<T, float>) return MyGUI::DragFloat(label, typed);
		else if constexpr (std::is_same_v<T, Vector3>) return MyGUI::DragVector3(label, typed);
		else return MyGUI::InputText(label, typed);
	}, value));
	return result;
}
