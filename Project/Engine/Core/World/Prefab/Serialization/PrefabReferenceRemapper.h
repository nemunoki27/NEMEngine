#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/AssetTypes.h>
#include <Engine/Core/Foundation/Identity/UUID.h>

// c++
#include <string>
#include <unordered_map>
// json
#include <json.hpp>

namespace Engine {
namespace PrefabReferenceRemapper {

	//============================================================================
	//	PrefabReferenceRemapper enum
	//	参照先ローカルIDをどの空間の値として扱うか
	//============================================================================
	enum class ReferenceSpace {

		Scene,
		Prefab,
	};

	using LocalFileIDMap = std::unordered_map<UUID, UUID>;

	// コンポーネントマップ内のEntity参照を変換する
	void RemapComponents(nlohmann::json& components, const LocalFileIDMap& localFileIDMap,
		ReferenceSpace referenceSpace, AssetID sourceAsset);
	// 単一コンポーネント内のEntity参照を変換する
	void RemapComponent(const std::string& componentType, nlohmann::json& component,
		const LocalFileIDMap& localFileIDMap, ReferenceSpace referenceSpace, AssetID sourceAsset);
	// 差分値内のEntity参照をパス情報込みで変換する
	void RemapValue(nlohmann::json& value, const std::string& path,
		const LocalFileIDMap& localFileIDMap, ReferenceSpace referenceSpace, AssetID sourceAsset);
	// Prefab内ジョイント接続の参照と通常階層を正規化する
	void NormalizePrefabFileJointAttachments(nlohmann::json& prefabFileJson);
	// 古いPrefabに残ったScriptRef参照をslot IDから復旧する
	void RepairPrefabFileScriptRefs(nlohmann::json& prefabFileJson, AssetID prefabAsset);
}
} // Engine
