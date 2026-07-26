#include "EngineApplication.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Build/BuildConfig.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>
#include <Engine/Core/World/Prefab/Runtime/PrefabSystem.h>
#include <Engine/Core/World/Components/Camera/CameraComponent.h>
#include <Engine/Core/World/Components/Lighting/DirectionalLightComponent.h>
#include <Engine/Core/World/Components/Prefab/PrefabLinkComponent.h>
#include <Engine/Core/World/Components/Transform/HierarchyComponent.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/World/Components/Animation/JointAttachmentComponent.h>
#include <Engine/Editor/Assets/Project/ProjectAssetFileUtility.h>
#include <Engine/Editor/Commands/Entity/EditorEntitySnapshot.h>

// c++
#include <algorithm>
#include <unordered_set>

//============================================================================
//	EngineApplication prefab methods
//============================================================================
const Engine::SceneHeader* Engine::EngineApplication::GetActiveSceneHeader() {

	// In-Context編集ではhostシーンを参照する
	const SceneInstance* instance = GetActiveScenes().GetActive();
	return instance ? &instance->header : nullptr;
}

void Engine::EngineApplication::EnterPrefabEdit(AssetID prefabAsset) {

	// プレファブ以外や無効IDは無視する、Play中は呼ばれない前提
	const AssetMeta* meta = assetDataBase_.Find(prefabAsset);
	if (!meta || meta->type != AssetType::Prefab) {
		Logger::Output(LogType::Engine, spdlog::level::warn,
			"EngineApplication: EnterPrefabEdit ignored. asset is not a prefab.");
		return;
	}

	// 隔離ワールドを新規に作り、そこへプレファブだけを展開する、デフォルトは隔離編集
	PrefabEditStage stage{};
	stage.asset = prefabAsset;
	stage.world = std::make_unique<ECSWorld>();
	stage.name = std::filesystem::path(meta->assetPath).stem().string();
	stage.inContext = false;
	// 編集前のベースを控えておき、退出時にインスタンスへ伝播する際のオーバーライド判定に使う
	stage.baseAtEnter = PrefabOverrideUtility::LoadPrefabBaseEntities(assetDataBase_, prefabAsset);
	// 編集セッションを束ねるインスタンスID、In-Context切替後もヒエラルキー絞り込みに使う
	stage.instanceID = UUID::New();

	// 遷移元(Editまたは親プレファブ)のワールドとシーンを、In-Context置き場と環境複製元として控える
	stage.hostWorld = GetActiveWorld();
	const SceneInstance* baseScene = GetActiveScenes().GetActive();
	stage.hostSceneInstanceID = baseScene ? baseScene->instanceID : UUID{};
	const SceneHeader baseHeader = baseScene ? baseScene->header : SceneHeader{};

	// 隔離ワールドに一時シーンを作り、プレファブを編集用に展開する、これが無いと描画でactiveSceneが無くビュー更新されない
	const UUID sceneInstanceID = stage.scenes.CreateScratchScene(baseHeader);
	PrefabInstantiateResult result{};
	if (!MaterializePrefabForEdit(*stage.world, prefabAsset, sceneInstanceID, stage.instanceID, result)) {

		Logger::Output(LogType::Engine, spdlog::level::err,
			"EngineApplication: failed to enter prefab edit. instantiate failed. name={}", stage.name);
		return;
	}
	stage.root = result.root;

	// 遷移元の3Dカメラと平行光源を環境として隔離ワールドへ複製する、保存対象にもヒエラルキーにも出さない
	CopyPrefabEditEnvironment(*stage.world, stage.hostWorld, sceneInstanceID, stage.environmentEntities);

	// 隔離ワールドへ切り替わるので、別ワールドのエンティティを指す選択や履歴を片付ける
	editorManager_.ResetSceneEditingState();

	// 末尾を現在の編集対象としてスタックへ積む、ネスト編集はこの上にさらに積む
	prefabStages_.emplace_back(std::move(stage));
}

bool Engine::EngineApplication::MaterializePrefabForEdit(ECSWorld& world, AssetID prefabAsset,
	UUID sceneInstanceID, UUID instanceID, PrefabInstantiateResult& outResult) {

	// プレファブ自身のlocalFileIDをそのまま使う恒等remapを作る
	// これで編集→保存でlocalFileIDが変わらず、既存インスタンスのオーバーライド参照が壊れない
	const auto base = PrefabOverrideUtility::LoadPrefabBaseEntities(assetDataBase_, prefabAsset);
	std::vector<std::pair<UUID, UUID>> identityRemap;
	identityRemap.reserve(base.size());
	for (const auto& [localID, baseEntity] : base) {
		identityRemap.emplace_back(localID, localID);
	}

	HierarchySystem hierarchySystem{};
	PrefabSystem prefabSystem{};
	PrefabInstantiateDesc desc{};
	desc.ownerSceneInstanceID = sceneInstanceID;
	desc.forcedInstanceID = instanceID;
	desc.localFileIDRemap = &identityRemap;
	return prefabSystem.InstantiatePrefab(assetDataBase_, hierarchySystem, world, prefabAsset, outResult, desc) &&
		world.IsAlive(outResult.root);
}

void Engine::EngineApplication::CopyPrefabEditEnvironment(ECSWorld& targetWorld, ECSWorld* sourceWorld,
	UUID sceneInstanceID, std::vector<Entity>& outEnvironmentEntities) {

	if (!sourceWorld) {
		return;
	}

	// サブツリーをスナップショット経由でワールド間コピーし、一時シーンへ所属させて描画/ライティングへ反映する
	HierarchySystem hierarchySystem{};
	auto copyEnvironment = [&](const Entity& sourceEntity) {

		if (!sourceWorld->IsAlive(sourceEntity)) {
			return;
		}
		EditorEntityTreeSnapshot snapshot{};
		EditorEntitySnapshotUtility::CaptureSubtree(*sourceWorld, sourceEntity, snapshot);
		std::vector<Entity> restored = EditorEntitySnapshotUtility::RestoreSubtree(targetWorld, snapshot);

		for (const Entity& entity : restored) {

			if (targetWorld.HasComponent<SceneObjectComponent>(entity)) {
				targetWorld.GetComponent<SceneObjectComponent>(entity).sceneInstanceID = sceneInstanceID;
			}
			outEnvironmentEntities.emplace_back(entity);
		}
		hierarchySystem.RebuildRuntimeLinks(targetWorld, restored);
		};

	// 最初に見つかった3Dカメラと平行光源を複製する
	Entity sourceCamera = Entity::Null();
	sourceWorld->ForEach<PerspectiveCameraComponent>([&](const Entity& entity, PerspectiveCameraComponent&) {
		if (!sourceCamera.IsValid()) { sourceCamera = entity; }
		});
	Entity sourceLight = Entity::Null();
	sourceWorld->ForEach<DirectionalLightComponent>([&](const Entity& entity, DirectionalLightComponent&) {
		if (!sourceLight.IsValid()) { sourceLight = entity; }
		});
	copyEnvironment(sourceCamera);
	copyEnvironment(sourceLight);
}

void Engine::EngineApplication::ExitPrefabEdit() {

	if (prefabStages_.empty()) {
		return;
	}
	// 退出処理と伝播に必要な情報を退出前に控える
	PrefabEditStage& top = prefabStages_.back();
	const AssetID editedAsset = top.asset;
	const std::unordered_map<UUID, PrefabBaseEntity> oldBase = top.baseAtEnter;
	const bool wasInContext = top.inContext;
	ECSWorld* hostWorld = top.hostWorld;
	const UUID editInstanceID = top.instanceID;

	// 退出時は現在の編集内容を元の.prefabへ自動保存する
	SaveCurrentPrefab();

	if (wasInContext) {

		// In-Context編集はセッション専用instanceIDの一時実体だけ破棄する、別IDの元シーン実インスタンスには触れない
		if (hostWorld) {
			const std::vector<Entity> members = PrefabOverrideUtility::CollectInstanceEntities(*hostWorld, editInstanceID);
			for (const Entity& member : members) {
				if (hostWorld->IsAlive(member)) {
					EditorEntitySnapshotUtility::DestroySubtree(*hostWorld, member);
				}
			}
		}
		prefabStages_.pop_back();
	} else {

		// 隔離編集はワールドごと破棄する、Schedulerが指したままだと次TickのDetachWorldがdangling参照になる
		scheduler_.DetachCurrentWorld(systemContext_);
		prefabStages_.pop_back();
	}

	// 戻り先の元シーンのインスタンスへ編集を反映する、再生成失敗時はバックアップ復元で実体を失わない
	if (ECSWorld* targetWorld = GetActiveWorld()) {
		PropagatePrefabToInstances(*targetWorld, editedAsset, oldBase);
	}

	// 破棄したワールドのエンティティを指す選択や履歴を片付ける
	editorManager_.ResetSceneEditingState();
}

void Engine::EngineApplication::ExitAllPrefabEdit() {

	// 各階層を保存・伝播しながら全て抜け、一回の操作で元のシーン編集へ戻す
	while (!prefabStages_.empty()) {
		ExitPrefabEdit();
	}
}

void Engine::EngineApplication::TogglePrefabInContextMode() {

	if (prefabStages_.empty()) {
		return;
	}
	PrefabEditStage& top = prefabStages_.back();

	// 切り替え前に現在の編集内容を.prefabへ保存して、置き場が変わっても編集が失われないようにする
	SaveCurrentPrefab();

	const AssetID asset = top.asset;
	const UUID instanceID = top.instanceID;

	if (!top.inContext) {

		// 隔離 -> In-Context、隔離ワールドを捨ててhostWorldへ展開する
		scheduler_.DetachCurrentWorld(systemContext_);
		top.world.reset();
		top.scenes = SceneInstanceManager{};
		top.environmentEntities.clear();
		top.inContext = true;

		PrefabInstantiateResult result{};
		if (top.hostWorld && MaterializePrefabForEdit(*top.hostWorld, asset, top.hostSceneInstanceID, instanceID, result)) {
			top.root = result.root;
		}
	} else {

		// In-Context -> 隔離、hostWorldの一時実体をinstanceIDで全て捨ててから隔離ワールドへ展開する
		// ルート外や複数ルートの実体も漏らさず消し、元シーンへ残さない
		if (top.hostWorld) {
			const std::vector<Entity> members = PrefabOverrideUtility::CollectInstanceEntities(*top.hostWorld, instanceID);
			for (const Entity& member : members) {
				if (top.hostWorld->IsAlive(member)) {
					EditorEntitySnapshotUtility::DestroySubtree(*top.hostWorld, member);
				}
			}
		}
		top.inContext = false;
		top.world = std::make_unique<ECSWorld>();

		const SceneInstance* hostScene = ResolveHostScenes(top).Find(top.hostSceneInstanceID);
		const SceneHeader baseHeader = hostScene ? hostScene->header : SceneHeader{};
		const UUID sceneInstanceID = top.scenes.CreateScratchScene(baseHeader);

		PrefabInstantiateResult result{};
		if (MaterializePrefabForEdit(*top.world, asset, sceneInstanceID, instanceID, result)) {
			top.root = result.root;
		}
		CopyPrefabEditEnvironment(*top.world, top.hostWorld, sceneInstanceID, top.environmentEntities);
	}

	editorManager_.ResetSceneEditingState();
}

void Engine::EngineApplication::SaveCurrentPrefab() {

	if (prefabStages_.empty()) {
		return;
	}
	// 保存前に新規作成エンティティをプレファブのサブツリーへ取り込み、保存漏れを防ぐ
	SyncPrefabEditedEntities();

	PrefabEditStage& stage = prefabStages_.back();
	const AssetMeta* meta = assetDataBase_.Find(stage.asset);
	// In-Context編集ではhostWorld、隔離編集では隔離ワールドの実体を書き戻す
	ECSWorld* editWorld = stage.inContext ? stage.hostWorld : stage.world.get();
	// rootが削除されていても保存できるようにする、root健在チェックは保存ルート確定後に行う
	if (!meta || !editWorld) {
		return;
	}

	auto entityKey = [](const Entity& e) { return (static_cast<uint64_t>(e.generation) << 32) | e.index; };
	auto isHierarchyRoot = [&](const Entity& e) {
		return !editWorld->HasComponent<HierarchyComponent>(e) ||
			!editWorld->IsAlive(editWorld->GetComponent<HierarchyComponent>(e).parent);
		};

	// 保存ルートを決める、隔離編集は環境以外の全ルート、In-Contextは編集インスタンスのメンバールートのみ
	std::vector<Entity> saveRoots;
	if (stage.inContext) {

		const std::vector<Entity> members = PrefabOverrideUtility::CollectInstanceEntities(*editWorld, stage.instanceID);
		std::unordered_set<uint64_t> memberKeys;
		for (const Entity& member : members) {
			memberKeys.insert(entityKey(member));
		}
		for (const Entity& member : members) {

			Entity parent = editWorld->HasComponent<HierarchyComponent>(member) ?
				editWorld->GetComponent<HierarchyComponent>(member).parent : Entity::Null();
			if (!(editWorld->IsAlive(parent) && memberKeys.count(entityKey(parent)))) {
				saveRoots.emplace_back(member);
			}
		}
	} else {

		// 環境エンティティ(複製したカメラ/平行光源)を除いたルートを保存対象にする、新規作成した複数ルートも含む
		editWorld->ForEachAliveEntity([&](Entity entity) {

			if (!editWorld->HasComponent<SceneObjectComponent>(entity)) {
				return;
			}
			if (!isHierarchyRoot(entity)) {
				return;
			}
			if (std::find(stage.environmentEntities.begin(), stage.environmentEntities.end(), entity) !=
				stage.environmentEntities.end()) {
				return;
			}
			saveRoots.emplace_back(entity);
			});
	}

	// 元のrootが削除されていたら、現在の保存ルートの先頭を新しいrootに採用する
	// これで全削除→追加した場合でも保存され、再オープン時に削除前のデータへ戻らない
	if (!editWorld->IsAlive(stage.root)) {
		stage.root = saveRoots.empty() ? Entity::Null() : saveRoots.front();
	}
	if (!editWorld->IsAlive(stage.root)) {
		// プレファブが完全に空、ヘッダのrootを決められないので保存しない
		return;
	}

	// 各保存ルートのサブツリー(追加した子も含む)を重複なく集める
	std::vector<Entity> saveEntities;
	std::unordered_set<uint64_t> seen;
	for (const Entity& root : saveRoots) {
		for (const Entity& entity : EditorEntitySnapshotUtility::CollectSubtreeEntities(*editWorld, root)) {

			if (!editWorld->HasComponent<SceneObjectComponent>(entity)) {
				continue;
			}
			if (!seen.insert(entityKey(entity)).second) {
				continue;
			}
			saveEntities.emplace_back(entity);
		}
	}

	PrefabSystem prefabSystem{};
	prefabSystem.SavePrefabFromEntities(assetDataBase_, *editWorld, stage.root, saveEntities, meta->assetPath);
}

void Engine::EngineApplication::SyncPrefabEditedEntities() {

	if (prefabStages_.empty()) {
		return;
	}
	PrefabEditStage& stage = prefabStages_.back();
	// In-Context編集ではhostWorldにシーンの実体も混在するため、誤って取り込まないよう自動取り込みは行わない
	// In-Contextでは新規実体をプレファブrootの子に手動で入れればSavePrefabのサブツリー収集で保存される
	if (stage.inContext || !stage.world) {
		return;
	}
	ECSWorld& world = *stage.world;

	// 編集セッションのインスタンスIDで束ねる、rootを全削除した後でも新規実体にPrefabLinkを付けて水色表示にする
	const UUID rootInstanceID = stage.instanceID;
	const bool hasRoot = world.IsAlive(stage.root);

	// ルートと環境エンティティ以外を対象にする、反復中の構造変更を避けて先に集める
	std::vector<Entity> targets;
	world.ForEachAliveEntity([&](Entity entity) {

		if (entity == stage.root) {
			return;
		}
		if (!world.HasComponent<SceneObjectComponent>(entity)) {
			return;
		}
		if (std::find(stage.environmentEntities.begin(), stage.environmentEntities.end(), entity) !=
			stage.environmentEntities.end()) {
			return;
		}
		targets.emplace_back(entity);
		});
	if (targets.empty()) {
		return;
	}

	HierarchySystem hierarchySystem{};
	PrefabSystem prefabSystem{};
	for (const Entity& entity : targets) {

		if (!world.IsAlive(entity)) {
			continue;
		}

		// Unity準拠で1プレファブ1ルートを強制し、トップレベルになった実体はプレファブルート配下へ入れる
		const bool isRoot = !world.HasComponent<HierarchyComponent>(entity) ||
			!world.IsAlive(world.GetComponent<HierarchyComponent>(entity).parent);
		if (hasRoot && isRoot && !world.HasComponent<JointAttachmentComponent>(entity)) {
			hierarchySystem.SetParent(world, entity, stage.root);
		}

		// 新規作成と追加Prefabの実体を編集中プレファブのメンバーへ揃える
		const bool belongsToStage = world.HasComponent<PrefabLinkComponent>(entity) &&
			world.GetComponent<PrefabLinkComponent>(entity).prefabAsset == stage.asset &&
			world.GetComponent<PrefabLinkComponent>(entity).prefabInstanceID == rootInstanceID;
		if (!belongsToStage) {

			UUID prefabLocalFileID{};
			if (world.HasComponent<SceneObjectComponent>(entity)) {
				prefabLocalFileID = world.GetComponent<SceneObjectComponent>(entity).localFileID;
			}
			prefabSystem.SetPrefabLink(world, entity, stage.asset, prefabLocalFileID, rootInstanceID, false);
		}
	}
}

void Engine::EngineApplication::PropagatePrefabToInstances(ECSWorld& world, AssetID prefabAsset,
	const std::unordered_map<UUID, PrefabBaseEntity>& oldBase) {

	// シーンロード時の展開と同じ経路を再利用する
	HierarchySystem hierarchySystem{};
	PrefabOverrideUtility::PropagateToInstances(world, assetDataBase_, hierarchySystem, prefabAsset, oldBase);
}


