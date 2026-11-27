#include "EditorGameBuildSession.h"
#include "GameBuildUtility.h"

#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonSerializer.h>
#include <Engine/Core/Rendering/Materials/MaterialPaintMask.h>
#include <Engine/Core/Runtime/Context/EngineContext.h>
#include <Engine/Core/Runtime/Paths/ConfigPaths.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>

#include <algorithm>
#include <utility>

using namespace Engine;

void Engine::EditorGameBuildSession::Prepare(const EditorPanelContext& context) {

	ResetStatus();
	try {
		draft_.additionalAssets = GameBuildUtility::LoadAdditionalAssets();
	} catch (const std::exception& error) {
		draft_.additionalAssets.clear();
		buildError_ = error.what();
	}
	gameBuildService_.RefreshScenes(*context.editorContext->assetDatabase);

	buildSceneNames_.clear();
	buildSceneNames_.reserve(gameBuildService_.GetScenes().size());
	for (const GameBuildSceneEntry& scene : gameBuildService_.GetScenes()) {
		buildSceneNames_.push_back(scene.displayName);
	}

	const AssetID activeScene = context.editorContext->activeSceneAsset;
	const auto active = std::find_if(gameBuildService_.GetScenes().begin(), gameBuildService_.GetScenes().end(),
		[activeScene](const GameBuildSceneEntry& scene) { return scene.assetID == activeScene; });
	if (active != gameBuildService_.GetScenes().end()) {
		draft_.sceneName = active->displayName;
	} else if (!buildSceneNames_.empty()) {
		draft_.sceneName = buildSceneNames_.front();
	} else {
		draft_.sceneName.clear();
	}

	if (draft_.executableName.empty()) {
		draft_.executableName = Algorithm::PathToUTF8(RuntimePaths::GetGameRoot().filename());
	}
	if (draft_.outputPath.empty()) {
		draft_.outputPath = Algorithm::PathToUTF8(
			RuntimePaths::GetEngineProjectRoot().parent_path() / "Build");
	}
	const Vector2I gameSize = EngineContext::GetWindowSetting().gameSize;
	draft_.gameWidth = gameSize.x;
	draft_.gameHeight = gameSize.y;
	requestOpenBuildPopup_ = true;
}

Engine::AssetID Engine::EditorGameBuildSession::ResolveBuildScene() const {

	const auto found = std::find_if(gameBuildService_.GetScenes().begin(), gameBuildService_.GetScenes().end(),
		[this](const GameBuildSceneEntry& scene) { return scene.displayName == draft_.sceneName; });
	return found != gameBuildService_.GetScenes().end() ? found->assetID : AssetID{};
}

void EditorGameBuildSession::Update() {

	gameBuildService_.Update();
	std::optional<std::filesystem::path> selectedDirectory;
	if (buildDirectoryDialog_.Poll(selectedDirectory) && selectedDirectory) {
		draft_.outputPath = Algorithm::PathToUTF8(*selectedDirectory);
	}
	std::optional<std::filesystem::path> selected;
	if (assetDialog_.Poll(selected) && selected) {
		auto path = RuntimePaths::ToAssetPath(GameBuildUtility::NormalizeBuildPath(*selected));
		std::ranges::replace(path, '\\', '/');
		if (!GameBuildUtility::IsAdditionalAssetPath(path)) {
			buildError_ = "GameAssetsまたはPackage内のアセットを選択してください";
		} else if (std::ranges::find(draft_.additionalAssets, path) == draft_.additionalAssets.end()) {
			auto assets = draft_.additionalAssets;
			assets.push_back(path);
			SaveAdditionalAssets(std::move(assets));
		}
	}
}

void EditorGameBuildSession::Start(const EditorPanelContext& context, bool confirmWarnings, bool useSavedFiles) {

	if (waitingForSceneSave_) { return; }
	if (confirmWarnings) { savedConfirmedWarnings_ = gameBuildService_.GetWarnings(); }
	if (context.editorContext && context.editorContext->hasDirtyScenes && !useSavedFiles && !sceneSaveReady_) {
		sceneSaveChoice_ = true;
		return;
	}
	sceneSaveChoice_ = false;
	sceneSaveReady_ = false;
	buildError_.clear();
	GameBuildSettings settings{};
	settings.startupScene = ResolveBuildScene();
	settings.executableName = draft_.executableName;
	settings.outputRoot = Algorithm::PathFromUTF8(draft_.outputPath);
	settings.gameWidth = static_cast<uint32_t>(draft_.gameWidth);
	settings.gameHeight = static_cast<uint32_t>(draft_.gameHeight);
	settings.startupFullscreen = draft_.startupFullscreen;
	settings.confirmedWarnings = std::move(savedConfirmedWarnings_);
	pendingSettings_ = std::move(settings);
	preparationShown_ = false;
}

void EditorGameBuildSession::ResetStatus() {

	if (waitingForSceneSave_ || IsPreparingBuild() || gameBuildService_.IsBuilding()) { return; }
	// 次のビルドへ保存済み通知や継続確認を持ち越さない
	sceneSaveReady_ = false;
	sceneSaveRequested_ = false;
	sceneSaveChoice_ = false;
	savedConfirmedWarnings_.clear();
	buildError_.clear();
	gameBuildService_.ResetStatus();
}

void EditorGameBuildSession::RequestAdditionalAsset(bool directory) {

	const auto root = RuntimePaths::GetGameRoot() / "GameAssets";
	if (directory) assetDialog_.Open(root);
	else assetDialog_.OpenFile(root);
}

void EditorGameBuildSession::RemoveAdditionalAsset(size_t index) {

	auto assets = draft_.additionalAssets;
	assets.erase(assets.begin() + index);
	SaveAdditionalAssets(std::move(assets));
}

void EditorGameBuildSession::SaveAdditionalAssets(std::vector<std::string> assets) {

	const auto path = RuntimePaths::GetProjectSettingsPath(ConfigPaths::kGameBuild);
	nlohmann::json data = JsonAdapter::Load(path, false);
	if (!data.is_object()) {
		if (std::filesystem::exists(path)) {
			buildError_ = "ゲームのビルド設定を読み込めません";
			return;
		}
		data = nlohmann::json::object();
	}
	data["additionalAssets"] = assets;
	if (JsonAdapter::SaveCanonical(path, data)) {
		draft_.additionalAssets = std::move(assets);
		buildError_.clear();
	} else {
		buildError_ = "追加アセットの設定を保存できません";
	}
}

void EditorGameBuildSession::RequestDirectory() {

	buildDirectoryDialog_.Open(Algorithm::PathFromUTF8(draft_.outputPath));
}

void EditorGameBuildSession::RequestSceneSave() {

	sceneSaveChoice_ = false;
	sceneSaveRequested_ = true;
	waitingForSceneSave_ = true;
}

bool EditorGameBuildSession::ConsumeSceneSaveRequest() {

	return std::exchange(sceneSaveRequested_, false);
}

void EditorGameBuildSession::CompleteSceneSave(bool success) {

	waitingForSceneSave_ = false;
	sceneSaveReady_ = success;
	if (!success) { buildError_ = "Sceneの保存が完了しなかったためビルドを中止しました"; }
}

void EditorGameBuildSession::AdvanceBuildRequest(const EditorPanelContext& context) {

	if (sceneSaveReady_) { Start(context); }
	if (!pendingSettings_) { return; }
	// 収集開始前に準備中の表示を1フレーム描画する
	if (!preparationShown_) {
		preparationShown_ = true;
		return;
	}
	GameBuildSettings settings = std::move(*pendingSettings_);
	pendingSettings_.reset();
	if (context.editorContext && context.editorContext->assetDatabase &&
		!PublishMaterialPaintMasks(*context.editorContext->assetDatabase, true)) {
		buildError_ = "ペイントマスクを保存できなかったため、ビルドを開始できません";
		return;
	}
	if (!context.editorContext || !context.editorContext->assetDatabase ||
		!gameBuildService_.Start(settings, *context.editorContext->assetDatabase, buildError_,
			context.editorContext->sceneStorage.get())) {

		if (buildError_.empty()) {
			buildError_ = "製品ビルドを開始できませんでした";
		}
	}
}

void Engine::EditorGameBuildSession::SetGameSize(int32_t width, int32_t height) {

	const Vector2I currentSize = EngineContext::GetWindowSetting().gameSize;
	if (currentSize.x == width && currentSize.y == height) {
		return;
	}
	const Vector2I maximum = WinApp::GetMaximumClientSize();
	draft_.gameWidth = std::clamp(width, 1, (std::max)(maximum.x, 1));
	draft_.gameHeight = std::clamp(height, 1, (std::max)(maximum.y, 1));
	if (currentSize.x == draft_.gameWidth && currentSize.y == draft_.gameHeight) {
		return;
	}

	// 他の製品設定を保ったまま画像サイズだけを保存する
	const std::filesystem::path path = RuntimePaths::GetProjectSettingsPath(ConfigPaths::kGameBuild);
	nlohmann::json data = JsonAdapter::Load(path, false);
	if (!data.is_object()) {
		data = nlohmann::json::object();
	}
	data["gameWidth"] = draft_.gameWidth;
	data["gameHeight"] = draft_.gameHeight;
	if (!JsonAdapter::Save(path, data)) {
		buildError_ = "ゲーム画像サイズを保存できませんでした";
		draft_.gameWidth = currentSize.x;
		draft_.gameHeight = currentSize.y;
		return;
	}
	EngineContext::SetGameSize({ draft_.gameWidth, draft_.gameHeight });
}

bool EditorGameBuildSession::ConsumeOpenPopup() {

	const bool requested = requestOpenBuildPopup_;
	requestOpenBuildPopup_ = false;
	return requested;
}
