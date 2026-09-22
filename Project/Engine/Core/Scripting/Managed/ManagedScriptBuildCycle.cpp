#include "ManagedScriptBuildService.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Scripting/Managed/ManagedScriptRuntime.h>
#include <Engine/Core/Scripting/Managed/Diagnostics/ManagedBuildDiagnosticStore.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>

// windowsの_wcsicmp等
#include <cwctype>
// c++
#include <algorithm>
#include <cctype>
#include <ctime>
#include <vector>

#include "ManagedBuildUtility.h"

using namespace Engine::ManagedBuildUtility;

bool Engine::ManagedScriptBuildService::StartBuild(bool forPlay) {

	const std::filesystem::path projectPath = runtime_->GameScriptProjectPath();
	if (projectPath.empty()) {

		// ビルド対象なし
		if (forPlay) {
			playBuildResult_ = PlayBuildResult::Succeeded;
		}
		dirty_ = false;
		SetState(State::Idle);
		return false;
	}

	// --no-dependenciesビルドのため前提成果物の有無を先に確認する、ScriptCoreはエディタがロード済みで作り直さない
	if (!VerifyBuildPrerequisites()) {

		if (forPlay) {
			playBuildResult_ = PlayBuildResult::Failed;
		}
		dirty_ = false;
		SetState(State::BuildFailed);
		FinishCycle(false);
		return false;
	}

	// 変更は消費する、ビルド中の追加変更はPollSourceChangesが再びdirtyへ戻す
	dirty_ = false;
	cycle_.currentForPlay = forPlay;
	cycle_.buildStartTime = std::chrono::steady_clock::now();
	cycle_.diagnostics = ManagedBuildCycle::ReloadDiagnostics{};
	cycle_.diagnostics.buildID = ++buildCounter_;
	// 新しいビルドサイクルの開始を診断ストアへ通知し、古いビルドの履歴を上限で間引く
	ManagedBuildDiagnosticStore::GetInstance().BeginBuild(cycle_.diagnostics.buildID);
	process_.ResetDiagnostics();

	// ステージングディレクトリ作成の失敗は無視せず、絶対パスとエラーを出して中断する
	std::error_code dirError{};
	cycle_.currentStagingDir = artifacts_.StagingRoot() / std::to_wstring(cycle_.diagnostics.buildID);
	std::filesystem::create_directories(cycle_.currentStagingDir, dirError);
	if (dirError) {

		Logger::Output(LogType::Engine, spdlog::level::err,
			"ManagedScriptBuildService: Staging Directoryの作成に失敗しました path={} 内容={}",
			ToUtf8Path(cycle_.currentStagingDir), dirError.message());
		if (forPlay) {
			playBuildResult_ = PlayBuildResult::Failed;
		}
		SetState(State::BuildFailed);
		FinishCycle(false);
		return false;
	}

	// ステージング出力へdotnet buildし実行中DLLは触らない、NEMScriptStagingOutputでこのプロジェクトだけステージングへ向ける
	cycle_.pendingBuildCommand =
		L"dotnet build \"" + projectPath.wstring() + L"\" -c " + Widen(BuildProfile()) +
		L" --nologo --no-dependencies -p:DebugType=portable -p:DebugSymbols=true -p:Optimize=false" +
		L" -p:NEMScriptMetadataMode=EditorSync" +
		L" -p:NEMScriptStagingOutput=\"" + cycle_.currentStagingDir.wstring() + L"\"";
	cycle_.lastBuildWorkingDir = projectPath.parent_path();

	// ビルド前に.cs.metaの安定ID採番と維持を非同期で実行する、ツールが無ければ飛ばして直接ビルドする
	// metadata sync toolはレイアウトで配置が異なるため候補を順に探す
	// エンジンソース構成: Project/Engine/Managed/NEM.ScriptMetaSync/bin/<profile>/net10.0/
	// prebuilt SDK構成: <GameRoot>/External/NEMEngine/Managed/Tools/
	const std::filesystem::path projectRoot = projectPath.parent_path().parent_path().parent_path();
	const std::filesystem::path syncToolCandidates[] = {
		projectRoot / "Engine" / "Managed" / "NEM.ScriptMetaSync" / "bin" / BuildProfile() / "net10.0" / "NEM.ScriptMetaSync.dll",
		projectRoot.parent_path() / "External" / "NEMEngine" / "Managed" / "Tools" / "NEM.ScriptMetaSync.dll",
	};
	std::filesystem::path syncToolDll;
	for (const std::filesystem::path& candidate : syncToolCandidates) {

		std::error_code candidateExists{};
		if (std::filesystem::exists(candidate, candidateExists) && !candidateExists) {
			syncToolDll = candidate;
			break;
		}
	}
	const std::filesystem::path scriptsRoot = projectPath.parent_path().parent_path() / "GameAssets";

	if (syncToolDll.empty()) {

		Logger::Output(LogType::Engine, spdlog::level::warn,
			"ManagedScriptBuildService: Script Metadata同期Toolが見つからないため既存.cs.metaを使用します "
			"候補={} | {}",
			ToUtf8Path(syncToolCandidates[0]), ToUtf8Path(syncToolCandidates[1]));
		return StartGameScriptsBuild();
	}

	const std::wstring syncCommand =
		L"dotnet \"" + syncToolDll.wstring() + L"\" --root \"" + scriptsRoot.wstring() + L"\" --mode EditorSync";

	Logger::Output(LogType::Engine, spdlog::level::info,
		"ManagedScriptBuildService: ビルドを開始します BuildID={} Play用={} Staging={}",
		cycle_.diagnostics.buildID, forPlay, ToUtf8Path(cycle_.currentStagingDir));
	Logger::Output(LogType::Engine, spdlog::level::info,
		"ManagedScriptBuildService: Metadataを同期します command={}", Algorithm::ConvertString(syncCommand));

	if (!process_.Start(syncCommand, cycle_.lastBuildWorkingDir)) {

		// 同期を起動できないときは既存.cs.metaを前提にそのままビルドへ進む
		Logger::Output(LogType::Engine, spdlog::level::warn,
			"ManagedScriptBuildService: Metadata同期Processを開始できないため既存.cs.metaでビルドします");
		return StartGameScriptsBuild();
	}

	SetState(State::MetadataSyncing);
	return true;
}

bool Engine::ManagedScriptBuildService::StartGameScriptsBuild() {

	cycle_.lastBuildCommandUtf8 = Algorithm::ConvertString(cycle_.pendingBuildCommand);

	Logger::Output(LogType::Engine, spdlog::level::info,
		"ManagedScriptBuildService: ビルドCommandを実行します cwd={} command={}",
		ToUtf8Path(cycle_.lastBuildWorkingDir), cycle_.lastBuildCommandUtf8);

	if (!process_.Start(cycle_.pendingBuildCommand, cycle_.lastBuildWorkingDir)) {

		Logger::Output(LogType::Engine, spdlog::level::err,
			"ManagedScriptBuildService: dotnet build Processを開始できません command={}", cycle_.lastBuildCommandUtf8);
		if (cycle_.currentForPlay) {
			playBuildResult_ = PlayBuildResult::Failed;
		}
		SetState(State::BuildFailed);
		FinishCycle(false);
		return false;
	}

	SetState(State::Building);
	return true;
}

bool Engine::ManagedScriptBuildService::VerifyBuildPrerequisites() const {

	const std::filesystem::path projectPath = runtime_ ? runtime_->GameScriptProjectPath() : std::filesystem::path{};
	if (projectPath.empty()) {
		return true;
	}

	// GameScripts.csprojの位置からエンジンのManagedディレクトリを導出する
	const std::filesystem::path projectRoot = projectPath.parent_path().parent_path().parent_path();
	const std::filesystem::path engineManagedDir = projectRoot / "Engine" / "Managed";

	std::error_code existsError{};
	if (!std::filesystem::exists(engineManagedDir, existsError) || existsError) {
		// 想定外のレイアウトつまりテンプレート等では誤検知を避けるため検証をスキップする
		return true;
	}

	const std::string profile = BuildProfile();

	// NEM.ScriptCodeGen.dllは--no-dependenciesでは作られないため必須
	const std::filesystem::path codeGenDll =
		engineManagedDir / "NEM.ScriptCodeGen" / "bin" / profile / "netstandard2.0" / "NEM.ScriptCodeGen.dll";

	// NEM.ScriptCore.dllは配置先が構成で異なるため候補を順に確認する
	const std::filesystem::path repoRoot = projectRoot.parent_path();
	const std::filesystem::path scriptCoreCandidates[] = {
		repoRoot / "Generated" / "Managed" / "NEM.ScriptCore" / profile / "NEM.ScriptCore.dll",
		projectRoot / "Engine" / "Library" / "Managed" / profile / "NEM.ScriptCore.dll",
	};

	const auto pathExists = [](const std::filesystem::path& path) {
		std::error_code error{};
		return std::filesystem::exists(path, error) && !error;
		};

	bool ok = true;

	if (!pathExists(codeGenDll)) {

		ok = false;
		Logger::Output(LogType::Engine, spdlog::level::err,
			"ManagedScriptBuildService: 必須Analyzerがありません path={} profile={}",
			ToUtf8Path(codeGenDll), profile);
	}

	bool scriptCoreFound = false;
	for (const std::filesystem::path& candidate : scriptCoreCandidates) {
		if (pathExists(candidate)) {
			scriptCoreFound = true;
			break;
		}
	}
	if (!scriptCoreFound) {

		ok = false;
		Logger::Output(LogType::Engine, spdlog::level::err,
			"ManagedScriptBuildService: 必須ScriptCoreがありません 探索先={} | {} profile={}",
			ToUtf8Path(scriptCoreCandidates[0]), ToUtf8Path(scriptCoreCandidates[1]), profile);
	}

	if (!ok) {

		// --no-dependenciesビルドは前提を作り直さないため明確な復旧手順を出す
		Logger::Output(LogType::Engine, spdlog::level::err,
			"ManagedScriptBuildService: 必須成果物がないためGameScriptsのStaging Buildを実行できません "
			"NEM.ScriptCoreとNEM.ScriptCodeGenを生成するためprofile '{}'でSandboxまたはEditorを再ビルドしてください "
			"Staging Buildは--no-dependenciesを使用するためRuntimeでは必須成果物を生成しません "
			"Editorが読み込み中のScriptCoreも上書きできません", profile);
	}
	return ok;
}
