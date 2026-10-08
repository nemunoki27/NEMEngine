#include "EditorEntityCommandTests.h"
#include "TestFixtures.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Commands/Entity/CreateEntityCommand.h>
#include <Engine/Editor/Commands/Entity/EntityPropertyCommands.h>
#include <Engine/Editor/Commands/Core/CompositeEditorCommand.h>
#include <Engine/Editor/Utility/EditorEntityPreview.h>
#include <Engine/Editor/Utility/EditorTransformPreview.h>
#include <Engine/Editor/Commands/Transform/TransformEditUtility.h>
#include <Engine/Editor/UI/Panels/Builtin/ViewportTransformUtility.h>
#include <Engine/Editor/Commands/Entity/InstantiatePrefabCommand.h>
#include <Engine/Editor/Commands/Core/EditorCommandExecution.h>
#include <Engine/Editor/Core/EditorState.h>
#include <Engine/Editor/Core/EditorSceneDirtyState.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonFile.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>
#include <Engine/Core/World/Components/Scene/NameComponent.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/World/Components/Prefab/PrefabLinkComponent.h>
#include <Engine/Core/World/Scene/Runtime/SceneInstanceManager.h>
#include <Engine/Core/World/Scene/Authoring/SceneAuthoring.h>
#include <Engine/Core/World/Systems/Hierarchy/HierarchyUtility.h>
#include <Engine/Core/World/Systems/Hierarchy/HierarchySystem.h>
#include <Engine/Core/World/Components/Animation/JointAttachmentComponent.h>
#include <Engine/Editor/Commands/Entity/SetPerformanceGridCommand.h>
#include <Engine/Editor/Commands/Entity/PerformanceGridUtility.h>
#include <Engine/Core/World/Components/Transform/TransformComponent.h>
#include <Engine/Core/World/Components/Transform/HierarchyComponent.h>
#include <Engine/Core/World/Components/Lighting/PointLightComponent.h>

// c++
#include <limits>
#include <stdexcept>

namespace {

	// 指定数の生成後に一度だけ通知を失敗させる
	void FailGridCreation([[maybe_unused]] Engine::ECSWorld& world, [[maybe_unused]] const Engine::Entity& entity,
		[[maybe_unused]] uint32_t typeID, Engine::ComponentMutationKind kind, void* data) {

		int& remaining = *static_cast<int*>(data);
		if (kind != Engine::ComponentMutationKind::EntityCreated || remaining < 0) {
			return;
		}
		if (remaining-- == 0) {
			throw std::runtime_error("グリッド生成途中の失敗を検証");
		}
	}
}

bool NEMTests::TestEditorEntityCommandRedo() {

	using namespace Engine;
	ECSWorld world;
	EditorState state;
	SceneInstanceManager scenes;
	const auto originalScene = scenes.CreateScratchScene({});
	const auto otherScene = scenes.CreateScratchScene({});
	scenes.Find(originalScene)->sceneAsset = AssetID{82, 83};
	scenes.Find(otherScene)->sceneAsset = AssetID{85, 86};
	EditorContext editor;
	editor.activeWorld = &world;
	editor.sceneInstances = &scenes;
	editor.activeSceneInstanceID = originalScene;
	editor.activeSceneAsset = AssetID{82, 83};
	EditorCommandContext context;
	context.editorContext = &editor;
	context.editorState = &state;
	CreateEntityCommand create("Created");
	if (!create.Execute(context)) {
		return false;
	}
	const Entity original = state.selectedEntity;
	const auto stableID = world.GetUUID(original);
	const auto membership = world.GetComponent<SceneObjectComponent>(original);
	create.Undo(context);
	editor.activeSceneInstanceID = otherScene;
	editor.activeSceneAsset = AssetID{85, 86};
	EditorSceneDirtyState dirty;
	if (!EditorCommandExecution::Run(
			editor, state, dirty, [&](EditorCommandContext& current) { return create.Redo(current); })) {
		return false;
	}
	const Entity restored = world.FindByUUID(stableID);
	const auto& restoredMembership = world.GetComponent<SceneObjectComponent>(restored);
	// Active Sceneが変わっても初回のIDと所属へ戻す
	if (restored == original || state.selectedEntity != restored || restoredMembership.localFileID != membership.localFileID ||
		restoredMembership.sceneInstanceID != membership.sceneInstanceID ||
		restoredMembership.sourceAsset != membership.sourceAsset ||
		!dirty.IsSceneDirty(membership.sourceAsset, originalScene) || dirty.IsSceneDirty(editor.activeSceneAsset, otherScene)) {
		return false;
	}

	TestDirectory directory("EditorEntityCommand", RuntimePaths::GetGameAssetsRoot());
	const auto path = directory.GetPath() / "Source.prefab.json";
	nlohmann::json prefab = {{"SchemaVersion", 2}, {"Header", {{"rootLocalFileID", "0000000000000001"}}},
		{"Entities",
			nlohmann::json::array({{{"LocalFileID", "0000000000000001"}, {"Components", {{"Name", {{"name", "Initial"}}}}}}})}};
	if (!JsonFile::Save(path, prefab)) {
		return false;
	}
	AssetDatabase database;
	database.Init();
	editor.assetDatabase = &database;
	const auto asset = database.ImportOrGet(RuntimePaths::ToAssetPath(path), AssetType::Prefab);
	InstantiatePrefabCommand instantiate(asset);
	if (!asset || !instantiate.Execute(context)) {
		return false;
	}
	const Entity prefabRoot = state.selectedEntity;
	const auto prefabStableID = world.GetUUID(prefabRoot);
	const auto instanceID = world.GetComponent<PrefabLinkComponent>(prefabRoot).prefabInstanceID;
	const auto localID = world.GetComponent<SceneObjectComponent>(prefabRoot).localFileID;
	const auto initialName = world.GetComponent<NameComponent>(prefabRoot).name;
	instantiate.Undo(context);
	// Undo中のAsset変更をRedoへ混ぜない
	prefab["Entities"][0]["Components"]["Name"]["name"] = 123;
	if (!JsonFile::Save(path, prefab) || !instantiate.Redo(context)) {
		return false;
	}
	const Entity restoredPrefab = world.FindByUUID(prefabStableID);
	return world.IsAlive(restoredPrefab) && state.selectedEntity == restoredPrefab &&
		   world.GetComponent<PrefabLinkComponent>(restoredPrefab).prefabInstanceID == instanceID &&
		   world.GetComponent<SceneObjectComponent>(restoredPrefab).localFileID == localID &&
		   world.GetComponent<NameComponent>(restoredPrefab).name == initialName;
}

bool NEMTests::TestLogicalSelectionRoots() {

	using namespace Engine;
	ECSWorld world;
	const Entity root = SceneAuthoring::CreateGameObject(world, "Root");
	const Entity child = SceneAuthoring::CreateGameObject(world, "Child");
	const Entity grandchild = SceneAuthoring::CreateGameObject(world, "Grandchild");
	const Entity joint = SceneAuthoring::CreateGameObject(world, "Joint");
	const Entity other = SceneAuthoring::CreateGameObject(world, "Other");
	const auto rootLocalID = world.GetComponent<SceneObjectComponent>(root).localFileID;
	for (const Entity entity : {root, child, grandchild, joint}) {
		world.GetComponent<SceneObjectComponent>(entity).sceneInstanceID = Engine::UUID{1};
	}
	world.GetComponent<SceneObjectComponent>(other).sceneInstanceID = Engine::UUID{2};
	world.GetComponent<SceneObjectComponent>(other).localFileID = rootLocalID;
	world.AddComponent<JointAttachmentComponent>(joint).skinnedEntityLocalFileID = rootLocalID;
	HierarchySystem hierarchy;
	hierarchy.SetParent(world, child, root);
	hierarchy.SetParent(world, grandchild, child);

	// 子を先に選択していても親の処理へまとめる
	std::vector<Entity> selection{child, joint, root, root, other, Entity::Null()};
	if (HierarchyUtility::CollectLogicalRoots(world, selection) != std::vector<Entity>{root, other}) {
		return false;
	}
	selection = {grandchild, child, other};
	if (HierarchyUtility::CollectLogicalRoots(world, selection) != std::vector<Entity>{child, other}) {
		return false;
	}
	// 別Sceneの同じLocalFileIDをJointの親と見なさない
	selection = {joint, other};
	if (HierarchyUtility::CollectLogicalRoots(world, selection) != selection) {
		return false;
	}
	world.DestroyEntity(other);
	world.FlushPendingDestroyEntities();
	return HierarchyUtility::CollectLogicalRoots(world, selection) == std::vector<Entity>{joint};
}

bool NEMTests::TestPerformanceGridCommandHistory() {

	using namespace Engine;
	ECSWorld world;
	EditorState state;
	EditorContext editor;
	editor.activeWorld = &world;
	editor.activeSceneInstanceID = Engine::UUID{84};
	editor.activeSceneAsset = AssetID{85, 86};
	EditorCommandContext context;
	context.editorContext = &editor;
	context.editorState = &state;
	const Engine::UUID rootID = Engine::UUID::New();
	const Engine::UUID originalScene = editor.activeSceneInstanceID;
	const AssetID originalSceneAsset = editor.activeSceneAsset;
	std::vector<MeshSubMeshLayoutItem> layout(1);
	SetPerformanceGridCommand create(rootID, AssetID{90, 91}, 2, 1, 2.0f, false, true, false, 1, 1.0f, 8.0f, 1.0f, layout);
	if (!create.Execute(context)) {
		return false;
	}

	// 再配置とRedoで同じEntityのUUIDを使う
	const Entity root = world.FindByUUID(rootID);
	const Entity child = world.GetComponent<HierarchyComponent>(root).firstChild;
	const Engine::UUID childID = world.GetUUID(child);
	SetPerformanceGridCommand replace(rootID, AssetID{90, 91}, 2, 1, 4.0f, false, true, false, 1, 2.0f, 8.0f, 1.0f, layout);
	if (!create.CanCoalesce(replace) || !create.ExecuteCoalesced(replace, context) || world.FindByUUID(childID) != child ||
		world.GetComponent<TransformComponent>(child).localPos.x != -2.0f) {
		return false;
	}
	create.Undo(context);
	// Active Sceneを変更しても初回の所属へ戻す
	editor.activeSceneInstanceID = Engine::UUID{92};
	editor.activeSceneAsset = AssetID{93, 94};
	if (world.IsAlive(world.FindByUUID(rootID)) || !create.Redo(context)) {
		return false;
	}
	const Entity restored = world.FindByUUID(childID);
	if (!world.IsAlive(restored) || world.GetComponent<TransformComponent>(restored).localPos.x != -2.0f ||
		world.GetComponent<SceneObjectComponent>(restored).sceneInstanceID != originalScene ||
		world.GetComponent<SceneObjectComponent>(restored).sourceAsset != originalSceneAsset) {
		return false;
	}

	// ライトも更新時と再生成時で同じセル中央へ配置する
	Entity light = Entity::Null();
	size_t lightCount = 0;
	world.ForEach<PointLightComponent>([&](Entity entity, const PointLightComponent&) {
		light = entity;
		++lightCount;
	});
	if (lightCount != 1 || world.GetComponent<TransformComponent>(light).localPos != Vector3(0.0f, 0.0f, 0.0f) ||
		world.GetComponent<PointLightComponent>(light).intensity != 2.0f) {
		return false;
	}

	// 無効な配置要求で生成済みのEntityを失わない
	SetPerformanceGridCommand invalid(rootID, AssetID{90, 91}, 0, 1, 2.0f, false, false, false, 1, 1.0f, 8.0f, 1.0f, layout);
	if (invalid.Execute(context) || create.ExecuteCoalesced(invalid, context) || invalid.Redo(context) ||
		!world.IsAlive(restored)) {
		return false;
	}
	// 上限超過と非有限値も既存配置へ適用しない
	for (const int32_t count : {513, (std::numeric_limits<int32_t>::max)()}) {
		SetPerformanceGridCommand oversized(
			rootID, AssetID{90, 91}, count, count, 2.0f, false, true, false, 1, 1.0f, 8.0f, 1.0f, layout);
		if (create.ExecuteCoalesced(oversized, context) || oversized.Redo(context) ||
			PerformanceGridUtility::CalculatePointLightCount(count, count, true, 1) != 0) {
			return false;
		}
	}
	for (const float width : {0.0f, -1.0f, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()}) {
		SetPerformanceGridCommand nonFinite(
			rootID, AssetID{90, 91}, 2, 1, width, false, true, false, 1, 1.0f, 8.0f, 1.0f, layout);
		if (create.ExecuteCoalesced(nonFinite, context) || nonFinite.Execute(context)) {
			return false;
		}
	}
	// 合計数の制限と別ルートへの合流を生成前に拒否
	SetPerformanceGridCommand excessive(
		rootID, AssetID{90, 91}, 100, 100, 2.0f, false, true, false, 1, 1.0f, 8.0f, 1.0f, layout);
	const Engine::UUID otherRootID = Engine::UUID::New();
	SetPerformanceGridCommand otherRoot(
		otherRootID, AssetID{90, 91}, 2, 1, 2.0f, false, false, false, 1, 1.0f, 8.0f, 1.0f, layout);
	if (create.ExecuteCoalesced(excessive, context) || create.ExecuteCoalesced(otherRoot, context) ||
		world.IsAlive(world.FindByUUID(otherRootID)) || world.FindByUUID(childID) != restored ||
		world.GetComponent<TransformComponent>(restored).localPos.x != -2.0f ||
		world.GetComponent<PointLightComponent>(light).intensity != 2.0f) {
		return false;
	}
	// 生成途中に失敗しても旧配置のUUIDと設定へ戻す
	int remaining = 2;
	const uint64_t listener = world.AddComponentMutationListener(&FailGridCreation, &remaining);
	SetPerformanceGridCommand failedReplacement(
		rootID, AssetID{90, 91}, 3, 1, 6.0f, false, true, false, 1, 3.0f, 8.0f, 1.0f, layout);
	bool failed = false;
	try {
		create.ExecuteCoalesced(failedReplacement, context);
	} catch (const std::runtime_error&) {
		failed = true;
	}
	world.RemoveComponentMutationListener(listener);
	const Entity recovered = world.FindByUUID(childID);
	const Entity recoveredRoot = world.FindByUUID(rootID);
	size_t aliveCount = 0;
	world.ForEachAliveEntity([&](Entity) { ++aliveCount; });
	if (!failed || remaining != -1 || aliveCount != 6 || !world.IsAlive(recovered) || !world.IsAlive(recoveredRoot) ||
		world.GetComponent<TransformComponent>(recovered).localPos.x != -2.0f ||
		world.GetComponent<HierarchyComponent>(recovered).parent != recoveredRoot ||
		world.GetComponent<SceneObjectComponent>(recovered).sceneInstanceID != originalScene) {
		return false;
	}
	SetPerformanceGridCommand remove(rootID);
	if (!remove.Execute(context) || world.IsAlive(world.FindByUUID(rootID))) {
		return false;
	}
	remove.Undo(context);
	return world.IsAlive(world.FindByUUID(childID)) &&
		   PerformanceGridUtility::FindPerformanceGridRoots(world, originalScene).size() == 1 &&
		   PerformanceGridUtility::FindPerformanceGridRoots(world, editor.activeSceneInstanceID).empty();
}

bool NEMTests::TestActiveSelectionCommandHistory() {

	using namespace Engine;
	ECSWorld world;
	EditorState state;
	EditorContext editor;
	editor.activeWorld = &world;
	EditorCommandContext context;
	context.editorContext = &editor;
	context.editorState = &state;
	const Entity first = SceneAuthoring::CreateGameObject(world, "First");
	const Entity second = SceneAuthoring::CreateGameObject(world, "Second");
	const std::vector<Entity> selection{first, second};
	state.SetSelectedEntities(selection);
	std::vector<std::unique_ptr<IEditorCommand>> commands;
	commands.emplace_back(std::make_unique<SetEntityActiveCommand>(first, false));
	commands.emplace_back(std::make_unique<SetEntityActiveCommand>(second, false));
	auto operation = std::make_unique<CompositeEditorCommand>(std::move(commands), true);

	// 一回の履歴で両方の状態を変更して選択を維持
	if (!state.commandHistory.Execute(std::move(operation), context) || state.GetSelectedEntities() != selection ||
		world.GetComponent<SceneObjectComponent>(first).activeSelf ||
		world.GetComponent<SceneObjectComponent>(second).activeSelf) {
		return false;
	}
	if (!state.commandHistory.Undo(context) || state.commandHistory.CanUndo() || state.GetSelectedEntities() != selection ||
		!world.GetComponent<SceneObjectComponent>(first).activeSelf ||
		!world.GetComponent<SceneObjectComponent>(second).activeSelf) {
		return false;
	}
	return state.commandHistory.Redo(context) && state.GetSelectedEntities() == selection &&
		   !world.GetComponent<SceneObjectComponent>(first).activeSelf &&
		   !world.GetComponent<SceneObjectComponent>(second).activeSelf;
}

bool NEMTests::TestEntityPreviewOwnership() {

	using namespace Engine;
	ECSWorld first;
	ECSWorld second;
	const Entity root = SceneAuthoring::CreateGameObject(first, "Preview");
	const Entity child = SceneAuthoring::CreateGameObject(first, "Child");
	HierarchySystem hierarchy;
	hierarchy.SetParent(first, child, root);
	EditorEntityPreview preview;
	if (!preview.Begin(first, root) || !preview.BelongsTo(first) || preview.BelongsTo(second) ||
		preview.Begin(first, Entity::Null()) || !first.IsAlive(root)) {
		return false;
	}

	// 別Worldへ移る前に元の仮Entityと子孫を破棄する
	const Entity replacement = SceneAuthoring::CreateGameObject(second, "Replacement");
	if (!preview.Begin(second, replacement) || first.IsAlive(root) || first.IsAlive(child)) {
		return false;
	}
	if (preview.Release() != replacement || preview.IsActive()) {
		return false;
	}
	preview.End();
	if (!second.IsAlive(replacement)) {
		return false;
	}

	// 寿命の参照が残っていても破棄済みWorldを参照しない
	auto expiredWorld = std::make_unique<ECSWorld>();
	const auto lifetime = expiredWorld->GetLifetime();
	const Entity expiredEntity = SceneAuthoring::CreateGameObject(*expiredWorld, "Expired");
	if (!preview.Begin(*expiredWorld, expiredEntity)) {
		return false;
	}
	expiredWorld.reset();
	if (lifetime->IsAlive() || preview.IsActive() || preview.GetEntity().IsValid()) {
		return false;
	}
	preview.End();
	return !preview.IsActive();
}

bool NEMTests::TestTransformPreviewOwnership() {

	using namespace Engine;
	ECSWorld world;
	EditorState state;
	EditorContext editor;
	editor.activeWorld = &world;
	EditorCommandContext context;
	context.editorContext = &editor;
	context.editorState = &state;
	const Entity first = SceneAuthoring::CreateGameObject(world, "First");
	const Entity second = SceneAuthoring::CreateGameObject(world, "Second");
	const std::vector<Entity> targets{first, second};
	state.SetSelectedEntities(targets);
	EditorTransformPreview preview;
	if (!preview.Begin(world, targets, false)) {
		return false;
	}
	for (Entity entity : targets) {
		auto transform = world.GetComponent<TransformComponent>(entity);
		transform.localPos.x = 5.0f;
		TransformEditUtility::ApplyImmediate(world, entity, transform);
	}

	// 開始値へ戻して一回の履歴で両方を確定する
	auto command = preview.BuildCommand(world);
	preview.Cancel();
	if (!command || world.GetComponent<TransformComponent>(first).localPos.x != 0.0f ||
		!state.commandHistory.Execute(std::move(command), context) || state.GetSelectedEntities() != targets ||
		world.GetComponent<TransformComponent>(first).localPos.x != 5.0f ||
		world.GetComponent<TransformComponent>(second).localPos.x != 5.0f || !state.commandHistory.Undo(context) ||
		state.commandHistory.CanUndo() || world.GetComponent<TransformComponent>(first).localPos.x != 0.0f ||
		world.GetComponent<TransformComponent>(second).localPos.x != 0.0f || !state.commandHistory.Redo(context) ||
		state.GetSelectedEntities() != targets) {
		return false;
	}

	// Play中の終了では実行値を巻き戻さない
	if (!preview.Begin(world, targets, true)) {
		return false;
	}
	world.GetComponent<TransformComponent>(first).localPos.x = 7.0f;
	if (preview.BuildCommand(world)) {
		return false;
	}
	preview.Cancel();
	if (world.GetComponent<TransformComponent>(first).localPos.x != 7.0f) {
		return false;
	}

	// World切替で元の編集を戻し、別Worldへ履歴を渡さない
	if (!preview.Begin(world, targets, false)) {
		return false;
	}
	world.GetComponent<TransformComponent>(first).localPos.x = 9.0f;
	ECSWorld other;
	const Entity otherEntity = SceneAuthoring::CreateGameObject(other, "Other");
	const Entity otherTargets[] = {otherEntity};
	if (preview.BuildCommand(other) || !preview.Begin(other, otherTargets, false) ||
		world.GetComponent<TransformComponent>(first).localPos.x != 7.0f || preview.BelongsTo(world)) {
		return false;
	}
	preview.Cancel();

	// 外部が寿命を保持していても破棄済みWorldを参照しない
	auto expired = std::make_unique<ECSWorld>();
	const auto lifetime = expired->GetLifetime();
	const Entity expiredTargets[] = {SceneAuthoring::CreateGameObject(*expired, "Expired")};
	if (!preview.Begin(*expired, expiredTargets, false)) {
		return false;
	}
	expired.reset();
	preview.Cancel();
	if (lifetime->IsAlive() || !preview.GetSnapshots().empty()) {
		return false;
	}

	// 回転と拡縮を持つ親の子もWorldで同じ距離だけ動かす
	const Entity parent = SceneAuthoring::CreateGameObject(world, "Parent");
	const Entity child = SceneAuthoring::CreateGameObject(world, "Child");
	HierarchySystem hierarchy;
	hierarchy.SetParent(world, child, parent);
	world.GetComponent<TransformComponent>(parent).worldMatrix =
		Matrix4x4::MakeAffineMatrix(Vector3(2.0f, 2.0f, 2.0f), Vector3(0.0f, 90.0f, 0.0f), Vector3(10.0f, 0.0f, 0.0f));
	world.GetComponent<TransformComponent>(child).localPos = Vector3(1.0f, 0.0f, 0.0f);
	TransformComponent result{};
	const Vector3 unit = Vector3::AnyInit(1.0f);
	if (!ViewportTransformUtility::ResolveWorldDelta(
			world, child, Vector3(1.0f, 0.0f, 0.0f), Quaternion::Identity(), unit, Vector3::AnyInit(0.0f), false, result)) {
		return false;
	}
	const Matrix4x4 after = Matrix4x4::MakeAffineMatrix(result.localScale, result.localRotation, result.localPos) *
							world.GetComponent<TransformComponent>(parent).worldMatrix;
	if (!Vector3::NearlyEqual(after.GetTranslationValue(), Vector3(11.0f, 0.0f, -2.0f))) {
		return false;
	}

	// 潰れた親の座標では結果を上書きしない
	const auto before = result;
	world.GetComponent<TransformComponent>(parent).worldMatrix = Matrix4x4::MakeScaleMatrix(Vector3(0.0f, 1.0f, 1.0f));
	return !ViewportTransformUtility::ResolveWorldDelta(
			   world, child, Vector3(1.0f, 0.0f, 0.0f), Quaternion::Identity(), unit, Vector3::AnyInit(0.0f), false, result) &&
		   result.localPos == before.localPos && result.localRotation == before.localRotation &&
		   result.localScale == before.localScale;
}
