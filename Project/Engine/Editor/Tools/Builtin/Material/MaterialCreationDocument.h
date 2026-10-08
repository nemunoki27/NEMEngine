#pragma once

//============================================================================
//	include
//============================================================================
#include "MaterialCreationSession.h"

// json
#include <json.hpp>

namespace Engine::MaterialCreationDocument {

	// 作成タイプをマテリアル用途へ変換する
	MaterialUsage ToMaterialUsage(MaterialCreateType type);

	// 描画タイプに合わせてShaderのステージを作る
	nlohmann::json MakeShaderJson(const std::string& name, AssetID vs, AssetID ps, AssetID ms, AssetID as, AssetID gs,
		bool includeMeshStages, bool includeGeometryStage, bool pixelOnly, const std::string& pixelEntry);

	// VSとMSの描画経路を持つPipelineを作る
	nlohmann::json MakePipelineJson(const std::string& name, AssetID shaderID, bool useMeshShader, bool useGeometryShader,
		int numRenderTargets, const PipelineCreateSettings& settings);

	// 描画タイプに合わせてMaterialのパスを作る
	nlohmann::json MakeMaterialJson(const std::string& name, AssetID pipelineID, AssetID transparentPipelineID,
		AssetID shaderOverride, MaterialCreateType type, bool useMeshShader, bool useGeometryShader,
		const PipelineCreateSettings& settings);
}
