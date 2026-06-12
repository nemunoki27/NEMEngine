#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/Renderer/Pipeline/RenderPipelineRunner.h>
#include <Engine/Core/Rendering/Renderer/Views/RenderFrameTypes.h>
#include <Engine/Core/Rendering/Meshes/Animation/SkinnedMeshAnimationManager.h>
#include <Engine/Core/World/Scene/Runtime/SceneSystem.h>
#include <Engine/Core/World/Scene/Runtime/SceneInstanceManager.h>
#include <Engine/Core/World/ECS/World/WorldManager.h>
#include <Engine/Core/World/ECS/Systems/Scheduler/SystemScheduler.h>
#include <Engine/Core/World/ECS/Systems/Context/SystemContext.h>
#include <Engine/Core/Scripting/Managed/ManagedScriptBuildService.h>
#include <Engine/Editor/Core/EditorManager.h>
#include <Engine/Editor/Core/EditorContext.h>

namespace Engine {

	//============================================================================
	//	EngineApplication class
	//	エンジンのコア部分を処理するクラス
	//============================================================================
	class EngineApplication {
	public:
		//============================================================================
		//	public Methods
		//============================================================================
		EngineApplication() = default;
		~EngineApplication() = default;

		// 初期化
		void Init(GraphicsCore& graphicsCore);

		// フレーム更新
		void Tick(GraphicsCore& graphicsCore, float deltaTime);
		// 重いモード切り替え後にフレームタイマー基準をリセットする要求を取得
		bool ConsumeFrameDeltaResetRequest();

		// 描画
		void Render(GraphicsCore& graphicsCore);

		// 終了処理
		void Finalize();
		// ウィンドウ終了要求でfalseを返すと終了をキャンセルする
		bool RequestClose();
		// Assert停止前に必要な保存処理を行う
		void NotifyAssertBeforeAbort();
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- variables ----------------------------------------------------

		// 現在アクティブなシーンで初期シーンもパスではなく.meta GUIDで参照する
		std::string activeScenePath_{};
		AssetID activeScene_{ 0x129d80fee6b506d1ull };

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
		// Editモードの非同期build/reloadを管理する
		ManagedScriptBuildService scriptBuildService_;
		// Play開始要求をbuild/reload完了まで保留しているか
		bool pendingPlayStart_ = false;

		bool playPaused_ = false;
		bool playFrameStepRequested_ = false;
		bool requestFrameDeltaReset_ = false;
		bool shutdownAccepted_ = false;
		bool closeRequestPending_ = false;
		bool handlingAssertAbort_ = false;

		//--------- functions ----------------------------------------------------

		// システムの初期化
		void InitSystems();
		// 最初のシーンを作成
		void InitFirstScene();
		// 前回開いていたシーン設定を読み込む
		void LoadActiveSceneConfig();
		// 現在開いているシーン設定を保存する
		void SaveActiveSceneConfig() const;

		// プレイモードの切り替え
		void HandlePlayToggle();
		// 保留中のPlay開始要求を、build/reload完了に応じて進める
		void ProcessPendingPlayStart();
		// PlayWorldを作成してプレイを開始する
		void StartPlayWorld();
		// Play中の一時停止/再開/コマ送り要求を処理する
		void HandlePlayPauseRequests();
		// このフレームにWorldを進行させるか
		bool ShouldAdvanceActiveWorld() const;
		// エディタから要求されたシーン操作を処理する
		void HandleEditorSceneRequests();
		// 新規シーンを作成して開く
		bool CreateNewEditScene();
		// 指定シーンをエディタワールドで開く
		bool OpenEditScene(AssetID sceneAsset);
		// エディタワールドのアクティブシーンを保存する
		bool SaveActiveEditScene();
		// 終了前の未保存確認結果を処理する
		void HandleCloseRequestResult();
		// 終了を確定して、必要ならウィンドウ破棄まで進める
		void AcceptCloseRequest(bool destroyWindow);

		// アクティブなワールドとシーンの取得
		ECSWorld* GetActiveWorld() { return worldManager_.IsPlaying() ? worldManager_.GetPlayWorld() : &worldManager_.GetEditWorld(); }
		SceneInstanceManager& GetActiveScenes() { return worldManager_.IsPlaying() ? playScenes_ : editScenes_; }
		const SceneHeader* GetActiveSceneHeader() const;

		// エディタ状態を描画要求へ変換する
		RenderFrameRequest BuildRenderFrameRequest(GraphicsCore& graphicsCore, ECSWorld* world, const SceneHeader* header);
	};
} // Engine

