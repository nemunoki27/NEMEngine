#include "EditorSceneOperations.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/World/Scene/Runtime/SceneInstanceManager.h>
#include <Engine/Core/World/Scene/Runtime/SceneSystem.h>
#include <Engine/Core/World/ECS/World/WorldManager.h>
#include <Engine/Core/World/ECS/Systems/Scheduler/SystemScheduler.h>
#include <Engine/Core/World/ECS/Systems/Context/SystemContext.h>
#include <Engine/Editor/Core/EditorManager.h>
#include <Engine/Editor/Assets/Project/ProjectAssetFileUtility.h>
#include <Engine/Core/Foundation/Build/BuildConfig.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>

using namespace Engine;

bool EditorSceneOperations::CreateNewEditScene(EditorSceneOperationContext& context) {

	// GameAssets/Scenes配下に重複しないシーンファイルを作成する
	ProjectAssetFileResult result = ProjectAssetFileUtility::Create(
		ProjectAssetSource::Game,
		"GameAssets/Scenes",
		ProjectAssetFileKind::Scene,
		"NewScene");
	if (!result.success) {
		Logger::Output(LogType::Engine, spdlog::level::err,
			"EngineApplication: 新規シーンの作成に失敗しました 内容={}", result.message);
		return false;
	}

	// 作成したシーンをAssetDatabaseへ登録し、開く処理へ渡す
	const AssetID sceneAsset = context.assetDatabase.ImportOrGet(result.assetPath, AssetType::Scene);
	context.assetDatabase.RebuildMeta();
	return OpenEditScene(context, sceneAsset);
}

bool EditorSceneOperations::OpenEditScene(EditorSceneOperationContext& context, AssetID sceneAsset) {

	// AssetDatabase上のメタ情報を取得し見つからなければ再走査する
	const AssetMeta* meta = context.assetDatabase.Find(sceneAsset);
	if (!meta) {

		context.assetDatabase.RebuildMeta();
		meta = context.assetDatabase.Find(sceneAsset);
	}
	if (!meta || meta->type != AssetType::Scene) {
		Logger::Output(LogType::Engine, spdlog::level::warn,
			"EngineApplication: 指定Assetはシーンではありません");
		return false;
	}

	// 実ファイルが存在するシーンだけ開く
	const std::filesystem::path fullPath = context.assetDatabase.ResolveFullPath(sceneAsset);
	if (fullPath.empty() || !std::filesystem::exists(fullPath)) {
		Logger::Output(LogType::Engine, spdlog::level::warn,
			"EngineApplication: シーンファイルが見つかりません path={}", meta->assetPath);
		return false;
	}

	// 既存のEditWorldを空にしてから、新しいシーンツリーをロードする
	context.scheduler.DetachCurrentWorld(context.systemContext);
	context.editScenes.UnloadAll(context.worldManager.GetEditWorld());

	// アクティブシーン情報を先に差し替える
	context.activeScene = sceneAsset;
	context.activeScenePath = meta->assetPath;

	// SceneSystemを通してEntity/Componentを復元する
	if (!context.editScenes.LoadSceneTree(context.assetDatabase, context.sceneSystem, context.worldManager.GetEditWorld(), context.activeScene)) {
		Logger::Output(LogType::Engine, spdlog::level::err,
			"EngineApplication: シーンを開けません path={}", context.activeScenePath);
		return false;
	}

	// シーン切り替え直後の大きな処理でdeltaTimeが跳ねないようにする
	context.requestFrameDeltaReset = true;
	if constexpr (BuildConfig::kEditorEnabled) {

		// 選択状態やUndo履歴は新しいシーンへ持ち越さない
		context.editorManager.ResetSceneEditingState();
		context.editorManager.ResetSceneDirtyState();
	}
	Logger::Output(LogType::Engine, spdlog::level::info,
		"EngineApplication: シーンを開きました path={}", context.activeScenePath);
	return true;
}
