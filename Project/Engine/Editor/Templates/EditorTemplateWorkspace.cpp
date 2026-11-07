#include "EditorTemplateStorage.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Utility/EditorSourceNavigation.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonFile.h>
#include <Engine/Core/Foundation/Serialization/ContentHash.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>

// c++
#include <fstream>
#include <regex>
#include <stdexcept>

// windows
#include <windows.h>

namespace {

	// 配布元を更新したときだけ作業用コピーと照合する
	std::string SourceRevision(const std::filesystem::path& root) {

		nlohmann::json manifest;
		if (!Engine::JsonFile::TryLoad(root / "template.json", manifest)) return {};
		return manifest.value("contentRevision", Engine::ContentHash::FileSHA256(root / "template.json"));
	}

	// XML属性へ使用するパスを変換する
	std::string XMLPath(const std::filesystem::path& path) {

		std::string result;
		for (const char value : Engine::Algorithm::PathToUTF8(path)) {
			if (value == '&') result += "&amp;";
			else if (value == '"') result += "&quot;";
			else result += value;
		}
		return result;
	}

	// 配布用のC#プロジェクトをソース参照へ切り替える
	void WriteSourceScriptProject(const std::filesystem::path& workspace) {

		const auto repository = Engine::EditorTemplateStorage::Repository();
		const auto project = workspace / "Scripts/GameScripts.csproj";
		std::ifstream input(project, std::ios::binary);
		if (!input) throw std::runtime_error("編集用のC#プロジェクトが見つかりません");
		std::string text((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
		input.close();
		text = std::regex_replace(text, std::regex(R"(<Import\s+Project="\.\./Library/EngineBinding\.props"\s*/>)"), "");
		text = std::regex_replace(text, std::regex(R"(<Reference\s+Include="NEM\.ScriptCore">[\s\S]*?</Reference>)"),
			"<ProjectReference Include=\"" + XMLPath(repository / "Project/Engine/Managed/NEM.ScriptCore/NEM.ScriptCore.csproj") + "\" />");
		for (const std::string analyzer : {"NEM.ScriptCodeGen", "NEM.ScriptAnalyzers"}) {
			const std::regex pattern("<Analyzer\\s+Include=\"[^\"]*/" + analyzer + "\\.dll\"\\s*/>");
			text = std::regex_replace(text, pattern, "<ProjectReference Include=\"" +
				XMLPath(repository / "Project/Engine/Managed" / analyzer / (analyzer + ".csproj")) +
				"\" OutputItemType=\"Analyzer\" ReferenceOutputAssembly=\"false\" />");
		}
		text = std::regex_replace(text, std::regex(R"(<Import\s+Project="[^\"]*/Managed/Build/NEM\.GameScripts\.targets"\s*/>)"),
			"<Import Project=\"" + XMLPath(repository / "Premake/NEM.GameScripts.targets") + "\" />");
		std::filesystem::create_directories(workspace / "Scripts");
		std::ofstream output(project, std::ios::binary | std::ios::trunc);
		output << text;
		if (!output) throw std::runtime_error("編集用C#プロジェクトを作成できません");
		std::ofstream solution(workspace / "Template.slnx", std::ios::binary | std::ios::trunc);
		solution << "<Solution><Project Path=\"Scripts/GameScripts.csproj\" /></Solution>\n";
		if (!solution) throw std::runtime_error("編集用ソリューションを作成できません");
	}

	// 起動済みの編集用プロジェクトを確認する
	bool IsWorkspaceRunning(const std::filesystem::path& workspace) {

		nlohmann::json session;
		if (!Engine::JsonFile::TryLoad(workspace / "Library/TemplateSession.json", session) || !session.is_object()) return false;
		const DWORD pid = session.value("processID", 0u);
		HANDLE process = pid ? ::OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid) : nullptr;
		if (!process) return false;
		FILETIME created{}, exited{}, kernel{}, user{};
		bool running = ::WaitForSingleObject(process, 0) == WAIT_TIMEOUT && ::GetProcessTimes(process, &created, &exited, &kernel, &user);
		const uint64_t creationTime = (uint64_t(created.dwHighDateTime) << 32) | created.dwLowDateTime;
		running &= session.value("creationTime", uint64_t{}) == creationTime;
		::CloseHandle(process);
		if (running) {
			::EnumWindows([](HWND window, LPARAM target) -> BOOL {
				DWORD processID = 0;
				::GetWindowThreadProcessId(window, &processID);
				if (processID != static_cast<DWORD>(target) || !::IsWindowVisible(window)) return TRUE;
				::ShowWindow(window, SW_RESTORE);
				::SetForegroundWindow(window);
				return FALSE;
			}, static_cast<LPARAM>(pid));
		}
		return running;
	}
}

Engine::EditorTemplateResult Engine::EditorTemplateStorage::OpenWorkspace(const EditorTemplateEntry& entry) {

	try {
		if (!EditorSourceNavigation::IsEngineSourceProject()) return {false, "エンジン開発環境でのみ読み込めます"};
		const auto source = ResolveRoot(entry.id);
		EditorTemplateEntry stored;
		if (!std::filesystem::is_directory(source) ||
			(std::filesystem::exists(source / "template.json") && !Read(source, stored)))
			return {false, "保存したテンプレートを読み込めません"};
		const auto workspace = Repository() / "Generated/TemplateAuthoring" / entry.id;
		if (IsWorkspaceRunning(workspace)) return {true, "編集中のテンプレートを表示しました", workspace};
		const auto descriptor = workspace / "Template.nemproject";
		bool recreate = !std::filesystem::is_regular_file(descriptor);
		if (!recreate) {
			nlohmann::json marker;
			if (!JsonFile::TryLoad(workspace / "Library/TemplateAuthoring.json", marker) ||
				marker.value("sourceRevision", std::string{}) != SourceRevision(source)) {
				const int choice = ::MessageBoxW(nullptr,
					L"保存元のテンプレートが更新されています。保存元から読み込み直しますか？\n\nはい: 作業用プロジェクトを保存元から作り直す\nいいえ: 前回の作業を再開する",
					L"テンプレートの読み込み", MB_YESNOCANCEL | MB_ICONQUESTION | MB_DEFBUTTON2);
				if (choice == IDCANCEL) return {false, "テンプレートの読み込みをキャンセルしました"};
				recreate = choice == IDYES;
			}
		}
		if (recreate) {
			const auto staged = workspace.parent_path() / (".opening-" + ToString(AssetID::New()));
			try {
				CopyTree(Repository() / "Templates/ManagedProject/Common", staged);
				CopyTree(source, staged);
				std::filesystem::create_directories(staged / "Library");
				if (!JsonFile::Save(staged / "Template.nemproject", nlohmann::json{
					{"schemaVersion", 1}, {"projectGuid", ToString(AssetID::New())}, {"name", entry.name},
					{"sceneStorage", "Monolithic"}, {"assetsDirectory", "GameAssets"},
					{"packagesDirectory", "Packages"}, {"projectSettingsDirectory", "ProjectSettings"}}) ||
					!JsonFile::Save(staged / "Library/TemplateAuthoring.json", {{"id", entry.id}, {"sourceRevision", SourceRevision(source)}}))
					throw std::runtime_error("編集用プロジェクトの設定を保存できません");
				WriteSourceScriptProject(staged);
				std::filesystem::create_directories(workspace.parent_path());
				ReplaceDirectory(staged, workspace);
			} catch (...) { std::error_code cleanup; std::filesystem::remove_all(staged, cleanup); throw; }
		} else WriteSourceScriptProject(workspace);
		std::wstring command = L"\"" + Algorithm::GetExecutablePath().wstring() + L"\" --project \"" + descriptor.wstring() + L"\"";
		STARTUPINFOW startup{};
		startup.cb = sizeof(startup);
		PROCESS_INFORMATION process{};
		if (!::CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, 0, nullptr, workspace.c_str(), &startup, &process))
			return {false, "テンプレート編集用Editorを起動できません"};
		FILETIME created{}, exited{}, kernel{}, user{};
		::GetProcessTimes(process.hProcess, &created, &exited, &kernel, &user);
		const uint64_t creationTime = (uint64_t(created.dwHighDateTime) << 32) | created.dwLowDateTime;
		JsonFile::Save(workspace / "Library/TemplateSession.json", {{"processID", process.dwProcessId}, {"creationTime", creationTime}});
		::CloseHandle(process.hThread);
		::CloseHandle(process.hProcess);
		return {true, "テンプレート編集用Editorを起動しました", workspace};
	} catch (const std::exception& error) { return {false, error.what()}; }
}

bool Engine::EditorTemplateStorage::ReadWorkspaceEntry(EditorTemplateEntry& entry) {

	nlohmann::json source;
	if (!JsonFile::TryLoad(RuntimePaths::GetLibraryPath("TemplateAuthoring.json"), source) || !source.is_object()) return false;
	try { return Read(ResolveRoot(source.value("id", std::string{})), entry); }
	catch (...) { return false; }
}
