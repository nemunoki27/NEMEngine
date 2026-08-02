#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/ShaderGraph/ShaderGraphCompiler.h>

// c++
#include <filesystem>

namespace Engine {

	class AssetDatabase;

	//============================================================================
	//	ShaderGraphArtifact structures
	//============================================================================
	struct ShaderGraphArtifact {

		AssetID opaqueShaderID{};
		AssetID transparentShaderID{};
		AssetID depthShaderID{};
		AssetID pickingShaderID{};
		AssetID computeShaderID{};
		AssetID opaquePipelineID{};
		AssetID transparentPipelineID{};
		AssetID depthPipelineID{};
		AssetID pickingPipelineID{};
		AssetID computePipelineID{};
		std::filesystem::path root;
		std::filesystem::path surfacePath;
		std::filesystem::path opaquePixelPath;
		std::filesystem::path transparentPixelPath;
		std::filesystem::path depthPixelPath;
		std::filesystem::path pickingPixelPath;
		std::filesystem::path vertexPath;
		std::filesystem::path meshPath;
		std::filesystem::path computePath;
		ShaderAsset opaqueShader{};
		ShaderAsset transparentShader{};
		ShaderAsset depthShader{};
		ShaderAsset pickingShader{};
		ShaderAsset computeShader{};
		RenderPipelineAsset opaquePipeline{};
		RenderPipelineAsset transparentPipeline{};
		RenderPipelineAsset depthPipeline{};
		RenderPipelineAsset pickingPipeline{};
		RenderPipelineAsset computePipeline{};
		ShaderGraphCompileOutput compileOutput{};
	};

	//============================================================================
	//	ShaderGraphArtifactCache class
	//	再生成可能なShader Graph成果物をLibraryへ保存する
	//============================================================================
	class ShaderGraphArtifactCache {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		ShaderGraphArtifactCache() = delete;
		~ShaderGraphArtifactCache() = delete;

		// グラフをコンパイルして派生成果物を更新
		static bool Compile(const ShaderGraphAsset& graph,
			AssetID graphID, ShaderGraphArtifact& outArtifact,
			AssetDatabase* database = nullptr);
		// Renderer向けのMaterial構成を生成
		static MaterialAsset CreateMaterial(
			const ShaderGraphAsset& graph, AssetID graphID);
		// 生成ShaderをMaterialの描画パスへ適用
		static void ApplyToMaterial(
			const ShaderGraphArtifact& artifact, MaterialAsset& material);
		// グラフと用途から決定的な派生IDを生成
		static AssetID MakeDerivedID(AssetID graphID, uint64_t discriminator);
	};
} // Engine
