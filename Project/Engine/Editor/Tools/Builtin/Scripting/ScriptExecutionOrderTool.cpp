#include "ScriptExecutionOrderTool.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Behavior/Registry/BehaviorTypeRegistry.h>
#include <Engine/Core/World/Systems/Behavior/BehaviorSystem.h>
#include <Engine/Core/Scripting/Managed/ScriptExecutionOrderTable.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>

// c++
#include <algorithm>
#include <cctype>
#include <string>

void Engine::ScriptExecutionOrderTool::OpenEditorTool() {

	openWindow_ = true;
}

void Engine::ScriptExecutionOrderTool::DrawEditorTool(const EditorToolContext& context) {

	if (openWindow_) {
		DrawWindow(context);
	}
}

void Engine::ScriptExecutionOrderTool::DrawWindow([[maybe_unused]] const EditorToolContext& context) {

	// このツールはprojectレベル設定のregistryとtable singletonだけを扱うためcontextは未使用

	if (!ImGui::Begin("Script Execution Order", &openWindow_)) {
		ImGui::End();
		return;
	}

	ImGui::TextWrapped("Script Type ごとの実行順。値が小さいほど先に実行されます（同値は安定キーで決定的に解決）。");
	ImGui::TextDisabled("identity は Stable Script Type GUID。型名変更・移動では維持されます。");
	ImGui::Separator();

	ScriptExecutionOrderTable& table = ScriptExecutionOrderTable::GetInstance();
	table.EnsureLoaded();
	BehaviorTypeRegistry& registry = BehaviorTypeRegistry::GetInstance();

	ImGui::SetNextItemWidth(220.0f);
	ImGui::InputTextWithHint("##ExecOrderFilter", "型名で検索", searchBuffer_, sizeof(searchBuffer_));
	ImGui::SameLine();
	if (ImGui::Button("保存")) {
		if (table.Save()) {
			// 保存後にtableを読み直し、次のsync境界でparticipantを再ソートさせる
			BehaviorSystem::InvalidateExecutionOrder();
			dirty_ = false;
		}
	}
	ImGui::SameLine();
	if (ImGui::Button("再読込")) {
		BehaviorSystem::InvalidateExecutionOrder();
		dirty_ = false;
	}
	if (dirty_) {
		ImGui::SameLine();
		ImGui::TextColored(ImVec4(0.95f, 0.80f, 0.25f, 1.0f), "未保存の変更があります");
	}

	const ImGuiTableFlags tableFlags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg
		| ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp;
	if (ImGui::BeginTable("##ExecOrderTable", 5, tableFlags)) {

		ImGui::TableSetupColumn("Script Type", ImGuiTableColumnFlags_WidthStretch);
		ImGui::TableSetupColumn("Default", ImGuiTableColumnFlags_WidthFixed, 60.0f);
		ImGui::TableSetupColumn("Override", ImGuiTableColumnFlags_WidthFixed, 110.0f);
		ImGui::TableSetupColumn("Effective", ImGuiTableColumnFlags_WidthFixed, 70.0f);
		ImGui::TableSetupColumn("操作", ImGuiTableColumnFlags_WidthFixed, 110.0f);
		ImGui::TableHeadersRow();

		const uint32_t count = registry.GetBehaviorTypeCount();
		for (uint32_t i = 0; i < count; ++i) {

			const BehaviorTypeInfo& info = registry.GetInfo(i);
			// managed scriptかつStable GUIDを持つ型のみ対象でnative behaviorは対象外
			if (!info.managed || info.scriptTypeID.empty()) {
				continue;
			}
			const std::string& label = !info.displayName.empty() ? info.displayName : info.name;
			if (!Algorithm::ContainsCaseInsensitive(label, searchBuffer_)) {
				continue;
			}

			// defaultはDefaultExecutionOrder属性でregistryに流れている、overrideはtableが持つ
			// precedenceはoverrideがdefaultより優先でdefaultは0が既定、effectiveはoverride優先で算出する
			const int32_t defaultOrder = info.defaultExecutionOrder;
			int32_t overrideValue = 0;
			const bool hasOverride = table.TryGetOverride(info.scriptTypeID, overrideValue);
			const int32_t effective = hasOverride ? overrideValue : defaultOrder;

			ImGui::TableNextRow();
			ImGui::PushID(static_cast<int>(i));

			ImGui::TableSetColumnIndex(0);
			ImGui::TextUnformatted(label.c_str());
			if (ImGui::IsItemHovered()) {
				ImGui::SetTooltip("%s", info.scriptTypeID.c_str());
			}

			ImGui::TableSetColumnIndex(1);
			ImGui::Text("%d", defaultOrder);

			ImGui::TableSetColumnIndex(2);
			// override編集用、未設定時はdefaultを初期値として表示し編集された時のみoverrideを作る
			int editValue = hasOverride ? overrideValue : defaultOrder;
			ImGui::SetNextItemWidth(-FLT_MIN);
			if (ImGui::InputInt("##order", &editValue)) {
				table.SetOrder(info.scriptTypeID, label, editValue);
				dirty_ = true;
			}
			if (!hasOverride) {
				ImGui::SameLine();
				ImGui::TextDisabled("(none)");
			}

			ImGui::TableSetColumnIndex(3);
			ImGui::Text("%d", effective);

			ImGui::TableSetColumnIndex(4);
			// Resetは値0の書き込みではなくoverride entryの削除
			ImGui::BeginDisabled(!hasOverride);
			if (ImGui::SmallButton("Override削除")) {
				table.Remove(info.scriptTypeID);
				dirty_ = true;
			}
			ImGui::EndDisabled();

			ImGui::PopID();
		}
		ImGui::EndTable();
	}

	ImGui::End();
}
