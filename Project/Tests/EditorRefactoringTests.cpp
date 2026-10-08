#include "EditorRefactoringTests.h"
#include "CommandHistoryTests.h"
#include "EditorEntityCommandTests.h"
#include "EditorDeleteCommandTests.h"
#include "EditorCloneCommandTests.h"
#include "HierarchyCommandContractTests.h"
#include "CurveEditorContractTests.h"
#include "ShaderGraphScenePreviewTests.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Inspectors/Core/AssetInspectorRegistry.h>
#include <Engine/Editor/Core/EditorSceneDirtyState.h>
#include <Engine/Editor/Core/EditorRequestSession.h>
#include <Engine/Editor/Core/EditorSceneEditScope.h>
#include <Engine/Editor/Core/EditorContext.h>
#include <Engine/Core/World/Scene/Runtime/SceneInstanceManager.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Editor/Core/Layout/EditorLayoutSerialization.h>
#include <Engine/Editor/Settings/ProjectTagSettings.h>
#include <Engine/Editor/Settings/ProjectRenderingLayerSettings.h>
#include <Engine/Core/Tools/Registry/ToolRegistry.h>
#include <Engine/Core/Rendering/Renderer/SceneComponentOverlay/SceneComponentOverlayCollector.h>
#include <Engine/Core/World/Components/Lighting/PointLightComponent.h>
#include <Engine/Core/World/Components/Transform/TransformComponent.h>

// c++
#include <algorithm>
#include <iostream>
#include <functional>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace {

	using namespace Engine;

	// Registryの読み取り専用取得を検証
	static_assert(std::is_same_v<decltype(std::declval<const AssetInspectorRegistry&>().Find(AssetType::Texture)),
		const IAssetInspectorDrawer*>);
	static_assert(std::is_same_v<decltype(std::declval<AssetInspectorRegistry&>().Find(AssetType::Texture)),
		IAssetInspectorDrawer*>);

	// ツール通知中の解除と再登録を検証する状態
	struct ToolProbeState {

		std::function<void()> registered;
		std::function<void()> unregistered;
		std::function<void()> enabled;
		std::function<void()> tick;
		int tickCount = 0;
		int unregisteredCount = 0;
		int destroyed = 0;
	};

	// 通知から登録配列を変更するテスト用ツール
	class RegistryProbeTool final : public ITool {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		RegistryProbeTool(std::string id, ToolProbeState& state) : state_(state) {
			descriptor_.id = std::move(id);
		}
		~RegistryProbeTool() override { ++state_.destroyed; }
		void OnRegistered() override { if (state_.registered) state_.registered(); }
		void OnUnregistered() override {
			++state_.unregisteredCount;
			if (state_.unregistered) state_.unregistered();
		}
		void Tick([[maybe_unused]] ToolContext& context) override {
			++state_.tickCount;
			if (state_.tick) state_.tick();
		}

		//--------- accessor -----------------------------------------------------

		const ToolDescriptor& GetDescriptor() const override { return descriptor_; }
		bool IsEnabled([[maybe_unused]] const ToolContext& context) const override {
			if (state_.enabled) state_.enabled();
			return true;
		}
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		ToolProbeState& state_;
		ToolDescriptor descriptor_;
	};

	// 読み取り専用Worldでもアイコンの矩形と重なり順を維持
	bool TestSceneComponentOverlayCollection() {

		ECSWorld world;
		const Entity light = world.CreateEntity(), camera = world.CreateEntity();
		world.AddComponent<TransformComponent>(light).localPos = Vector3(0.0f, 0.0f, 0.5f);
		world.AddComponent<PointLightComponent>(light).enabled = false;
		world.AddComponent<TransformComponent>(camera).localPos = Vector3(0.0f, 0.0f, 0.5f);
		world.AddComponent<PerspectiveCameraComponent>(camera).common.enabled = false;
		ResolvedRenderView view{};
		view.valid = true;
		view.width = 800;
		view.height = 600;
		view.perspective.valid = true;
		SceneComponentOverlaySettings settings{};
		settings.lightIconMaxPixelSize = settings.lightIconMinPixelSize = 64.0f;
		settings.lightIconCullPixelSize = 32.0f;
		SceneComponentOverlayRegistry registry;
		SceneComponentOverlayCollector collector;
		SceneComponentOverlayItemList items;
		const ECSWorld& readonly = world;
		collector.Collect(readonly, view, registry, settings, items);
		if (items.size() != 2 || items[0].entity != light || items[1].entity != camera ||
			items[0].kind != SceneComponentOverlayKind::LightIcon || items[1].kind != SceneComponentOverlayKind::CameraIcon ||
			items[0].screenCenter.x != 400.0f || items[1].screenCenter.x != 405.0f ||
			items[0].rectMin.x != 368.0f || items[1].rectMax.x != 437.0f ||
			items[0].enabled || items[1].enabled || items[0].color.a != settings.disabledAlpha ||
			items[1].color.a != settings.disabledAlpha) return false;

		// 小さいライトを除外してもカメラの重なり位置を変えない
		settings.lightIconMaxPixelSize = settings.lightIconMinPixelSize = 16.0f;
		collector.Collect(readonly, view, registry, settings, items);
		if (items.size() != 1 || items[0].entity != camera || items[0].screenCenter.x != 405.0f ||
			items[0].stableOrder != 0) return false;
		world.AddComponent<SceneObjectComponent>(light).activeInHierarchy = false;
		collector.Collect(readonly, view, registry, settings, items);
		if (items.size() != 1 || items[0].screenCenter.x != 400.0f) return false;
		view.valid = false;
		collector.Collect(readonly, view, registry, settings, items);
		return items.empty();
	}

	bool TestToolRegistryReentry() {

		ToolProbeState first, second, replacement, duplicate, selfDisabled, old, renewed, aborted, failed;
		ToolProbeState blocked, clearFailure, clearObserver, destructorFailure;
		bool valid = true;
		ToolRegistry registry;
		ToolContext context;
		first.registered = [&]() {
			valid &= registry.Find("a.first") != nullptr;
			valid &= !registry.Register(std::make_unique<RegistryProbeTool>("a.first", duplicate));
		};
		if (!registry.Register(std::make_unique<RegistryProbeTool>("a.first", first)) ||
			!registry.Register(std::make_unique<RegistryProbeTool>("b.second", second))) return false;
		// 更新中の自己解除でもコールバックの最後まで生存する
		first.tick = [&]() {
			valid &= registry.Unregister("a.first") && registry.Unregister("b.second");
			valid &= first.destroyed == 0 && second.destroyed == 0;
			valid &= registry.Register(std::make_unique<RegistryProbeTool>("c.replacement", replacement));
		};
		registry.Tick(context);
		if (!valid || first.tickCount != 1 || second.tickCount != 0 || replacement.tickCount != 0 ||
			first.destroyed != 1 || second.destroyed != 1 || duplicate.destroyed != 1) return false;
		registry.Tick(context);
		if (replacement.tickCount != 1) return false;

		// 有効判定中に解除されたツールは更新しない
		selfDisabled.enabled = [&]() {
			valid &= registry.Unregister("d.disabled") && selfDisabled.destroyed == 0;
		};
		if (!registry.Register(std::make_unique<RegistryProbeTool>("d.disabled", selfDisabled))) return false;
		registry.Tick(context);
		if (!valid || selfDisabled.tickCount != 0 || selfDisabled.destroyed != 1) return false;

		// 解除通知で同じIDを再登録しても新しい対象を消さない
		old.unregistered = [&]() {
			valid &= !registry.Unregister("e.renewed");
			valid &= registry.Register(std::make_unique<RegistryProbeTool>("e.renewed", renewed));
		};
		if (!registry.Register(std::make_unique<RegistryProbeTool>("e.renewed", old))) return false;
		const auto lease = registry.Acquire("e.renewed");
		if (!registry.Unregister("e.renewed") || !valid || old.destroyed != 0 ||
			registry.Find("e.renewed") == lease.get()) return false;

		// 登録通知で解除や例外が起きても登録結果を残さない
		aborted.registered = [&]() { valid &= registry.Unregister("f.aborted") && aborted.destroyed == 0; };
		if (registry.Register(std::make_unique<RegistryProbeTool>("f.aborted", aborted)) ||
			!valid || aborted.destroyed != 1 || registry.Find("f.aborted")) return false;
		failed.registered = []() { throw std::runtime_error("登録通知のテスト例外"); };
		try {
			registry.Register(std::make_unique<RegistryProbeTool>("g.failed", failed));
			return false;
		} catch (const std::runtime_error&) {
			if (failed.destroyed != 1 || registry.Find("g.failed")) return false;
		}
		registry.Clear();
		if (!registry.Empty() || renewed.destroyed != 1 || replacement.destroyed != 1) return false;

		// 終了通知の失敗でも残りを通知し、終了中の登録を拒否する
		clearFailure.unregistered = [&]() {
			valid &= registry.Empty() && !registry.Register(std::make_unique<RegistryProbeTool>("j.blocked", blocked));
			registry.Clear();
			throw std::runtime_error("終了通知のテスト例外");
		};
		if (!registry.Register(std::make_unique<RegistryProbeTool>("h.failure", clearFailure)) ||
			!registry.Register(std::make_unique<RegistryProbeTool>("i.observer", clearObserver))) return false;
		try {
			registry.Clear();
			return false;
		} catch (const std::runtime_error&) {
			if (!valid || !registry.Empty() || clearFailure.destroyed != 1 || clearObserver.destroyed != 1 ||
				clearObserver.unregisteredCount != 1 || blocked.destroyed != 1) return false;
		}
		// 終了失敗後も新しい登録を受け付ける
		if (!registry.Register(std::make_unique<RegistryProbeTool>("k.recovered", clearObserver))) return false;
		registry.Clear();
		if (clearObserver.destroyed != 2) return false;

		// 終了通知の例外でも呼出元の後処理を続ける
		destructorFailure.unregistered = []() { throw std::runtime_error("破棄中のテスト例外"); };
		{
			ToolRegistry finalizing;
			if (!finalizing.Register(std::make_unique<RegistryProbeTool>("l.finalizing", destructorFailure))) return false;
			finalizing.ClearNoThrow();
			if (!finalizing.Empty() || destructorFailure.destroyed != 1) return false;
		}
		// デストラクタから終了通知の例外を出さない
		{
			ToolRegistry finalizing;
			if (!finalizing.Register(std::make_unique<RegistryProbeTool>("l.finalizing", destructorFailure))) return false;
		}
		return destructorFailure.destroyed == 2 && destructorFailure.unregisteredCount == 2;
	}

	bool TestSceneSaveConflictSelection() {

		EditorRequestSession session;
		const AssetID first{ 12, 1 }, second{ 12, 2 };
		const auto a = Engine::UUID::New(), b = Engine::UUID::New(), c = Engine::UUID::New();
		const std::vector<SceneSaveConflictChoice> choices{ { first, { a, b } }, { second, { c } } };
		session.RequestSceneSaveConflict(choices);
		if (session.ConsumeSceneSaveConflictResult()) return false;

		// 不足した回答や別AssetのInstanceでは保存を開始しない
		if (session.SubmitSceneSaveConflictResult({ false, { { first, a } } }) ||
			session.SubmitSceneSaveConflictResult({ false, { { first, c }, { second, a } } }) ||
			session.ConsumeSceneSaveConflictResult()) return false;
		if (!session.SubmitSceneSaveConflictResult({ false, { { first, b }, { second, c } } })) return false;
		const auto saved = session.ConsumeSceneSaveConflictResult();
		if (!saved || saved->cancelled || saved->selectedInstances.at(first) != b ||
			saved->selectedInstances.at(second) != c || session.ConsumeSceneSaveConflictResult()) return false;

		// 取消を一度だけ返し、古い保存元を残さない
		session.RequestSceneSaveConflict(choices);
		if (!session.SubmitSceneSaveConflictResult({ true, { { first, a } } })) return false;
		const auto cancelled = session.ConsumeSceneSaveConflictResult();
		if (!cancelled || !cancelled->cancelled || !cancelled->selectedInstances.empty() ||
			session.ConsumeSceneSaveConflictResult()) return false;

		// World切替後は以前のPopupからの回答を受け付けない
		session.RequestSceneSaveConflict(choices);
		session.ResetPending();
		return !session.SubmitSceneSaveConflictResult({ false, { { first, a }, { second, c } } }) &&
			!session.ConsumeSceneSaveConflictResult();
	}

	bool TestProjectSettingsOwnership() {

		ProjectTagSettings firstTags, secondTags;
		std::string tag = "RefactoringTag";
		while (!firstTags.IsValidNewTag(tag)) {
			tag += "_";
		}
		if (!firstTags.AddTag(" " + tag + " ") || !firstTags.IsDirty() ||
			firstTags.IsValidNewTag(tag) || !secondTags.IsValidNewTag(tag) || secondTags.IsDirty()) {
			return false;
		}
		if (firstTags.RemoveTag("Untagged") || firstTags.RenameTag("Untagged", tag + "New")) {
			return false;
		}
		firstTags.Reload();
		if (firstTags.IsDirty() || !firstTags.IsValidNewTag(tag)) {
			return false;
		}
		ProjectRenderingLayerSettings firstLayers, secondLayers;
		const auto unchangedNames = secondLayers.GetNames();
		std::string layer = "RefactoringLayer";
		while (std::ranges::find(unchangedNames, layer) != unchangedNames.end()) {
			layer += "_";
		}
		if (!firstLayers.SetName(1, " " + layer + " ") || !firstLayers.IsDirty() ||
			firstLayers.GetNames()[1] != layer || secondLayers.GetNames() != unchangedNames || secondLayers.IsDirty()) {
			return false;
		}
		if (firstLayers.SetName(0, layer) || firstLayers.RemoveLayer(0) ||
			firstLayers.SetName(ProjectRenderingLayerSettings::kLayerCount, layer)) {
			return false;
		}
		firstLayers.Reload();
		return !firstLayers.IsDirty() && firstLayers.GetNames() == unchangedNames;
	}

	bool TestSceneSaveRevision() {

		EditorSceneDirtyState state;
		const AssetID first{ 1, 1 }, second{ 1, 2 };
		state.MarkDirty(first);
		const uint64_t savingRevision = state.GetSceneDirtyRevision(first);
		state.MarkDirty(second);
		state.MarkDirty(first);
		state.MarkSceneSaved(first, savingRevision);
		if (!state.IsSceneDirty(first) || !state.IsSceneDirty(second)) {
			return false;
		}
		state.MarkSceneSaved(first, state.GetSceneDirtyRevision(first));
		if (state.IsSceneDirty(first) || !state.IsSceneDirty(second)) {
			return false;
		}
		// 編集セッションを初期化しても古い保存完了を新しい編集へ適用しない
		state.ResetSceneDirtyState();
		state.MarkDirty(first);
		state.MarkSceneSaved(first, savingRevision);
		if (!state.IsSceneDirty(first) || state.GetSceneDirtyRevision(first) <= savingRevision) {
			return false;
		}
		state.MarkAllScenesSaved();
		if (state.HasDirtyScenes() || state.GetSceneDirtyRevision(first) != 0) return false;

		// 同じAssetでも別Instanceの編集と保存中の再編集を残す
		const auto a = Engine::UUID::New(), b = Engine::UUID::New();
		state.MarkDirty(first, a);
		state.MarkDirty(first, b);
		const uint64_t revisionA = state.GetSceneDirtyRevision(first, a);
		state.MarkDirty(first, b);
		state.MarkSceneSaved(first, revisionA, a);
		if (state.IsSceneDirty(first, a) || !state.IsSceneDirty(first, b)) return false;
		const uint64_t revisionB = state.GetSceneDirtyRevision(first, b);
		state.MarkDirty(first, b);
		state.MarkSceneSaved(first, revisionB, b);
		if (!state.IsSceneDirty(first, b)) return false;
		state.MarkAllScenesSaved();

		// Active以外への操作と削除も所属Instanceへ記録する
		ECSWorld world;
		SceneInstanceManager scenes;
		const auto active = scenes.CreateScratchScene({});
		const auto edited = scenes.CreateScratchScene({});
		scenes.Find(active)->sceneAsset = first;
		scenes.Find(edited)->sceneAsset = first;
		const auto entity = world.CreateEntity();
		world.AddComponent<SceneObjectComponent>(entity).sceneInstanceID = edited;
		EditorContext context{};
		context.activeWorld = &world;
		context.activeSceneAsset = first;
		context.activeSceneInstanceID = active;
		context.sceneInstances = &scenes;
		{
			EditorSceneEditScope operation(context, state);
			world.MarkComponentModified<SceneObjectComponent>(entity);
		}
		if (state.HasDirtyScenes()) return false;
		{
			EditorSceneEditScope operation(context, state);
			world.DestroyEntity(entity);
			world.FlushPendingDestroyEntities();
			operation.Commit();
		}
		return !state.IsSceneDirty(first, active) && state.IsSceneDirty(first, edited);
	}

	bool TestLayoutRoundTrip() {

		EditorLayoutSnapshot layout;
		layout.layoutID = "custom.layout";
		layout.displayName = "編集用";
		layout.order = 4;
		layout.visibility.showConsole = false;
		layout.imguiIniData = "[Window][Scene]\nPos=8,16\n";
		layout.panels.push_back({ "extension.panel", "instance.2", false, false, { { "selected", "asset" } } });
		const auto saved = EditorLayoutSerialization::MakeLayoutJson(layout, true);
		EditorLayoutSnapshot restored;
		bool imported = false;
		if (!EditorLayoutSerialization::ReadLayout(saved, restored, imported) || !imported ||
			saved != EditorLayoutSerialization::MakeLayoutJson(restored, imported)) {
			return false;
		}
		auto partial = saved;
		partial["panels"].push_back(10);
		partial["panels"].push_back({ { "typeID", "missing.instance" } });
		if (!EditorLayoutSerialization::ReadLayout(partial, restored, imported) || restored.panels.size() != 1) {
			return false;
		}
		return !EditorLayoutSerialization::ReadLayout({ { "layoutID", "missing.name" } }, restored, imported);
	}
}

bool TestEditorContracts() {

	if (!TestSceneComponentOverlayCollection() || !TestToolRegistryReentry() ||
		!NEMTests::TestCommandHistoryFailures() || !NEMTests::TestCompositeCommandFailures() ||
		!NEMTests::TestEditorSelectionRecovery() || !NEMTests::TestEditorEntityCommandRedo() ||
		!NEMTests::TestLogicalSelectionRoots() || !NEMTests::TestPerformanceGridCommandHistory() ||
		!NEMTests::TestActiveSelectionCommandHistory() ||
		!NEMTests::TestEntityPreviewOwnership() ||
		!NEMTests::TestTransformPreviewOwnership() ||
		!NEMTests::TestBulkDeleteRecovery() || !NEMTests::TestBulkCloneRecovery() || !NEMTests::TestHierarchyCommandContracts() ||
		!TestSceneSaveRevision() || !TestSceneSaveConflictSelection() ||
		!TestLayoutRoundTrip() || !TestProjectSettingsOwnership() || !NEMTests::TestCurveEditorContracts() ||
		!NEMTests::TestShaderGraphScenePreview()) {
		std::cerr << "Editor state contract failed\n";
		return false;
	}
	return true;
}
