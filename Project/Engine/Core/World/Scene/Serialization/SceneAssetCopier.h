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
	//	SceneAssetCopier class
	//	シーンと所有Actorの複製を行う
	//============================================================================
	class SceneAssetCopier {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		// シーンと所有Actorを複製し失敗時は作成分を取り消す
		static bool CopySceneAssets(const std::vector<SceneAssetCopy>& copies, std::string& error,
			std::shared_ptr<SceneAssetStorage> storage);
	};
} // Engine
