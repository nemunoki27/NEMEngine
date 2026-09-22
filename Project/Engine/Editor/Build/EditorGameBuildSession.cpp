#include "EditorGameBuildSession.h"

#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>

#include <algorithm>

using namespace Engine;

void Engine::EditorGameBuildSession::Prepare(const EditorPanelContext& context) {

	buildError_.clear();
	gameBuildService_.ResetStatus();
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
}

void EditorGameBuildSession::Start(const EditorPanelContext& context) {

	buildError_.clear();
	GameBuildSettings settings{};
	settings.startupScene = ResolveBuildScene();
	settings.executableName = draft_.executableName;
	settings.outputRoot = Algorithm::PathFromUTF8(draft_.outputPath);
	settings.startupFullscreen = draft_.startupFullscreen;
	if (!context.editorContext || !context.editorContext->assetDatabase ||
		!gameBuildService_.Start(settings, *context.editorContext->assetDatabase, buildError_,
			context.editorContext->sceneStorage.get())) {

		if (buildError_.empty()) {
			buildError_ = "製品ビルドを開始できませんでした";
		}
	}
}

void EditorGameBuildSession::ResetStatus() {

	buildError_.clear();
	gameBuildService_.ResetStatus();
}

void EditorGameBuildSession::RequestDirectory() {

	buildDirectoryDialog_.Open(Algorithm::PathFromUTF8(draft_.outputPath));
}

bool EditorGameBuildSession::ConsumeOpenPopup() {

	const bool requested = requestOpenBuildPopup_;
	requestOpenBuildPopup_ = false;
	return requested;
}
