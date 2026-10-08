#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/AssetTypes.h>

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace Engine {
	//============================================================================
	//	EditorSceneRequest structures
	//============================================================================
	enum class EditorSceneRequestType :
		uint8_t {

		None,
		NewScene,
		OpenScene,
		SaveScene,
		SaveAndNewScene,
		SaveAndOpenScene,
		EnterPrefabEdit,
		ExitPrefabEdit,
		ExitPrefabEditAll,
		TogglePrefabInContext,
		SavePrefab,
	};

	enum class EditorUnsavedScenePopupResult :
		uint8_t {

		None,
		Save,
		DontSave,
		Cancel,
	};

	struct EditorSceneRequest {

		EditorSceneRequestType type = EditorSceneRequestType::None;
		AssetID sceneAsset{};
	};

	// 同一Scene Assetを複数Instanceから保存するときの選択肢
	struct SceneSaveConflictChoice {

		AssetID sceneAsset{};
		std::vector<UUID> instanceIDs;
	};

	enum class SceneSaveOutcome : uint8_t {

		Saved,
		UnsavedInstances,
		Conflict,
		Failed,
		Cancelled,
	};

	enum class EditorSceneSaveAction : uint8_t {

		None,
		NewScene,
		OpenScene,
		Play,
		Close,
		Build,
	};

	struct EditorSceneSaveRequest {

		EditorSceneSaveAction action = EditorSceneSaveAction::None;
		AssetID sceneAsset{};
	};

	struct SceneSaveConflictResult {

		bool cancelled = false;
		std::unordered_map<AssetID, UUID> selectedInstances;
	};

}
