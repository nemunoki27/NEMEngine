#pragma once

//============================================================================
//	include
//============================================================================
#include "ShaderCook.h"
#include <Engine/Core/Rendering/DxObject/Core/DxShaderCompiler.h>

namespace Engine {

	class AssetDatabase;

	//============================================================================
	//	ShaderCookCompiler class
	//	ShaderのStageをコンパイルしてCook成果物へ書き出す
	//============================================================================
	class ShaderCookCompiler {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		ShaderCookCompiler(AssetDatabase& database, const std::filesystem::path& outputRoot);
		// Stageの成果物と情報を追加する
		bool Cook(ShaderAsset shader, std::string_view sourceName, nlohmann::json& cookedManifest,
			ShaderCookResult& outResult, std::string& outError);
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		AssetDatabase& database_;
		std::filesystem::path outputRoot_;
		DxShaderCompiler compiler_{};
	};
}
