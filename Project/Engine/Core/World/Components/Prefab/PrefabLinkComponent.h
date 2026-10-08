#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/Components/Registry/ComponentTypeRegistry.h>
#include <Engine/Core/Assets/AssetTypes.h>

// c++
#include <utility>
#include <vector>

namespace Engine {

	//============================================================================
	//	PrefabLinkComponent struct
	//============================================================================
	// シーン内のエンティティの元プレファブへの情報
	struct PrefabLinkComponent {

		// 元になったプレファブアセット
		AssetID prefabAsset{};

		// プレファブファイル内でのローカル
		UUID prefabLocalFileID{};
		// 同じ生成呼び出しで生成されたエンティティ群を束ねるID
		UUID prefabInstanceID{};
		// Sceneファイル内のIDを実行用IDと分けて保持する
		UUID savedInstanceID{};
		// ネスト元のPrefabインスタンスID、Scene直下では空
		UUID ownerPrefabInstanceID{};
		// 親Prefabアセット内のネスト位置を識別するID
		UUID nestedSlotID{};
		// 親Prefabアセットに定義されたネストPrefabか
		bool isPrefabAssetNested = false;

		// プレファブのルートかどうか
		bool isPrefabRoot = false;
		// ルートだけが保持する追加Entityの宣言IDと実体ID
		std::vector<std::pair<UUID, UUID>> addedEntityMap;
	};

	// json変換
	void from_json(const nlohmann::json& in, PrefabLinkComponent& component);
	void to_json(nlohmann::json& out, const PrefabLinkComponent& component);

} // Engine
