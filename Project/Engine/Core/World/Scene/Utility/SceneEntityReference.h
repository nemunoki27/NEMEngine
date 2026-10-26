#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/AssetTypes.h>
#include <Engine/Core/World/ECS/Entity/Entity.h>

namespace Engine {

	class ECSWorld;

	//============================================================================
	//	SceneEntityReference structure
	//	保存元のAssetと文書内IDでEntityを参照する
	//============================================================================
	struct SceneEntityReference {

		AssetID sourceAsset{};
		UUID localFileID{};
		bool prefab = false;

		// Entityの保存識別子を取得する
		static SceneEntityReference FromEntity(const ECSWorld& world, Entity entity, bool prefab = false);
		// 所有元のSceneを優先し、一意に決まる対象だけを返す
		Entity Resolve(const ECSWorld& world, Entity owner) const;
	};

	// 既存の参照張替え処理で扱える形式を読み書きする
	void from_json(const nlohmann::json& in, SceneEntityReference& reference);
	void to_json(nlohmann::json& out, const SceneEntityReference& reference);
}
