#include "ScriptProfilerTool.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Tools/Core/EditorToolContext.h>
#include <Engine/Editor/UI/Panels/Core/EditorPanelContext.h>
#include <Engine/Editor/Core/EditorContext.h>
#include <Engine/Editor/Core/EditorState.h>
#include <Engine/Core/Scripting/Managed/Diagnostics/ManagedScriptProfilerStore.h>
#include <Engine/Core/World/Behavior/Registry/BehaviorTypeRegistry.h>

// imgui
#include <imgui.h>

// c++
#include <algorithm>
#include <string>
#include <vector>

namespace {

	// runtime typeIDから表示名を解決する、無ければidを出し識別自体はscriptTypeIdが正
	std::string ResolveTypeLabel(uint32_t typeID) {

		Engine::BehaviorTypeRegistry& registry = Engine::BehaviorTypeRegistry::GetInstance();
		if (typeID >= registry.GetBehaviorTypeCount()) {
			return "type#" + std::to_string(typeID);
		}
		const Engine::BehaviorTypeInfo& info = registry.GetInfo(typeID);
		if (!info.displayName.empty()) {
			return info.displayName;
		}
		if (!info.name.empty()) {
			return info.name;
		}
		return "type#" + std::to_string(typeID);
	}
}

void Engine::ScriptProfilerTool::OpenEditorTool() {

	openWindow_ = true;
}

void Engine::ScriptProfilerTool::DrawEditorTool(const EditorToolContext& context) {

	if (openWindow_) {
		DrawWindow(context);
	}
}

void Engine::ScriptProfilerTool::DrawWindow(const EditorToolContext& context) {

	if (!ImGui::Begin("Script Profiler", &openWindow_)) {
		ImGui::End();
		return;
	}

	ManagedScriptProfilerStore& store = ManagedScriptProfilerStore::GetInstance();

	// detail無効構成ではpolicyを明示しaggregateはFrameProfiler側で見る旨を案内する
	// kDetailEnabledはconstexprだがunreachable code警告を避けるため通常ifで分岐する
	if (!ManagedScriptProfilerStore::kDetailEnabled) {
		ImGui::TextWrapped("このビルド構成では script detail profiler は無効です（Release policy）。");
		ImGui::TextDisabled("aggregate な Script 計測は FrameProfiler::Category::Script を参照してください。");
		ImGui::End();
		return;
	}

	ImGui::Text("coroutine resume(累積): %.3f ms | profiler overhead(累積): %.3f ms",
		store.CoroutineResumeMs(), store.ProfilerOverheadMs());
	ImGui::Text("detail entries: %zu / %zu%s",
		store.EntryCount(), ManagedScriptProfilerStore::kMaxEntries, store.IsCapped() ? "  (capped)" : "");

	if (ImGui::Button("Reset")) {
		store.Reset();
	}
	ImGui::SameLine();
	ImGui::SetNextItemWidth(120.0f);
	ImGui::SliderInt("Top N", &topN_, 1, 100);
	ImGui::SameLine();
	ImGui::SetNextItemWidth(120.0f);
	ImGui::Combo("Sort", &sortMode_, "Total ms\0Max ms\0Call count\0");
	ImGui::Checkbox("選択 entity のみ", &onlySelectedEntity_);

	// 選択entityをfilterに使う、Play中PlayWorldのentity indexで突き合わせる
	const EditorState* editorState = context.panelContext ? context.panelContext->editorState : nullptr;
	const bool hasSelection = editorState && editorState->selectedEntity.IsValid();
	const uint32_t selectedIndex = hasSelection ? editorState->selectedEntity.index : 0xFFFFFFFF;
	if (onlySelectedEntity_ && !hasSelection) {
		ImGui::SameLine();
		ImGui::TextDisabled("(entity 未選択)");
	}
	ImGui::Separator();

	// storeを破壊しないようローカルへコピーしてソートする、上限512件で低コスト
	std::vector<ManagedScriptProfileEntry> rows;
	rows.reserve(store.Entries().size());
	for (const ManagedScriptProfileEntry& entry : store.Entries()) {
		if (onlySelectedEntity_ && entry.entityIndex != selectedIndex) {
			continue;
		}
		rows.push_back(entry);
	}

	std::sort(rows.begin(), rows.end(), [this](const ManagedScriptProfileEntry& a, const ManagedScriptProfileEntry& b) {
		switch (sortMode_) {
		case 1:  return a.maxMs > b.maxMs;
		case 2:  return a.callCount > b.callCount;
		default: return a.totalMs > b.totalMs;
		}
		});

	const int limit = std::min<int>(topN_, static_cast<int>(rows.size()));

	const ImGuiTableFlags tableFlags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg
		| ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable;
	if (ImGui::BeginTable("##profTable", 8, tableFlags)) {

		ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthStretch);
		ImGui::TableSetupColumn("Entity", ImGuiTableColumnFlags_WidthFixed, 60.0f);
		ImGui::TableSetupColumn("Slot", ImGuiTableColumnFlags_WidthFixed, 45.0f);
		ImGui::TableSetupColumn("Callback", ImGuiTableColumnFlags_WidthFixed, 85.0f);
		ImGui::TableSetupColumn("Calls", ImGuiTableColumnFlags_WidthFixed, 70.0f);
		ImGui::TableSetupColumn("Total ms", ImGuiTableColumnFlags_WidthFixed, 75.0f);
		ImGui::TableSetupColumn("Avg ms", ImGuiTableColumnFlags_WidthFixed, 75.0f);
		ImGui::TableSetupColumn("Max ms / Exc", ImGuiTableColumnFlags_WidthFixed, 90.0f);
		ImGui::TableHeadersRow();

		for (int i = 0; i < limit; ++i) {

			const ManagedScriptProfileEntry& entry = rows[i];
			const double avg = entry.callCount > 0 ? entry.totalMs / static_cast<double>(entry.callCount) : 0.0;

			ImGui::TableNextRow();

			ImGui::TableSetColumnIndex(0);
			ImGui::TextUnformatted(ResolveTypeLabel(entry.typeID).c_str());

			ImGui::TableSetColumnIndex(1);
			ImGui::Text("#%u", entry.entityIndex);

			ImGui::TableSetColumnIndex(2);
			ImGui::Text("%d", entry.slot);

			ImGui::TableSetColumnIndex(3);
			ImGui::TextUnformatted(ToString(entry.callback));

			ImGui::TableSetColumnIndex(4);
			ImGui::Text("%llu", static_cast<unsigned long long>(entry.callCount));

			ImGui::TableSetColumnIndex(5);
			ImGui::Text("%.3f", entry.totalMs);

			ImGui::TableSetColumnIndex(6);
			ImGui::Text("%.4f", avg);

			ImGui::TableSetColumnIndex(7);
			if (entry.exceptionCount > 0) {
				ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.45f, 1.0f), "%.3f / %llu",
					entry.maxMs, static_cast<unsigned long long>(entry.exceptionCount));
			}
			else {
				ImGui::Text("%.3f / 0", entry.maxMs);
			}
		}
		ImGui::EndTable();
	}

	ImGui::End();
}
