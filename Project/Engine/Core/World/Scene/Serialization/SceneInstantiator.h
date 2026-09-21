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
	//	SceneInstantiator class
	//	シーン文書からエンティティを生成する
	//============================================================================
	class SceneInstantiator {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		// シーン文書をワールドへ生成する
		static bool LoadFromJson(const nlohmann::json& sourceRoot, ECSWorld& world,
			AssetDatabase* assetDatabase, AssetID sourceAsset, UUID sceneInstanceID,
			std::vector<Entity>* outCreatedEntities);
	};
} // Engine
