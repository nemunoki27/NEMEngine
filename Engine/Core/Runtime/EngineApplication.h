#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Asset/AssetDatabase.h>
#include <Engine/Core/Graphics/GraphicsCore.h>
#include <Engine/Core/Graphics/Render/RenderPipelineRunner.h>
#include <Engine/Core/Graphics/Render/View/RenderFrameTypes.h>
#include <Engine/Core/Graphics/Mesh/Animation/SkinnedMeshAnimationManager.h>
#include <Engine/Core/Scene/SceneSystem.h>
#include <Engine/Core/Scene/Instance/SceneInstanceManager.h>
#include <Engine/Core/ECS/World/WorldManager.h>
#include <Engine/Core/ECS/System/Scheduler/SystemScheduler.h>
#include <Engine/Core/ECS/System/Context/SystemContext.h>
#include <Engine/Editor/EditorManager.h>
#include <Engine/Editor/EditorContext.h>

namespace Engine {

	//============================================================================
	//	EngineApplication class
	//	エンジンのコア部分を処理するクラス
	//============================================================================
	class EngineApplication {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		EngineApplication() = default;
		~EngineApplication() = default;

		// 初期化
		void Init(GraphicsCore& graphicsCore);

		// フレーム更新
		void Tick(GraphicsCore& graphicsCore, float deltaTime);

		// 描画
		void Render(GraphicsCore& graphicsCore);

		// 終了処理
		void Finalize();
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		// 現在アクティブなシーンのパス
		std::string activeScenePath_ = "Assets/Scenes/sampleScene.scene.json";
		AssetID activeScene_{};

		// アセット管理
		AssetDatabase assetDataBase_;

		// エディタとプレイのシーンインスタンス
		SceneInstanceManager editScenes_;
		SceneInstanceManager playScenes_;
		// シーン管理
		SceneSystem sceneSystem_;

		// ワールド管理
		WorldManager worldManager_;
		SystemScheduler scheduler_;
		SystemContext systemContext_;

		// 描画パイプラインの実行管理
		std::unique_ptr<RenderPipelineRunner> renderPipeline_;

		// スキンメッシュアニメーション管理
		SkinnedMeshAnimationManager skinnedAnimationManager_{};

		// エディタ管理
		EditorManager editorManager_;
		EditorContext editorContext_{};

		//--------- functions ----------------------------------------------------

		// システムの初期化
		void InitSystems();
		// 最初のシーンを作成
		void InitFirstScene();

		// プレイモードの切り替え
		void HandlePlayToggle();

		// アクティブなワールドとシーンの取得
		ECSWorld* GetActiveWorld() { return worldManager_.IsPlaying() ? worldManager_.GetPlayWorld() : &worldManager_.GetEditWorld(); }
		SceneInstanceManager& GetActiveScenes() { return worldManager_.IsPlaying() ? playScenes_ : editScenes_; }
		const SceneHeader* GetActiveSceneHeader() const;

		// エディタ状態を描画要求へ変換する
		RenderFrameRequest BuildRenderFrameRequest(GraphicsCore& graphicsCore, ECSWorld* world, const SceneHeader* header);
	};
} // Engine