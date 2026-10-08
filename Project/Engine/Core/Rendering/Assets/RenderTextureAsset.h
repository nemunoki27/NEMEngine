#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/AssetTypes.h>

// c++
#include <cstdint>

namespace Engine {

	//============================================================================
	//	RenderTextureAsset structure
	//	CameraのTexture出力先を定義するアセット
	//============================================================================
	struct RenderTextureAsset {

		AssetID guid{};
		uint32_t width = 512;
		uint32_t height = 512;
	};

	// JSONからRenderTexture設定を読み込む
	bool FromJson(const nlohmann::json& data, RenderTextureAsset& outAsset);
	// RenderTexture設定をJSONへ変換する
	nlohmann::json ToJson(const RenderTextureAsset& asset);
} // Engine
