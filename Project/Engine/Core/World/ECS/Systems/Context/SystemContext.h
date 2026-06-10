#pragma once

//============================================================================
//	include
//============================================================================
namespace Engine {

	// front
	class EngineContext;
	class GraphicsPlatform;
	class AssetDatabase;
	class SkinnedMeshAnimationManager;
	class ECSWorld;
	struct SceneHeader;

	//============================================================================
	//	SystemContext struct
	//	システムで共有する必要な情報をまとめる
	//============================================================================
	// ワールドの現在のモード
	enum class WorldMode {

		Edit, // エディタモード
		Play  // プレイモード
	};

	// システムで共有する必要な情報をまとめる
	struct SystemContext {

		// 現在 tick 中の active world（scripting callback が parent 無し生成等で参照する。非所有）
		ECSWorld* world = nullptr;

		// エンジンのコア機能
		EngineContext* engineContext = nullptr;
		GraphicsPlatform* graphicsPlatform = nullptr;
		AssetDatabase* assetDatabase = nullptr;
		SkinnedMeshAnimationManager* skinnedAnimationManager = nullptr;
		const SceneHeader* activeSceneHeader = nullptr;

		// ワールドの現在のモード
		WorldMode mode = WorldMode::Edit;

		// フレーム計測時間
		float deltaTime = 0.0f;
		float fixedDeltaTime = 1.0f / 60.0f;
	};
} // Engine
