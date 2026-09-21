#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/AssetTypes.h>

// c++
#include <filesystem>
// json
#include <json.hpp>

namespace Engine::SceneDocument {

	inline constexpr uint32_t kSceneSchemaVersion = 3;

	// シーン文書の版と必須項目を検証する
	bool ValidateSceneFileRoot(const nlohmann::json& root);

	// 比較に使う正規化パスを取得する
	std::filesystem::path NormalizePath(const std::filesystem::path& path);

	// 保存先に適用するActor保存形式を判定する
	bool ShouldUseExternalActors(
		const std::filesystem::path& scenePath);

	// シーンが所有するActorの配置先を取得する
	std::filesystem::path ResolveExternalActorsRoot(
		const std::filesystem::path& scenePath, Engine::AssetID sceneAsset);

	// 外部Actorを読み取り文書へまとめる
	bool LoadExternalActors(const std::filesystem::path& scenePath,
		Engine::AssetID sceneAsset, nlohmann::json& root);

	// 保存済みのSceneローカルIDを検証する
	bool ValidateSerializedLocalFileIDs(const nlohmann::json& root);
}
