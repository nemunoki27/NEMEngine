#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/AssetTypes.h>
#include <Engine/Core/Foundation/Identity/UUID.h>

// c++
#include <functional>
#include <string>
#include <unordered_map>
// json
#include <json.hpp>

namespace Engine {
	class AssetDatabase;
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
	using EntityReferenceMapper = std::function<void(nlohmann::json&)>;

	// 有効な保存Entity参照からAssetとLocalFileIDを読む
	bool TryReadEntityReference(const nlohmann::json& reference, AssetID& sourceAsset, UUID& localFileID);

	// 参照元Assetが一致する複製範囲内の参照だけを変換する
	void RemapMatchedEntityReference(nlohmann::json& reference, const LocalFileIDMap& localFileIDMap,
		const std::unordered_map<UUID, AssetID>& sourceAssets);

	// 旧シーンの重複対応を先頭へ統合し、シーン参照だけを張り替える
	bool NormalizeLegacySceneInstances(nlohmann::json& scene, AssetID sourceAsset, std::string& diagnostic,
		AssetDatabase* database = nullptr);

	// コンポーネントマップ内のEntity参照を変換する
	void RemapComponents(nlohmann::json& components, const LocalFileIDMap& localFileIDMap,
		ReferenceSpace referenceSpace, AssetID sourceAsset);
	// 単一コンポーネント内のEntity参照を変換する
	void RemapComponent(const std::string& componentType, nlohmann::json& component,
		const LocalFileIDMap& localFileIDMap, ReferenceSpace referenceSpace, AssetID sourceAsset);
	// 参照元Assetを照合する複製処理とNative参照の変換を共有する
	void RemapComponentReferences(const std::string& componentType, nlohmann::json& component,
		const LocalFileIDMap& localFileIDMap, const EntityReferenceMapper& mapper);
	// 差分値内のEntity参照をパス情報込みで変換する
	void RemapValue(nlohmann::json& value, const std::string& path,
		const LocalFileIDMap& localFileIDMap, ReferenceSpace referenceSpace, AssetID sourceAsset);
	// Prefabへ保存できないScene実体への参照を空にする
	void ClearExternalSceneReferences(nlohmann::json& value);
	// Prefab外の親参照を除去して階層を正規化する
	void NormalizePrefabFileHierarchy(nlohmann::json& prefabFileJson);
	// Prefab内ジョイント接続の参照と通常階層を正規化する
	void NormalizePrefabFileJointAttachments(nlohmann::json& prefabFileJson);
}
} // Engine
