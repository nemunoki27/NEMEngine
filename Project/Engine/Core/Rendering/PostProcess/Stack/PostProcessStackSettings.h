#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/AssetTypes.h>
#include <Engine/Core/Rendering/Assets/MaterialAsset.h>

// c++
#include <string>
#include <vector>
#include <unordered_map>

namespace Engine {

	//============================================================================
	//	PostProcessStack structures
	//============================================================================
	// ポストプロセスの1パス分の設定データ
	struct PostProcessStackPassSettings {

		// パスの識別ID（並び替え後も参照を保つ）
		UUID id{};
		// 表示名
		std::string name;
		// 有効フラグ
		bool enabled = true;
		// 実行するMaterialアセットのGUID
		AssetID materialGuid{};
		// 実行するパス名
		std::string passName = "PostProcess";
		// CBufferパラメータのScene毎overrideマップ
		std::unordered_map<std::string, MaterialParameterValue> parameterOverrides;
		// TextureのScene毎overrideマップ
		std::unordered_map<std::string, AssetID> textureGuids;
	};

	// シーンごとのPostProcessStackの設定データ
	struct PostProcessStackSettings {

		int version = 1;
		std::vector<PostProcessStackPassSettings> passes;
	};
} // Engine
