#pragma once

//============================================================================
//	include
//============================================================================
#include "MaterialCreationSession.h"

// c++
#include <optional>
// json
#include <json.hpp>

namespace Engine::MaterialCreationImportUtility {

	// Materialのパスが参照するShaderのステージ
	struct MaterialShaderReferences {

		Engine::AssetID vs{};
		Engine::AssetID ps{};
		Engine::AssetID ms{};
		Engine::AssetID as{};
		Engine::AssetID gs{};
		std::string psEntry = "main";
	};

	// マテリアル用途を作成タイプへ変換する
	std::optional<MaterialCreateType> ToMaterialCreateType(MaterialUsage usage);

	// Pipelineの先頭variantから設定を読む
	bool ReadPipelineSettings(AssetDatabase& assetDatabase, AssetID pipelineID, PipelineCreateSettings& outSettings);

	// MaterialのパスからShaderのステージを読む
	bool ReadShaderReferences(
		AssetDatabase& assetDatabase, const nlohmann::json& pass, MaterialShaderReferences& outReferences);
}
