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

namespace Engine {

	// front
	class AssetDatabase;
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

		// シーンのパス
		std::string activeScenePath;
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
		// managed scriptingのEditor向けサービス境界でread-only snapshot + request interface
		// panelはEngineApplicationのprivate memberやLoggerを直接見ず、これ経由で観測・操作する
		ManagedScriptBuildService* scriptBuildService = nullptr;
	};
} // Engine
