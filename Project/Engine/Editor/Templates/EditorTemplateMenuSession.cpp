#include "EditorTemplateMenuSession.h"

//============================================================================
//	include
//============================================================================
#include "EditorTemplateSnapshot.h"
#include "EditorTemplateStorage.h"
#include <Engine/Editor/UI/Panels/Core/EditorPanelContext.h>
#include <Engine/Editor/UI/Panels/Core/IEditorPanelHost.h>
#include <Engine/Editor/UI/ImGui/ImGuiHelpers.h>
#include <Engine/Editor/Utility/EditorSourceNavigation.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>

// c++
#include <algorithm>
#include <chrono>

void Engine::EditorTemplateMenuSession::Prepare(const EditorPanelContext& context, const std::string& kind) {

	draft_ = EditorTemplateStorage::MakeEntry(kind);
	EditorTemplateEntry workspace;
	if (EditorTemplateStorage::ReadWorkspaceEntry(workspace) && workspace.kind == kind) draft_ = workspace;
	draft_.startupScene = context.editorContext->activeSceneAsset;
	message_.clear();
	failed_ = false;
	openSave_ = true;
}

void Engine::EditorTemplateMenuSession::DrawMenu(const EditorPanelContext& context) {

	if (!EditorSourceNavigation::IsEngineSourceProject() || !ImGui::BeginMenu("テンプレート")) return;
	const bool busy = saving_.valid() || waitingForSceneSave_ || distribution_.IsBusy();
	const bool canSave = !busy && context.CanEditScene() && !context.editorContext->isPrefabEditing &&
		context.editorContext->activeSceneAsset && context.editorContext->assetDatabase;
	ImGui::BeginDisabled(busy);
	if (ImGui::MenuItem("2Dテンプレートを保存", nullptr, false, canSave)) Prepare(context, "2D");
	if (ImGui::MenuItem("3Dテンプレートを保存", nullptr, false, canSave)) Prepare(context, "3D");
	ImGui::SeparatorText("サンプルプロジェクト");
	if (ImGui::MenuItem("現在のシーンをサンプルプロジェクトとして保存", nullptr, false, canSave)) Prepare(context, "Sample");
	samples_ = EditorTemplateStorage::ListSamples();
	for (const auto& entry : samples_) {
		ImGui::PushID(entry.id.c_str());
		if (ImGui::BeginMenu(entry.name.c_str())) {
			if (ImGui::MenuItem("読み込み")) {
				const auto result = EditorTemplateStorage::OpenWorkspace(entry);
				message_ = result.message;
				failed_ = !result.success;
				if (!result.success) Logger::Output(LogType::Engine, spdlog::level::err, "[テンプレート] {}", result.message);
			}
			if (ImGui::MenuItem("削除")) distribution_.RequestDeleteSample(entry);
			ImGui::EndMenu();
		}
		ImGui::PopID();
	}
	if (samples_.empty()) ImGui::TextDisabled("保存済みサンプルはありません");
	ImGui::Separator();
	for (const std::string kind : {"2D", "3D"}) {
		const auto entry = EditorTemplateStorage::MakeEntry(kind);
		if (ImGui::MenuItem((kind + "テンプレートを読み込み").c_str())) {
			const auto result = EditorTemplateStorage::OpenWorkspace(entry);
			message_ = result.message;
			failed_ = !result.success;
			if (!result.success) Logger::Output(LogType::Engine, spdlog::level::err, "[テンプレート] {}", result.message);
		}
	}
	ImGui::Separator();
	if (ImGui::MenuItem("配布管理")) distribution_.Open();
	ImGui::EndDisabled();
	ImGui::EndMenu();
}

void Engine::EditorTemplateMenuSession::StartSave(const EditorPanelContext& context) try {

	if (saving_.valid() || !context.editorContext || context.editorContext->activeSceneAsset != draft_.startupScene) {
		message_ = "保存中にシーンが切り替わったため操作を中止しました";
		failed_ = true;
		return;
	}
	EditorTemplateSaveRequest request{draft_, EditorTemplateStorage::Repository(), RuntimePaths::GetProjectRoot()};
	auto database = std::make_shared<AssetDatabase>(*context.editorContext->assetDatabase);
	saving_ = std::async(std::launch::async, [request, database]() { return SaveEditorTemplate(request, *database); });
	message_ = "依存アセットとスクリプトを収集中です";
	failed_ = false;
} catch (const std::exception& error) {
	message_ = std::string("テンプレートの保存を開始できません: ") + error.what();
	failed_ = true;
}

void Engine::EditorTemplateMenuSession::Update(const EditorPanelContext& context) {

	if (waitingForSceneSave_) {
		if (context.IsPlaying() || context.editorContext->activeSceneAsset != draft_.startupScene) {
			waitingForSceneSave_ = false;
			message_ = "シーンが切り替わったため保存を中止しました";
			failed_ = true;
		} else if (!context.editorContext->activeSceneDirty) {
			waitingForSceneSave_ = false;
			StartSave(context);
		}
	}
	if (saving_.valid() && saving_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
		const auto result = saving_.get();
		message_ = result.message;
		failed_ = !result.success;
	}
	std::optional<std::filesystem::path> selected;
	if (additionalDialog_.Poll(selected) && selected) AddPath(*selected);
}

void Engine::EditorTemplateMenuSession::AddPath(const std::filesystem::path& path) {

	const auto relative = path.is_absolute() ? path.lexically_relative(RuntimePaths::GetProjectRoot()) : path;
	if (relative.empty() || relative.has_root_path()) { message_ = "プロジェクト内のファイルを選択してください"; failed_ = true; return; }
	for (const auto& part : relative) {
		if (part == "..") { message_ = "プロジェクト内のファイルを選択してください"; failed_ = true; return; }
	}
	const auto text = Algorithm::ConvertString(relative.generic_wstring());
	if (std::find(draft_.additionalPaths.begin(), draft_.additionalPaths.end(), text) == draft_.additionalPaths.end())
		draft_.additionalPaths.push_back(text);
	additionalPath_.clear();
}

void Engine::EditorTemplateMenuSession::DrawSaveFields() {

	MyGUI::ScopedPropertyLabelWidth labels("TemplateSaveFields");
	MyGUI::InputText("名前", draft_.name);
	TextEditSetting description;
	description.multiLine = true;
	description.size = ImVec2(0, 60);
	MyGUI::InputText("説明", draft_.description, description);
	MyGUI::Text("保存先", Algorithm::PathToUTF8(draft_.root));
	ImGui::SeparatorText("動的に読み込む追加アセット");
	ImGui::TextWrapped("シーンの依存アセットと全スクリプトは自動で含まれます。C#からパス指定で読むファイルは追加してください。");
	MyGUI::InputText("相対パス", additionalPath_);
	const float width = MyGUI::CalcEqualItemWidth(3);
	if (ImGui::Button("パスを追加", ImVec2(width, 0)) && !additionalPath_.empty()) AddPath(Algorithm::PathFromUTF8(additionalPath_));
	ImGui::SameLine();
	if (ImGui::Button("ファイルを選択", ImVec2(width, 0))) additionalDialog_.OpenFile(RuntimePaths::GetGameAssetsRoot());
	ImGui::SameLine();
	if (ImGui::Button("フォルダーを選択", ImVec2(width, 0))) additionalDialog_.Open(RuntimePaths::GetGameAssetsRoot());
	for (size_t i = 0; i < draft_.additionalPaths.size();) {
		ImGui::PushID(static_cast<int>(i));
		if (ImGui::SmallButton("削除")) { draft_.additionalPaths.erase(draft_.additionalPaths.begin() + static_cast<std::ptrdiff_t>(i)); }
		else { ImGui::SameLine(); ImGui::TextUnformatted(draft_.additionalPaths[i].c_str()); ++i; }
		ImGui::PopID();
	}
}

void Engine::EditorTemplateMenuSession::DrawPopup(const EditorPanelContext& context) {

	if (!EditorSourceNavigation::IsEngineSourceProject()) return;
	Update(context);
	if (openSave_) { ImGui::OpenPopup("テンプレートを保存"); openSave_ = false; }
	ImGui::SetNextWindowSizeConstraints(ImVec2(720, 0), ImVec2(900, 800));
	if (MyGUI::BeginPopupModal("テンプレートを保存", nullptr, ImGuiWindowFlags_AlwaysAutoResize, 720)) {
		const bool busy = saving_.valid() || waitingForSceneSave_;
		ImGui::BeginDisabled(busy);
		DrawSaveFields();
		ImGui::EndDisabled();
		ImGui::Separator();
		if (!message_.empty()) ImGui::TextColored(failed_ ? ImVec4(1, .35f, .3f, 1) : ImVec4(.5f, .9f, .6f, 1), "%s", message_.c_str());
		const float width = MyGUI::CalcEqualItemWidth(2);
		ImGui::BeginDisabled(busy || draft_.name.empty());
		if (ImGui::Button("保存", ImVec2(width, 0))) {
			if (context.editorContext->activeSceneDirty) {
				context.host->RequestSaveScene();
				waitingForSceneSave_ = true;
				message_ = "シーンの保存を待っています";
			} else StartSave(context);
		}
		ImGui::EndDisabled();
		ImGui::SameLine();
		ImGui::BeginDisabled(saving_.valid());
		if (ImGui::Button(waitingForSceneSave_ ? "待機を中止" : "閉じる", ImVec2(width, 0))) {
			waitingForSceneSave_ = false;
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndDisabled();
		ImGui::EndPopup();
	}
	distribution_.Draw();
}
