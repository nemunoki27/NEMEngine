#include "TestContracts.h"
#include "TestFixtures.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonFile.h>
#include <Engine/Core/Foundation/Utility/ScopedTransaction.h>
#include <Engine/Core/Rendering/RenderFeatures/RenderFeatureProfileService.h>
#include <Engine/Core/Rendering/RenderFeatures/RenderFeatureReflectionCache.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/World/Components/Rendering/MeshRendererComponent.h>
#include <Engine/Core/World/Scene/Authoring/SceneAuthoring.h>
#include <Engine/Editor/Commands/Entity/PerformanceGridUtility.h>
#include <Engine/Editor/Commands/Entity/SetPerformanceGridCommand.h>
#include <Engine/Editor/Core/EditorState.h>
#include <Engine/Editor/Tools/Builtin/RenderFeatures/RenderFeatureEditSession.h>
#include <Engine/Editor/Tools/Core/EditorToolContext.h>

// c++
#include <memory>
#include <stdexcept>
#include <utility>

namespace {

	int remainingCaptures = -1; // 失敗までに取得する保存値の数
	int remainingRestores = -1; // 失敗までに復元する保存値の数

	// 保存途中の失敗を発生させるComponent
	struct GridCaptureComponent {

		int value = 0; // 復元後に照合する値
	};

	void to_json(nlohmann::json& out, const GridCaptureComponent& component) {

		if (remainingCaptures >= 0 && remainingCaptures-- == 0) {
			throw std::runtime_error("グリッド保存途中の失敗を検証");
		}
		out = component.value;
	}

	void from_json(const nlohmann::json& in, GridCaptureComponent& component) {

		if (remainingRestores >= 0 && remainingRestores-- == 0) {
			throw std::runtime_error("グリッド復元途中の失敗を検証");
		}
		component.value = in.get<int>();
	}
}

bool NEMTests::TestEditorTransactionRollback() {

	// 途中失敗で入出力と階層をまとめて戻す
	Engine::RenderFeatureProfileAsset profile;
	profile.name = "Before";
	Engine::RenderFeaturePassSettings pass;
	pass.id = Engine::UUID{8001};
	profile.passes.push_back(pass);
	try {
		Engine::ScopedTransaction transaction(profile);
		profile.name = "After";
		profile.passes.clear();
		throw std::runtime_error("編集途中の失敗を検証");
	} catch (const std::runtime_error&) {
	}
	if (profile.name != "Before" || profile.passes.size() != 1 || profile.passes[0].id != pass.id) {
		return false;
	}
	// 確定後の処理終了では変更を維持する
	{
		Engine::ScopedTransaction transaction(profile);
		profile.name = "Committed";
		transaction.Commit();
	}
	return profile.name == "Committed" && profile.passes.size() == 1;
}

bool NEMTests::TestRenderPassesReflectionCache() {

	using namespace Engine;
	RenderFeatureReflectionCache cache;
	const AssetID first{9001, 9002};
	const AssetID second{9003, 9004};
	ShaderConstantBufferVariable variable{};
	variable.name = "First";
	ShaderResourceBinding resource{};
	resource.name = "Texture";
	ShaderResourceBinding sampler{};
	sampler.name = "Sampler";
	cache.CacheReflection(first, MaterialPassKind::Draw, {variable}, {resource}, {sampler});
	cache.CacheReflection(first, MaterialPassKind::Masked, {variable}, {}, {});
	cache.CacheReflection(second, MaterialPassKind::Draw, {}, {resource}, {});
	const auto* variables = cache.FindReflectionVariables(first, MaterialPassKind::Draw);
	const auto* resources = cache.FindReflectionResources(first, MaterialPassKind::Draw);
	const auto* samplers = cache.FindReflectionSamplers(first, MaterialPassKind::Draw);
	if (!variables || variables->size() != 1 || !resources || resources->size() != 1 || !samplers || samplers->size() != 1) {
		return false;
	}
	// 同じShaderの全種類を入れ替える
	variable.name = "Reloaded";
	cache.CacheReflection(first, MaterialPassKind::Draw, {variable}, {}, {});
	if (variables != cache.FindReflectionVariables(first, MaterialPassKind::Draw) || variables->at(0).name != "Reloaded" ||
		!resources->empty() || !samplers->empty()) {
		return false;
	}
	// Materialの削除は他のMaterialへ影響させない
	cache.ClearReflection(first);
	if (cache.FindReflectionVariables(first, MaterialPassKind::Draw) ||
		cache.FindReflectionVariables(first, MaterialPassKind::Masked) ||
		!cache.FindReflectionResources(second, MaterialPassKind::Draw)) {
		return false;
	}
	cache.ClearReflectionCache();
	return !cache.FindReflectionResources(second, MaterialPassKind::Draw);
}

bool NEMTests::TestRenderPassesSelectionRequests() {

	using namespace Engine;
	TestDirectory directory("RenderPassesSelection", RuntimePaths::GetGameAssetsRoot());
	const auto firstPath = directory.GetPath() / "first.renderpasses.json";
	const auto secondPath = directory.GetPath() / "second.renderpasses.json";
	RenderPassesAsset first;
	first.name = "First";
	RenderPassesAsset second;
	second.name = "Second";
	if (!JsonFile::Save(firstPath, ToJson(first)) || !JsonFile::Save(secondPath, ToJson(second))) {
		return false;
	}
	AssetDatabase database;
	if (!database.Init()) {
		return false;
	}
	const AssetID firstID = database.ImportOrGet(RuntimePaths::ToAssetPath(firstPath), AssetType::RenderPasses);
	const AssetID secondID = database.ImportOrGet(RuntimePaths::ToAssetPath(secondPath), AssetType::RenderPasses);
	if (!firstID || !secondID) {
		return false;
	}
	RenderFeatureProfileService& service = RenderFeatureProfileService::GetInstance();
	const auto previousPath = service.GetCurrentPath();
	auto previousProfile = service.GetProfile();
	const bool previousDirty = service.IsDirty();
	EditorToolContext context;
	context.toolContext.assetDatabase = &database;
	RenderFeatureEditSession session;
	const bool passed = [&]() {
		// 選択解除も更新要求として一度だけ適用する
		session.RequestProfile(firstID);
		if (!session.Tick(context.toolContext) || session.GetProfileID() != firstID || service.GetProfile().name != "First") {
			return false;
		}
		service.MarkDirty();
		session.RequestProfile({});
		if (!session.Tick(context.toolContext) || session.GetProfileID() || !service.GetCurrentPath().empty() ||
			service.IsDirty() || !service.GetProfile().passes.empty()) {
			return false;
		}
		const auto generation = service.GetRuntimeGeneration();
		if (session.Tick(context.toolContext) || service.GetRuntimeGeneration() != generation) {
			return false;
		}
		// 確認後の直接選択を古い予約で上書きしない
		session.RequestProfile(firstID);
		session.SelectProfile(context, secondID);
		if (session.Tick(context.toolContext) || session.GetProfileID() != secondID || service.GetProfile().name != "Second") {
			return false;
		}
		// 現在の選択に戻す操作で切替予約を取り消す
		service.MarkDirty();
		session.RequestProfile({});
		session.RequestProfile(secondID);
		if (session.Tick(context.toolContext) || !service.IsDirty() || session.GetProfileID() != secondID) {
			return false;
		}
		// 読込失敗で現在の選択と未保存値を失わない
		const auto previousGeneration = service.GetRuntimeGeneration();
		if (!JsonFile::Save(firstPath, nlohmann::json{{"name", 123}})) {
			return false;
		}
		session.RequestProfile(firstID);
		if (session.Tick(context.toolContext) || !session.HasError() || session.GetProfileID() != secondID ||
			service.GetProfile().name != "Second" || !service.IsDirty() ||
			service.GetRuntimeGeneration() != previousGeneration || session.Tick(context.toolContext)) {
			return false;
		}
		if (!JsonFile::Save(secondPath, nlohmann::json{{"name", 123}})) {
			return false;
		}
		session.Reload();
		if (!session.HasError() || !service.IsDirty() || service.GetProfile().name != "Second" ||
			service.GetRuntimeGeneration() != previousGeneration || service.SetActiveProfileAsset(firstID, nullptr)) {
			return false;
		}
		// 修復後の再読込でだけ編集を破棄する
		second.name = "Reloaded";
		if (!JsonFile::Save(secondPath, ToJson(second))) {
			return false;
		}
		session.Reload();
		return !session.HasError() && !service.IsDirty() && service.GetProfile().name == "Reloaded";
	}();
	// 他の検証へ編集用の状態を持ち越さない
	service.SetActiveProfilePath(previousPath);
	service.GetProfile() = std::move(previousProfile);
	if (previousDirty) {
		service.MarkDirty();
	} else {
		service.ClearDirty();
	}
	service.RebuildRuntime();
	return passed;
}

bool NEMTests::TestPerformanceGridCaptureRetry() {

	using namespace Engine;
	auto& registry = ComponentTypeRegistry::GetInstance();
	if (!registry.FindByName("GridCaptureComponent")) {
		registry.Register<GridCaptureComponent>(registry.GetComponentTypeCount(), "GridCaptureComponent");
	}
	ECSWorld world;
	EditorState state;
	EditorContext editor;
	editor.activeWorld = &world;
	editor.activeSceneInstanceID = Engine::UUID{2001};
	editor.activeSceneAsset = AssetID{2002, 2003};
	EditorCommandContext context;
	context.editorContext = &editor;
	context.editorState = &state;
	const Entity first = SceneAuthoring::CreateGameObject(world, "PerformanceGrid");
	const Entity second = SceneAuthoring::CreateGameObject(world, "PerformanceGrid");
	world.GetComponent<SceneObjectComponent>(first).sceneInstanceID = editor.activeSceneInstanceID;
	world.GetComponent<SceneObjectComponent>(second).sceneInstanceID = editor.activeSceneInstanceID;
	world.AddComponent<GridCaptureComponent>(first).value = 11;
	world.AddComponent<GridCaptureComponent>(second).value = 22;
	const Engine::UUID firstID = world.GetUUID(first);
	const Engine::UUID secondID = world.GetUUID(second);
	SetPerformanceGridCommand command(firstID);
	remainingCaptures = 1;
	bool failed = false;
	try {
		command.Execute(context);
	} catch (const std::runtime_error&) {
		failed = true;
	}
	remainingCaptures = -1;
	if (!failed || !world.IsAlive(first) || !world.IsAlive(second) || !command.Execute(context)) {
		return false;
	}
	// 2件目の復元失敗で1件目も残さない
	remainingRestores = 1;
	failed = false;
	try {
		command.Undo(context);
	} catch (const std::runtime_error&) {
		failed = true;
	}
	remainingRestores = -1;
	if (!failed || !PerformanceGridUtility::FindPerformanceGridRoots(world, editor.activeSceneInstanceID).empty()) {
		return false;
	}
	// 再試行の取消でも同じルートを重複して生成しない
	command.Undo(context);
	const Entity restoredFirst = world.FindByUUID(firstID);
	const Entity restoredSecond = world.FindByUUID(secondID);
	if (!world.IsAlive(restoredFirst) || !world.IsAlive(restoredSecond) ||
		PerformanceGridUtility::FindPerformanceGridRoots(world, editor.activeSceneInstanceID).size() != 2 ||
		world.GetComponent<GridCaptureComponent>(restoredFirst).value != 11 ||
		world.GetComponent<GridCaptureComponent>(restoredSecond).value != 22) {
		return false;
	}
	// 生成後のUndo失敗では生成した配置を戻す
	MeshSubMeshLayoutItem layout;
	layout.name = "Test";
	SetPerformanceGridCommand generated(
		firstID, AssetID{2999, 2998}, 1, 1, 1.0f, false, false, false, 0, 1.0f, 8.0f, 1.0f, {layout});
	if (!generated.Execute(context)) {
		return false;
	}
	EditorEntityTreeSnapshot beforeUndo;
	EditorEntitySnapshotUtility::CaptureSubtree(world, world.FindByUUID(firstID), beforeUndo);
	remainingRestores = 1;
	failed = false;
	try {
		generated.Undo(context);
	} catch (const std::runtime_error&) {
		failed = true;
	}
	remainingRestores = -1;
	if (!failed || PerformanceGridUtility::FindPerformanceGridRoots(world, editor.activeSceneInstanceID).size() != 1 ||
		world.HasComponent<GridCaptureComponent>(world.FindByUUID(firstID))) {
		return false;
	}
	for (const SerializedEntitySnapshot& saved : beforeUndo.entities) {
		if (!world.IsAlive(world.FindByUUID(saved.stableUUID))) {
			return false;
		}
	}
	generated.Undo(context);
	return PerformanceGridUtility::FindPerformanceGridRoots(world, editor.activeSceneInstanceID).size() == 2 &&
		   world.GetComponent<GridCaptureComponent>(world.FindByUUID(firstID)).value == 11 &&
		   world.GetComponent<GridCaptureComponent>(world.FindByUUID(secondID)).value == 22;
}

bool NEMTests::TestPerformanceGridUpdateRollback() {

	using namespace Engine;
	ECSWorld world;
	EditorState state;
	EditorContext editor;
	editor.activeWorld = &world;
	editor.activeSceneInstanceID = Engine::UUID{3101};
	editor.activeSceneAsset = AssetID{3102, 3103};
	EditorCommandContext context;
	context.editorContext = &editor;
	context.editorState = &state;
	const Engine::UUID rootID{3104};
	MeshSubMeshLayoutItem layout;
	layout.name = "Before";
	SetPerformanceGridCommand initial(
		rootID, AssetID{3110, 3111}, 2, 1, 1.0f, false, true, false, 1, 1.0f, 8.0f, 1.0f, {layout});
	if (!initial.Execute(context)) {
		return false;
	}
	EditorEntityTreeSnapshot before;
	EditorEntitySnapshotUtility::CaptureSubtree(world, world.FindByUUID(rootID), before);
	layout.name = "After";
	SetPerformanceGridCommand update(
		rootID, AssetID{3120, 3121}, 2, 1, 5.0f, false, true, true, 1, 3.0f, 10.0f, 2.0f, {layout});
	bool injected = false;
	const auto listener = world.AddComponentMutationListener(
		[](ECSWorld&, const Entity&, uint32_t, ComponentMutationKind kind, void* userData) {
			auto& failed = *static_cast<bool*>(userData);
			if (kind == ComponentMutationKind::Modified && !failed) {
				failed = true;
				throw std::runtime_error("グリッド更新通知の失敗を検証");
			}
		},
		&injected);
	bool failed = false;
	try {
		update.Execute(context);
	} catch (const std::runtime_error&) {
		failed = true;
	}
	world.RemoveComponentMutationListener(listener);
	EditorEntityTreeSnapshot after;
	EditorEntitySnapshotUtility::CaptureSubtree(world, world.FindByUUID(rootID), after);
	if (!failed || !injected || before.entities.size() != after.entities.size()) {
		return false;
	}
	// 全EntityのIDと保存値を更新前と照合
	for (size_t index = 0; index < before.entities.size(); ++index) {
		if (before.entities[index].stableUUID != after.entities[index].stableUUID ||
			before.entities[index].components != after.entities[index].components) {
			return false;
		}
	}
	// 通知からのBuffer削除を拒否し、全編集値を復元
	const auto removal = world.AddComponentMutationListener(
		[](ECSWorld& target, const Entity& entity, uint32_t, ComponentMutationKind kind, void*) {
			if (kind == ComponentMutationKind::Modified && target.HasComponent<MeshRendererComponent>(entity)) {
				target.RemoveBuffer<SubMeshMaterial>(entity);
			}
		},
		nullptr);
	failed = false;
	try {
		update.Execute(context);
	} catch (const std::logic_error&) {
		failed = true;
	}
	world.RemoveComponentMutationListener(removal);
	EditorEntitySnapshotUtility::CaptureSubtree(world, world.FindByUUID(rootID), after);
	if (!failed || before.entities.size() != after.entities.size()) {
		return false;
	}
	for (size_t index = 0; index < before.entities.size(); ++index) {
		if (before.entities[index].stableUUID != after.entities[index].stableUUID ||
			before.entities[index].components != after.entities[index].components) {
			return false;
		}
	}
	// World切替後へ元Worldの更新結果を持ち越さない
	ECSWorld nextWorld;
	std::pair<EditorContext*, ECSWorld*> switchTarget{&editor, &nextWorld};
	const auto switching = world.AddComponentMutationListener(
		[](ECSWorld&, const Entity&, uint32_t, ComponentMutationKind kind, void* data) {
			if (kind == ComponentMutationKind::Modified) {
				auto& target = *static_cast<std::pair<EditorContext*, ECSWorld*>*>(data);
				target.first->activeWorld = target.second;
			}
		},
		&switchTarget);
	failed = false;
	try {
		update.Execute(context);
	} catch (const std::runtime_error&) {
		failed = true;
	}
	world.RemoveComponentMutationListener(switching);
	if (!failed || editor.activeWorld != &nextWorld || nextWorld.GetRecordCount() != 0) {
		return false;
	}
	editor.activeWorld = &world;
	EditorEntitySnapshotUtility::CaptureSubtree(world, world.FindByUUID(rootID), after);
	if (before.entities.size() != after.entities.size()) {
		return false;
	}
	for (size_t index = 0; index < before.entities.size(); ++index) {
		if (before.entities[index].stableUUID != after.entities[index].stableUUID ||
			before.entities[index].components != after.entities[index].components) {
			return false;
		}
	}
	if (!update.Execute(context)) {
		return false;
	}
	// 再試行後もUndoで初回の編集値へ戻す
	update.Undo(context);
	EditorEntitySnapshotUtility::CaptureSubtree(world, world.FindByUUID(rootID), after);
	if (before.entities.size() != after.entities.size()) {
		return false;
	}
	for (size_t index = 0; index < before.entities.size(); ++index) {
		if (before.entities[index].stableUUID != after.entities[index].stableUUID ||
			before.entities[index].components != after.entities[index].components) {
			return false;
		}
	}
	return true;
}

bool NEMTests::TestPerformanceGridWorldEnd() {

	using namespace Engine;
	auto world = std::make_unique<ECSWorld>();
	EditorState state;
	EditorContext editor;
	editor.activeWorld = world.get();
	editor.activeSceneInstanceID = Engine::UUID{3201};
	editor.activeSceneAsset = AssetID{3202, 3203};
	EditorCommandContext context;
	context.editorContext = &editor;
	context.editorState = &state;
	MeshSubMeshLayoutItem layout;
	layout.name = "Before";
	SetPerformanceGridCommand initial(
		Engine::UUID{3204}, AssetID{3210, 3211}, 1, 1, 1.0f, false, false, false, 0, 1.0f, 8.0f, 1.0f, {layout});
	if (!initial.Execute(context)) {
		return false;
	}
	const auto lifetime = world->GetLifetime();
	bool tailCalled = false;
	// 通知中のWorld終了後は残りの購読先を呼ばない
	world->AddComponentMutationListener(
		[](ECSWorld&, const Entity&, uint32_t, ComponentMutationKind kind, void* data) {
			if (kind == ComponentMutationKind::Modified) {
				static_cast<std::unique_ptr<ECSWorld>*>(data)->reset();
			}
		},
		&world);
	world->AddComponentMutationListener(
		[](ECSWorld&, const Entity&, uint32_t, ComponentMutationKind, void* data) { *static_cast<bool*>(data) = true; },
		&tailCalled);
	layout.name = "After";
	SetPerformanceGridCommand update(
		Engine::UUID{3204}, AssetID{3220, 3221}, 1, 1, 5.0f, false, false, false, 0, 1.0f, 8.0f, 1.0f, {layout});
	bool failed = false;
	try {
		update.Execute(context);
	} catch (const std::runtime_error&) {
		failed = true;
	}
	editor.activeWorld = nullptr;
	return failed && !world && !lifetime->IsAlive() && !tailCalled;
}
