#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/RenderFeatures/RenderFeatureProfile.h>

namespace Engine {

	//============================================================================
	//	VolumeProfileAsset structure
	//	Volumeから適用する画面効果値を保持するアセット
	//============================================================================
	struct VolumeProfileAsset {

		AssetID guid{};
		std::string name = "Volume Profile";
		ColorPipelineSettings colorPipeline{};
	};

	// JSONからVolume Profileを読み込む
	bool FromJson(const nlohmann::json& data, VolumeProfileAsset& outAsset);
	// Volume ProfileをJSONへ変換する
	nlohmann::json ToJson(const VolumeProfileAsset& asset);
} // Engine
