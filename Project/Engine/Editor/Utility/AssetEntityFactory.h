#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/Entity/Entity.h>
#include <Engine/Core/Foundation/Identity/UUID.h>
#include <Engine/Editor/UI/Panels/Core/IEditorPanel.h>

namespace Engine {

	// front
	class ECSWorld;
	class AssetDatabase;
	class HierarchySystem;
	class GraphicsCore;

	//============================================================================
	//	AssetSpawnResult struct
	//	アセットからの生成結果、配置側が2D/3Dを判別するのに使う
	//============================================================================
	struct AssetSpawnResult {

		// 生成したルートエンティティ
		Entity root = Entity::Null();
		// 3D要素を持つか、配置のレイキャスト方式の選択に使う
		bool isThreeD = false;
		// 生成に成功したか
		bool valid = false;
	};

	//============================================================================
	//	AssetEntityFactory namespace
	//	ドラッグされたアセットから対応するエンティティを生成する
	//============================================================================
	namespace AssetEntityFactory {

		// このペイロードのアセットからエンティティを生成できるか
		bool CanSpawn(const EditorAssetDragDropPayload& payload);

		// アセットからエンティティを生成する、prefabは展開しisThreeDは3D要素の有無で決まる
		AssetSpawnResult Spawn(ECSWorld& world, AssetDatabase& database, GraphicsCore& graphicsCore,
			HierarchySystem& hierarchySystem, const EditorAssetDragDropPayload& payload, UUID sceneInstanceID);
	}
} // Engine
