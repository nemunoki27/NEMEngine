#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/Database/AssetMetadata.h>
#include <Engine/Core/World/Scene/Serialization/SceneSerializationTypes.h>

namespace Engine {

	// 複製先のシーン保存値と新しいAsset識別
	struct SceneAssetCopySnapshot {

		SceneSaveSnapshot snapshot; // SceneとActorの保存値
		AssetMeta meta;				// 複製先のmeta
	};
}
