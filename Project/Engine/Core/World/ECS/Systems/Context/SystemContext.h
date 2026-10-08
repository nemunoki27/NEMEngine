#pragma once

//============================================================================
//	include
//============================================================================
#include <functional>
#include <memory>

namespace Engine {

	// front
	class EngineContext;
	class GraphicsPlatform;
	class AssetDatabase;
	class SkinnedMeshAnimationManager;
	class AnimationClipManager;
	class AnimationControllerManager;
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

		// 更新中のWorldを所有せず参照する
		ECSWorld* world = nullptr;

		// Applicationが所有する共通機能を参照する
		EngineContext* engineContext = nullptr;
		GraphicsPlatform* graphicsPlatform = nullptr;
		AssetDatabase* assetDatabase = nullptr;
		SkinnedMeshAnimationManager* skinnedAnimationManager = nullptr;
		AnimationClipManager* animationClipManager = nullptr;
		AnimationControllerManager* animationControllerManager = nullptr;
		RuntimeWorldBaker* runtimeWorldBaker = nullptr;

		// ワールドの現在のモード
		WorldMode mode = WorldMode::Edit;

		// フレーム計測時間
		float deltaTime = 0.0f;
		float fixedDeltaTime = 1.0f / 60.0f;
		// TimeScaleを適用しない編集と実行の経過時間
		float unscaledDeltaTime = 0.0f;
		// ShaderGraphなどフレーム共通処理が参照する経過時間
		float time = 0.0f;
		float unscaledTime = 0.0f;
		float smoothDeltaTime = 0.0f;

		// 更新の中断条件は呼出し元が所有する
		std::function<bool()> updateInterruption;

		// callback終了後の中断要求を取得する
		bool IsUpdateInterrupted() const { return updateInterruption && updateInterruption(); }

		// 有効なScene情報をコピーして保持する
		void SetActiveSceneHeader(const SceneHeader* header);
		// 保持したScene情報を参照する
		const SceneHeader* GetActiveSceneHeader() const { return activeSceneHeader_.get(); }
		// Context更新後も同じScene情報を保持する
		std::shared_ptr<const SceneHeader> GetActiveSceneHeaderSnapshot() const { return activeSceneHeader_; }

	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		// Sceneの削除や一覧の再確保に依存しない読み取り専用情報
		std::shared_ptr<const SceneHeader> activeSceneHeader_;
	};
} // Engine
