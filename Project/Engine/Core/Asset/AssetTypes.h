#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/UUID/UUID.h>

// c++
#include <string_view>
// json
#include <json.hpp>

namespace Engine {

	// front
	class AssetDatabase;

	//============================================================================
	//	AssetTypes
	//============================================================================

	using AssetID = UUID;

	// アセットの種類
	enum class AssetType {

		Unknown = 0,
		Prefab,
		Texture,
		Material,
		Mesh,
		Scene,
		Shader,
		RenderPipeline,
		Font,
		Script,
	};

	// nlohmann::jsonからAssetIDを取得する
	AssetID ParseAssetID(const nlohmann::json& in, const char* key);
	// UUID文字列でもAssets/...パス文字列でも受ける
	AssetID ParseAssetReference(const nlohmann::json& in, const char* key,
		AssetDatabase* database, AssetType expectedType);
} // Engine
