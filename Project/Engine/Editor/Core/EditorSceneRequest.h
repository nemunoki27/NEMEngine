#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/AssetTypes.h>

#include <cstdint>

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

}
