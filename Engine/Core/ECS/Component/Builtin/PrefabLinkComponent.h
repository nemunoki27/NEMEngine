#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/ECS/Component/Registry/ComponentTypeRegistry.h>
#include <Engine/Core/Asset/AssetTypes.h>

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

		// プレファブのルートかどうか
		bool isPrefabRoot = false;
	};

	// json変換
	void from_json(const nlohmann::json& in, PrefabLinkComponent& component);
	void to_json(nlohmann::json& out, const PrefabLinkComponent& component);

	ENGINE_REGISTER_COMPONENT(PrefabLinkComponent, "PrefabLink");
} // Engine