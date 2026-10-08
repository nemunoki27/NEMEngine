#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/World/Scene/Runtime/SceneInstanceManager.h>
#include <Engine/Core/World/Prefab/Override/PrefabOverrideUtility.h>

// c++
#include <memory>
#include <string>
#include <vector>
#include <unordered_map>

namespace Engine {

	class AssetDatabase;
	class WorldManager;
	class SystemScheduler;
	struct SystemContext;
	class EditorManager;
	struct PrefabInstantiateResult;
	struct EditorContext;

	//============================================================================
	//	PrefabEditSession class
	//	プレファブ編集の階層と実体の寿命を管理する
	//============================================================================
	class PrefabEditSession {
	public:
		//========================================================================
		//	public Methods
		//========================================================================


		PrefabEditSession(AssetDatabase& assetDatabase, WorldManager& worldManager, SceneInstanceManager& editScenes,
			SceneInstanceManager& playScenes, SystemScheduler& scheduler, SystemContext& systemContext,
			EditorManager& editorManager);

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
		// 現在の対象ワールドのシーン管理を取得する
		SceneInstanceManager& GetActiveScenes();
		// 編集状態を表示へ渡す
		void ApplyEditorContext(EditorContext& context) const;
		bool IsEditing() const { return !prefabStages_.empty(); }
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- structure ----------------------------------------------------

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
			// 遷移元から複製した環境エンティティであるカメラと平行光源、プレファブの一部ではなく保存対象外
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

		//--------- variables ----------------------------------------------------

		AssetDatabase& assetDatabase_;
		WorldManager& worldManager_;
		SceneInstanceManager& editScenes_;
		SceneInstanceManager& playScenes_;
		SystemScheduler& scheduler_;
		SystemContext& systemContext_;
		EditorManager& editorManager_;
		std::vector<PrefabEditStage> prefabStages_;

		//--------- functions ----------------------------------------------------

		// プレファブを指定ワールドへ編集用に展開する、localFileIDは恒等で保存往復が壊れないようにする
		bool MaterializePrefabForEdit(ECSWorld& world, AssetID prefabAsset, UUID sceneInstanceID, UUID instanceID,
			PrefabInstantiateResult& outResult);
		// 遷移元の3Dカメラと平行光源を編集ワールドへ環境として複製する
		void CopyPrefabEditEnvironment(ECSWorld& targetWorld, ECSWorld* sourceWorld, UUID sceneInstanceID,
			std::vector<Entity>& outEnvironmentEntities);
		// 編集後のプレファブを、指定ワールドの該当インスタンスへ伝播する、オーバーライドは保持する
		bool PropagatePrefabToInstances(ECSWorld& world, AssetID prefabAsset,
			const std::unordered_map<UUID, PrefabBaseEntity>& oldBase);
		// 遷移元ワールドのシーン管理を取得する
		SceneInstanceManager& ResolveHostScenes(PrefabEditStage& stage);
	};
}
