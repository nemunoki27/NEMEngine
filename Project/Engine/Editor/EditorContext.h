#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Scene/Header/SceneHeader.h>
#include <Engine/Core/ECS/World/ECSWorld.h>
#include <Engine/Core/Asset/AssetTypes.h>
#include <Engine/Core/UUID/UUID.h>

// c++
#include <string>

namespace Engine {

	// front
	class AssetDatabase;

	//============================================================================
	//	EditorContext struct
	//============================================================================

	// エディタの現在の状態
	struct EditorContext {

		// 現在プレイモードかどうか
		bool isPlaying = false;

		// シーンのパス
		std::string activeScenePath;
		// シーンのヘッダ情報
		const SceneHeader* activeSceneHeader = nullptr;
		// 現在アクティブなシーンのランタイム情報
		AssetID activeSceneAsset{};
		UUID activeSceneInstanceID{};
		// ECSワールド
		ECSWorld* activeWorld = nullptr;
		// アセットデータベース
		AssetDatabase* assetDatabase = nullptr;
	};
} // Engine