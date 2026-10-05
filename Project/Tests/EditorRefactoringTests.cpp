#include "EditorRefactoringTests.h"
#include "CommandHistoryTests.h"
#include "EditorEntityCommandTests.h"
#include "EditorDeleteCommandTests.h"
#include "EditorCloneCommandTests.h"
#include "HierarchyCommandContractTests.h"
#include "CurveEditorContractTests.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Core/EditorSceneDirtyState.h>
#include <Engine/Editor/Core/EditorRequestSession.h>
#include <Engine/Editor/Core/EditorSceneEditScope.h>
#include <Engine/Editor/Core/EditorContext.h>
#include <Engine/Core/World/Scene/Runtime/SceneInstanceManager.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Editor/Core/Layout/EditorLayoutSerialization.h>
#include <Engine/Editor/Settings/ProjectTagSettings.h>
#include <Engine/Editor/Settings/ProjectRenderingLayerSettings.h>

#include <algorithm>
#include <iostream>

namespace {

	using namespace Engine;

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

	if (!NEMTests::TestCommandHistoryFailures() || !NEMTests::TestCompositeCommandFailures() ||
		!NEMTests::TestEditorSelectionRecovery() || !NEMTests::TestEditorEntityCommandRedo() ||
		!NEMTests::TestLogicalSelectionRoots() || !NEMTests::TestPerformanceGridCommandHistory() ||
		!NEMTests::TestActiveSelectionCommandHistory() ||
		!NEMTests::TestEntityPreviewOwnership() ||
		!NEMTests::TestTransformPreviewOwnership() ||
		!NEMTests::TestBulkDeleteRecovery() || !NEMTests::TestBulkCloneRecovery() || !NEMTests::TestHierarchyCommandContracts() ||
		!TestSceneSaveRevision() || !TestSceneSaveConflictSelection() ||
		!TestLayoutRoundTrip() || !TestProjectSettingsOwnership() || !NEMTests::TestCurveEditorContracts()) {
		std::cerr << "Editor state contract failed\n";
		return false;
	}
	return true;
}
