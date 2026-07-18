#include "CanvasComponent.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Components/UI/UIComponentSerialization.h>
#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>

// c++
#include <algorithm>
#include <utility>

//============================================================================
//	CanvasComponent internal
//============================================================================
namespace {

	void ReadKeyBindings(const nlohmann::json& in, std::vector<KeyDIKCode>& bindings) {

		if (!in.is_array()) {
			return;
		}
		bindings.clear();
		for (const nlohmann::json& value : in) {

			if (!value.is_number_integer()) {
				continue;
			}
			const int32_t code = value.get<int32_t>();
			const KeyDIKCode key = static_cast<KeyDIKCode>(code);
			if (0 < code && code <= 255 &&
				std::find(bindings.begin(), bindings.end(), key) == bindings.end()) {
				bindings.emplace_back(key);
			}
		}
	}

	void ReadGamepadBindings(const nlohmann::json& in,
		std::vector<GamePadButtons>& bindings) {

		if (!in.is_array()) {
			return;
		}
		bindings.clear();
		for (const nlohmann::json& value : in) {

			if (!value.is_number_integer()) {
				continue;
			}
			const int32_t code = value.get<int32_t>();
			const GamePadButtons button = static_cast<GamePadButtons>(code);
			if (0 <= code && code < static_cast<int32_t>(GamePadButtons::Counts) &&
				std::find(bindings.begin(), bindings.end(), button) == bindings.end()) {
				bindings.emplace_back(button);
			}
		}
	}

	template <typename T>
	nlohmann::json WriteInputBindings(const std::vector<T>& bindings) {

		nlohmann::json out = nlohmann::json::array();
		for (T binding : bindings) {
			out.emplace_back(static_cast<int32_t>(binding));
		}
		return out;
	}
}

//============================================================================
//	CanvasComponent classMethods
//============================================================================

void Engine::ResizeCanvasNavigationTable(CanvasNavigationTable& table, int32_t rows, int32_t columns) {

	rows = std::clamp(rows, 1, CanvasNavigationTable::kMaxSize);
	columns = std::clamp(columns, 1, CanvasNavigationTable::kMaxSize);
	if (table.rows == rows && table.columns == columns &&
		table.cells.size() == static_cast<size_t>(rows * columns)) {
		return;
	}

	std::vector<UUID> resized(static_cast<size_t>(rows * columns));
	const int32_t copyRows = (std::min)(table.rows, rows);
	const int32_t copyColumns = (std::min)(table.columns, columns);
	for (int32_t row = 0; row < copyRows; ++row) {
		for (int32_t column = 0; column < copyColumns; ++column) {

			const size_t source = static_cast<size_t>(row * table.columns + column);
			const size_t destination = static_cast<size_t>(row * columns + column);
			if (source < table.cells.size()) {
				resized[destination] = table.cells[source];
			}
		}
	}

	table.rows = rows;
	table.columns = columns;
	table.cells = std::move(resized);
}

void Engine::from_json(const nlohmann::json& in, CanvasComponent& component) {

	component.enabled = in.value("enabled", component.enabled);
	component.scaleMode = EnumAdapter<CanvasScaleMode>::FromString(
		in.value("scaleMode", "ScaleWithScreenSize")).value_or(component.scaleMode);
	component.scaleFactor = in.value("scaleFactor", component.scaleFactor);
	component.matchWidthOrHeight = in.value("matchWidthOrHeight", component.matchWidthOrHeight);
	component.sortingLayer = in.value("sortingLayer", component.sortingLayer);
	component.order = in.value("order", component.order);
	component.blockGameplayInput = in.value("blockGameplayInput", component.blockGameplayInput);
	component.inputInEditMode = in.value("inputInEditMode", component.inputInEditMode);
	component.blockInputAfterSubmit = in.value("blockInputAfterSubmit", component.blockInputAfterSubmit);
	if (const auto settings = in.find("inputSettings");
		settings != in.end() && settings->is_object()) {

		component.keyboardInputEnabled =
			settings->value("keyboardEnabled", component.keyboardInputEnabled);
		component.gamepadInputEnabled =
			settings->value("gamepadEnabled", component.gamepadInputEnabled);
		component.gamepadLeftStickEnabled =
			settings->value("gamepadLeftStickEnabled", component.gamepadLeftStickEnabled);
		ReadKeyBindings(settings->value("navigationUpKeys", nlohmann::json{}),
			component.navigationUpKeys);
		ReadKeyBindings(settings->value("navigationDownKeys", nlohmann::json{}),
			component.navigationDownKeys);
		ReadKeyBindings(settings->value("navigationLeftKeys", nlohmann::json{}),
			component.navigationLeftKeys);
		ReadKeyBindings(settings->value("navigationRightKeys", nlohmann::json{}),
			component.navigationRightKeys);
		ReadGamepadBindings(settings->value("navigationUpGamepadButtons", nlohmann::json{}),
			component.navigationUpGamepadButtons);
		ReadGamepadBindings(settings->value("navigationDownGamepadButtons", nlohmann::json{}),
			component.navigationDownGamepadButtons);
		ReadGamepadBindings(settings->value("navigationLeftGamepadButtons", nlohmann::json{}),
			component.navigationLeftGamepadButtons);
		ReadGamepadBindings(settings->value("navigationRightGamepadButtons", nlohmann::json{}),
			component.navigationRightGamepadButtons);
		ReadKeyBindings(settings->value("submitKeys", nlohmann::json{}), component.submitKeys);
		ReadGamepadBindings(settings->value("submitGamepadButtons", nlohmann::json{}),
			component.submitGamepadButtons);
	}
	component.wrapNavigation = in.value("wrapNavigation", component.wrapNavigation);
	component.navigationMode = EnumAdapter<CanvasNavigationMode>::FromString(
		in.value("navigationMode", "Automatic")).value_or(component.navigationMode);
	if (const auto table = in.find("navigationTable"); table != in.end() && table->is_object()) {

		const int32_t rows = table->value("rows", component.navigationTable.rows);
		const int32_t columns = table->value("columns", component.navigationTable.columns);
		ResizeCanvasNavigationTable(component.navigationTable, rows, columns);

		if (const auto cells = table->find("cells"); cells != table->end() && cells->is_array()) {
			std::fill(component.navigationTable.cells.begin(), component.navigationTable.cells.end(), UUID{});
			const size_t count = (std::min)(cells->size(), component.navigationTable.cells.size());
			for (size_t i = 0; i < count; ++i) {
				component.navigationTable.cells[i] = UIComponentSerialization::ReadEntityReference((*cells)[i]);
			}
		}
	}
	component.repeatDelay = in.value("repeatDelay", component.repeatDelay);
	component.repeatInterval = in.value("repeatInterval", component.repeatInterval);
	component.stickThreshold = in.value("stickThreshold", component.stickThreshold);
	component.firstSelectedLocalFileID = UIComponentSerialization::ReadEntityReference(in, "firstSelected");
}

void Engine::to_json(nlohmann::json& out, const CanvasComponent& component) {

	out["enabled"] = component.enabled;
	out["scaleMode"] = EnumAdapter<CanvasScaleMode>::ToString(component.scaleMode);
	out["scaleFactor"] = component.scaleFactor;
	out["matchWidthOrHeight"] = component.matchWidthOrHeight;
	out["sortingLayer"] = component.sortingLayer;
	out["order"] = component.order;
	out["blockGameplayInput"] = component.blockGameplayInput;
	out["inputInEditMode"] = component.inputInEditMode;
	out["blockInputAfterSubmit"] = component.blockInputAfterSubmit;
	out["inputSettings"]["keyboardEnabled"] = component.keyboardInputEnabled;
	out["inputSettings"]["gamepadEnabled"] = component.gamepadInputEnabled;
	out["inputSettings"]["gamepadLeftStickEnabled"] = component.gamepadLeftStickEnabled;
	out["inputSettings"]["navigationUpKeys"] = WriteInputBindings(component.navigationUpKeys);
	out["inputSettings"]["navigationDownKeys"] = WriteInputBindings(component.navigationDownKeys);
	out["inputSettings"]["navigationLeftKeys"] = WriteInputBindings(component.navigationLeftKeys);
	out["inputSettings"]["navigationRightKeys"] = WriteInputBindings(component.navigationRightKeys);
	out["inputSettings"]["navigationUpGamepadButtons"] =
		WriteInputBindings(component.navigationUpGamepadButtons);
	out["inputSettings"]["navigationDownGamepadButtons"] =
		WriteInputBindings(component.navigationDownGamepadButtons);
	out["inputSettings"]["navigationLeftGamepadButtons"] =
		WriteInputBindings(component.navigationLeftGamepadButtons);
	out["inputSettings"]["navigationRightGamepadButtons"] =
		WriteInputBindings(component.navigationRightGamepadButtons);
	out["inputSettings"]["submitKeys"] = WriteInputBindings(component.submitKeys);
	out["inputSettings"]["submitGamepadButtons"] =
		WriteInputBindings(component.submitGamepadButtons);
	out["wrapNavigation"] = component.wrapNavigation;
	out["navigationMode"] = EnumAdapter<CanvasNavigationMode>::ToString(component.navigationMode);
	out["navigationTable"]["rows"] = component.navigationTable.rows;
	out["navigationTable"]["columns"] = component.navigationTable.columns;
	out["navigationTable"]["cells"] = nlohmann::json::array();
	for (UUID localFileID : component.navigationTable.cells) {
		out["navigationTable"]["cells"].emplace_back(
			UIComponentSerialization::WriteEntityReference(localFileID));
	}
	out["repeatDelay"] = component.repeatDelay;
	out["repeatInterval"] = component.repeatInterval;
	out["stickThreshold"] = component.stickThreshold;
	out["firstSelected"] = UIComponentSerialization::WriteEntityReference(component.firstSelectedLocalFileID);
}
