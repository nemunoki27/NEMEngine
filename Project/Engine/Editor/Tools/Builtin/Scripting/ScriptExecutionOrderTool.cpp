#include "ScriptExecutionOrderTool.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Scripting/Managed/ScriptExecutionOrderSettings.h>
#include <Engine/Core/World/Behavior/Registry/BehaviorTypeRegistry.h>

// c++
#include <algorithm>
#include <vector>

void Engine::ScriptExecutionOrderTool::OpenEditorTool() {

	// 実行順の編集画面を開く
	openWindow_ = true;
}

void Engine::ScriptExecutionOrderTool::DrawEditorTool(const EditorToolContext& context) {

	if (openWindow_) {
		DrawWindow(context);
	}
}

void Engine::ScriptExecutionOrderTool::DrawWindow(const EditorToolContext& context) {

	if (!ImGui::Begin("スクリプト実行順", &openWindow_)) {
		ImGui::End();
		return;
	}

	// 保存と再読込は停止中だけ受け付ける
	BehaviorTypeRegistry& registry = BehaviorTypeRegistry::GetInstance();
	ImGui::BeginDisabled(context.IsPlaying());
	if (ImGui::Button("保存")) {
		if (ScriptExecutionOrderSettings::Save()) {
			dirty_ = false;
		}
	}
	ImGui::SameLine();
	if (ImGui::Button("再読込")) {
		ScriptExecutionOrderSettings::Reload();
		registry.RefreshManagedExecutionOrders();
		dirty_ = false;
	}
	if (dirty_) {
		ImGui::SameLine();
		ImGui::TextColored(ImVec4(0.95f, 0.80f, 0.25f, 1.0f), "未保存の変更があります");
	}
	ImGui::EndDisabled();

	if (context.IsPlaying()) {
		ImGui::TextDisabled("実行順はシーン停止中に編集できます");
	}
	ImGui::SetNextItemWidth(280.0f);
	searchFilter_.DrawInput("##ScriptExecutionOrderSearch");
	ImGui::Separator();

	// 検索条件に一致するManaged型を収集
	std::vector<const BehaviorTypeInfo*> scripts;
	for (uint32_t index = 0; index < registry.GetBehaviorTypeCount(); ++index) {

		const BehaviorTypeInfo& info = registry.GetInfo(index);
		if (!info.managed || info.scriptTypeID.empty()) {
			continue;
		}
		if (!searchFilter_.Matches(info.displayName) &&
			!searchFilter_.Matches(info.name) && !searchFilter_.Matches(info.sourcePath)) {

			continue;
		}
		scripts.push_back(&info);
	}
	// 実行順と表示名で並べる
	std::sort(scripts.begin(), scripts.end(),
		[](const BehaviorTypeInfo* lhs, const BehaviorTypeInfo* rhs) {

			if (lhs->executionOrder != rhs->executionOrder) {
				return lhs->executionOrder < rhs->executionOrder;
			}
			return lhs->displayName < rhs->displayName;
		});

	if (scripts.empty()) {
		ImGui::TextDisabled("登録済みのC#スクリプトはありません");
		ImGui::End();
		return;
	}

	// 既定値と編集用の実行順を表示
	const ImGuiTableFlags tableFlags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
		ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY;
	if (ImGui::BeginTable("##ScriptExecutionOrderTable", 4, tableFlags)) {

		ImGui::TableSetupColumn("スクリプト", ImGuiTableColumnFlags_WidthStretch);
		ImGui::TableSetupColumn("既定値", ImGuiTableColumnFlags_WidthFixed, 80.0f);
		ImGui::TableSetupColumn("実行順", ImGuiTableColumnFlags_WidthFixed, 120.0f);
		ImGui::TableSetupColumn("操作", ImGuiTableColumnFlags_WidthFixed, 100.0f);
		ImGui::TableHeadersRow();

		for (const BehaviorTypeInfo* info : scripts) {

			ImGui::PushID(info->scriptTypeID.c_str());
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::TextUnformatted(info->displayName.c_str());
			if (ImGui::IsItemHovered() && !info->sourcePath.empty()) {
				ImGui::SetTooltip("%s", info->sourcePath.c_str());
			}

			ImGui::TableSetColumnIndex(1);
			ImGui::Text("%d", info->defaultExecutionOrder);

			ImGui::TableSetColumnIndex(2);
			// 変更した実行順を登録済み型へ反映
			int32_t executionOrder = info->executionOrder;
			ImGui::SetNextItemWidth(-FLT_MIN);
			ImGui::BeginDisabled(context.IsPlaying());
			if (ImGui::DragInt("##ExecutionOrder", &executionOrder, 1.0f, -32000, 32000)) {
				if (ScriptExecutionOrderSettings::SetOverride(info->scriptTypeID, executionOrder)) {
					registry.RefreshManagedExecutionOrders();
					dirty_ = true;
				}
			}
			ImGui::EndDisabled();

			ImGui::TableSetColumnIndex(3);
			// 上書きを解除して既定の実行順へ戻す
			const bool hasOverride =
				ScriptExecutionOrderSettings::HasOverride(info->scriptTypeID);
			ImGui::BeginDisabled(context.IsPlaying() || !hasOverride);
			if (ImGui::SmallButton("既定値に戻す")) {
				if (ScriptExecutionOrderSettings::RemoveOverride(info->scriptTypeID)) {
					registry.RefreshManagedExecutionOrders();
					dirty_ = true;
				}
			}
			ImGui::EndDisabled();
			ImGui::PopID();
		}
		ImGui::EndTable();
	}

	ImGui::End();
}
