#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Assets/ShaderAsset.h>
#include <Engine/Core/Rendering/Assets/RenderPipelineAsset.h>

// c++
#include <filesystem>
#include <string>

namespace Engine {

	//============================================================================
	//	ShaderCook structures
	//============================================================================
	struct ShaderCookRequest {

		AssetID shader{};
		ShaderStage stage = ShaderStage::None;
		std::string entry = "main";
		std::string profile;
	};

	struct ShaderCookResult {

		size_t shaderCount = 0;
		size_t stageCount = 0;
		uintmax_t bytecodeSize = 0;
	};

	//============================================================================
	//	ShaderCook class
	//	製品向けDXILとReflectionの生成と読み込みを管理する
	//============================================================================
	class ShaderCook {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		ShaderCook() = delete;
		~ShaderCook() = delete;

		// GameBuild manifestに含まれるShader AssetをCookする
		static bool Cook(const std::filesystem::path& manifestPath,
			const std::filesystem::path& outputRoot,
			ShaderCookResult& outResult, std::string& outError);
		// 製品出力かどうか
		static bool IsCookedProduct();
		// Cook済みShader Assetメタデータを取得する
		static bool LoadShaderAsset(AssetID shaderID,
			ShaderAsset& outAsset);
		// Shader Graphから派生したPipelineを取得する
		static bool LoadPipelineAsset(AssetID pipelineID,
			RenderPipelineAsset& outAsset);
		// Cook済みDXILとReflectionを取得する
		static bool Load(const ShaderCookRequest& request,
			CompiledShader& outShader);
	};
} // Engine
