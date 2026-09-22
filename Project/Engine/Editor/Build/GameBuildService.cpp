#include "GameBuildService.h"
#include "GameBuildAssetCollector.h"
#include "GameBuildUtility.h"
#include "GameBuildManifest.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Assets/Utility/AssetTypeResolver.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>

// c++
#include <algorithm>
#include <exception>

using namespace Engine::GameBuildUtility;

namespace {

	// PowerShellへ渡す引数を二重引用符で囲む
	std::wstring QuoteArgument(const std::filesystem::path& path) {

		return L"\"" + path.wstring() + L"\"";
	}

}

//============================================================================
//	GameBuildService classMethods
//============================================================================
Engine::GameBuildService::~GameBuildService() {

	processRunner_.Terminate();
	RemoveManifest();
}

bool Engine::GameBuildService::WriteManifest(const GameBuildSettings& settings, const AssetDatabase& database,
	std::filesystem::path& scriptPath, std::string& error, SceneAssetStorage* sceneStorage) {

	return GameBuildManifest::Write(settings, database, manifestPath_, outputDirectory_, scriptPath, error, sceneStorage);
}

void Engine::GameBuildService::RefreshScenes(const AssetDatabase& database) {

	scenes_.clear();
	const std::filesystem::path gameAssetsRoot = RuntimePaths::GetGameRoot() / "GameAssets";

	std::error_code ec;
	for (std::filesystem::recursive_directory_iterator it(gameAssetsRoot, ec), end;
		it != end && !ec; it.increment(ec)) {

		if (!it->is_regular_file(ec) || AssetTypeResolver::GuessByPath(it->path()) != AssetType::Scene) {
			continue;
		}
		const std::string assetPath = RuntimePaths::ToAssetPath(it->path());
		const AssetMeta* meta = database.FindByPath(assetPath);
		if (!meta || meta->type != AssetType::Scene || !StartsWith(meta->assetPath, "GameAssets/")) {
			continue;
		}

		std::filesystem::path displayPath = std::filesystem::relative(it->path(), gameAssetsRoot, ec);
		if (ec) {
			ec.clear();
			displayPath = it->path().filename();
		}
		scenes_.push_back({
			meta->guid,
			meta->assetPath,
			Algorithm::ConvertString(displayPath.generic_wstring())
			});
	}

	std::sort(scenes_.begin(), scenes_.end(), [](const GameBuildSceneEntry& lhs, const GameBuildSceneEntry& rhs) {
		return lhs.displayName < rhs.displayName;
		});
}

bool Engine::GameBuildService::Start(const GameBuildSettings& settings,
	const AssetDatabase& database, std::string& outError, SceneAssetStorage* sceneStorage) {

	if (IsBuilding()) {
		outError = "ビルドは既に実行中です";
		return false;
	}

	RemoveManifest();
	std::filesystem::path scriptPath;
	bool manifestWritten = false;
	try {
		manifestWritten = WriteManifest(settings, database, scriptPath, outError, sceneStorage);
	} catch (const std::exception& exception) {
		// ファイル操作の失敗でEditorを終了させず、ビルド画面へ理由を返す
		outError = std::string("製品ビルドの準備中に例外が発生しました: ") + exception.what();
		Logger::Output(LogType::Engine, spdlog::level::err, "[ゲームビルド] {}", outError);
	}
	if (!manifestWritten) {
		RemoveManifest();
		state_ = GameBuildState::Failed;
		statusMessage_ = "失敗しました";
		failureDetail_ = outError;
		return false;
	}

	std::wstring commandLine = L"powershell.exe -NoProfile -ExecutionPolicy Bypass -File ";
	commandLine += QuoteArgument(scriptPath);
	commandLine += L" -ManifestPath ";
	commandLine += QuoteArgument(manifestPath_);

	const std::filesystem::path buildRoot = ResolveGameBuildRoot(RuntimePaths::GetGameRoot());
	if (!processRunner_.Start(commandLine, buildRoot)) {
		RemoveManifest();
		outError = "製品ビルドプロセスを開始できませんでした";
		state_ = GameBuildState::Failed;
		statusMessage_ = "失敗しました";
		failureDetail_ = outError;
		return false;
	}

	state_ = GameBuildState::Building;
	statusMessage_ = "ビルド中...";
	failureDetail_.clear();
	Logger::Output(LogType::Engine, "[ゲームビルド] ビルドを開始しました 出力={}",
		Algorithm::PathToUTF8(outputDirectory_));
	return true;
}

void Engine::GameBuildService::Update() {

	if (!IsBuilding()) {
		return;
	}

	const bool finished = processRunner_.Poll([this](const std::string& line) {

		if (line.empty()) {
			return;
		}
		Logger::Output(LogType::Engine, "[ゲームビルド] {}", line);
		if (line.rfind("[NEM_GAME_BUILD_ERROR]", 0) == 0) {
			failureDetail_ = line.substr(std::string("[NEM_GAME_BUILD_ERROR]").size());
			while (!failureDetail_.empty() && failureDetail_.front() == ' ') {
				failureDetail_.erase(failureDetail_.begin());
			}
		}
		});
	if (!finished) {
		return;
	}

	const int32_t exitCode = processRunner_.ExitCode();
	RemoveManifest();
	if (exitCode == 0) {

		state_ = GameBuildState::Completed;
		statusMessage_ = "完了しました";
		Logger::Output(LogType::Engine, "[ゲームビルド] ビルドが完了しました 出力={}",
			Algorithm::PathToUTF8(outputDirectory_));
	} else {

		state_ = GameBuildState::Failed;
		statusMessage_ = "失敗しました";
		if (failureDetail_.empty()) {
			failureDetail_ = "製品ビルドに失敗しました、engine.logを確認してください";
		}
		Logger::Output(LogType::Engine, spdlog::level::err,
			"[ゲームビルド] ビルドに失敗しました 終了コード={} 詳細={}", exitCode, failureDetail_);
	}
}

void Engine::GameBuildService::ResetStatus() {

	if (IsBuilding()) {
		return;
	}
	state_ = GameBuildState::Idle;
	statusMessage_.clear();
	failureDetail_.clear();
}

bool Engine::GameBuildService::CollectFiles(AssetID startupScene, const AssetDatabase& database,
	std::vector<GameBuildFileEntry>& outFiles, std::string& outError, SceneAssetStorage* sceneStorage) {

	return GameBuildAssetCollector::CollectFiles(startupScene, database, outFiles, outError, sceneStorage);
}

void Engine::GameBuildService::RemoveManifest() {

	if (manifestPath_.empty()) {
		return;
	}
	std::error_code ec;
	std::filesystem::remove(manifestPath_, ec);
	manifestPath_.clear();
}
