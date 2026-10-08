#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Scene/Serialization/SceneSerializationTypes.h>
#include <Engine/Core/World/Scene/Serialization/SceneHeader.h>
#include <Engine/Core/World/ECS/Entity/Entity.h>

// c++
#include <vector>

namespace Engine {

	class ECSWorld;
	class AssetDatabase;

	//============================================================================
	//	SceneSnapshotBuilder class
	//	ワールドから保存用シーンデータを構築する
	//============================================================================
	class SceneSnapshotBuilder {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		// 保存対象を確定して書込用データを構築する
		static bool CaptureSaveSnapshot(
			const std::filesystem::path& scenePath, ECSWorld& world,
			const SceneHeader& header, AssetDatabase& database,
			SceneSaveSnapshot& outSnapshot,
			const std::vector<Entity>* entitiesSubset);

		// 対象エンティティの保存データを構築する
		static nlohmann::json SerializeEntities(ECSWorld& world, const std::vector<Entity>* subset);
	};
} // Engine
