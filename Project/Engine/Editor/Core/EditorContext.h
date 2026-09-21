#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Scene/Serialization/SceneHeader.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/Assets/AssetTypes.h>
#include <Engine/Core/Foundation/Identity/UUID.h>

// c++
#include <string>
#include <vector>

namespace Engine {

	// front
	class AssetDatabase;
	class SceneAssetStorage;
	class SceneInstanceManager;
	class ManagedScriptBuildService;

	//============================================================================
	//	EditorContext struct
	//============================================================================
	// エディタの現在の状態
	struct EditorContext {

		// 現在プレイモードかどうか
		bool isPlaying = false;
		// Play中に一時停止しているかどうか
		bool isPlayPaused = false;

		// プレファブ編集中かどうか、trueならヒエラルキー等は隔離ワールドを指す
		bool isPrefabEditing = false;
		// 編集中プレファブの表示名、ヒエラルキーのバナーに使う
		std::string prefabEditName;
		// プレファブ編集のネスト深さ、0なら通常編集
		int prefabEditDepth = 0;
		// 編集中プレファブのアセットID
		AssetID prefabEditAsset{};
		// 編集中プレファブを構成するインスタンスID
		UUID prefabEditInstanceID{};
		// In-Context編集中かどうか、trueなら元シーンに置いて編集している
		bool isPrefabInContext = false;
		// In-Context編集で表示対象を絞り込むためのプレファブインスタンスID
		UUID prefabInContextInstanceID{};
		// 隔離プレファブ編集で隠す環境エンティティ(複製したカメラ/平行光源)、非所有でエンジンが毎フレーム設定する
		const std::vector<Entity>* prefabEnvironmentEntities = nullptr;

		// シーンのパス
		std::string activeScenePath;
		// アクティブシーンに未保存の変更があるか
		bool activeSceneDirty = false;
		// シーンのヘッダ情報
		const SceneHeader* activeSceneHeader = nullptr;
		// 現在アクティブなシーンのランタイム情報
		AssetID activeSceneAsset{};
		UUID activeSceneInstanceID{};
		SceneInstanceManager* sceneInstances = nullptr;
		// ECSワールドでactiveはPlay中はPlayWorld
		ECSWorld* activeWorld = nullptr;
		// 常にauthoringのEditWorldでPlay中でも有効、Apply Runtime Values To Authoring等で使う
		ECSWorld* editWorld = nullptr;
		// アセットデータベース
		AssetDatabase* assetDatabase = nullptr;
		// 編集セッションが所有するScene保存状態
		std::shared_ptr<SceneAssetStorage> sceneStorage;
		// managed scriptingのEditor向けサービス境界でread-only snapshot + request interface
		// panelはEngineApplicationのprivate memberやLoggerを直接見ず、これ経由で観測・操作する
		ManagedScriptBuildService* scriptBuildService = nullptr;
	};
} // Engine
