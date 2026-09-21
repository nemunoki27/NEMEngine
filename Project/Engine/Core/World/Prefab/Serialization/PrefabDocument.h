#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Prefab/Serialization/PrefabReferenceRemapper.h>

// c++
#include <filesystem>

namespace Engine { class AssetDatabase; }

namespace Engine::PrefabDocument {

	inline constexpr uint32_t kPrefabSchemaVersion = 2;
	inline constexpr uint32_t kMinimumPrefabSchemaVersion = 1;

	// JSON値からローカルIDを読み取る
	Engine::UUID ReadLocalFileID(const nlohmann::json& value);

	// 保存実体のローカルIDを読み取る
	Engine::UUID ReadEntityLocalFileID(const nlohmann::json& entityJson);

	// Joint参照が同じPrefabの範囲内か調べる
	bool HasPrefabLocalJointTarget(const nlohmann::json& component,
		const Engine::PrefabReferenceRemapper::LocalFileIDMap& prefabLocalToSceneLocal);
	// ファイルを読み取り生成前の形式を検証する
	bool Read(AssetDatabase& database, AssetID prefabAsset, std::filesystem::path& fullPath, nlohmann::json& fileJson);
}
