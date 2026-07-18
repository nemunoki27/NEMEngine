#include "UIComponentInspectorDrawers.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Commands/Components/SetUIProgressDelayedCommand.h>
#include <Engine/Editor/UI/Inspectors/Common/InspectorDrawerCommon.h>
#include <Engine/Editor/UI/Panels/Core/IEditorPanel.h>
#include <Engine/Editor/Core/EditorContext.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/World/Components/Scene/NameComponent.h>
#include <Engine/Core/World/Components/Transform/HierarchyComponent.h>
#include <Engine/Core/World/Components/Rendering/PrimitiveRendererComponent.h>
#include <Engine/Core/World/Components/Rendering/SpriteRendererComponent.h>
#include <Engine/Core/World/Components/Rendering/TextRendererComponent.h>
#include <Engine/Core/World/Scene/Utility/SceneObjectUtility.h>
#include <Engine/Core/Tools/ImGui/ImGuiHelpers.h>

// c++
#include <algorithm>
#include <array>
#include <memory>
#include <utility>
#include <vector>

//============================================================================
//	UIComponentInspectorDrawers internal
//============================================================================
namespace {

	constexpr const char* kNavigationCellDragDropType = "NEM_UI_NAV_CELL";

	Engine::ValueEditResult DrawEntityReference(const char* label,
		Engine::ECSWorld& world, Engine::UUID& localFileID) {

		Engine::ValueEditResult result{};
		if (!Engine::MyGUI::BeginPropertyRow(label)) {
			return result;
		}

		std::string preview = "None (Drop entity here)";
		if (localFileID) {
			const Engine::Entity target = Engine::SceneObjectUtility::FindByLocalFileID(world, localFileID);
			if (world.IsAlive(target)) {
				const auto* name = world.TryGetComponent<Engine::NameComponent>(target);
				preview = name ? name->name : "Entity";
			} else {
				preview = "Missing Entity | " + Engine::ToString(localFileID);
			}
		}

		ImGui::PushID(label);
		if (!localFileID) {
			ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
		}
		ImGui::Button(preview.c_str(), ImVec2(ImGui::GetContentRegionAvail().x, ImGui::GetFrameHeight()));
		if (!localFileID) {
			ImGui::PopStyleColor();
		}
		result.anyItemActive = ImGui::IsItemActive();

		if (localFileID && ImGui::BeginPopupContextItem("##deleteEntityReference")) {
			if (ImGui::MenuItem("削除")) {
				localFileID = {};
				result.valueChanged = true;
				result.editFinished = true;
			}
			ImGui::EndPopup();
		}
		if (ImGui::BeginDragDropTarget()) {
			if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(
				Engine::IEditorPanel::kHierarchyDragDropPayloadType)) {
				if (payload->IsDelivery() && payload->DataSize == sizeof(Engine::UUID)) {
					const Engine::UUID stableUUID = *static_cast<const Engine::UUID*>(payload->Data);
					const Engine::Entity target = world.FindByUUID(stableUUID);
					if (world.IsAlive(target)) {
						const auto* sceneObject = world.TryGetComponent<Engine::SceneObjectComponent>(target);
						if (sceneObject) {
							localFileID = sceneObject->localFileID;
							result.valueChanged = true;
							result.editFinished = true;
						}
					}
				}
			}
			ImGui::EndDragDropTarget();
		}
		ImGui::PopID();
		Engine::MyGUI::EndPropertyRow();
		return result;
	}

	Engine::UUID GetLocalFileID(Engine::ECSWorld& world, Engine::Entity entity) {

		const auto* sceneObject = world.TryGetComponent<Engine::SceneObjectComponent>(entity);
		return sceneObject ? sceneObject->localFileID : Engine::UUID{};
	}

	bool IsCanvasButton(Engine::ECSWorld& world, Engine::Entity canvas, Engine::Entity entity) {

		if (!world.IsAlive(entity) || entity == canvas ||
			!world.HasComponent<Engine::UISelectableComponent>(entity) ||
			(!world.HasComponent<Engine::UIImageButtonComponent>(entity) &&
				!world.HasComponent<Engine::UITextButtonComponent>(entity))) {
			return false;
		}

		Engine::Entity current = entity;
		while (world.IsAlive(current)) {

			if (current == canvas) {
				return true;
			}
			if (current != entity && world.HasComponent<Engine::CanvasComponent>(current)) {
				return false;
			}
			const auto* hierarchy = world.TryGetComponent<Engine::HierarchyComponent>(current);
			current = hierarchy ? hierarchy->parent : Engine::Entity::Null();
		}
		return false;
	}

	void CollectCanvasButtons(Engine::ECSWorld& world, Engine::Entity canvas,
		Engine::Entity parent, std::vector<Engine::Entity>& buttons) {

		const auto* hierarchy = world.TryGetComponent<Engine::HierarchyComponent>(parent);
		Engine::Entity child = hierarchy ? hierarchy->firstChild : Engine::Entity::Null();
		while (world.IsAlive(child)) {

			const auto& childHierarchy = world.GetComponent<Engine::HierarchyComponent>(child);
			const Engine::Entity next = childHierarchy.nextSibling;
			if (!world.HasComponent<Engine::CanvasComponent>(child)) {
				if (IsCanvasButton(world, canvas, child)) {
					buttons.emplace_back(child);
				}
				CollectCanvasButtons(world, canvas, child, buttons);
			}
			child = next;
		}
	}

	std::string GetNavigationCellLabel(Engine::ECSWorld& world, Engine::UUID localFileID) {

		if (!localFileID) {
			return "(Empty)";
		}
		const Engine::Entity entity = Engine::SceneObjectUtility::FindByLocalFileID(world, localFileID);
		if (!world.IsAlive(entity)) {
			return "Missing Entity";
		}
		const auto* name = world.TryGetComponent<Engine::NameComponent>(entity);
		return name ? name->name : "Entity";
	}

	void AssignNavigationCell(Engine::CanvasNavigationTable& table, size_t index,
		Engine::UUID localFileID) {

		if (table.cells.size() <= index) {
			return;
		}
		if (localFileID) {
			for (Engine::UUID& cell : table.cells) {
				if (cell == localFileID) {
					cell = {};
				}
			}
		}
		table.cells[index] = localFileID;
	}

	Engine::ValueEditResult DrawNavigationCell(Engine::ECSWorld& world, Engine::Entity canvas,
		Engine::CanvasNavigationTable& table, size_t index, const std::vector<Engine::Entity>& buttons) {

		Engine::ValueEditResult result{};
		Engine::UUID& cell = table.cells[index];
		const std::string cellLabel = GetNavigationCellLabel(world, cell);
		const std::string label = cellLabel + "##cell";
		const float buttonWidth = (std::max)(1.0f, ImGui::GetContentRegionAvail().x);

		ImGui::PushID(static_cast<int32_t>(index));
		ImGui::Button(label.c_str(), ImVec2(buttonWidth, ImGui::GetFrameHeight()));
		result.anyItemActive = ImGui::IsItemActive();
		if (ImGui::IsItemHovered() &&
			buttonWidth < ImGui::CalcTextSize(cellLabel.c_str()).x + ImGui::GetStyle().FramePadding.x * 2.0f) {
			ImGui::SetTooltip("%s", cellLabel.c_str());
		}

		if (cell && ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
			const int32_t sourceIndex = static_cast<int32_t>(index);
			ImGui::SetDragDropPayload(kNavigationCellDragDropType, &sourceIndex, sizeof(sourceIndex));
			ImGui::TextUnformatted(GetNavigationCellLabel(world, cell).c_str());
			ImGui::EndDragDropSource();
		}
		if (ImGui::BeginDragDropTarget()) {
			if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kNavigationCellDragDropType)) {
				if (payload->IsDelivery() && payload->DataSize == sizeof(int32_t)) {
					const int32_t sourceIndex = *static_cast<const int32_t*>(payload->Data);
					if (0 <= sourceIndex && static_cast<size_t>(sourceIndex) < table.cells.size()) {
						std::swap(table.cells[static_cast<size_t>(sourceIndex)], cell);
						result.valueChanged = true;
						result.editFinished = true;
					}
				}
			}
			if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(
				Engine::IEditorPanel::kHierarchyDragDropPayloadType)) {
				if (payload->IsDelivery() && payload->DataSize == sizeof(Engine::UUID)) {
					const Engine::UUID stableUUID = *static_cast<const Engine::UUID*>(payload->Data);
					const Engine::Entity target = world.FindByUUID(stableUUID);
					if (IsCanvasButton(world, canvas, target)) {
						AssignNavigationCell(table, index, GetLocalFileID(world, target));
						result.valueChanged = true;
						result.editFinished = true;
					}
				}
			}
			ImGui::EndDragDropTarget();
		}

		if (ImGui::BeginPopupContextItem("##navigationCellMenu")) {
			if (ImGui::MenuItem("空にする", nullptr, false, static_cast<bool>(cell))) {
				cell = {};
				result.valueChanged = true;
				result.editFinished = true;
			}
			ImGui::Separator();
			for (Engine::Entity button : buttons) {

				const Engine::UUID localFileID = GetLocalFileID(world, button);
				const std::string buttonLabel = GetNavigationCellLabel(world, localFileID);
				ImGui::PushID(Engine::ToString(localFileID).c_str());
				if (ImGui::MenuItem(buttonLabel.c_str(), nullptr, cell == localFileID)) {
					AssignNavigationCell(table, index, localFileID);
					result.valueChanged = true;
					result.editFinished = true;
				}
				ImGui::PopID();
			}
			ImGui::EndPopup();
		}
		ImGui::PopID();
		return result;
	}

	Engine::ValueEditResult DrawNavigationTable(Engine::ECSWorld& world, Engine::Entity canvas,
		Engine::CanvasNavigationTable& table) {

		Engine::ValueEditResult result{};
		Engine::ResizeCanvasNavigationTable(table, table.rows, table.columns);

		std::vector<Engine::Entity> buttons;
		CollectCanvasButtons(world, canvas, canvas, buttons);

		if (ImGui::Button("配下のボタンを自動配置")) {
			std::fill(table.cells.begin(), table.cells.end(), Engine::UUID{});
			for (size_t i = 0; i < buttons.size() && i < table.cells.size(); ++i) {
				table.cells[i] = GetLocalFileID(world, buttons[i]);
			}
			result.valueChanged = true;
			result.editFinished = true;
		}
		ImGui::SameLine();
		if (ImGui::Button("すべて空にする")) {
			std::fill(table.cells.begin(), table.cells.end(), Engine::UUID{});
			result.valueChanged = true;
			result.editFinished = true;
		}

		const ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_SizingStretchSame;
		const float height = ImGui::GetFrameHeightWithSpacing() * table.rows;
		if (ImGui::BeginTable("##CanvasNavigationTable", table.columns, flags, ImVec2(0.0f, height))) {
			for (int32_t column = 0; column < table.columns; ++column) {
				ImGui::TableSetupColumn(nullptr, ImGuiTableColumnFlags_WidthStretch, 1.0f);
			}
			for (int32_t row = 0; row < table.rows; ++row) {

				ImGui::TableNextRow();
				for (int32_t column = 0; column < table.columns; ++column) {

					ImGui::TableSetColumnIndex(column);
					const size_t index = static_cast<size_t>(row * table.columns + column);
					const Engine::ValueEditResult cellResult =
						DrawNavigationCell(world, canvas, table, index, buttons);
					result.valueChanged |= cellResult.valueChanged;
					result.anyItemActive |= cellResult.anyItemActive;
					result.editFinished |= cellResult.editFinished;
				}
			}
			ImGui::EndTable();
		}
		return result;
	}

	constexpr auto kKeyboardSubmitInputs = std::to_array<KeyDIKCode>({
		KeyDIKCode::ESCAPE,
		KeyDIKCode::_1,KeyDIKCode::_2,KeyDIKCode::_3,KeyDIKCode::_4,KeyDIKCode::_5,
		KeyDIKCode::_6,KeyDIKCode::_7,KeyDIKCode::_8,KeyDIKCode::_9,KeyDIKCode::_0,
		KeyDIKCode::MINUS,KeyDIKCode::EQUALS,KeyDIKCode::BACK,KeyDIKCode::TAB,
		KeyDIKCode::Q,KeyDIKCode::W,KeyDIKCode::E,KeyDIKCode::R,KeyDIKCode::T,
		KeyDIKCode::Y,KeyDIKCode::U,KeyDIKCode::I,KeyDIKCode::O,KeyDIKCode::P,
		KeyDIKCode::LBRACKET,KeyDIKCode::RBRACKET,KeyDIKCode::RETURN,KeyDIKCode::LCONTROL,
		KeyDIKCode::A,KeyDIKCode::S,KeyDIKCode::D,KeyDIKCode::F,KeyDIKCode::G,
		KeyDIKCode::H,KeyDIKCode::J,KeyDIKCode::K,KeyDIKCode::L,
		KeyDIKCode::SEMICOLON,KeyDIKCode::APOSTROPHE,KeyDIKCode::GRAVE,
		KeyDIKCode::LSHIFT,KeyDIKCode::BACKSLASH,
		KeyDIKCode::Z,KeyDIKCode::X,KeyDIKCode::C,KeyDIKCode::V,
		KeyDIKCode::B,KeyDIKCode::N,KeyDIKCode::M,
		KeyDIKCode::COMMA,KeyDIKCode::PERIOD,KeyDIKCode::SLASH,KeyDIKCode::RSHIFT,
		KeyDIKCode::MULTIPLY,KeyDIKCode::LALT,KeyDIKCode::SPACE,KeyDIKCode::CAPITAL,
		KeyDIKCode::F1,KeyDIKCode::F2,KeyDIKCode::F3,KeyDIKCode::F4,KeyDIKCode::F5,
		KeyDIKCode::F6,KeyDIKCode::F7,KeyDIKCode::F8,KeyDIKCode::F9,KeyDIKCode::F10,
		KeyDIKCode::F11,KeyDIKCode::F12,
		KeyDIKCode::NUMLOCK,KeyDIKCode::SCROLL,
		KeyDIKCode::NUMPAD7,KeyDIKCode::NUMPAD8,KeyDIKCode::NUMPAD9,KeyDIKCode::SUBTRACT,
		KeyDIKCode::NUMPAD4,KeyDIKCode::NUMPAD5,KeyDIKCode::NUMPAD6,KeyDIKCode::ADD,
		KeyDIKCode::NUMPAD1,KeyDIKCode::NUMPAD2,KeyDIKCode::NUMPAD3,
		KeyDIKCode::NUMPAD0,KeyDIKCode::DECIMAL,KeyDIKCode::NUMPADENTER,
		KeyDIKCode::RCONTROL,KeyDIKCode::DIVIDE,KeyDIKCode::RALT,
		KeyDIKCode::HOME,KeyDIKCode::UP,KeyDIKCode::PRIOR,KeyDIKCode::LEFT,
		KeyDIKCode::RIGHT,KeyDIKCode::END,KeyDIKCode::DOWN,KeyDIKCode::NEXT,
		KeyDIKCode::INSERT,KeyDIKCode::DELETE_KEY,
		KeyDIKCode::LWIN,KeyDIKCode::RWIN,KeyDIKCode::APPS
		});

	constexpr auto kGamepadSubmitInputs = std::to_array<GamePadButtons>({
		GamePadButtons::ARROW_UP,GamePadButtons::ARROW_DOWN,
		GamePadButtons::ARROW_LEFT,GamePadButtons::ARROW_RIGHT,
		GamePadButtons::START,GamePadButtons::BACK,
		GamePadButtons::LEFT_THUMB,GamePadButtons::RIGHT_THUMB,
		GamePadButtons::LEFT_SHOULDER,GamePadButtons::RIGHT_SHOULDER,
		GamePadButtons::LEFT_TRIGGER,GamePadButtons::RIGHT_TRIGGER,
		GamePadButtons::A,GamePadButtons::B,GamePadButtons::X,GamePadButtons::Y
		});

	const char* KeyboardInputName(KeyDIKCode input) {

		switch (input) {
		case KeyDIKCode::NUMPADENTER: return "NUMPADENTER";
		case KeyDIKCode::RCONTROL: return "RCONTROL";
		case KeyDIKCode::DIVIDE: return "DIVIDE";
		case KeyDIKCode::RALT: return "RALT";
		case KeyDIKCode::HOME: return "HOME";
		case KeyDIKCode::UP: return "UP";
		case KeyDIKCode::PRIOR: return "PAGE_UP";
		case KeyDIKCode::LEFT: return "LEFT";
		case KeyDIKCode::RIGHT: return "RIGHT";
		case KeyDIKCode::END: return "END";
		case KeyDIKCode::DOWN: return "DOWN";
		case KeyDIKCode::NEXT: return "PAGE_DOWN";
		case KeyDIKCode::INSERT: return "INSERT";
		case KeyDIKCode::DELETE_KEY: return "DELETE";
		case KeyDIKCode::LWIN: return "LWIN";
		case KeyDIKCode::RWIN: return "RWIN";
		case KeyDIKCode::APPS: return "APPS";
		default: return Engine::EnumAdapter<KeyDIKCode>::ToString(input);
		}
	}

	bool DrawKeyboardInputCombo(const char* label, KeyDIKCode& current) {

		bool changed = false;
		if (ImGui::BeginCombo(label, KeyboardInputName(current))) {
			for (KeyDIKCode input : kKeyboardSubmitInputs) {

				const bool selected = current == input;
				if (ImGui::Selectable(KeyboardInputName(input), selected)) {
					current = input;
					changed = true;
				}
				if (selected) {
					ImGui::SetItemDefaultFocus();
				}
			}
			ImGui::EndCombo();
		}
		return changed;
	}

	bool DrawGamepadInputCombo(const char* label, GamePadButtons& current) {

		bool changed = false;
		if (ImGui::BeginCombo(label, Engine::EnumAdapter<GamePadButtons>::ToString(current))) {
			for (GamePadButtons input : kGamepadSubmitInputs) {

				const bool selected = current == input;
				if (ImGui::Selectable(Engine::EnumAdapter<GamePadButtons>::ToString(input), selected)) {
					current = input;
					changed = true;
				}
				if (selected) {
					ImGui::SetItemDefaultFocus();
				}
			}
			ImGui::EndCombo();
		}
		return changed;
	}

	template <typename T, size_t Size, typename DrawComboFn>
	Engine::ValueEditResult DrawInputBindings(const char* label, std::vector<T>& bindings,
		const std::array<T, Size>& candidates, T preferred, DrawComboFn&& drawCombo) {

		Engine::ValueEditResult result{};
		if (!Engine::MyGUI::BeginPropertyRow(label)) {
			return result;
		}

		int32_t removeIndex = -1;
		for (int32_t i = 0; i < static_cast<int32_t>(bindings.size()); ++i) {

			ImGui::PushID(i);
			const float removeWidth = ImGui::CalcTextSize("削除").x +
				ImGui::GetStyle().FramePadding.x * 2.0f;
			ImGui::SetNextItemWidth((std::max)(
				ImGui::GetContentRegionAvail().x - removeWidth - ImGui::GetStyle().ItemSpacing.x, 1.0f));
			if (drawCombo("##Input", bindings[static_cast<size_t>(i)])) {
				result.valueChanged = true;
				result.editFinished = true;
			}
			result.anyItemActive |= ImGui::IsItemActive();
			ImGui::SameLine();
			if (ImGui::SmallButton("削除")) {
				removeIndex = i;
			}
			ImGui::PopID();
		}
		if (0 <= removeIndex) {
			bindings.erase(bindings.begin() + removeIndex);
			result.valueChanged = true;
			result.editFinished = true;
		}

		auto next = std::find(candidates.begin(), candidates.end(), preferred);
		if (next == candidates.end() ||
			std::find(bindings.begin(), bindings.end(), *next) != bindings.end()) {
			next = std::find_if(candidates.begin(), candidates.end(), [&](T candidate) {
				return std::find(bindings.begin(), bindings.end(), candidate) == bindings.end();
				});
		}
		if (next == candidates.end()) {
			ImGui::BeginDisabled();
		}
		if (ImGui::Button("追加", ImVec2(ImGui::GetContentRegionAvail().x, 0.0f)) &&
			next != candidates.end()) {
			bindings.emplace_back(*next);
			result.valueChanged = true;
			result.editFinished = true;
		}
		if (next == candidates.end()) {
			ImGui::EndDisabled();
		}

		if (result.valueChanged) {
			std::vector<T> unique;
			unique.reserve(bindings.size());
			for (T binding : bindings) {
				if (std::find(unique.begin(), unique.end(), binding) == unique.end()) {
					unique.emplace_back(binding);
				}
			}
			bindings = std::move(unique);
		}
		Engine::MyGUI::EndPropertyRow();
		return result;
	}

	template <class DrawFieldFn>
	void DrawTransitionAnimationFields(DrawFieldFn&& drawField,
		const Engine::EditorPanelContext& context,
		Engine::UITransitionStyle& style, bool& anyItemActive) {

		drawField(anyItemActive, [&]() {
			return Engine::InspectorDrawerCommon::DrawCheckboxField("アニメーションクリップを使用", style.useAnimationClip);
			});
		if (style.useAnimationClip) {
			if (context.editorContext) {
				drawField(anyItemActive, [&]() {
					return Engine::MyGUI::AssetReferenceField("アニメーションクリップ", style.animationClip,
						context.editorContext->assetDatabase, { Engine::AssetType::AnimationClip });
					});
			}
		} else {
			ImGui::SeparatorText("色");
			{
				ImGui::PushID("UIColorAnim");
				drawField(anyItemActive, [&]() { return Engine::MyGUI::ColorEdit("色", style.color); });
				drawField(anyItemActive, [&]() {
					return Engine::MyGUI::DragFloat("遷移時間", style.colorTransitionDuration,
						{ .dragSpeed = 0.01f,.minValue = 0.0f,.maxValue = 10.0f });
					});
				drawField(anyItemActive, [&]() {
					return Engine::InspectorDrawerCommon::DrawEnumComboField("イージング", style.colorEasing);
					});
				ImGui::PopID();
			}
			ImGui::SeparatorText("スケール");
			{
				ImGui::PushID("UIScaleAnim");
				drawField(anyItemActive, [&]() {
					return Engine::MyGUI::DragVector2("スケール", style.scale,
						{ .dragSpeed = 0.01f,.minValue = 0.0f,.maxValue = 8.0f });
					});
				drawField(anyItemActive, [&]() {
					return Engine::MyGUI::DragFloat("遷移時間", style.scaleTransitionDuration,
						{ .dragSpeed = 0.01f,.minValue = 0.0f,.maxValue = 10.0f });
					});
				drawField(anyItemActive, [&]() {
					return Engine::InspectorDrawerCommon::DrawEnumComboField("イージング", style.scaleEasing);
					});
				ImGui::PopID();
			}
		}
	}

	template <class DrawFieldFn>
	void DrawTransitionTextureFields(DrawFieldFn&& drawField,
		const Engine::EditorPanelContext& context,
		Engine::UITransitionStyle& style, bool& anyItemActive) {

		drawField(anyItemActive, [&]() {
			return Engine::InspectorDrawerCommon::DrawCheckboxField(
				"上書きするか", style.overrideTexture);
			});
		if (style.overrideTexture && context.editorContext) {
			drawField(anyItemActive, [&]() {
				return Engine::MyGUI::AssetReferenceField("テクスチャ", style.texture,
					context.editorContext->assetDatabase, { Engine::AssetType::Texture });
				});
		}
	}

	template <class DrawFieldFn>
	void DrawTransitionSoundFields(DrawFieldFn&& drawField,
		const Engine::EditorPanelContext& context,
		Engine::UITransitionStyle& style, bool& anyItemActive) {

		if (context.editorContext) {
			drawField(anyItemActive, [&]() {
				return Engine::MyGUI::AssetReferenceField("オーディオクリップ", style.sound,
					context.editorContext->assetDatabase, { Engine::AssetType::Audio });
				});
		}
		drawField(anyItemActive, [&]() {
			return Engine::MyGUI::DragFloat("音量", style.soundVolume,
				{ .dragSpeed = 0.01f,.minValue = 0.0f,.maxValue = 1.0f });
			});
	}

	template <class DrawFieldFn>
	void DrawTransitionStyleFields(DrawFieldFn&& drawField,
		const Engine::EditorPanelContext& context,
		Engine::UITransitionStyle& style, bool& anyItemActive) {

		if (ImGui::BeginTabBar("##UITransitionStyleTabs")) {
			if (ImGui::BeginTabItem("アニメーション")) {
				DrawTransitionAnimationFields(
					drawField, context, style, anyItemActive);
				ImGui::EndTabItem();
			}
			if (ImGui::BeginTabItem("テクスチャ")) {
				DrawTransitionTextureFields(
					drawField, context, style, anyItemActive);
				ImGui::EndTabItem();
			}
			if (ImGui::BeginTabItem("サウンド")) {
				DrawTransitionSoundFields(
					drawField, context, style, anyItemActive);
				ImGui::EndTabItem();
			}
			ImGui::EndTabBar();
		}
	}

	template <class DrawFieldFn>
	void DrawTransitionStyle(DrawFieldFn&& drawField, const Engine::EditorPanelContext& context,
		const char* label, Engine::UITransitionStyle& style, bool& anyItemActive) {

		ImGui::Indent();
		if (!Engine::MyGUI::CollapsingHeader(label, false)) {
			ImGui::Unindent();
			return;
		}
		ImGui::PushID(label);
		DrawTransitionStyleFields(drawField, context, style, anyItemActive);
		ImGui::PopID();
		ImGui::Unindent();
	}
}

//========================================================================================================================================================
//	CanvasInspectorDrawer classMethods
//========================================================================================================================================================

void Engine::CanvasInspectorDrawer::DrawFields([[maybe_unused]] const EditorPanelContext& context,
	ECSWorld& world, [[maybe_unused]] const Entity& entity, bool& anyItemActive) {

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
		DrawField(anyItemActive, [&]() { return InspectorDrawerCommon::DrawCheckboxField("ゲーム入力をブロック", draft.blockGameplayInput); });
		DrawField(anyItemActive, [&]() { return InspectorDrawerCommon::DrawCheckboxField("編集中に入力有効", draft.inputInEditMode); });
		DrawField(anyItemActive, [&]() { return InspectorDrawerCommon::DrawCheckboxField("決定後入力を受け付けない", draft.blockInputAfterSubmit); });
		DrawField(anyItemActive, [&]() {
			return MyGUI::DragFloat("リピート開始", draft.repeatDelay,
				{ .dragSpeed = 0.01f,.minValue = 0.0f,.maxValue = 10.0f });
			});
		DrawField(anyItemActive, [&]() {
			return MyGUI::DragFloat("リピート間隔", draft.repeatInterval,
				{ .dragSpeed = 0.01f,.minValue = 0.01f,.maxValue = 10.0f });
			});
		DrawField(anyItemActive, [&]() { return DrawEntityReference("初期アクティブUI", world, draft.firstSelectedLocalFileID); });

		DrawField(anyItemActive, [&]() { return InspectorDrawerCommon::DrawEnumComboField("ナビゲーション方式", draft.navigationMode); });
		DrawField(anyItemActive, [&]() { return InspectorDrawerCommon::DrawCheckboxField("ナビゲーションをループ", draft.wrapNavigation); });

		ImGui::Indent();
		if (MyGUI::CollapsingHeader("UI入力デバイス", false)) {
			DrawField(anyItemActive, [&]() {
				return InspectorDrawerCommon::DrawCheckboxField(
					"キーボード入力有効", draft.keyboardInputEnabled);
				});
			DrawField(anyItemActive, [&]() {
				return InspectorDrawerCommon::DrawCheckboxField(
					"ゲームパッド入力有効", draft.gamepadInputEnabled);
				});

			if (ImGui::BeginTabBar("##CanvasUIInputTabs")) {
				if (ImGui::BeginTabItem("選択")) {
					ImGui::SeparatorText("キーボード");
					ImGui::PushID("KeyboardNavigation");
					DrawField(anyItemActive, [&]() {
						return DrawInputBindings("上", draft.navigationUpKeys,
							kKeyboardSubmitInputs, KeyDIKCode::UP, DrawKeyboardInputCombo);
						});
					DrawField(anyItemActive, [&]() {
						return DrawInputBindings("下", draft.navigationDownKeys,
							kKeyboardSubmitInputs, KeyDIKCode::DOWN, DrawKeyboardInputCombo);
						});
					DrawField(anyItemActive, [&]() {
						return DrawInputBindings("左", draft.navigationLeftKeys,
							kKeyboardSubmitInputs, KeyDIKCode::LEFT, DrawKeyboardInputCombo);
						});
					DrawField(anyItemActive, [&]() {
						return DrawInputBindings("右", draft.navigationRightKeys,
							kKeyboardSubmitInputs, KeyDIKCode::RIGHT, DrawKeyboardInputCombo);
						});
					ImGui::PopID();

					ImGui::SeparatorText("ゲームパッド");
					ImGui::PushID("GamepadNavigation");
					DrawField(anyItemActive, [&]() {
						return InspectorDrawerCommon::DrawCheckboxField(
							"左スティックを使用", draft.gamepadLeftStickEnabled);
						});
					if (draft.gamepadLeftStickEnabled) {
						DrawField(anyItemActive, [&]() {
							return MyGUI::DragFloat("スティックしきい値", draft.stickThreshold,
								{ .dragSpeed = 0.01f,.minValue = 0.0f,.maxValue = 1.0f,
								.flags = ImGuiSliderFlags_AlwaysClamp });
							});
					}
					DrawField(anyItemActive, [&]() {
						return DrawInputBindings("上", draft.navigationUpGamepadButtons,
							kGamepadSubmitInputs, GamePadButtons::ARROW_UP, DrawGamepadInputCombo);
						});
					DrawField(anyItemActive, [&]() {
						return DrawInputBindings("下", draft.navigationDownGamepadButtons,
							kGamepadSubmitInputs, GamePadButtons::ARROW_DOWN, DrawGamepadInputCombo);
						});
					DrawField(anyItemActive, [&]() {
						return DrawInputBindings("左", draft.navigationLeftGamepadButtons,
							kGamepadSubmitInputs, GamePadButtons::ARROW_LEFT, DrawGamepadInputCombo);
						});
					DrawField(anyItemActive, [&]() {
						return DrawInputBindings("右", draft.navigationRightGamepadButtons,
							kGamepadSubmitInputs, GamePadButtons::ARROW_RIGHT, DrawGamepadInputCombo);
						});
					ImGui::PopID();
					ImGui::EndTabItem();
				}
				if (ImGui::BeginTabItem("決定")) {
					ImGui::SeparatorText("キーボード");
					DrawField(anyItemActive, [&]() {
						return DrawInputBindings("決定", draft.submitKeys,
							kKeyboardSubmitInputs, KeyDIKCode::RETURN, DrawKeyboardInputCombo);
						});
					ImGui::SeparatorText("ゲームパッド");
					DrawField(anyItemActive, [&]() {
						return DrawInputBindings("決定", draft.submitGamepadButtons,
							kGamepadSubmitInputs, GamePadButtons::A, DrawGamepadInputCombo);
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
					int32_t rows = draft.navigationTable.rows;
					ValueEditResult result = MyGUI::DragInt("縦", rows,
						{ .dragSpeed = 1.0f,.minValue = 1,.maxValue = CanvasNavigationTable::kMaxSize });
					if (result.valueChanged) {
						ResizeCanvasNavigationTable(draft.navigationTable, rows, draft.navigationTable.columns);
					}
					return result;
					});
				DrawField(anyItemActive, [&]() {
					int32_t columns = draft.navigationTable.columns;
					ValueEditResult result = MyGUI::DragInt("横", columns,
						{ .dragSpeed = 1.0f,.minValue = 1,.maxValue = CanvasNavigationTable::kMaxSize });
					if (result.valueChanged) {
						ResizeCanvasNavigationTable(draft.navigationTable, draft.navigationTable.rows, columns);
					}
					return result;
					});
				DrawField(anyItemActive, [&]() {
					return DrawNavigationTable(world, entity, draft.navigationTable);
					});
			}
			ImGui::Unindent();
		}
	}
	ImGui::Unindent();
}

//========================================================================================================================================================
//	UISelectableInspectorDrawer classMethods
//========================================================================================================================================================

void Engine::UISelectableInspectorDrawer::DrawFields(const EditorPanelContext& context,
	[[maybe_unused]] ECSWorld& world, [[maybe_unused]] const Entity& entity, bool& anyItemActive) {

	auto& draft = GetDraft();

	DrawField(anyItemActive, [&]() { return InspectorDrawerCommon::DrawCheckboxField("入力操作対象、アクティブ設定", draft.interactable); });

	auto drawField = [&](bool& active, auto&& function) {
		DrawField(active, std::forward<decltype(function)>(function));
	};
	DrawTransitionStyle(drawField, context, "通常", draft.normal, anyItemActive);
	DrawTransitionStyle(drawField, context, "選択", draft.selected, anyItemActive);
	DrawTransitionStyle(drawField, context, "決定", draft.submitted, anyItemActive);
	DrawTransitionStyle(drawField, context, "無効", draft.disabled, anyItemActive);
}

void Engine::UISelectableInspectorDrawer::ApplyPreview(ECSWorld& world,
	const Entity& entity, const UISelectableComponent& previewComponent) {

	if (!world.IsAlive(entity) || !world.HasComponent<UISelectableComponent>(entity)) {
		return;
	}
	ApplyUISelectableAuthoring(previewComponent, world.GetComponent<UISelectableComponent>(entity));
}

//========================================================================================================================================================
//	UIImageButtonInspectorDrawer classMethods
//========================================================================================================================================================

void Engine::UIImageButtonInspectorDrawer::DrawFields([[maybe_unused]] const EditorPanelContext& context,
	ECSWorld& world, const Entity& entity, bool& anyItemActive) {

	auto& draft = GetDraft();
	DrawField(anyItemActive, [&]() { return InspectorDrawerCommon::DrawCheckboxField("有効", draft.enabled); });
	DrawField(anyItemActive, [&]() { return MyGUI::InputText("アクション名", draft.actionName); });
	if (!world.HasComponent<SpriteRendererComponent>(entity)) {
		ImGui::TextDisabled("Sprite Rendererが必要です");
	}
}

//========================================================================================================================================================
//	UITextButtonInspectorDrawer classMethods
//========================================================================================================================================================

void Engine::UITextButtonInspectorDrawer::DrawFields([[maybe_unused]] const EditorPanelContext& context,
	ECSWorld& world, const Entity& entity, bool& anyItemActive) {

	auto& draft = GetDraft();
	DrawField(anyItemActive, [&]() { return InspectorDrawerCommon::DrawCheckboxField("有効", draft.enabled); });
	DrawField(anyItemActive, [&]() { return MyGUI::InputText("アクション名", draft.actionName); });
	if (!world.HasComponent<TextRendererComponent>(entity)) {
		ImGui::TextDisabled("Text Rendererが必要です");
	}
}

//========================================================================================================================================================
//	UIProgressInspectorDrawer classMethods
//========================================================================================================================================================

void Engine::UIProgressInspectorDrawer::DrawFields(const EditorPanelContext& context,
	ECSWorld& world, const Entity& entity, bool& anyItemActive) {

	auto& draft = GetDraft();
	DrawField(anyItemActive, [&]() { return InspectorDrawerCommon::DrawCheckboxField("有効", draft.enabled); });
	DrawField(anyItemActive, [&]() {
		return InspectorDrawerCommon::DrawCheckboxField(
			"編集中にプレビュー", draft.previewInEditMode);
		});
	const auto clampValue = [&]() {
		const float minValue = (std::min)(draft.minValue, draft.maxValue);
		const float maxValue = (std::max)(draft.minValue, draft.maxValue);
		draft.value = std::clamp(draft.value, minValue, maxValue);
		};
	DrawField(anyItemActive, [&]() {
		ValueEditResult result = MyGUI::DragFloat("最小値", draft.minValue, { .dragSpeed = 0.01f });
		if (result.valueChanged) {
			clampValue();
		}
		return result;
		});
	DrawField(anyItemActive, [&]() {
		ValueEditResult result = MyGUI::DragFloat("最大値", draft.maxValue, { .dragSpeed = 0.01f });
		if (result.valueChanged) {
			clampValue();
		}
		return result;
		});
	DrawField(anyItemActive, [&]() {
		const float minValue = (std::min)(draft.minValue, draft.maxValue);
		const float maxValue = (std::max)(draft.minValue, draft.maxValue);
		return MyGUI::DragFloat("現在値", draft.value,
			{ .dragSpeed = 0.01f,.minValue = minValue,.maxValue = maxValue,
			.flags = ImGuiSliderFlags_AlwaysClamp });
		});
	DrawField(anyItemActive, [&]() {

		ValueEditResult result{};
		const float spacing = ImGui::GetStyle().ItemSpacing.x;
		const float buttonWidth = (std::max)(1.0f,
			(ImGui::GetContentRegionAvail().x - spacing) * 0.5f);
		if (ImGui::Button("最大にする", ImVec2(buttonWidth, 0.0f))) {
			draft.value = draft.maxValue;
			result.valueChanged = true;
			result.editFinished = true;
		}
		ImGui::SameLine();
		if (ImGui::Button("最小にする", ImVec2(buttonWidth, 0.0f))) {
			draft.value = draft.minValue;
			result.valueChanged = true;
			result.editFinished = true;
		}
		return result;
		});
	DrawField(anyItemActive, [&]() { return InspectorDrawerCommon::DrawEnumComboField("方向", draft.direction); });
	const auto* primitive = world.TryGetComponent<PrimitiveRendererComponent>(entity);
	const bool hasPrimitiveRenderer = primitive != nullptr;
	if (!hasPrimitiveRenderer) {
		ImGui::TextDisabled("Primitive Rendererが必要です");
	} else if (!IsPrimitiveScreen2D(*primitive)) {
		ImGui::TextDisabled("Screen2DのPlaneまたはRingが必要です");
	}

	ImGui::Indent();
	if (MyGUI::CollapsingHeader("補間")) {
		DrawField(anyItemActive, [&]() { return InspectorDrawerCommon::DrawCheckboxField("補間する", draft.smooth); });
		if (draft.smooth) {
			DrawField(anyItemActive, [&]() {
				return MyGUI::DragFloat("補間時間", draft.smoothDuration,
					{ .dragSpeed = 0.01f,.minValue = 0.0f,.maxValue = 60.0f });
				});
			DrawField(anyItemActive, [&]() { return InspectorDrawerCommon::DrawEnumComboField("イージング", draft.smoothEasing); });
		}
	}
	if (MyGUI::CollapsingHeader("遅延表示")) {

		const bool enableDisabled = !hasPrimitiveRenderer && !draft.delayed;
		if (enableDisabled) {
			ImGui::BeginDisabled();
		}
		const bool beforeDelayed = draft.delayed;
		const ValueEditResult result =
			InspectorDrawerCommon::DrawCheckboxField("遅延表示する", draft.delayed);
		if (enableDisabled) {
			ImGui::EndDisabled();
		}
		anyItemActive |= result.anyItemActive;
		if (result.valueChanged) {

			const bool executed = context.host &&
				context.host->ExecuteEditorCommand(
					std::make_unique<SetUIProgressDelayedCommand>(entity, draft.delayed));
			if (executed && world.IsAlive(entity) &&
				world.HasComponent<UIProgressComponent>(entity)) {

				draft = world.GetComponent<UIProgressComponent>(entity);
			} else {
				draft.delayed = beforeDelayed;
			}
		}
		ImGui::Separator();
		if (draft.delayed) {
			DrawField(anyItemActive, [&]() {
				AssetEditSetting setting{};
				setting.graphicsCore = context.graphicsCore;
				return MyGUI::AssetReferenceField("遅延テクスチャ", draft.delayedTexture,
					context.editorContext->assetDatabase, { AssetType::Texture }, setting);
				});
			DrawField(anyItemActive, [&]() {
				return MyGUI::DragFloat("待機時間", draft.delayedWait,
					{ .dragSpeed = 0.01f,.minValue = 0.0f,.maxValue = 60.0f });
				});
			DrawField(anyItemActive, [&]() {
				return MyGUI::DragFloat("遅延時間", draft.delayedDuration,
					{ .dragSpeed = 0.01f,.minValue = 0.0f,.maxValue = 60.0f });
				});
			DrawField(anyItemActive, [&]() { return InspectorDrawerCommon::DrawEnumComboField("遅延イージング", draft.delayedEasing); });
		}
	}
	ImGui::Unindent();
	DrawField(anyItemActive, [&]() { return InspectorDrawerCommon::DrawCheckboxField("非スケール時間", draft.useUnscaledTime); });
}

void Engine::UIProgressInspectorDrawer::ApplyPreview(ECSWorld& world,
	const Entity& entity, const UIProgressComponent& previewComponent) {

	if (!world.IsAlive(entity) || !world.HasComponent<UIProgressComponent>(entity)) {
		return;
	}
	ApplyUIProgressAuthoring(previewComponent, world.GetComponent<UIProgressComponent>(entity));
}
