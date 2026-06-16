#include "ScriptExceptionListTool.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Tools/Core/EditorToolContext.h>
#include <Engine/Editor/UI/Panels/Core/EditorPanelContext.h>
#include <Engine/Editor/Core/EditorContext.h>
#include <Engine/Editor/Core/EditorState.h>
#include <Engine/Editor/Scripting/ManagedIdeLauncher.h>
#include <Engine/Core/Scripting/Managed/Diagnostics/ManagedScriptExceptionStore.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>

// imgui
#include <imgui.h>

// c++
#include <algorithm>
#include <cctype>
#include <string>

namespace {

	// 完全修飾型名から表示用の短い名前を取り出す、識別には使わない
	std::string ShortTypeName(const std::string& fullName) {
		const size_t dot = fullName.find_last_of('.');
		return dot == std::string::npos ? fullName : fullName.substr(dot + 1);
	}
}

void Engine::ScriptExceptionListTool::OpenEditorTool() {

	openWindow_ = true;
}

void Engine::ScriptExceptionListTool::DrawEditorTool(const EditorToolContext& context) {

	if (openWindow_) {
		DrawWindow(context);
	}
}

void Engine::ScriptExceptionListTool::DrawWindow(const EditorToolContext& context) {

	if (!ImGui::Begin("Script Exception List", &openWindow_)) {
		ImGui::End();
		return;
	}

	ManagedScriptExceptionStore& store = ManagedScriptExceptionStore::GetInstance();

	ImGui::TextWrapped("Script callback で送出された未処理の例外。該当 instance のみ faulted 化され engine 全体は停止しません。");
	ImGui::TextDisabled("identity は scriptTypeId の Stable GUID。表示名は識別には使いません。");

	//--------- filter + actions ---------------------------------------------
	ImGui::SetNextItemWidth(200.0f);
	ImGui::InputTextWithHint("##excFilter", "type/message で検索", textFilter_, sizeof(textFilter_));
	ImGui::SameLine();
	if (ImGui::Button("Clear")) {
		store.Clear();
	}
	ImGui::SameLine();
	ImGui::TextDisabled("count: %llu (bounded %llu)",
		static_cast<unsigned long long>(store.Count()),
		static_cast<unsigned long long>(ManagedScriptExceptionStore::kMaxEntries));
	ImGui::Separator();

	// 選択やjump先の解決元、例外はPlay中のPlayWorldで発生するためactiveWorldを見る
	ECSWorld* world = context.GetWorld();
	EditorState* editorState = context.panelContext ? context.panelContext->editorState : nullptr;

	const ImGuiTableFlags tableFlags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg
		| ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable;
	if (ImGui::BeginTable("##excTable", 5, tableFlags)) {

		ImGui::TableSetupColumn("Time", ImGuiTableColumnFlags_WidthFixed, 70.0f);
		ImGui::TableSetupColumn("Callback", ImGuiTableColumnFlags_WidthFixed, 90.0f);
		ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 140.0f);
		ImGui::TableSetupColumn("Entity", ImGuiTableColumnFlags_WidthFixed, 120.0f);
		ImGui::TableSetupColumn("Exception", ImGuiTableColumnFlags_WidthStretch);
		ImGui::TableHeadersRow();

		int rowId = 0;
		for (const ManagedScriptException& exc : store.Entries()) {

			const std::string shortType = ShortTypeName(exc.typeName);
			if (!Algorithm::ContainsCaseInsensitive(shortType, textFilter_)
				&& !Algorithm::ContainsCaseInsensitive(exc.message, textFilter_)
				&& !Algorithm::ContainsCaseInsensitive(exc.exceptionType, textFilter_)) {
				continue;
			}

			ImGui::TableNextRow();
			ImGui::PushID(rowId++);

			ImGui::TableSetColumnIndex(0);
			ImGui::TextUnformatted(exc.timestamp.c_str());

			ImGui::TableSetColumnIndex(1);
			ImGui::TextUnformatted(exc.callback.c_str());

			ImGui::TableSetColumnIndex(2);
			ImGui::TextUnformatted(shortType.c_str());
			if (ImGui::IsItemHovered()) {
				ImGui::SetTooltip("%s\nscriptTypeId: %s\nslot: %llu",
					exc.typeName.c_str(),
					exc.scriptTypeId.empty() ? "(unresolved)" : exc.scriptTypeId.c_str(),
					static_cast<unsigned long long>(exc.scriptSlotId));
			}

			ImGui::TableSetColumnIndex(3);
			// entity handleをEntityに復元し、生存していればclickで選択する
			const Entity owner{ exc.entityIndex, exc.entityGeneration };
			const bool ownerAlive = world && owner.IsValid() && world->IsAlive(owner);
			const std::string entityLabel = exc.entityName.empty()
				? ("#" + std::to_string(exc.entityIndex) + ":" + std::to_string(exc.entityGeneration))
				: exc.entityName;
			ImGui::BeginDisabled(!ownerAlive || !editorState);
			if (ImGui::SmallButton(entityLabel.c_str())) {
				if (editorState && ownerAlive) {
					editorState->SelectEntity(owner);
				}
			}
			ImGui::EndDisabled();
			if (ImGui::IsItemHovered()) {
				ImGui::SetTooltip("%s", ownerAlive ? "クリックで該当 entity を選択" : "entity は既に破棄済み");
			}

			ImGui::TableSetColumnIndex(4);
			// 例外型とメッセージを表示し、展開したstack frameのframeをdouble-clickでIDE jumpする
			const std::string header = exc.exceptionType + ": " + exc.message;
			if (ImGui::TreeNodeEx("##exc", ImGuiTreeNodeFlags_SpanAvailWidth, "%s", header.c_str())) {

				if (exc.frames.empty()) {
					ImGui::TextDisabled("stack frame 情報はありません。");
				}
				for (size_t f = 0; f < exc.frames.size(); ++f) {

					const ManagedScriptExceptionFrame& frame = exc.frames[f];
					ImGui::PushID(static_cast<int>(f));
					// fileとlineがあるframeだけjump可能にし、generatedや外部frameは表示のみ
					const bool jumpable = !frame.file.empty();
					std::string frameLabel = frame.method;
					if (jumpable) {
						frameLabel += "  " + frame.file + ":" + std::to_string(frame.line);
					}
					ImGui::BeginDisabled(!jumpable);
					if (ImGui::Selectable(frameLabel.c_str(), false, ImGuiSelectableFlags_AllowDoubleClick)) {
						if (jumpable && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
							ManagedIdeLauncher::OpenFile(frame.file, frame.line, frame.column > 0 ? frame.column : 1);
						}
					}
					ImGui::EndDisabled();
					ImGui::PopID();
				}
				ImGui::TreePop();
			}

			ImGui::PopID();
		}
		ImGui::EndTable();
	}

	ImGui::End();
}
