#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Asset/AssetTypes.h>

// c++
#include <string>
// json
#include <Externals/nlohmann/json.hpp>

namespace Engine {

	//============================================================================
	//	PrefabHeader struct
	//============================================================================

	// プレファブアセットのヘッダ情報
	struct PrefabHeader {

		// プレファブアセットアセット自体のGUID
		AssetID guid{};

		// 名前
		std::string name;

		// プレファブ内のルートエンティティのローカル
		UUID rootLocalFileID{};

		// フォーマットバージョン
		uint32_t version = 1;
	};

	// json変換
	bool FromJson(const nlohmann::json& data, PrefabHeader& prefabHeader);
	nlohmann::json ToJson(const PrefabHeader& prefabHeader);
} // Engine