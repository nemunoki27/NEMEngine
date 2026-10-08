#include "CanvasComponentSerialization.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Components/UI/UIComponentSerialization.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>

// c++
#include <algorithm>
#include <array>
#include <new>
#include <stdexcept>

namespace {

	using CanvasBinding = Engine::CanvasInputBinding;
	using CanvasAction = Engine::CanvasInputAction;
	using CanvasDevice = Engine::CanvasInputDevice;

	constexpr auto kDefaultBindings = std::to_array<CanvasBinding>({
		{static_cast<uint16_t>(KeyDIKCode::W), CanvasAction::Up, CanvasDevice::Keyboard},
		{static_cast<uint16_t>(KeyDIKCode::UP), CanvasAction::Up, CanvasDevice::Keyboard},
		{static_cast<uint16_t>(KeyDIKCode::S), CanvasAction::Down, CanvasDevice::Keyboard},
		{static_cast<uint16_t>(KeyDIKCode::DOWN), CanvasAction::Down, CanvasDevice::Keyboard},
		{static_cast<uint16_t>(KeyDIKCode::A), CanvasAction::Left, CanvasDevice::Keyboard},
		{static_cast<uint16_t>(KeyDIKCode::LEFT), CanvasAction::Left, CanvasDevice::Keyboard},
		{static_cast<uint16_t>(KeyDIKCode::D), CanvasAction::Right, CanvasDevice::Keyboard},
		{static_cast<uint16_t>(KeyDIKCode::RIGHT), CanvasAction::Right, CanvasDevice::Keyboard},
		{static_cast<uint16_t>(KeyDIKCode::RETURN), CanvasAction::Submit, CanvasDevice::Keyboard},
		{static_cast<uint16_t>(KeyDIKCode::SPACE), CanvasAction::Submit, CanvasDevice::Keyboard},
		{static_cast<uint16_t>(GamePadButtons::ARROW_UP), CanvasAction::Up, CanvasDevice::Gamepad},
		{static_cast<uint16_t>(GamePadButtons::ARROW_DOWN), CanvasAction::Down, CanvasDevice::Gamepad},
		{static_cast<uint16_t>(GamePadButtons::ARROW_LEFT), CanvasAction::Left, CanvasDevice::Gamepad},
		{static_cast<uint16_t>(GamePadButtons::ARROW_RIGHT), CanvasAction::Right, CanvasDevice::Gamepad},
		{static_cast<uint16_t>(GamePadButtons::A), CanvasAction::Submit, CanvasDevice::Gamepad},
	});

	void ReadBindings(
		const nlohmann::json& values, CanvasAction action, CanvasDevice device, std::vector<CanvasBinding>& bindings) {

		if (!values.is_array()) {
			return;
		}
		for (const nlohmann::json& value : values) {

			if (!value.is_number_integer()) {
				continue;
			}
			// 範囲外の整数を縮小変換する前に除外する
			const double code = value.get<double>();
			const int32_t maxCode = device == CanvasDevice::Keyboard ? 255 : static_cast<int32_t>(GamePadButtons::Counts) - 1;
			const int32_t minCode = device == CanvasDevice::Keyboard ? 1 : 0;
			if (code < minCode || maxCode < code) {
				continue;
			}
			const CanvasBinding binding{static_cast<uint16_t>(code), action, device};
			if (std::find_if(bindings.begin(), bindings.end(), [&](const CanvasBinding& current) {
					return current.code == binding.code && current.action == binding.action && current.device == binding.device;
				}) == bindings.end()) {
				bindings.emplace_back(binding);
			}
		}
	}

	nlohmann::json WriteBindings(std::span<const CanvasBinding> bindings, CanvasAction action, CanvasDevice device) {

		// 操作と入力機器が一致するコードだけを保存する
		nlohmann::json out = nlohmann::json::array();
		for (const CanvasBinding& binding : bindings) {
			if (binding.action == action && binding.device == device) {
				out.emplace_back(binding.code);
			}
		}
		return out;
	}

	void ReadCanvasInputBindings(const nlohmann::json& in, std::vector<CanvasBinding>& bindings) {

		// 保存された割当がなければ既定入力を使う
		bindings.clear();
		const auto settings = in.find("inputSettings");
		if (settings == in.end() || !settings->is_object()) {
			bindings.assign(kDefaultBindings.begin(), kDefaultBindings.end());
			return;
		}

		ReadBindings(settings->value("navigationUpKeys", nlohmann::json{}), CanvasAction::Up, CanvasDevice::Keyboard, bindings);
		ReadBindings(
			settings->value("navigationDownKeys", nlohmann::json{}), CanvasAction::Down, CanvasDevice::Keyboard, bindings);
		ReadBindings(
			settings->value("navigationLeftKeys", nlohmann::json{}), CanvasAction::Left, CanvasDevice::Keyboard, bindings);
		ReadBindings(
			settings->value("navigationRightKeys", nlohmann::json{}), CanvasAction::Right, CanvasDevice::Keyboard, bindings);
		ReadBindings(settings->value("submitKeys", nlohmann::json{}), CanvasAction::Submit, CanvasDevice::Keyboard, bindings);
		ReadBindings(
			settings->value("navigationUpGamepadButtons", nlohmann::json{}), CanvasAction::Up, CanvasDevice::Gamepad, bindings);
		ReadBindings(settings->value("navigationDownGamepadButtons", nlohmann::json{}), CanvasAction::Down,
			CanvasDevice::Gamepad, bindings);
		ReadBindings(settings->value("navigationLeftGamepadButtons", nlohmann::json{}), CanvasAction::Left,
			CanvasDevice::Gamepad, bindings);
		ReadBindings(settings->value("navigationRightGamepadButtons", nlohmann::json{}), CanvasAction::Right,
			CanvasDevice::Gamepad, bindings);
		ReadBindings(
			settings->value("submitGamepadButtons", nlohmann::json{}), CanvasAction::Submit, CanvasDevice::Gamepad, bindings);
	}

	bool ReadCanvasNavigationCells(
		const nlohmann::json& in, const Engine::CanvasComponent& component, std::vector<Engine::UUID>& cells) {

		// 行列数を確認して空のセルを用意する
		size_t cellCount = 0;
		if (!Engine::TryGetCanvasNavigationCellCount(component.navigationRows, component.navigationColumns, cellCount)) {
			return false;
		}
		try {
			cells.assign(cellCount, Engine::UUID{});
		} catch (const std::bad_alloc&) {
			return false;
		} catch (const std::length_error&) {
			return false;
		}
		const auto table = in.find("navigationTable");
		if (table == in.end() || !table->is_object()) {
			return true;
		}
		const auto values = table->find("cells");
		if (values == table->end() || !values->is_array()) {
			return true;
		}
		const size_t count = (std::min)(values->size(), cells.size());
		for (size_t index = 0; index < count; ++index) {
			cells[index] = Engine::UIComponentSerialization::ReadEntityReference((*values)[index]);
		}
		return true;
	}
}

std::span<const Engine::CanvasInputBinding> Engine::CanvasComponentSerialization::GetDefaultInputBindings() {

	return kDefaultBindings;
}

void Engine::CanvasComponent::DeserializeECS(
	ECSWorld& world, const Entity& entity, const nlohmann::json& in, CanvasComponent& component) {

	// 設定値の後に付随Bufferを復元する
	from_json(in, component);

	std::vector<CanvasInputBinding> bindings;
	ReadCanvasInputBindings(in, bindings);
	SetCanvasInputBindings(world, entity, bindings);

	std::vector<UUID> cells;
	if (!ReadCanvasNavigationCells(in, component, cells)) {
		component.navigationRows = 3;
		component.navigationColumns = 3;
		cells.assign(9, UUID{});
	}
	SetCanvasNavigationCells(world, entity, cells);
}

void Engine::CanvasComponent::SerializeECS(
	const ECSWorld& world, const Entity& entity, const CanvasComponent& component, nlohmann::json& out) {

	// 追加待ちのBufferも含めて保存する
	SerializeCanvas(component, world.TryGetBufferForBinding<CanvasInputBinding>(entity).GetSpan(),
		world.TryGetBufferForBinding<CanvasNavigationCell>(entity).GetSpan(), out);
}

void Engine::from_json(const nlohmann::json& in, CanvasComponent& component) {

	// 基本設定とPlayerごとの入力条件を取り込む
	component.enabled = in.value("enabled", component.enabled);
	component.scaleMode =
		EnumAdapter<CanvasScaleMode>::FromString(in.value("scaleMode", "ScaleWithScreenSize")).value_or(component.scaleMode);
	component.scaleFactor = in.value("scaleFactor", component.scaleFactor);
	component.matchWidthOrHeight = in.value("matchWidthOrHeight", component.matchWidthOrHeight);
	component.sortingLayer = in.value("sortingLayer", component.sortingLayer);
	component.order = in.value("order", component.order);
	if (const auto mode = in.find("inputBlockMode"); mode != in.end() && mode->is_string()) {
		component.inputBlockMode =
			EnumAdapter<CanvasInputBlockMode>::FromString(mode->get<std::string>()).value_or(component.inputBlockMode);
	} else {
		// 旧設定の有効値は入力を消費したフレームへ移行する
		component.inputBlockMode =
			in.value("blockGameplayInput", false) ? CanvasInputBlockMode::ConsumedFrame : CanvasInputBlockMode::None;
	}
	component.playerIndex = (std::min)(in.value("playerIndex", component.playerIndex), 3u);
	component.inputInEditMode = in.value("inputInEditMode", component.inputInEditMode);
	component.blockInputAfterSubmit = in.value("blockInputAfterSubmit", component.blockInputAfterSubmit);
	if (const auto settings = in.find("inputSettings"); settings != in.end() && settings->is_object()) {

		component.keyboardInputEnabled = settings->value("keyboardEnabled", component.keyboardInputEnabled);
		component.gamepadInputEnabled = settings->value("gamepadEnabled", component.gamepadInputEnabled);
		component.gamepadLeftStickEnabled = settings->value("gamepadLeftStickEnabled", component.gamepadLeftStickEnabled);
	}
	component.wrapNavigation = in.value("wrapNavigation", component.wrapNavigation);
	component.navigationMode = EnumAdapter<CanvasNavigationMode>::FromString(in.value("navigationMode", "Automatic"))
								   .value_or(component.navigationMode);
	if (const auto table = in.find("navigationTable"); table != in.end() && table->is_object()) {
		const int32_t rows = table->value("rows", component.navigationRows);
		const int32_t columns = table->value("columns", component.navigationColumns);
		size_t cellCount = 0;
		if (TryGetCanvasNavigationCellCount(rows, columns, cellCount)) {
			component.navigationRows = rows;
			component.navigationColumns = columns;
		}
	}
	component.repeatDelay = in.value("repeatDelay", component.repeatDelay);
	component.repeatInterval = in.value("repeatInterval", component.repeatInterval);
	component.stickThreshold = in.value("stickThreshold", component.stickThreshold);
	component.firstSelectedLocalFileID = UIComponentSerialization::ReadEntityReference(in, "firstSelected");
}

void Engine::to_json(nlohmann::json& out, const CanvasComponent& component) {

	// 基本設定と選択遷移の構成を保存する
	out["enabled"] = component.enabled;
	out["scaleMode"] = EnumAdapter<CanvasScaleMode>::ToString(component.scaleMode);
	out["scaleFactor"] = component.scaleFactor;
	out["matchWidthOrHeight"] = component.matchWidthOrHeight;
	out["sortingLayer"] = component.sortingLayer;
	out["order"] = component.order;
	out["inputBlockMode"] = EnumAdapter<CanvasInputBlockMode>::ToString(component.inputBlockMode);
	out["playerIndex"] = component.playerIndex;
	out["inputInEditMode"] = component.inputInEditMode;
	out["blockInputAfterSubmit"] = component.blockInputAfterSubmit;
	out["inputSettings"]["keyboardEnabled"] = component.keyboardInputEnabled;
	out["inputSettings"]["gamepadEnabled"] = component.gamepadInputEnabled;
	out["inputSettings"]["gamepadLeftStickEnabled"] = component.gamepadLeftStickEnabled;
	out["wrapNavigation"] = component.wrapNavigation;
	out["navigationMode"] = EnumAdapter<CanvasNavigationMode>::ToString(component.navigationMode);
	out["navigationTable"]["rows"] = component.navigationRows;
	out["navigationTable"]["columns"] = component.navigationColumns;
	out["repeatDelay"] = component.repeatDelay;
	out["repeatInterval"] = component.repeatInterval;
	out["stickThreshold"] = component.stickThreshold;
	out["firstSelected"] = UIComponentSerialization::WriteEntityReference(component.firstSelectedLocalFileID);
}

void Engine::SerializeCanvas(const CanvasComponent& component, std::span<const CanvasInputBinding> bindings,
	std::span<const CanvasNavigationCell> cells, nlohmann::json& out) {

	// Componentと付随Bufferを同じ文書へまとめる
	to_json(out, component);
	out["inputSettings"]["navigationUpKeys"] = WriteBindings(bindings, CanvasInputAction::Up, CanvasInputDevice::Keyboard);
	out["inputSettings"]["navigationDownKeys"] = WriteBindings(bindings, CanvasInputAction::Down, CanvasInputDevice::Keyboard);
	out["inputSettings"]["navigationLeftKeys"] = WriteBindings(bindings, CanvasInputAction::Left, CanvasInputDevice::Keyboard);
	out["inputSettings"]["navigationRightKeys"] =
		WriteBindings(bindings, CanvasInputAction::Right, CanvasInputDevice::Keyboard);
	out["inputSettings"]["submitKeys"] = WriteBindings(bindings, CanvasInputAction::Submit, CanvasInputDevice::Keyboard);
	out["inputSettings"]["navigationUpGamepadButtons"] =
		WriteBindings(bindings, CanvasInputAction::Up, CanvasInputDevice::Gamepad);
	out["inputSettings"]["navigationDownGamepadButtons"] =
		WriteBindings(bindings, CanvasInputAction::Down, CanvasInputDevice::Gamepad);
	out["inputSettings"]["navigationLeftGamepadButtons"] =
		WriteBindings(bindings, CanvasInputAction::Left, CanvasInputDevice::Gamepad);
	out["inputSettings"]["navigationRightGamepadButtons"] =
		WriteBindings(bindings, CanvasInputAction::Right, CanvasInputDevice::Gamepad);
	out["inputSettings"]["submitGamepadButtons"] =
		WriteBindings(bindings, CanvasInputAction::Submit, CanvasInputDevice::Gamepad);
	out["navigationTable"]["cells"] = nlohmann::json::array();
	for (const CanvasNavigationCell& cell : cells) {
		out["navigationTable"]["cells"].emplace_back(UIComponentSerialization::WriteEntityReference(cell.localFileID));
	}
}
