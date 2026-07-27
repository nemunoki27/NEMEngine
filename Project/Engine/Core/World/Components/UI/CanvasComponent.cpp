#include "CanvasComponent.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Components/UI/UIComponentSerialization.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>

// c++
#include <algorithm>
#include <array>
#include <utility>

//============================================================================
//	CanvasComponent internal
//============================================================================
namespace {

	using CanvasBinding = Engine::CanvasInputBinding;
	using CanvasAction = Engine::CanvasInputAction;
	using CanvasDevice = Engine::CanvasInputDevice;

	constexpr auto kDefaultBindings = std::to_array<CanvasBinding>({
		{ static_cast<uint16_t>(KeyDIKCode::W), CanvasAction::Up, CanvasDevice::Keyboard },
		{ static_cast<uint16_t>(KeyDIKCode::UP), CanvasAction::Up, CanvasDevice::Keyboard },
		{ static_cast<uint16_t>(KeyDIKCode::S), CanvasAction::Down, CanvasDevice::Keyboard },
		{ static_cast<uint16_t>(KeyDIKCode::DOWN), CanvasAction::Down, CanvasDevice::Keyboard },
		{ static_cast<uint16_t>(KeyDIKCode::A), CanvasAction::Left, CanvasDevice::Keyboard },
		{ static_cast<uint16_t>(KeyDIKCode::LEFT), CanvasAction::Left, CanvasDevice::Keyboard },
		{ static_cast<uint16_t>(KeyDIKCode::D), CanvasAction::Right, CanvasDevice::Keyboard },
		{ static_cast<uint16_t>(KeyDIKCode::RIGHT), CanvasAction::Right, CanvasDevice::Keyboard },
		{ static_cast<uint16_t>(KeyDIKCode::RETURN), CanvasAction::Submit, CanvasDevice::Keyboard },
		{ static_cast<uint16_t>(KeyDIKCode::SPACE), CanvasAction::Submit, CanvasDevice::Keyboard },
		{ static_cast<uint16_t>(GamePadButtons::ARROW_UP), CanvasAction::Up, CanvasDevice::Gamepad },
		{ static_cast<uint16_t>(GamePadButtons::ARROW_DOWN), CanvasAction::Down, CanvasDevice::Gamepad },
		{ static_cast<uint16_t>(GamePadButtons::ARROW_LEFT), CanvasAction::Left, CanvasDevice::Gamepad },
		{ static_cast<uint16_t>(GamePadButtons::ARROW_RIGHT), CanvasAction::Right, CanvasDevice::Gamepad },
		{ static_cast<uint16_t>(GamePadButtons::A), CanvasAction::Submit, CanvasDevice::Gamepad },
		});

	void ReadBindings(const nlohmann::json& values,
		CanvasAction action, CanvasDevice device,
		std::vector<CanvasBinding>& bindings) {

		if (!values.is_array()) {
			return;
		}
		for (const nlohmann::json& value : values) {

			if (!value.is_number_integer()) {
				continue;
			}
			const int32_t code = value.get<int32_t>();
			const int32_t maxCode = device == CanvasDevice::Keyboard ?
				255 : static_cast<int32_t>(GamePadButtons::Counts) - 1;
			const int32_t minCode = device == CanvasDevice::Keyboard ? 1 : 0;
			if (code < minCode || maxCode < code) {
				continue;
			}
			const CanvasBinding binding{
				static_cast<uint16_t>(code), action, device
			};
			if (std::find_if(bindings.begin(), bindings.end(),
				[&](const CanvasBinding& current) {
					return current.code == binding.code &&
						current.action == binding.action &&
						current.device == binding.device;
				}) == bindings.end()) {
				bindings.emplace_back(binding);
			}
		}
	}

	nlohmann::json WriteBindings(
		std::span<const CanvasBinding> bindings,
		CanvasAction action, CanvasDevice device) {

		nlohmann::json out = nlohmann::json::array();
		for (const CanvasBinding& binding : bindings) {
			if (binding.action == action && binding.device == device) {
				out.emplace_back(binding.code);
			}
		}
		return out;
	}

	void ReadCanvasInputBindings(
		const nlohmann::json& in, std::vector<CanvasBinding>& bindings) {

		bindings.clear();
		const auto settings = in.find("inputSettings");
		if (settings == in.end() || !settings->is_object()) {
			bindings.assign(kDefaultBindings.begin(), kDefaultBindings.end());
			return;
		}

		ReadBindings(settings->value("navigationUpKeys", nlohmann::json{}),
			CanvasAction::Up, CanvasDevice::Keyboard, bindings);
		ReadBindings(settings->value("navigationDownKeys", nlohmann::json{}),
			CanvasAction::Down, CanvasDevice::Keyboard, bindings);
		ReadBindings(settings->value("navigationLeftKeys", nlohmann::json{}),
			CanvasAction::Left, CanvasDevice::Keyboard, bindings);
		ReadBindings(settings->value("navigationRightKeys", nlohmann::json{}),
			CanvasAction::Right, CanvasDevice::Keyboard, bindings);
		ReadBindings(settings->value("submitKeys", nlohmann::json{}),
			CanvasAction::Submit, CanvasDevice::Keyboard, bindings);
		ReadBindings(settings->value("navigationUpGamepadButtons", nlohmann::json{}),
			CanvasAction::Up, CanvasDevice::Gamepad, bindings);
		ReadBindings(settings->value("navigationDownGamepadButtons", nlohmann::json{}),
			CanvasAction::Down, CanvasDevice::Gamepad, bindings);
		ReadBindings(settings->value("navigationLeftGamepadButtons", nlohmann::json{}),
			CanvasAction::Left, CanvasDevice::Gamepad, bindings);
		ReadBindings(settings->value("navigationRightGamepadButtons", nlohmann::json{}),
			CanvasAction::Right, CanvasDevice::Gamepad, bindings);
		ReadBindings(settings->value("submitGamepadButtons", nlohmann::json{}),
			CanvasAction::Submit, CanvasDevice::Gamepad, bindings);
	}

	void ReadCanvasNavigationCells(
		const nlohmann::json& in, const Engine::CanvasComponent& component,
		std::vector<Engine::UUID>& cells) {

		const size_t cellCount = static_cast<size_t>(
			component.navigationRows * component.navigationColumns);
		cells.assign(cellCount, Engine::UUID{});
		const auto table = in.find("navigationTable");
		if (table == in.end() || !table->is_object()) {
			return;
		}
		const auto values = table->find("cells");
		if (values == table->end() || !values->is_array()) {
			return;
		}
		const size_t count = (std::min)(values->size(), cells.size());
		for (size_t index = 0; index < count; ++index) {
			cells[index] =
				Engine::UIComponentSerialization::ReadEntityReference((*values)[index]);
		}
	}
}

//============================================================================
//	CanvasComponent classMethods
//============================================================================
void Engine::CanvasComponent::OnAdded(
	ECSWorld& world, const Entity& entity,
	[[maybe_unused]] CanvasComponent& component) {

	DynamicBuffer<CanvasInputBinding> bindings =
		world.AddBuffer<CanvasInputBinding>(entity);
	if (bindings.IsEmpty()) {
		for (const CanvasInputBinding& binding : kDefaultBindings) {
			bindings.Add(binding);
		}
	}

	DynamicBuffer<CanvasNavigationCell> cells =
		world.AddBuffer<CanvasNavigationCell>(entity);
	if (cells.IsEmpty()) {
		cells.Resize(9);
	}
	if (!world.HasComponent<CanvasRuntimeComponent>(entity)) {
		world.AddComponent<CanvasRuntimeComponent>(entity);
	}
}

void Engine::CanvasComponent::OnRemoved(
	ECSWorld& world, const Entity& entity) {

	if (world.HasBuffer<CanvasInputBinding>(entity)) {
		world.RemoveBuffer<CanvasInputBinding>(entity);
	}
	if (world.HasBuffer<CanvasNavigationCell>(entity)) {
		world.RemoveBuffer<CanvasNavigationCell>(entity);
	}
	if (world.HasComponent<CanvasRuntimeComponent>(entity)) {
		world.RemoveComponent<CanvasRuntimeComponent>(entity);
	}
}

void Engine::CanvasComponent::InitializeStorage(
	[[maybe_unused]] ECSWorld& world, [[maybe_unused]] const Entity& entity,
	[[maybe_unused]] CanvasComponent& component) {
}

void Engine::CanvasComponent::ReleaseStorage(
	[[maybe_unused]] ECSWorld& world, [[maybe_unused]] const Entity& entity,
	[[maybe_unused]] CanvasComponent& component) {
}

void Engine::CanvasComponent::DeserializeECS(
	ECSWorld& world, const Entity& entity, const nlohmann::json& in,
	CanvasComponent& component) {

	from_json(in, component);

	std::vector<CanvasInputBinding> bindings;
	ReadCanvasInputBindings(in, bindings);
	SetCanvasInputBindings(world, entity, bindings);

	std::vector<UUID> cells;
	ReadCanvasNavigationCells(in, component, cells);
	SetCanvasNavigationCells(world, entity, cells);
}

void Engine::CanvasComponent::SerializeECS(
	const ECSWorld& world, const Entity& entity,
	const CanvasComponent& component, nlohmann::json& out) {

	SerializeCanvas(component,
		GetCanvasInputBindings(world, entity),
		GetCanvasNavigationCells(world, entity), out);
}

void Engine::ResizeCanvasNavigationTable(
	CanvasNavigationTable& table, int32_t rows, int32_t columns) {

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

			// 行列数変更前のセル位置を保ったまま重なる範囲だけ移す
			const size_t source =
				static_cast<size_t>(row * table.columns + column);
			const size_t destination =
				static_cast<size_t>(row * columns + column);
			if (source < table.cells.size()) {
				resized[destination] = table.cells[source];
			}
		}
	}

	table.rows = rows;
	table.columns = columns;
	table.cells = std::move(resized);
}

void Engine::from_json(
	const nlohmann::json& in, CanvasComponent& component) {

	component.enabled = in.value("enabled", component.enabled);
	component.scaleMode = EnumAdapter<CanvasScaleMode>::FromString(
		in.value("scaleMode", "ScaleWithScreenSize")).value_or(component.scaleMode);
	component.scaleFactor = in.value("scaleFactor", component.scaleFactor);
	component.matchWidthOrHeight =
		in.value("matchWidthOrHeight", component.matchWidthOrHeight);
	component.sortingLayer = in.value("sortingLayer", component.sortingLayer);
	component.order = in.value("order", component.order);
	component.blockGameplayInput =
		in.value("blockGameplayInput", component.blockGameplayInput);
	component.inputInEditMode =
		in.value("inputInEditMode", component.inputInEditMode);
	component.blockInputAfterSubmit =
		in.value("blockInputAfterSubmit", component.blockInputAfterSubmit);
	if (const auto settings = in.find("inputSettings");
		settings != in.end() && settings->is_object()) {

		component.keyboardInputEnabled =
			settings->value("keyboardEnabled", component.keyboardInputEnabled);
		component.gamepadInputEnabled =
			settings->value("gamepadEnabled", component.gamepadInputEnabled);
		component.gamepadLeftStickEnabled =
			settings->value("gamepadLeftStickEnabled",
				component.gamepadLeftStickEnabled);
	}
	component.wrapNavigation =
		in.value("wrapNavigation", component.wrapNavigation);
	component.navigationMode = EnumAdapter<CanvasNavigationMode>::FromString(
		in.value("navigationMode", "Automatic")).value_or(component.navigationMode);
	if (const auto table = in.find("navigationTable");
		table != in.end() && table->is_object()) {
		component.navigationRows = std::clamp(
			table->value("rows", component.navigationRows),
			1, CanvasNavigationTable::kMaxSize);
		component.navigationColumns = std::clamp(
			table->value("columns", component.navigationColumns),
			1, CanvasNavigationTable::kMaxSize);
	}
	component.repeatDelay = in.value("repeatDelay", component.repeatDelay);
	component.repeatInterval =
		in.value("repeatInterval", component.repeatInterval);
	component.stickThreshold =
		in.value("stickThreshold", component.stickThreshold);
	component.firstSelectedLocalFileID =
		UIComponentSerialization::ReadEntityReference(in, "firstSelected");
}

void Engine::to_json(
	nlohmann::json& out, const CanvasComponent& component) {

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
	out["inputSettings"]["gamepadLeftStickEnabled"] =
		component.gamepadLeftStickEnabled;
	out["wrapNavigation"] = component.wrapNavigation;
	out["navigationMode"] =
		EnumAdapter<CanvasNavigationMode>::ToString(component.navigationMode);
	out["navigationTable"]["rows"] = component.navigationRows;
	out["navigationTable"]["columns"] = component.navigationColumns;
	out["repeatDelay"] = component.repeatDelay;
	out["repeatInterval"] = component.repeatInterval;
	out["stickThreshold"] = component.stickThreshold;
	out["firstSelected"] = UIComponentSerialization::WriteEntityReference(
		component.firstSelectedLocalFileID);
}

std::span<Engine::CanvasInputBinding> Engine::GetCanvasInputBindings(
	ECSWorld& world, const Entity& entity) {

	return world.TryGetBuffer<CanvasInputBinding>(entity).GetSpan();
}

std::span<const Engine::CanvasInputBinding> Engine::GetCanvasInputBindings(
	const ECSWorld& world, const Entity& entity) {

	return world.GetBufferSpan<CanvasInputBinding>(entity);
}

void Engine::SetCanvasInputBindings(
	ECSWorld& world, const Entity& entity,
	std::span<const CanvasInputBinding> bindings) {

	DynamicBuffer<CanvasInputBinding> buffer =
		world.TryGetBuffer<CanvasInputBinding>(entity);
	if (!buffer.IsValid()) {
		buffer = world.AddBuffer<CanvasInputBinding>(entity);
	}
	buffer.Clear();
	buffer.Reserve(static_cast<uint32_t>(bindings.size()));
	for (const CanvasInputBinding& binding : bindings) {
		buffer.Add(binding);
	}
	world.MarkComponentModified<CanvasInputBinding>(entity);
}

std::span<Engine::CanvasNavigationCell> Engine::GetCanvasNavigationCells(
	ECSWorld& world, const Entity& entity) {

	return world.TryGetBuffer<CanvasNavigationCell>(entity).GetSpan();
}

std::span<const Engine::CanvasNavigationCell> Engine::GetCanvasNavigationCells(
	const ECSWorld& world, const Entity& entity) {

	return world.GetBufferSpan<CanvasNavigationCell>(entity);
}

void Engine::SetCanvasNavigationCells(
	ECSWorld& world, const Entity& entity, std::span<const UUID> cells) {

	DynamicBuffer<CanvasNavigationCell> buffer =
		world.TryGetBuffer<CanvasNavigationCell>(entity);
	if (!buffer.IsValid()) {
		buffer = world.AddBuffer<CanvasNavigationCell>(entity);
	}
	buffer.Resize(static_cast<uint32_t>(cells.size()));
	for (size_t index = 0; index < cells.size(); ++index) {
		buffer[static_cast<uint32_t>(index)].localFileID = cells[index];
	}
	world.MarkComponentModified<CanvasNavigationCell>(entity);
}

void Engine::SerializeCanvas(
	const CanvasComponent& component,
	std::span<const CanvasInputBinding> bindings,
	std::span<const CanvasNavigationCell> cells, nlohmann::json& out) {

	to_json(out, component);
	out["inputSettings"]["navigationUpKeys"] = WriteBindings(
		bindings, CanvasInputAction::Up, CanvasInputDevice::Keyboard);
	out["inputSettings"]["navigationDownKeys"] = WriteBindings(
		bindings, CanvasInputAction::Down, CanvasInputDevice::Keyboard);
	out["inputSettings"]["navigationLeftKeys"] = WriteBindings(
		bindings, CanvasInputAction::Left, CanvasInputDevice::Keyboard);
	out["inputSettings"]["navigationRightKeys"] = WriteBindings(
		bindings, CanvasInputAction::Right, CanvasInputDevice::Keyboard);
	out["inputSettings"]["submitKeys"] = WriteBindings(
		bindings, CanvasInputAction::Submit, CanvasInputDevice::Keyboard);
	out["inputSettings"]["navigationUpGamepadButtons"] = WriteBindings(
		bindings, CanvasInputAction::Up, CanvasInputDevice::Gamepad);
	out["inputSettings"]["navigationDownGamepadButtons"] = WriteBindings(
		bindings, CanvasInputAction::Down, CanvasInputDevice::Gamepad);
	out["inputSettings"]["navigationLeftGamepadButtons"] = WriteBindings(
		bindings, CanvasInputAction::Left, CanvasInputDevice::Gamepad);
	out["inputSettings"]["navigationRightGamepadButtons"] = WriteBindings(
		bindings, CanvasInputAction::Right, CanvasInputDevice::Gamepad);
	out["inputSettings"]["submitGamepadButtons"] = WriteBindings(
		bindings, CanvasInputAction::Submit, CanvasInputDevice::Gamepad);
	out["navigationTable"]["cells"] = nlohmann::json::array();
	for (const CanvasNavigationCell& cell : cells) {
		out["navigationTable"]["cells"].emplace_back(
			UIComponentSerialization::WriteEntityReference(cell.localFileID));
	}
}
