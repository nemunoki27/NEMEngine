#include "CanvasNavigationEditing.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Panels/Core/IEditorPanel.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/World/Components/Scene/NameComponent.h>
#include <Engine/Core/World/Components/Transform/HierarchyComponent.h>
#include <Engine/Core/World/Scene/Utility/SceneObjectUtility.h>
#include <Engine/Editor/UI/ImGui/ImGuiHelpers.h>

// c++
#include <algorithm>
#include <string>

//============================================================================
//	CanvasNavigationEditing internal
//============================================================================

namespace {

	constexpr const char* kNavigationCellDragDropType = "NEM_UI_NAV_CELL";

	Engine::UUID GetLocalFileID(Engine::ECSWorld& world, Engine::Entity entity) {

		const auto* sceneObject = world.TryGetComponent<Engine::SceneObjectComponent>(entity);
		return sceneObject ? sceneObject->localFileID : Engine::UUID{};
	}

	void CollectCanvasSelectables(
		Engine::ECSWorld& world, Engine::Entity canvas, Engine::Entity parent, std::vector<Engine::Entity>& selectables) {

		const auto* hierarchy = world.TryGetComponent<Engine::HierarchyComponent>(parent);
		Engine::Entity child = hierarchy ? hierarchy->firstChild : Engine::Entity::Null();
		// 子Canvasを除いて選択先を集める
		while (world.IsAlive(child)) {

			const auto& childHierarchy = world.GetComponent<Engine::HierarchyComponent>(child);
			const Engine::Entity next = childHierarchy.nextSibling;
			if (!world.HasComponent<Engine::CanvasComponent>(child)) {
				if (Engine::IsCanvasNavigationTarget(world, canvas, child)) {
					selectables.emplace_back(child);
				}
				CollectCanvasSelectables(world, canvas, child, selectables);
			}
			child = next;
		}
	}

	std::string GetNavigationCellLabel(Engine::ECSWorld& world, Engine::Entity canvas, Engine::UUID localFileID) {

		if (!localFileID) {
			return "(Empty)";
		}
		Engine::Entity entity = Engine::Entity::Null();
		const auto* sceneObject = world.TryGetComponent<Engine::SceneObjectComponent>(canvas);
		if (sceneObject) {
			entity = Engine::SceneObjectUtility::FindByLocalFileID(world, sceneObject->sceneInstanceID, localFileID);
		}
		if (!world.IsAlive(entity)) {
			return "Missing Entity";
		}
		const auto* name = world.TryGetComponent<Engine::NameComponent>(entity);
		return name ? name->name : "Entity";
	}

	Engine::ValueEditResult DrawNavigationCell(Engine::ECSWorld& world, Engine::Entity canvas,
		Engine::CanvasNavigationTable& table, size_t index, const std::vector<Engine::Entity>& selectables,
		Engine::UUID selectedLocalFileID) {

		Engine::ValueEditResult result{};
		Engine::UUID& cell = table.cells[index];
		const std::string cellLabel = GetNavigationCellLabel(world, canvas, cell);
		const std::string label = cellLabel + "##cell";
		const float buttonWidth = (std::max)(1.0f, ImGui::GetContentRegionAvail().x);

		ImGui::PushID(static_cast<int32_t>(index));
		const bool selected = cell && cell == selectedLocalFileID;
		if (selected) {
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 0.0f, 1.0f));
		}
		ImGui::Button(label.c_str(), ImVec2(buttonWidth, ImGui::GetFrameHeight()));
		if (selected) {
			ImGui::PopStyleColor();
		}
		result.anyItemActive = ImGui::IsItemActive();
		if (ImGui::IsItemHovered() &&
			buttonWidth < ImGui::CalcTextSize(cellLabel.c_str()).x + ImGui::GetStyle().FramePadding.x * 2.0f) {
			ImGui::SetTooltip("%s", cellLabel.c_str());
		}

		if (cell && ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
			// セル番号を渡して配置を入れ替える
			const int32_t sourceIndex = static_cast<int32_t>(index);
			ImGui::SetDragDropPayload(kNavigationCellDragDropType, &sourceIndex, sizeof(sourceIndex));
			ImGui::TextUnformatted(GetNavigationCellLabel(world, canvas, cell).c_str());
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
			if (const ImGuiPayload* payload =
					ImGui::AcceptDragDropPayload(Engine::IEditorPanel::kHierarchyDragDropPayloadType)) {
				if (payload->IsDelivery() && payload->DataSize == sizeof(Engine::UUID)) {
					const Engine::UUID stableUUID = *static_cast<const Engine::UUID*>(payload->Data);
					const Engine::Entity target = world.FindByUUID(stableUUID);
					if (Engine::IsCanvasNavigationTarget(world, canvas, target)) {
						Engine::SetCanvasNavigationCell(table, index, GetLocalFileID(world, target));
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
			for (Engine::Entity selectable : selectables) {

				const Engine::UUID localFileID = GetLocalFileID(world, selectable);
				const std::string buttonLabel = GetNavigationCellLabel(world, canvas, localFileID);
				ImGui::PushID(Engine::ToString(localFileID).c_str());
				if (ImGui::MenuItem(buttonLabel.c_str(), nullptr, cell == localFileID)) {
					Engine::SetCanvasNavigationCell(table, index, localFileID);
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

}

//============================================================================
//	CanvasNavigationEditing functions
//============================================================================

Engine::ValueEditResult Engine::CanvasNavigationEditing::DrawEntityReference(
	const char* label, Engine::ECSWorld& world, Engine::UUID& localFileID) {

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
		if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(Engine::IEditorPanel::kHierarchyDragDropPayloadType)) {
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

Engine::ValueEditResult Engine::CanvasNavigationEditing::DrawNavigationTable(
	Engine::ECSWorld& world, Engine::Entity canvas, Engine::CanvasNavigationTable& table) {

	Engine::ValueEditResult result{};
	Engine::ResizeCanvasNavigationTable(table, table.rows, table.columns);

	std::vector<Engine::Entity> selectables;
	CollectCanvasSelectables(world, canvas, canvas, selectables);
	Engine::UUID selectedLocalFileID{};
	if (const auto* runtime = world.TryGetComponent<Engine::CanvasRuntimeComponent>(canvas)) {
		selectedLocalFileID = runtime->selectedLocalFileID;
	}

	if (ImGui::Button("配下のUIを自動配置")) {
		std::fill(table.cells.begin(), table.cells.end(), Engine::UUID{});
		for (size_t i = 0; i < selectables.size() && i < table.cells.size(); ++i) {
			table.cells[i] = GetLocalFileID(world, selectables[i]);
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

	const ImGuiTableFlags flags =
		ImGuiTableFlags_Borders | ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_ScrollX | ImGuiTableFlags_ScrollY;
	const int32_t visibleRows = (std::min)(table.rows, 8);
	const ImGuiStyle& style = ImGui::GetStyle();
	const float availableWidth = ImGui::GetContentRegionAvail().x - (table.rows > visibleRows ? style.ScrollbarSize : 0.0f);
	const bool hasHorizontalScrollbar = availableWidth < static_cast<float>(table.columns) * 120.0f;
	const float height = ImGui::GetFrameHeightWithSpacing() * static_cast<float>(visibleRows) +
						 (hasHorizontalScrollbar ? style.ScrollbarSize : 0.0f);
	if (ImGui::BeginTable("##CanvasNavigationTable", table.columns, flags, ImVec2(0.0f, height))) {
		for (int32_t column = 0; column < table.columns; ++column) {
			ImGui::TableSetupColumn(nullptr, ImGuiTableColumnFlags_WidthFixed, 120.0f);
		}
		for (int32_t row = 0; row < table.rows; ++row) {

			ImGui::TableNextRow();
			for (int32_t column = 0; column < table.columns; ++column) {

				ImGui::TableSetColumnIndex(column);
				const size_t index =
					static_cast<size_t>(row) * static_cast<size_t>(table.columns) + static_cast<size_t>(column);
				const Engine::ValueEditResult cellResult =
					DrawNavigationCell(world, canvas, table, index, selectables, selectedLocalFileID);
				result.valueChanged |= cellResult.valueChanged;
				result.anyItemActive |= cellResult.anyItemActive;
				result.editFinished |= cellResult.editFinished;
			}
		}
		ImGui::EndTable();
	}
	return result;
}
