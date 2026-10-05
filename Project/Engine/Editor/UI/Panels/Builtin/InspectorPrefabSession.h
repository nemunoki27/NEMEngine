#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Prefab/Serialization/PrefabBaseCache.h>

// c++
#include <string>
#include <unordered_map>

namespace Engine {

	struct EditorPanelContext;
	struct PrefabLinkComponent;

	//============================================================================
	//	InspectorPrefabSession class
	//	Prefab差分の選択と基準データを保持する
	//============================================================================
	class InspectorPrefabSession {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		InspectorPrefabSession() = default;
		~InspectorPrefabSession() = default;

		// Prefab差分の選択と反映を表示する
		void Draw(const EditorPanelContext& context, ECSWorld& world, const Entity& entity);
		// 保持した比較基準を解除する
		void Clear();

	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		// 差分ごとの維持・反映・復元の選択
		std::unordered_map<std::string, int> overrideChoices_;
		// 比較中のPrefab基準データ
		PrefabBaseCache prefabBaseCache_;

		//--------- functions ----------------------------------------------------

		// 選択した差分を保存して実体へ反映する
		bool ApplySelection(const EditorPanelContext& context, ECSWorld& world, AssetDatabase& database,
			const PrefabLinkComponent& link, const PrefabBaseEntities& base, const PrefabInstanceData& data,
			const std::vector<Entity>& addedEntityRoots);
	};
} // Engine
