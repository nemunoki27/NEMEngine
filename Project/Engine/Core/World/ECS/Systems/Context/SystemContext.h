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
	class AnimationClipManager;
	class ECSWorld;
	class RuntimeWorldBaker;
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

		// 現在tick中のactive worldでscripting callbackがparent無し生成等で参照する非所有ポインタ
		ECSWorld* world = nullptr;

		// エンジンのコア機能
		EngineContext* engineContext = nullptr;
		GraphicsPlatform* graphicsPlatform = nullptr;
		AssetDatabase* assetDatabase = nullptr;
		SkinnedMeshAnimationManager* skinnedAnimationManager = nullptr;
		AnimationClipManager* animationClipManager = nullptr;
		RuntimeWorldBaker* runtimeWorldBaker = nullptr;
		const SceneHeader* activeSceneHeader = nullptr;

		// ワールドの現在のモード
		WorldMode mode = WorldMode::Edit;

		// フレーム計測時間
		float deltaTime = 0.0f;
		float fixedDeltaTime = 1.0f / 60.0f;
		// TimeScale非適用のリアルフレーム時間でEdit中もPlay同様に進む、Editプレビュー再生などが参照する
		float unscaledDeltaTime = 0.0f;
	};
} // Engine
