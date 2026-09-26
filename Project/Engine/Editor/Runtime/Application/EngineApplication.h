#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Assets/Watch/AssetWatchService.h>
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/Renderer/Pipeline/RenderPipelineRunner.h>
#include <Engine/Core/Rendering/Renderer/Views/RenderFrameTypes.h>
#include <Engine/Core/Rendering/Meshes/Animation/SkinnedMeshAnimationManager.h>
#include <Engine/Core/Animation/Clips/AnimationClipManager.h>
#include <Engine/Core/World/Scene/Runtime/SceneSystem.h>
#include <Engine/Core/World/Scene/Runtime/SceneInstanceManager.h>
#include <Engine/Core/World/ECS/World/WorldManager.h>
#include <Engine/Core/World/ECS/Systems/Scheduler/SystemScheduler.h>
#include <Engine/Core/World/ECS/Baking/RuntimeWorldBaker.h>
#include <Engine/Core/World/ECS/Systems/Context/SystemContext.h>
#include <Engine/Core/Scripting/Managed/ManagedScriptBuildService.h>
#include <Engine/Core/Runtime/Framework/EngineFramework.h>
#include <Engine/Editor/Core/EditorManager.h>
#include <Engine/Editor/Core/EditorContext.h>

namespace Engine {

	class EditorRenderRequestBuilder;
	class EditorPlaySession;

	class SceneSaveController;
	class PrefabEditSession;

	class UIInputSystem;

	//============================================================================
	//	EngineApplication class
	//	エンジンのコア部分を処理するクラス
	//============================================================================
	class EngineApplication final : public IEngineApplication {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		EngineApplication();
		~EngineApplication() override;

		// 初期化
		void Init(GraphicsCore& graphicsCore) override;

		// フレーム更新
		void Tick(GraphicsCore& graphicsCore, float deltaTime) override;
		// 重いモード切り替え後にフレームタイマー基準をリセットする要求を取得
		bool ConsumeFrameDeltaResetRequest() override;
		bool UsesEditorUI() const override { return true; }

		// 描画
		void Render(GraphicsCore& graphicsCore) override;
		void RenderPlatformWindows(GraphicsCore& graphicsCore) override;

		// 終了処理
		void Finalize() override;
		// ウィンドウ終了要求でfalseを返すと終了をキャンセルする
		bool RequestClose();
		// Assert停止前に必要な保存処理を行う
		void NotifyAssertBeforeAbort();
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- variables ----------------------------------------------------

		// 初期化途中の終了で未作成のサービスを呼ばない
		bool initializationComplete_ = false;
		bool managedStarted_ = false;
		bool debugDrawingStarted_ = false;

		// 現在アクティブなシーンで初期シーンもパスではなく.meta GUIDで参照する
		std::string activeScenePath_{};
		AssetID activeScene_{};

		// アセット管理
		AssetDatabase assetDatabase_;
		// アセットの外部編集を非同期監視してホットリロードを発火する
		AssetWatchService assetWatchService_;

		// エディタとプレイのシーンインスタンス
		SceneInstanceManager editScenes_;
		SceneInstanceManager playScenes_;
		// シーン管理
		SceneSystem sceneSystem_;

		// ワールド管理
		WorldManager worldManager_;
		RuntimeWorldBaker runtimeWorldBaker_;
		SystemScheduler scheduler_;
		SystemContext systemContext_;
		// Schedulerが所有するUI入力Systemへの非所有参照
		UIInputSystem* uiInputSystem_ = nullptr;

		// プレファブ編集ワールドと階層の所有
		std::unique_ptr<PrefabEditSession> prefabSession_;

		// 描画パイプラインの実行管理
		std::unique_ptr<RenderPipelineRunner> renderPipeline_;
		std::unique_ptr<EditorRenderRequestBuilder> renderRequestBuilder_;

		// スキンメッシュアニメーション管理
		SkinnedMeshAnimationManager skinnedAnimationManager_{};

		// AnimationClipアセットのパースキャッシュ
		AnimationClipManager animationClipManager_{};

		// エディタ管理
		EditorManager editorManager_;
		EditorContext editorContext_{};
		// Editモードの非同期build/reloadを管理する
		ManagedScriptBuildService scriptBuildService_;

		bool requestFrameDeltaReset_ = false;
		bool shutdownAccepted_ = false;
		bool closeRequestPending_ = false;
		bool handlingAssertAbort_ = false;

		std::unique_ptr<EditorPlaySession> playSession_;
		std::unique_ptr<SceneSaveController> sceneSaveController_;

		//--------- functions ----------------------------------------------------

		// プレファブ編集の開始/終了/保存、隔離ワールドへの展開と.prefab保存を行う
		void EnterPrefabEdit(AssetID prefabAsset);

		// プレファブ編集を保存して一階層戻る
		bool ExitPrefabEdit();

		// プレファブ編集を一括で抜けて元のシーン編集へ戻る、各階層を保存しながら戻る
		void ExitAllPrefabEdit();

		// In-Context編集のオンオフを切り替える、現在の編集内容を保存してから置き場を変える
		void TogglePrefabInContextMode();

		// 現在のプレファブを保存する
		bool SaveCurrentPrefab();

		// 新規エンティティを編集中のプレファブへ取り込む
		void SyncPrefabEditedEntities();

		// In-Context編集中は隔離ワールドではなく遷移元のhostWorldを使う
		ECSWorld* GetActiveWorld();

		// 現在の編集対象または実行対象のシーン管理を取得する
		SceneInstanceManager& GetActiveScenes();

		// エディタワールドのアクティブシーンを保存する
		bool SaveActiveEditScene();

		// エディタワールドで読み込み中のシーンを全て保存する
		bool SaveAllEditScenes();

		// 完了した保存と再要求を処理する
		void UpdateSceneSave();

		// 保存と再要求が完了するまで待機する
		bool WaitForSceneSave();

		// プレイモードの切り替え
		void HandlePlayToggle();

		// PlayWorldを破棄してEditへ戻す、StopトグルとPlay中script例外の両方で使う
		void StopPlayWorld();

		// C#のApplication終了要求を安全なフレーム終端で処理する
		bool HandleApplicationQuitRequest();

		// PlayWorldを作成してプレイを開始する
		void StartPlayWorld();

		// Play中の一時停止/再開/コマ送り要求を処理する
		void HandlePlayPauseRequests();

		// このフレームにWorldを進行させるか
		bool ShouldAdvanceActiveWorld() const;

		// エディタ状態を描画要求へ変換する
		RenderFrameRequest BuildRenderFrameRequest(GraphicsCore& graphicsCore, ECSWorld* world, const SceneHeader* header);

		// システムの初期化
		void InitSystems();

		// Release開始前に全アセットとシーン描画リソースを同期作成する
		void PreloadReleaseResources(GraphicsCore& graphicsCore);

		// 最初のシーンを作成
		void InitFirstScene();

		// 前回開いていたシーン設定を読み込む
		void LoadActiveSceneConfig();

		// 現在開いているシーン設定を保存する
		void SaveActiveSceneConfig() const;

		// 現在のActive World/SceneをSystemContextとEditorContextへ反映する
		void RefreshActiveWorldContext();

		// エディタから要求されたシーン操作を処理する
		void HandleEditorSceneRequests();

		// 新規シーンを作成して開く
		bool CreateNewEditScene();

		// 指定シーンをエディタワールドで開く
		bool OpenEditScene(AssetID sceneAsset);

		// シーン保存前に編集モードのUI遷移表示を元へ戻す
		void RestoreEditModeUIVisuals();

		// 終了前の未保存確認結果を処理する
		void HandleCloseRequestResult();

		// 終了を確定して、必要ならウィンドウ破棄まで進める
		void AcceptCloseRequest(bool destroyWindow);

		// プレファブ編集中か、ネスト含めいずれかのステージがあればtrue
		bool IsPrefabEditing() const;

		// 現在の対象シーンのヘッダを取得する
		const SceneHeader* GetActiveSceneHeader();

	};

	// Editor実行用Applicationを生成してFrameworkを起動
	int RunEditorApplication();
} // Engine

