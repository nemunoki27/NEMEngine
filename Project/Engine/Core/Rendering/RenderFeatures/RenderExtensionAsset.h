#pragma once

//============================================================================
//	include
//============================================================================
#include "RenderFeatureProfile.h"

namespace Engine {

	//============================================================================
	//	RenderExtensionAsset structure
	//	Cameraへ追加する任意描画Passを保持するアセット
	//============================================================================
	struct RenderExtensionAsset {

		AssetID guid{};
		std::string name = "Render Extension";
		std::vector<RenderFeaturePassSettings> passes{};
		std::vector<RenderFeatureHierarchyItem> hierarchy{};
	};

	// JSONからRender Extensionを読み込む
	bool FromJson(const nlohmann::json& data, RenderExtensionAsset& outAsset);
	// Render ExtensionをJSONへ変換する
	nlohmann::json ToJson(const RenderExtensionAsset& asset);
	// 既存実行器で扱う一時Profileへ変換する
	RenderFeatureProfileAsset ToRuntimeProfile(const RenderExtensionAsset& asset);
} // Engine
