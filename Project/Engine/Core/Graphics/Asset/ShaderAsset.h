#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Graphics/Pipeline/Stage/ShaderReflection.h>
#include <Engine/Core/Asset/AssetTypes.h>

namespace Engine {

	//============================================================================
	//	ShaderAsset structures
	//============================================================================

	// シェーダーステージの情報
	struct ShaderStageEntry {

		// シェーダーの種類
		ShaderStage stage{};

		// シェーダーファイルのパス
		std::string file;
		std::string entry = "main";
		std::string profile;
	};
	// シェーダーアセットの情報
	struct ShaderAsset {

		// アセットID
		AssetID guid{};
		// シェーダーの名前
		std::string name;

		// 使用されるシェーダーリスト
		std::vector<ShaderStageEntry> stages;
	};

	// json変換
	bool FromJson(const nlohmann::json& data, ShaderAsset& outAsset);
	nlohmann::json ToJson(const ShaderAsset& asset);

	// シェーダーステージを検索する
	const ShaderStageEntry* FindShaderStage(const ShaderAsset& asset, ShaderStage stage);
} // Engine