#pragma once

//============================================================================
//	include
//============================================================================
#include "ShaderGraphCompiler.h"

namespace Engine {
	class AssetDatabase;
}

namespace Engine::ShaderGraphArtifactBuilder {

	// Pixel Shaderの構成を作成する
	Engine::ShaderAsset MakePixelShader(std::string_view name, Engine::AssetID shaderID, const std::filesystem::path& path,
		std::string_view entry, const std::vector<Engine::ShaderParameterMetadata>& parameters);
	// Compute Shaderの構成を作成する
	Engine::ShaderAsset MakeComputeShader(std::string_view name, Engine::AssetID shaderID, const std::filesystem::path& path,
		const std::vector<Engine::ShaderParameterMetadata>& parameters);
	// RayTracing Shaderの構成を作成する
	Engine::ShaderAsset MakeRayTracingShader(std::string_view name, Engine::AssetID shaderID, const std::filesystem::path& path,
		const std::vector<Engine::ShaderParameterMetadata>& parameters, bool renderFeature);
	// 基底PipelineへGraph設定を反映する
	bool MakeGraphPipeline(const Engine::ShaderGraphAsset& graph, const Engine::ShaderGraphCompileOutput& compileOutput,
		Engine::AssetID graphID, bool transparent, Engine::AssetDatabase* database, Engine::RenderPipelineAsset& outPipeline,
		Engine::AssetID& outPipelineID, Engine::AssetID baseOverride = {}, uint64_t derivedDiscriminator = 0,
		std::string_view nameSuffix = {});
	// 反射PipelineへGraph設定を反映する
	bool MakeRayTracingPipeline(const Engine::ShaderGraphAsset& graph, const Engine::ShaderGraphCompileOutput& compileOutput,
		Engine::AssetID graphID, Engine::AssetDatabase* database, Engine::RenderPipelineAsset& outPipeline,
		Engine::AssetID& outPipelineID);
}
