#include "ImGuiHelpers.h"
#include "ImGuiHelpersInternal.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Editor/UI/Panels/Core/IEditorPanel.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/World/Components/Scene/NameComponent.h>
#include <Engine/Core/World/Components/Transform/TransformComponent.h>
#include <Engine/Core/World/Components/Rendering/MeshRendererComponent.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/Textures/TextureUploadService.h>

// c++
#include <algorithm>
#include <unordered_map>
#include <utility>
#include <vector>

//============================================================================
//	MyGUI classMethods
//============================================================================
namespace {

	//============================================================================
	//	レイアウト定数
	//============================================================================
	// 左側に表示する文字の幅
	constexpr float kLabelColumnWidth = 168.0f;

	// プロパティグループのラベル幅
	struct PropertyLabelWidthState {

		ImGuiID id = 0;
		float labelWidth = 0.0f;
		float measuredWidth = 0.0f;
	};
	std::vector<PropertyLabelWidthState> propertyLabelWidthStack{};
	std::unordered_map<ImGuiID, float> propertyLabelWidthCache{};
	const std::function<void()>* propertyLabelContextMenu = nullptr;

	// プロパティグループのラベル幅計測を開始する
	void BeginPropertyLabelWidth(const char* id) {

		const ImGuiID scopeID = ImGui::GetID(id);
		const auto it = propertyLabelWidthCache.find(scopeID);
		PropertyLabelWidthState state{};
		state.id = scopeID;
		state.labelWidth = it != propertyLabelWidthCache.end() ? it->second : 0.0f;
		propertyLabelWidthStack.emplace_back(state);
	}

	// プロパティグループのラベル幅計測を終了する
	void EndPropertyLabelWidth() {

		if (propertyLabelWidthStack.empty()) {
			return;
		}
		const PropertyLabelWidthState state = propertyLabelWidthStack.back();
		propertyLabelWidthStack.pop_back();
		propertyLabelWidthCache[state.id] = state.measuredWidth;
	}

}

Engine::MyGUI::ScopedPropertyLabelWidth::ScopedPropertyLabelWidth(const char* id) {

	BeginPropertyLabelWidth(id);
}

Engine::MyGUI::ScopedPropertyLabelWidth::~ScopedPropertyLabelWidth() {

	EndPropertyLabelWidth();
}

Engine::MyGUI::ScopedPropertyLabelContextMenu::ScopedPropertyLabelContextMenu(std::function<void()> callback)
	: callback_(std::move(callback)), previous_(propertyLabelContextMenu) {

	propertyLabelContextMenu = &callback_;
}

Engine::MyGUI::ScopedPropertyLabelContextMenu::~ScopedPropertyLabelContextMenu() {

	propertyLabelContextMenu = previous_;
}

bool Engine::MyGUI::CollapsingHeader(const char* label, bool stratOpen) {

	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6.0f, 0.0f));
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 4.0f));
	ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 2.0f);

	ImGuiTreeNodeFlags flags = {};
	if (stratOpen) {

		flags |= ImGuiTreeNodeFlags_DefaultOpen;
	}
	bool open = ImGui::CollapsingHeader(label, flags);

	ImGui::PopStyleVar(3);

	return open;
}

Engine::TextInputPopupResult Engine::MyGUI::InputTextPopupContent(const char* label, std::string& text, const char* errorText) {

	TextInputPopupResult result{};

	ImGui::TextUnformatted(label);
	ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);

	const bool submittedByEnter = ImGui::InputText("##InputTextPopupValue", &text, ImGuiInputTextFlags_EnterReturnsTrue);
	if (ImGui::IsWindowAppearing()) {
		ImGui::SetKeyboardFocusHere(-1);
	}

	if (errorText && errorText[0] != '\0') {
		ImGui::TextColored(ImVec4(0.95f, 0.32f, 0.24f, 1.0f), "%s", errorText);
	} else {
		ImGui::Spacing();
	}

	ImGui::Separator();

	if (ImGui::Button("OK", ImVec2(96.0f, 0.0f)) || submittedByEnter) {
		result.submitted = true;
	}
	ImGui::SameLine();
	if (ImGui::Button("Cancel", ImVec2(96.0f, 0.0f)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
		result.canceled = true;
	}
	return result;
}

bool Engine::MyGUI::BeginPropertyRow(const char* label, const PropertyRowSetting& setting) {

	float labelWidth = kLabelColumnWidth;
	if (setting.labelWidth.has_value()) {
		labelWidth = setting.labelWidth.value();
	} else if (!propertyLabelWidthStack.empty()) {

		PropertyLabelWidthState& state = propertyLabelWidthStack.back();
		const float measuredWidth = ImGui::CalcTextSize(label).x + 4.0f;
		state.measuredWidth = (std::max)(state.measuredWidth, measuredWidth);
		labelWidth = (std::max)(state.labelWidth, measuredWidth);
	}

	const std::string tableID = std::string("##MyGUI_RowTable_Public_") + label;
	const ImVec2 tableSize = setting.rowWidth.has_value() ? ImVec2(setting.rowWidth.value(), 0.0f) : ImVec2(0.0f, 0.0f);
	if (!ImGui::BeginTable(tableID.c_str(), 2,
			ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_NoSavedSettings, tableSize)) {
		return false;
	}

	ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, labelWidth);
	ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);

	ImGui::TableNextRow();

	ImGui::TableSetColumnIndex(0);
	ImGui::AlignTextToFramePadding();
	ImGui::TextUnformatted(label);
	if (propertyLabelContextMenu && *propertyLabelContextMenu) {

		ImGui::PushID(label);
		if (ImGui::BeginPopupContextItem("##PropertyLabelContextMenu")) {
			(*propertyLabelContextMenu)();
			ImGui::EndPopup();
		}
		ImGui::PopID();
	}

	ImGui::TableSetColumnIndex(1);
	ImGui::PushID(label);
	return true;
}

void Engine::MyGUI::EndPropertyRow() {

	ImGui::PopID();
	ImGui::EndTable();
}

Engine::ValueEditResult Engine::MyGUI::DragInt(const char* label, int32_t& value, const IntEditSetting& setting) {

	ValueEditResult result{};

	if (!BeginPropertyRow(label, setting.propertyRow)) {
		return result;
	}

	int32_t v = value;
	result.valueChanged = ImGui::DragInt("##Value", &v, setting.dragSpeed, setting.minValue, setting.maxValue);
	result.anyItemActive = ImGui::IsItemActive();
	result.editFinished = ImGui::IsItemDeactivatedAfterEdit();

	if (result.valueChanged) {
		value = v;
	}

	EndPropertyRow();
	return result;
}

Engine::ValueEditResult Engine::MyGUI::ColorEdit(const char* label, Color3& value, ImGuiColorEditFlags flags) {

	ValueEditResult result{};

	if (!BeginPropertyRow(label)) {
		return result;
	}

	float color[3] = {value.r, value.g, value.b};
	result.valueChanged = ImGui::ColorEdit3("##Value", color, flags);
	result.anyItemActive = ImGui::IsItemActive();
	result.editFinished = ImGui::IsItemDeactivatedAfterEdit() || result.valueChanged;

	if (result.valueChanged) {
		value.r = color[0];
		value.g = color[1];
		value.b = color[2];
	}

	EndPropertyRow();
	return result;
}

Engine::ValueEditResult Engine::MyGUI::ColorEdit(const char* label, Color4& value, ImGuiColorEditFlags flags) {

	ValueEditResult result{};

	if (!BeginPropertyRow(label)) {
		return result;
	}

	float color[4] = {value.r, value.g, value.b, value.a};
	result.valueChanged = ImGui::ColorEdit4("##Value", color, flags);
	result.anyItemActive = ImGui::IsItemActive();
	result.editFinished = ImGui::IsItemDeactivatedAfterEdit() || result.valueChanged;

	if (result.valueChanged) {
		value.r = color[0];
		value.g = color[1];
		value.b = color[2];
		value.a = color[3];
	}

	EndPropertyRow();
	return result;
}

bool Engine::MyGUI::Checkbox(const char* label, bool& value, const PropertyRowSetting& setting) {

	if (!BeginPropertyRow(label, setting)) {
		return false;
	}

	bool changed = ImGui::Checkbox("##Value", &value);

	EndPropertyRow();
	return changed;
}

bool Engine::MyGUI::SmallCheckbox(const char* id, bool& value) {

	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2.0f, 1.0f));
	ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 2.0f);

	const bool changed = ImGui::Checkbox(id, &value);

	ImGui::PopStyleVar(2);
	return changed;
}

Engine::ValueEditResult Engine::MyGUI::InputText(const char* label, std::string& text, const TextEditSetting& setting) {

	ValueEditResult result{};

	if (!BeginPropertyRow(label, setting.propertyRow)) {
		return result;
	}

	bool submittedByEnter = false;
	if (setting.multiLine) {

		ImVec2 inputSize = setting.size;
		if (inputSize.x <= 0.0f) {
			inputSize.x = ImGui::GetContentRegionAvail().x;
		}
		if (inputSize.y <= 0.0f) {
			inputSize.y = ImGui::GetFrameHeightWithSpacing() * 4.0f;
		}
		ImGui::InputTextMultiline("##Value", &text, inputSize, setting.flags);
	} else {

		ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
		submittedByEnter = ImGui::InputText("##Value", &text, setting.flags | ImGuiInputTextFlags_EnterReturnsTrue);
	}

	result.valueChanged = ImGui::IsItemEdited();
	result.anyItemActive = ImGui::IsItemActive();
	result.editFinished = submittedByEnter || ImGui::IsItemDeactivatedAfterEdit();

	EndPropertyRow();
	return result;
}

Engine::ValueEditResult Engine::MyGUI::StringCombo(const char* label, std::string& currentValue,
	std::span<const std::string> items, const char* emptyPreview, bool allowEmptySelection, const ComboEditSetting& setting) {

	ValueEditResult result{};

	if (!BeginPropertyRow(label, setting.propertyRow)) {
		return result;
	}

	const char* preview = currentValue.empty() ? emptyPreview : currentValue.c_str();
	if (items.empty()) {

		ImGui::TextDisabled("%s", emptyPreview);
	} else {

		const float width = ImGui::GetContentRegionAvail().x - setting.reserveRightWidth;
		ImGui::SetNextItemWidth((std::max)(1.0f, width));
	}
	if (!items.empty() && ImGui::BeginCombo("##Value", preview)) {
		// 空選択を許可する場合
		if (allowEmptySelection) {
			const bool selected = currentValue.empty();
			if (ImGui::Selectable(emptyPreview, selected)) {
				if (!currentValue.empty()) {
					currentValue.clear();
					result.valueChanged = true;
				}
			}
			if (selected) {
				ImGui::SetItemDefaultFocus();
			}
		}

		int itemIndex = 0;
		for (const std::string& item : items) {

			ImGui::PushID(itemIndex);
			const bool selected = (currentValue == item);
			const char* itemLabel = item.empty() ? "##empty" : item.c_str();
			if (ImGui::Selectable(itemLabel, selected)) {
				if (currentValue != item) {
					currentValue = item;
					result.valueChanged = true;
				}
			}
			if (selected) {
				ImGui::SetItemDefaultFocus();
			}
			ImGui::PopID();
			++itemIndex;
		}
		ImGui::EndCombo();
	}

	result.anyItemActive = ImGui::IsItemActive();
	result.editFinished = result.valueChanged || ImGui::IsItemDeactivatedAfterEdit();

	EndPropertyRow();
	return result;
}
