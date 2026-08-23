#include "ScriptBuildDiagnosticsTool.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Tools/Core/EditorToolContext.h>
#include <Engine/Editor/UI/Panels/Core/EditorPanelContext.h>
#include <Engine/Editor/Core/EditorContext.h>
#include <Engine/Editor/Scripting/ManagedIdeLauncher.h>
#include <Engine/Core/Scripting/Managed/ManagedScriptBuildService.h>
#include <Engine/Core/Scripting/Managed/Diagnostics/ManagedBuildDiagnosticStore.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>

// c++
#include <algorithm>
#include <cctype>
#include <fstream>
#include <string>

namespace {

	using State = Engine::ManagedScriptBuildService::State;

	const char* BuildStateLabel(State state) {
		switch (state) {
		case State::Idle: return "Idle";
		case State::Debouncing: return "Debouncing";
		case State::MetadataSyncing: return "MetadataSyncing";
		case State::Building: return "Building";
		case State::BuildSucceeded: return "BuildSucceeded";
		case State::BuildFailed: return "BuildFailed";
		case State::Staging: return "Staging";
		case State::ReloadPending: return "ReloadPending";
		case State::Reloading: return "Reloading";
		case State::ReloadSucceeded: return "ReloadSucceeded";
		case State::ReloadFailed: return "ReloadFailed";
		case State::FallbackLoading: return "FallbackLoading";
		case State::FallbackSucceeded: return "FallbackSucceeded";
		case State::FallbackFailed: return "FallbackFailed";
		default: return "Unknown";
		}
	}

	const char* ProcessKindLabel(Engine::ManagedBuildProcessKind kind) {
		switch (kind) {
		case Engine::ManagedBuildProcessKind::MetadataSync: return "MetadataSync";
		case Engine::ManagedBuildProcessKind::Build: return "Build";
		case Engine::ManagedBuildProcessKind::ProjectRefresh: return "ProjectRefresh";
		case Engine::ManagedBuildProcessKind::IdeLaunch: return "IdeLaunch";
		default: return "Other";
		}
	}

	const char* AlcStatusLabel(Engine::AlcUnloadStatus status) {
		switch (status) {
		case Engine::AlcUnloadStatus::UnloadSucceeded: return "UnloadSucceeded";
		case Engine::AlcUnloadStatus::LeakSuspected: return "LeakSuspected";
		default: return "Unknown";
		}
	}

	// GameScripts.csprojを無条件再生成せず、必須要素をvalidateして不足をdiagnosticに出す
	void RefreshIdeProject() {

		const std::filesystem::path csproj =
			(Engine::RuntimePaths::GetGameRoot() / Engine::ManagedIdeLauncher::GetSettings().project).lexically_normal();
		std::error_code ec{};
		if (!std::filesystem::exists(csproj, ec)) {
			Engine::Logger::Output(Engine::LogType::Engine, spdlog::level::err,
				"RefreshIdeProject: csprojが見つかりません: {}", csproj.string());
			return;
		}
		std::ifstream file(csproj);
		std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

		struct Check { const char* needle; const char* label; bool required; };
		const Check checks[] = {
			{ "GameAssets", "GameAssets compile glob", true },
			{ ".cs.meta", ".cs.meta AdditionalFiles", true },
			{ "NEM.ScriptCore", "ScriptCore reference", true },
			{ "NEM.ScriptCodeGen", "NEM.ScriptCodeGen analyzer reference", true },
			{ "NEM.ScriptAnalyzers", "NEM.ScriptAnalyzers analyzer reference", true },
		};
		bool allOk = true;
		for (const Check& check : checks) {
			if (text.find(check.needle) == std::string::npos) {
				if (check.required) {
					allOk = false;
				}
				Engine::Logger::Output(Engine::LogType::Engine,
					check.required ? spdlog::level::err : spdlog::level::warn,
					"RefreshIdeProject: {}に{}がありません 種別={}",
					csproj.filename().string(), check.label, check.required ? "必須" : "任意または将来用");
			}
		}
		if (allOk) {
			Engine::Logger::Output(Engine::LogType::Engine, spdlog::level::info,
				"RefreshIdeProject: {}の必須参照を確認しました", csproj.filename().string());
		}
	}
}

void Engine::ScriptBuildDiagnosticsTool::OpenEditorTool() {

	openWindow_ = true;
}

void Engine::ScriptBuildDiagnosticsTool::DrawEditorTool(const EditorToolContext& context) {

	if (openWindow_) {
		DrawWindow(context);
	}
}

void Engine::ScriptBuildDiagnosticsTool::DrawWindow(const EditorToolContext& context) {

	if (!ImGui::Begin("Compiler Error List", &openWindow_)) {
		ImGui::End();
		return;
	}

	// build serviceへの参照、read-only snapshotとrequestのみでLogger internalsは読まない
	ManagedScriptBuildService* service = nullptr;
	if (context.panelContext && context.panelContext->editorContext) {
		service = context.panelContext->editorContext->scriptBuildService;
	}

	//---------上部: build status + actions ---------------------------------
	if (service) {
		const ManagedScriptBuildService::Snapshot snapshot = service->GetSnapshot();
		ImGui::Text("State: %s  | build #%llu  reload #%llu",
			BuildStateLabel(snapshot.state),
			static_cast<unsigned long long>(snapshot.buildID),
			static_cast<unsigned long long>(snapshot.reloadID));
		ImGui::Text("pending changes: %s | reload deferred(Play): %s | LKG usable: %s | LKG update failed: %s",
			snapshot.hasPendingSourceChanges ? "yes" : "no",
			snapshot.reloadDeferredByPlayMode ? "yes" : "no",
			snapshot.hasUsableLastKnownGood ? "yes" : "no",
			snapshot.lastKnownGoodUpdateFailed ? "yes" : "no");
		ImGui::Text("ALC unload: %s | last success: %s",
			AlcStatusLabel(snapshot.alcUnloadStatus),
			snapshot.lastSuccessfulBuildTime.empty() ? "(none)" : snapshot.lastSuccessfulBuildTime.c_str());
		if (!snapshot.lastFailureSummary.empty()) {
			ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.45f, 1.0f), "last failure: %s", snapshot.lastFailureSummary.c_str());
		}

		if (ImGui::Button("Rebuild")) { service->RequestRebuild(); }
		ImGui::SameLine();
		if (ImGui::Button("Retry")) { service->RequestRetry(); }
		ImGui::SameLine();
		if (ImGui::Button("Metadata Sync")) { service->RequestMetadataSync(); }
		ImGui::SameLine();
		if (ImGui::Button("Refresh IDE Project")) { RefreshIdeProject(); }
	} else {
		ImGui::TextDisabled("build service が利用できません。");
	}
	ImGui::Separator();

	//--------- filter -------------------------------------------------------
	ManagedBuildDiagnosticStore& store = ManagedBuildDiagnosticStore::GetInstance();
	ImGui::Checkbox("Errors", &showErrors_);
	ImGui::SameLine();
	ImGui::Checkbox("Warnings", &showWarnings_);
	ImGui::SameLine();
	ImGui::Checkbox("Latest build only", &latestBuildOnly_);
	ImGui::SameLine();
	ImGui::SetNextItemWidth(180.0f);
	ImGui::InputTextWithHint("##diagFilter", "message/file で検索", textFilter_, sizeof(textFilter_));
	ImGui::SameLine();
	if (ImGui::Button("Clear")) { store.Clear(); }
	ImGui::SameLine();
	ImGui::TextDisabled("E:%llu W:%llu (bounded %llu)",
		static_cast<unsigned long long>(store.ErrorCount()),
		static_cast<unsigned long long>(store.WarningCount()),
		static_cast<unsigned long long>(ManagedBuildDiagnosticStore::kMaxEntries));

	// latest build idを決定する、最後のentryのbuildIDを使う
	uint64_t latestBuildID = 0;
	if (!store.Entries().empty()) {
		latestBuildID = store.Entries().back().buildID;
	}

	//--------- list ---------------------------------------------------------
	const ImGuiTableFlags tableFlags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg
		| ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable;
	if (ImGui::BeginTable("##diagTable", 5, tableFlags)) {

		ImGui::TableSetupColumn("Kind", ImGuiTableColumnFlags_WidthFixed, 90.0f);
		ImGui::TableSetupColumn("Code", ImGuiTableColumnFlags_WidthFixed, 70.0f);
		ImGui::TableSetupColumn("File", ImGuiTableColumnFlags_WidthStretch);
		ImGui::TableSetupColumn("Line", ImGuiTableColumnFlags_WidthFixed, 50.0f);
		ImGui::TableSetupColumn("Message", ImGuiTableColumnFlags_WidthStretch);
		ImGui::TableHeadersRow();

		int rowID = 0;
		for (const ManagedBuildDiagnostic& d : store.Entries()) {

			if (d.severity == DiagnosticSeverity::Error && !showErrors_) { continue; }
			if (d.severity == DiagnosticSeverity::Warning && !showWarnings_) { continue; }
			if (d.severity == DiagnosticSeverity::Info) { continue; }
			if (latestBuildOnly_ && latestBuildID != 0 && d.buildID != latestBuildID) { continue; }
			if (!Algorithm::ContainsCaseInsensitive(d.message, textFilter_) && !Algorithm::ContainsCaseInsensitive(d.file, textFilter_)) {
				continue;
			}

			ImGui::TableNextRow();
			ImGui::PushID(rowID++);

			const ImVec4 color = (d.severity == DiagnosticSeverity::Error)
				? ImVec4(0.95f, 0.45f, 0.45f, 1.0f) : ImVec4(0.95f, 0.85f, 0.40f, 1.0f);

			ImGui::TableSetColumnIndex(0);
			ImGui::TextColored(color, "%s", ProcessKindLabel(d.processKind));

			ImGui::TableSetColumnIndex(1);
			ImGui::TextUnformatted(d.code.c_str());

			ImGui::TableSetColumnIndex(2);
			// クリック/ダブルクリックで共通IDE launcherを使って該当位置を開く
			const std::string fileLabel = d.file.empty() ? "(global)" : d.file;
			ImGui::Selectable(fileLabel.c_str(), false, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick);
			if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && !d.file.empty()) {
				ManagedIdeLauncher::OpenFile(d.file, d.line, d.column);
			}

			ImGui::TableSetColumnIndex(3);
			ImGui::Text("%d:%d", d.line, d.column);

			ImGui::TableSetColumnIndex(4);
			ImGui::TextUnformatted(d.message.c_str());
			if (ImGui::IsItemHovered()) {
				ImGui::SetTooltip("%s", d.rawLine.c_str());
			}

			ImGui::PopID();
		}
		ImGui::EndTable();
	}

	ImGui::End();
}
