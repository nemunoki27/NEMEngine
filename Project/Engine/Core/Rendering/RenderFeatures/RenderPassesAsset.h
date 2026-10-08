#pragma once

//============================================================================
//	include
//============================================================================
#include "RenderFeatureProfile.h"

namespace Engine {

	//============================================================================
	//	RenderPassesAsset structure
	//	Cameraへ追加する任意描画Passを保持するアセット
	//============================================================================
	struct RenderPassesAsset {

		AssetID guid{};
		std::string name = "Render Passes";
		std::vector<RenderFeaturePassSettings> passes{};
		std::vector<RenderFeatureHierarchyItem> hierarchy{};
	};

	// JSONからRender Passesを読み込む
	bool FromJson(const nlohmann::json& data, RenderPassesAsset& outAsset);
	// Render PassesをJSONへ変換する
	nlohmann::json ToJson(const RenderPassesAsset& asset);
	// 既存実行器で扱う一時Profileへ変換する
	RenderFeatureProfileAsset ToRuntimeProfile(const RenderPassesAsset& asset);
} // Engine
