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
#include <Engine/Core/World/Scene/Runtime/SceneSystem.h>
#include <Engine/Core/World/Scene/Runtime/SceneInstanceManager.h>
#include <Engine/Core/World/ECS/World/WorldManager.h>
#include <Engine/Core/World/ECS/Systems/Scheduler/SystemScheduler.h>
#include <Engine/Core/World/ECS/Systems/Context/SystemContext.h>
#include <Engine/Core/Scripting/Managed/ManagedScriptBuildService.h>
#include <Engine/Editor/Core/EditorManager.h>
#include <Engine/Editor/Core/EditorContext.h>
#include <Engine/Core/World/Prefab/Override/PrefabOverrideUtility.h>

namespace Engine {

	// front
	struct PrefabInstantiateResult;

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
		// アセットの外部編集を非同期監視してホットリロードを発火する
		AssetWatchService assetWatchService_;

		// エディタとプレイのシーンインスタンス
		SceneInstanceManager editScenes_;
		SceneInstanceManager playScenes_;
		// シーン管理
		SceneSystem sceneSystem_;

		// ワールド管理
		WorldManager worldManager_;
		SystemScheduler scheduler_;
		SystemContext systemContext_;

		// プレファブ編集の1階層分、隔離ワールドにプレファブだけを展開して編集する
		struct PrefabEditStage {

			// 編集中の.prefabアセット
			AssetID asset{};
			// プレファブ専用の隔離ワールド
			std::unique_ptr<ECSWorld> world;
			// プレファブワールドのシーン管理、シーンを持たないので描画フィルタは無効になる
			SceneInstanceManager scenes;
			// プレファブのルートエンティティ
			Entity root = Entity::Null();
			// 表示名、ヒエラルキーのバナーに使う
			std::string name;
			// 遷移元から複製した環境エンティティ(カメラ/平行光源)、プレファブの一部ではなく保存対象外
			std::vector<Entity> environmentEntities;
			// 編集開始時点のプレファブベース、退出時のインスタンスへのオーバーライド伝播に使う
			std::unordered_map<UUID, PrefabBaseEntity> baseAtEnter;

			// In-Context編集中か、trueなら隔離ワールドではなくhostWorldに置いて編集する
			bool inContext = false;
			// In-Context編集の置き場、遷移元ワールドで非所有
			ECSWorld* hostWorld = nullptr;
			// In-Context編集での所属シーンインスタンスID
			UUID hostSceneInstanceID{};
			// プレファブを束ねるインスタンスID、ヒエラルキー絞り込みに使う
			UUID instanceID{};
		};
		// プレファブ編集スタック、ネスト中は末尾が現在の編集対象
		std::vector<PrefabEditStage> prefabStages_;

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
		// Play開始直後の最初の1フレームはビルド待ちで大きくなったdeltaによる貫通を防ぐため進めない
		bool playWorldJustStarted_ = false;
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

		// プレファブ編集中か、ネスト含めいずれかのステージがあればtrue
		bool IsPrefabEditing() const { return !prefabStages_.empty(); }

		// アクティブなワールドとシーンの取得、優先度はPlay > プレファブ編集 > Edit
		// In-Context編集中は隔離ワールドではなく遷移元のhostWorldを使う
		ECSWorld* GetActiveWorld() {
			if (worldManager_.IsPlaying()) { return worldManager_.GetPlayWorld(); }
			if (!prefabStages_.empty()) {
				PrefabEditStage& top = prefabStages_.back();
				return top.inContext ? top.hostWorld : top.world.get();
			}
			return &worldManager_.GetEditWorld();
		}
		SceneInstanceManager& GetActiveScenes() {
			if (worldManager_.IsPlaying()) { return playScenes_; }
			if (!prefabStages_.empty()) {
				PrefabEditStage& top = prefabStages_.back();
				if (top.inContext) { return ResolveHostScenes(top); }
				return top.scenes;
			}
			return editScenes_;
		}
		// In-Context編集のhostWorldが属するシーン管理を引く
		SceneInstanceManager& ResolveHostScenes(PrefabEditStage& stage) {
			if (stage.hostWorld == &worldManager_.GetEditWorld()) { return editScenes_; }
			for (size_t i = prefabStages_.size(); i-- > 0; ) {
				if (prefabStages_[i].world.get() == stage.hostWorld) { return prefabStages_[i].scenes; }
			}
			return editScenes_;
		}
		const SceneHeader* GetActiveSceneHeader();

		// プレファブ編集の開始/終了/保存、隔離ワールドへの展開と.prefab保存を行う
		void EnterPrefabEdit(AssetID prefabAsset);
		void ExitPrefabEdit();
		// プレファブ編集を一括で抜けて元のシーン編集へ戻る、各階層を保存しながら戻る
		void ExitAllPrefabEdit();
		// In-Context編集のオンオフを切り替える、現在の編集内容を保存してから置き場を変える
		void TogglePrefabInContextMode();
		void SaveCurrentPrefab();
		// プレファブを指定ワールドへ編集用に展開する、localFileIDは恒等で保存往復が壊れないようにする
		bool MaterializePrefabForEdit(ECSWorld& world, AssetID prefabAsset, UUID sceneInstanceID,
			UUID instanceID, PrefabInstantiateResult& outResult);
		// 遷移元の3Dカメラと平行光源を編集ワールドへ環境として複製する
		void CopyPrefabEditEnvironment(ECSWorld& targetWorld, ECSWorld* sourceWorld, UUID sceneInstanceID,
			std::vector<Entity>& outEnvironmentEntities);
		// プレファブ編集中に新規作成されたエンティティを、プレファブの一部(rootの子+PrefabLink)へ取り込む
		void SyncPrefabEditedEntities();
		// 編集後のプレファブを、指定ワールドの該当インスタンスへ伝播する、オーバーライドは保持する
		void PropagatePrefabToInstances(ECSWorld& world, AssetID prefabAsset,
			const std::unordered_map<UUID, PrefabBaseEntity>& oldBase);

		// エディタ状態を描画要求へ変換する
		RenderFrameRequest BuildRenderFrameRequest(GraphicsCore& graphicsCore, ECSWorld* world, const SceneHeader* header);
	};
} // Engine

