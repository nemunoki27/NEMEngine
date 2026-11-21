#include "EditorDeveloperLauncher.h"

//============================================================================
//	include
//============================================================================
#include "EditorTemplateSnapshot.h"
#include "EditorTemplateStorage.h"
#include <Engine/Editor/Runtime/Application/SceneSaveController.h>
#include <Engine/Editor/UI/Panels/Core/EditorPanelContext.h>
#include <Engine/Editor/Utility/EditorSourceNavigation.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>
#include <EditorBuildInfo.generated.h>

// c++
#include <stdexcept>
// imgui
#include <imgui.h>

namespace {

	using namespace Engine;

	std::string SaveUnavailable(const EditorPanelContext& context) {

		if (context.IsPlaying()) return "プレイを終了してから保存してください";
		if (!context.CanEditScene() || !context.editorContext->activeSceneAsset ||
			!context.editorContext->assetDatabase || !context.editorContext->sceneSaveController)
			return "保存するシーンを開いてください";
		if (context.editorContext->isPrefabEditing) return "Prefab編集を終了してから保存してください";
		return {};
	}

	void LogLaunchError(const std::string& message) {

		Logger::Output(LogType::Engine, spdlog::level::err, "[バージョン管理] {}", message);
		::MessageBoxW(nullptr, Algorithm::ConvertString(message).c_str(), L"バージョン管理", MB_OK | MB_ICONERROR);
	}

	void Discard(const EditorTemplateResult& staged) {

		if (!staged.success) return;
		std::error_code error;
		std::filesystem::remove_all(staged.path, error);
	}
}

Engine::EditorDeveloperLauncher::~EditorDeveloperLauncher() {

	if (staging_.valid()) {
		try { Discard(staging_.get()); }
		catch (const std::exception& error) { Logger::Output(LogType::Engine, spdlog::level::err, "[テンプレート] {}", error.what()); }
	}
	if (bootstrap_) ::CloseHandle(bootstrap_);
}

void Engine::EditorDeveloperLauncher::DrawButton() {

	if (!available_) available_ = EditorSourceNavigation::IsEngineSourceProject();
	if (!*available_) return;
	const char* label = "バージョン管理";
	const float buttonWidth = ImGui::CalcTextSize(label).x + ImGui::GetStyle().ItemSpacing.x * 2.0f;
	const float right = ImGui::GetWindowWidth() - ImGui::GetStyle().WindowPadding.x - buttonWidth;
	if (ImGui::GetCursorPosX() < right) ImGui::SetCursorPosX(right);
	if (ImGui::MenuItem(label)) Launch();
}

void Engine::EditorDeveloperLauncher::Launch() {

	const auto repository = EditorTemplateStorage::Repository();
	const auto directory = repository / "Project/Tools/NEM.DeveloperLauncher";
	if (!pipe_.Open()) { LogLaunchError("Editor接続用の名前付きパイプを作成できません"); return; }

	std::filesystem::path executable;
	for (const auto& config : {std::string(EditorBuildInfo::kConfiguration), std::string("Release"), std::string("Develop"), std::string("Debug")}) {
		const auto candidate = directory / "bin" / config / "net10.0-windows/NEMDeveloperLauncher.exe";
		if (std::filesystem::is_regular_file(candidate)) { executable = candidate; break; }
	}
	std::wstring command;
	if (executable.empty()) {
		if (bootstrap_) return;
		command = L"dotnet.exe run --project \"" + (directory / "NEM.DeveloperLauncher.csproj").wstring() + L"\" -c Release --";
	} else command = L"\"" + executable.wstring() + L"\"";
	command += L" --repository \"" + repository.wstring() + L"\" --editor-pipe \"" + pipe_.GetName() + L"\"";
	STARTUPINFOW startup{sizeof(startup)};
	PROCESS_INFORMATION process{};
	if (!::CreateProcessW(executable.empty() ? nullptr : executable.c_str(), command.data(), nullptr, nullptr,
		FALSE, CREATE_NO_WINDOW, nullptr, repository.c_str(), &startup, &process)) {
		LogLaunchError("開発者ランチャーを起動できません。.NET SDKと開発用プロジェクトを確認してください");
		return;
	}
	::AllowSetForegroundWindow(process.dwProcessId);
	::CloseHandle(process.hThread);
	if (executable.empty()) bootstrap_ = process.hProcess;
	else ::CloseHandle(process.hProcess);
}

void Engine::EditorDeveloperLauncher::Update(const EditorPanelContext& context) try {

	if (bootstrap_ && ::WaitForSingleObject(bootstrap_, 0) == WAIT_OBJECT_0) {
		DWORD exitCode = 0;
		::GetExitCodeProcess(bootstrap_, &exitCode);
		::CloseHandle(bootstrap_);
		bootstrap_ = nullptr;
		if (exitCode) LogLaunchError("開発者ランチャーのビルドまたは起動に失敗しました。NEM.DeveloperLauncherをビルドしてください");
	}
	const auto input = pipe_.Poll(!request_);
	if (request_) UpdateSave(context);
	if (input) Dispatch(nlohmann::json::parse(*input), context);
} catch (const std::exception& error) {
	pipe_.Reply(nlohmann::json{{"ok", false}, {"message", error.what()}}.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace));
}

void Engine::EditorDeveloperLauncher::Dispatch(const nlohmann::json& data, const EditorPanelContext& context) {

	const auto action = data.at("action").get<std::string>();
	nlohmann::json response;
	if (action == "status") {
		const auto* active = context.editorContext;
		EditorTemplateEntry workspace;
		const auto reason = request_ ? "テンプレートを保存中です" : SaveUnavailable(context);
		const auto name = active && active->activeSceneHeader ? active->activeSceneHeader->name : std::string{};
		response = {{"ok", true}, {"project", RuntimePaths::GetProjectName()},
			{"projectRoot", Algorithm::PathToUTF8(RuntimePaths::GetProjectRoot())},
			{"scene", name.empty() && active ? active->activeScenePath : name},
			{"sceneId", active && active->activeSceneAsset ? ToString(active->activeSceneAsset) : ""},
			{"workspaceId", EditorTemplateStorage::ReadWorkspaceEntry(workspace) ? workspace.id : ""},
			{"canSave", reason.empty()}, {"message", reason}};
	} else if (action == "open") {
		if (request_) throw std::runtime_error("テンプレートの保存完了を待ってください");
		EditorTemplateEntry entry;
		const auto id = data.at("id").get<std::string>();
		if (!EditorTemplateStorage::Read(EditorTemplateStorage::ResolveRoot(id), entry) || entry.id != id)
			throw std::runtime_error("指定したテンプレートが見つかりません");
		std::optional<bool> recreate;
		if (data.contains("recreate")) recreate = data.at("recreate").get<bool>();
		const auto result = EditorTemplateStorage::OpenWorkspace(entry, recreate);
		response = {{"ok", result.success}, {"message", result.message}, {"chooseWorkspace", result.chooseWorkspace}};
	} else if (action == "save") {
		BeginSave(data, context);
		return;
	} else throw std::runtime_error("未対応のEditor操作です");
	pipe_.Reply(response.dump());
}

void Engine::EditorDeveloperLauncher::BeginSave(const nlohmann::json& data, const EditorPanelContext& context) {

	if (request_) throw std::runtime_error("テンプレートの保存完了を待ってください");
	const auto reason = SaveUnavailable(context);
	if (!reason.empty()) throw std::runtime_error(reason);
	const auto& active = *context.editorContext;
	if (ToString(active.activeSceneAsset) != data.at("sceneId").get<std::string>()) throw std::runtime_error("保存対象のシーンが切り替わりました");

	const auto kind = data.at("kind").get<std::string>();
	if (kind != "2D" && kind != "3D" && kind != "Sample") throw std::runtime_error("テンプレートの種類が不正です");
	auto entry = EditorTemplateStorage::MakeEntry(kind);
	const auto id = data.value("id", std::string{});
	if (kind == "Sample" && !id.empty()) {
		if (!EditorTemplateStorage::ReadWorkspaceEntry(entry) || entry.kind != kind || entry.id != id)
			throw std::runtime_error("このサンプルの編集用Editorから保存してください");
	} else if (!id.empty() && id != entry.id) throw std::runtime_error("テンプレートの保存先が一致しません");

	entry.name = data.at("name").get<std::string>();
	if (entry.name.empty()) throw std::runtime_error("名前を入力してください");
	entry.description = data.value("description", entry.description);
	entry.additionalPaths = data.value("additionalPaths", entry.additionalPaths);
	entry.version = data.value("version", entry.version);
	entry.engineVersion = data.value("engineVersion", entry.engineVersion);
	entry.startupScene = active.activeSceneAsset;
	if (active.activeSceneDirty && !active.sceneSaveController->SaveActiveEditScene())
		throw std::runtime_error("編集中のシーンを保存できません");
	sourceInstance_ = active.activeSceneInstanceID;
	request_.emplace(EditorTemplateSaveRequest{entry, EditorTemplateStorage::Repository(), RuntimePaths::GetProjectRoot()});
	interrupted_ = false;
}

void Engine::EditorDeveloperLauncher::UpdateSave(const EditorPanelContext& context) {

	if (!pipe_.HasRequest() || !SaveUnavailable(context).empty() || context.editorContext->activeSceneInstanceID != sourceInstance_ ||
		context.editorContext->activeSceneAsset != request_->entry.startupScene) interrupted_ = true;
	if (!staging_.valid() && !interrupted_) {
		const auto& active = *context.editorContext;
		if (active.sceneSaveController->IsSaving()) return;
		if (active.activeSceneDirty) {
			request_.reset();
			throw std::runtime_error("シーンの保存に失敗したか、保存中に編集されました");
		}
		try {
			auto database = std::make_shared<AssetDatabase>(*active.assetDatabase);
			staging_ = std::async(std::launch::async, [request = *request_, database]() { return StageEditorTemplate(request, *database); });
		} catch (...) { request_.reset(); throw; }
		return;
	}
	EditorTemplateResult result;
	if (staging_.valid()) {
		if (context.editorContext && context.editorContext->activeSceneDirty) interrupted_ = true;
		if (staging_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
		try { result = staging_.get(); }
		catch (...) { request_.reset(); throw; }
		if (interrupted_) Discard(result);
		else if (result.success) result = CommitEditorTemplate(*request_, result.path);
	}
	if (interrupted_) result = {false, "保存元の状態が変わったか、ランチャーとの接続が切れたため保存を中止しました"};
	const auto id = request_->entry.id;
	request_.reset();
	if (pipe_.HasRequest()) pipe_.Reply(nlohmann::json{{"ok", result.success}, {"id", id}, {"message", result.message}}.dump());
}
