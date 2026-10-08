#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/Database/AssetMetadata.h>

// c++
#include <string>

namespace Engine {

	class AssetDatabase;
	class SceneInstanceManager;
	class SceneSystem;
	class WorldManager;
	class SystemScheduler;
	struct SystemContext;
	class EditorManager;

	// シーン操作中だけ借用する編集状態
	struct EditorSceneOperationContext {

		AssetDatabase& assetDatabase;
		SceneInstanceManager& editScenes;
		SceneSystem& sceneSystem;
		WorldManager& worldManager;
		SystemScheduler& scheduler;
		SystemContext& systemContext;
		EditorManager& editorManager;
		AssetID& activeScene;
		std::string& activeScenePath;
		bool& requestFrameDeltaReset;
	};

	//============================================================================
	//	EditorSceneOperations class
	//	編集シーンの作成と切り替えを適用する
	//============================================================================
	class EditorSceneOperations {
	public:

		// 新規アセットを登録して編集シーンを開く
		static bool CreateNewEditScene(EditorSceneOperationContext& context);
		// 対象を検証して編集シーンを切り替える
		static bool OpenEditScene(EditorSceneOperationContext& context, AssetID sceneAsset);
	};
}
