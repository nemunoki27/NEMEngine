#include "EditorRefactoringTests.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Core/EditorSceneDirtyState.h>
#include <Engine/Editor/Core/Layout/EditorLayoutSerialization.h>
#include <Engine/Editor/Settings/ProjectTagSettings.h>
#include <Engine/Editor/Settings/ProjectRenderingLayerSettings.h>

#include <algorithm>
#include <iostream>

namespace {

	using namespace Engine;

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
		return !state.HasDirtyScenes() && state.GetSceneDirtyRevision(first) == 0;
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

	if (!TestSceneSaveRevision() || !TestLayoutRoundTrip() || !TestProjectSettingsOwnership()) {
		std::cerr << "Editor state contract failed\n";
		return false;
	}
	return true;
}
