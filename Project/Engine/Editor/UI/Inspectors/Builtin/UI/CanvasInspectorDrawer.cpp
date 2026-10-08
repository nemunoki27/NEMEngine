#include "CanvasInspectorDrawer.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Inspectors/Common/InspectorDrawerCommon.h>
#include <Engine/Editor/UI/ImGui/ImGuiHelpers.h>
#include "CanvasInputEditing.h"
#include "CanvasNavigationEditing.h"

// c++
#include <algorithm>

//============================================================================
//	CanvasInspectorDrawer classMethods
//============================================================================

Engine::CanvasInspectorDrawer::CanvasInspectorDrawer() : SerializedComponentInspectorDrawer("Canvas", "Canvas") {
}

void Engine::CanvasInspectorDrawer::DrawFields([[maybe_unused]] const EditorPanelContext& context, ECSWorld& world,
	const Entity& entity, bool& anyItemActive) {

	auto& draft = GetDraft();
	DrawField(anyItemActive, [&]() { return InspectorDrawerCommon::DrawCheckboxField("有効", draft.enabled); });

	// 基準解像度は変更不可
	MyGUI::TextVector2("基準解像度", draft.referenceResolution, 1);

	DrawField(anyItemActive, [&]() { return MyGUI::DragInt("ソートレイヤー", draft.sortingLayer); });
	DrawField(anyItemActive, [&]() { return MyGUI::DragInt("描画順", draft.order); });
	if (ImGui::IsItemHovered()) {
		ImGui::BeginTooltip();

		ImGui::Text("キャンバス単位の制御");

		ImGui::EndTooltip();
	}

	ImGui::Indent();
	if (MyGUI::CollapsingHeader("入力設定", false)) {
		DrawField(anyItemActive,
			[&]() { return InspectorDrawerCommon::DrawEnumComboField("ゲーム入力ブロック", draft.inputBlockMode); });
		DrawField(anyItemActive, [&]() {
			int32_t playerIndex = static_cast<int32_t>(draft.playerIndex);
			ValueEditResult result = MyGUI::DragInt("Player", playerIndex, {.dragSpeed = 1.0f, .minValue = 0, .maxValue = 3});
			draft.playerIndex = static_cast<uint32_t>(std::clamp(playerIndex, 0, 3));
			return result;
		});
		DrawField(anyItemActive,
			[&]() { return InspectorDrawerCommon::DrawCheckboxField("編集中に入力有効", draft.inputInEditMode); });
		DrawField(anyItemActive, [&]() {
			return InspectorDrawerCommon::DrawCheckboxField("決定後入力を受け付けない", draft.blockInputAfterSubmit);
		});
		DrawField(anyItemActive, [&]() {
			return MyGUI::DragFloat(
				"リピート開始", draft.repeatDelay, {.dragSpeed = 0.01f, .minValue = 0.0f, .maxValue = 10.0f});
		});
		DrawField(anyItemActive, [&]() {
			return MyGUI::DragFloat(
				"リピート間隔", draft.repeatInterval, {.dragSpeed = 0.01f, .minValue = 0.01f, .maxValue = 10.0f});
		});
		DrawField(anyItemActive, [&]() {
			return CanvasNavigationEditing::DrawEntityReference("初期アクティブUI", world, draft.firstSelectedLocalFileID);
		});

		DrawField(anyItemActive,
			[&]() { return InspectorDrawerCommon::DrawEnumComboField("ナビゲーション方式", draft.navigationMode); });
		DrawField(anyItemActive,
			[&]() { return InspectorDrawerCommon::DrawCheckboxField("ナビゲーションをループ", draft.wrapNavigation); });

		ImGui::Indent();
		if (MyGUI::CollapsingHeader("UI入力デバイス", false)) {
			DrawField(anyItemActive,
				[&]() { return InspectorDrawerCommon::DrawCheckboxField("キーボード入力有効", draft.keyboardInputEnabled); });
			DrawField(anyItemActive,
				[&]() { return InspectorDrawerCommon::DrawCheckboxField("ゲームパッド入力有効", draft.gamepadInputEnabled); });

			if (ImGui::BeginTabBar("##CanvasUIInputTabs")) {
				if (ImGui::BeginTabItem("選択")) {
					ImGui::SeparatorText("キーボード");
					ImGui::PushID("KeyboardNavigation");
					DrawField(anyItemActive,
						[&]() { return CanvasInputEditing::DrawKeyboardBindings("上", navigationUpKeys_, KeyDIKCode::UP); });
					DrawField(anyItemActive, [&]() {
						return CanvasInputEditing::DrawKeyboardBindings("下", navigationDownKeys_, KeyDIKCode::DOWN);
					});
					DrawField(anyItemActive, [&]() {
						return CanvasInputEditing::DrawKeyboardBindings("左", navigationLeftKeys_, KeyDIKCode::LEFT);
					});
					DrawField(anyItemActive, [&]() {
						return CanvasInputEditing::DrawKeyboardBindings("右", navigationRightKeys_, KeyDIKCode::RIGHT);
					});
					ImGui::PopID();

					ImGui::SeparatorText("ゲームパッド");
					ImGui::PushID("GamepadNavigation");
					DrawField(anyItemActive, [&]() {
						return InspectorDrawerCommon::DrawCheckboxField("左スティックを使用", draft.gamepadLeftStickEnabled);
					});
					if (draft.gamepadLeftStickEnabled) {
						DrawField(anyItemActive, [&]() {
							return MyGUI::DragFloat("スティックしきい値", draft.stickThreshold,
								{.dragSpeed = 0.01f,
									.minValue = 0.0f,
									.maxValue = 1.0f,
									.flags = ImGuiSliderFlags_AlwaysClamp});
						});
					}
					DrawField(anyItemActive, [&]() {
						return CanvasInputEditing::DrawGamepadBindings(
							"上", navigationUpGamepadButtons_, GamePadButtons::ARROW_UP);
					});
					DrawField(anyItemActive, [&]() {
						return CanvasInputEditing::DrawGamepadBindings(
							"下", navigationDownGamepadButtons_, GamePadButtons::ARROW_DOWN);
					});
					DrawField(anyItemActive, [&]() {
						return CanvasInputEditing::DrawGamepadBindings(
							"左", navigationLeftGamepadButtons_, GamePadButtons::ARROW_LEFT);
					});
					DrawField(anyItemActive, [&]() {
						return CanvasInputEditing::DrawGamepadBindings(
							"右", navigationRightGamepadButtons_, GamePadButtons::ARROW_RIGHT);
					});
					ImGui::PopID();
					ImGui::EndTabItem();
				}
				if (ImGui::BeginTabItem("決定")) {
					ImGui::SeparatorText("キーボード");
					DrawField(anyItemActive,
						[&]() { return CanvasInputEditing::DrawKeyboardBindings("決定", submitKeys_, KeyDIKCode::RETURN); });
					ImGui::SeparatorText("ゲームパッド");
					DrawField(anyItemActive, [&]() {
						return CanvasInputEditing::DrawGamepadBindings("決定", submitGamepadButtons_, GamePadButtons::A);
					});
					ImGui::EndTabItem();
				}
				ImGui::EndTabBar();
			}
		}
		ImGui::Unindent();

		if (draft.navigationMode == CanvasNavigationMode::TransitionTable) {
			ImGui::Indent();
			if (MyGUI::CollapsingHeader("遷移テーブル", false)) {
				DrawField(anyItemActive, [&]() {
					int32_t rows = navigationTable_.rows;
					ValueEditResult result = MyGUI::DragInt("縦", rows, {.dragSpeed = 1.0f, .minValue = 1});
					if (result.valueChanged) {
						if (!ResizeCanvasNavigationTable(navigationTable_, rows, navigationTable_.columns)) {
							result.valueChanged = false;
						}
					}
					return result;
				});
				DrawField(anyItemActive, [&]() {
					int32_t columns = navigationTable_.columns;
					ValueEditResult result = MyGUI::DragInt("横", columns, {.dragSpeed = 1.0f, .minValue = 1});
					if (result.valueChanged) {
						if (!ResizeCanvasNavigationTable(navigationTable_, navigationTable_.rows, columns)) {
							result.valueChanged = false;
						}
					}
					return result;
				});
				DrawField(anyItemActive,
					[&]() { return CanvasNavigationEditing::DrawNavigationTable(world, entity, navigationTable_); });
			}
			ImGui::Unindent();
		}
	}
	ImGui::Unindent();
}

void Engine::CanvasInspectorDrawer::OnSyncDraftFromWorld(
	ECSWorld& world, const Entity& entity, const CanvasComponent& component) {

	// 前のEntityの入力設定を解除する
	navigationUpKeys_.clear();
	navigationDownKeys_.clear();
	navigationLeftKeys_.clear();
	navigationRightKeys_.clear();
	navigationUpGamepadButtons_.clear();
	navigationDownGamepadButtons_.clear();
	navigationLeftGamepadButtons_.clear();
	navigationRightGamepadButtons_.clear();
	submitKeys_.clear();
	submitGamepadButtons_.clear();

	// Bufferを機器別の編集配列へ展開する
	for (const CanvasInputBinding& binding : GetCanvasInputBindings(world, entity)) {

		if (binding.device == CanvasInputDevice::Keyboard) {
			const KeyDIKCode key = static_cast<KeyDIKCode>(binding.code);
			switch (binding.action) {
			case CanvasInputAction::Up:
				navigationUpKeys_.emplace_back(key);
				break;
			case CanvasInputAction::Down:
				navigationDownKeys_.emplace_back(key);
				break;
			case CanvasInputAction::Left:
				navigationLeftKeys_.emplace_back(key);
				break;
			case CanvasInputAction::Right:
				navigationRightKeys_.emplace_back(key);
				break;
			case CanvasInputAction::Submit:
				submitKeys_.emplace_back(key);
				break;
			}
			continue;
		}

		const GamePadButtons button = static_cast<GamePadButtons>(binding.code);
		switch (binding.action) {
		case CanvasInputAction::Up:
			navigationUpGamepadButtons_.emplace_back(button);
			break;
		case CanvasInputAction::Down:
			navigationDownGamepadButtons_.emplace_back(button);
			break;
		case CanvasInputAction::Left:
			navigationLeftGamepadButtons_.emplace_back(button);
			break;
		case CanvasInputAction::Right:
			navigationRightGamepadButtons_.emplace_back(button);
			break;
		case CanvasInputAction::Submit:
			submitGamepadButtons_.emplace_back(button);
			break;
		}
	}

	// 遷移表の配置をBufferから復元する
	navigationTable_.rows = component.navigationRows;
	navigationTable_.columns = component.navigationColumns;
	navigationTable_.cells.clear();
	for (const CanvasNavigationCell& cell : GetCanvasNavigationCells(world, entity)) {
		navigationTable_.cells.emplace_back(cell.localFileID);
	}
	ResizeCanvasNavigationTable(navigationTable_, navigationTable_.rows, navigationTable_.columns);
}

void Engine::CanvasInspectorDrawer::SerializeDraft([[maybe_unused]] ECSWorld& world, [[maybe_unused]] const Entity& entity,
	const CanvasComponent& component, nlohmann::json& out) const {

	// 機器別の編集値を保存順へまとめる
	std::vector<CanvasInputBinding> bindings;
	auto appendKeys = [&](CanvasInputAction action, const std::vector<KeyDIKCode>& values) {
		for (KeyDIKCode value : values) {
			bindings.emplace_back(CanvasInputBinding{static_cast<uint16_t>(value), action, CanvasInputDevice::Keyboard});
		}
	};
	auto appendButtons = [&](CanvasInputAction action, const std::vector<GamePadButtons>& values) {
		for (GamePadButtons value : values) {
			bindings.emplace_back(CanvasInputBinding{static_cast<uint16_t>(value), action, CanvasInputDevice::Gamepad});
		}
	};
	appendKeys(CanvasInputAction::Up, navigationUpKeys_);
	appendKeys(CanvasInputAction::Down, navigationDownKeys_);
	appendKeys(CanvasInputAction::Left, navigationLeftKeys_);
	appendKeys(CanvasInputAction::Right, navigationRightKeys_);
	appendKeys(CanvasInputAction::Submit, submitKeys_);
	appendButtons(CanvasInputAction::Up, navigationUpGamepadButtons_);
	appendButtons(CanvasInputAction::Down, navigationDownGamepadButtons_);
	appendButtons(CanvasInputAction::Left, navigationLeftGamepadButtons_);
	appendButtons(CanvasInputAction::Right, navigationRightGamepadButtons_);
	appendButtons(CanvasInputAction::Submit, submitGamepadButtons_);

	// セル配置と表の寸法を一緒に保存する
	std::vector<CanvasNavigationCell> cells;
	cells.reserve(navigationTable_.cells.size());
	for (UUID localFileID : navigationTable_.cells) {
		cells.emplace_back(CanvasNavigationCell{localFileID});
	}

	CanvasComponent serialized = component;
	serialized.navigationRows = navigationTable_.rows;
	serialized.navigationColumns = navigationTable_.columns;
	SerializeCanvas(serialized, bindings, cells, out);
}

void Engine::CanvasInspectorDrawer::ApplyPreview(
	ECSWorld& world, const Entity& entity, const CanvasComponent& previewComponent) {

	if (!world.IsAlive(entity) || !world.HasComponent<CanvasComponent>(entity)) {
		return;
	}

	nlohmann::json data;
	SerializeDraft(world, entity, previewComponent, data);

	// 保存と同じ経路でBufferを更新する
	CanvasComponent::DeserializeECS(world, entity, data, world.GetComponent<CanvasComponent>(entity));
	world.MarkComponentModified<CanvasComponent>(entity);
}
