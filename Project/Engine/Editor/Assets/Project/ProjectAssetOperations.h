#pragma once

//============================================================================
//	include
//============================================================================
#include "ProjectAssetFileTypes.h"
#include <Engine/Core/World/ECS/Entity/Entity.h>

namespace Engine {

	struct EditorPanelContext;
	class ECSWorld;

	namespace ProjectAssetOperations {

		// Entity階層を保存してPrefab参照を設定する
		bool SavePrefab(AssetDatabase& database, ECSWorld& world, const Entity& entity,
			ProjectAssetSource source, const std::string& directoryVirtualPath, ProjectAssetFileResult& result);
		// 改名したSceneを参照するロード済み文書の表示名を更新する
		void UpdateLoadedSceneName(const EditorPanelContext& context, AssetID assetID, const std::filesystem::path& path);
	}
}
