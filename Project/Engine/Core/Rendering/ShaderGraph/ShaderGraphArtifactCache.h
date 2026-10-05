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

		AssetID opaqueShaderID{};					// 不透明Shaderの派生ID
		AssetID transparentShaderID{};				// 透明Shaderの派生ID
		AssetID depthShaderID{};					// 深度Shaderの派生ID
		AssetID pickingShaderID{};					// 選択Shaderの派生ID
		AssetID outlineShaderID{};					// 輪郭Shaderの派生ID
		AssetID computeShaderID{};					// Compute Shaderの派生ID
		AssetID rayTracingShaderID{};				// DXR Shaderの派生ID
		AssetID opaquePipelineID{};					// 不透明Pipelineの派生ID
		AssetID transparentPipelineID{};			// 透明Pipelineの派生ID
		AssetID depthPipelineID{};					// 深度Pipelineの派生ID
		AssetID pickingPipelineID{};				// 選択Pipelineの派生ID
		AssetID outlinePipelineID{};				// 輪郭Pipelineの派生ID
		AssetID computePipelineID{};				// Compute Pipelineの派生ID
		AssetID rayTracingPipelineID{};				// DXR Pipelineの派生ID
		std::filesystem::path root;					// 成果物の保存先
		std::filesystem::path surfacePath;			// 共通Shaderの保存先
		std::filesystem::path opaquePixelPath;		// 不透明Shaderの保存先
		std::filesystem::path transparentPixelPath; // 透明Shaderの保存先
		std::filesystem::path depthPixelPath;		// 深度Shaderの保存先
		std::filesystem::path pickingPixelPath;		// 選択Shaderの保存先
		std::filesystem::path outlinePixelPath;		// 輪郭Shaderの保存先
		std::filesystem::path vertexPath;			// 頂点Shaderの保存先
		std::filesystem::path meshPath;				// Mesh Shaderの保存先
		std::filesystem::path computePath;			// Compute Shaderの保存先
		std::filesystem::path rayTracingPath;		// DXR Shaderの保存先
		ShaderAsset opaqueShader{};					// 不透明Shaderの構成
		ShaderAsset transparentShader{};			// 透明Shaderの構成
		ShaderAsset depthShader{};					// 深度Shaderの構成
		ShaderAsset pickingShader{};				// 選択Shaderの構成
		ShaderAsset outlineShader{};				// 輪郭Shaderの構成
		ShaderAsset computeShader{};				// Compute Shaderの構成
		ShaderAsset rayTracingShader{};				// DXR Shaderの構成
		RenderPipelineAsset opaquePipeline{};		// 不透明Pipelineの構成
		RenderPipelineAsset transparentPipeline{};	// 透明Pipelineの構成
		RenderPipelineAsset depthPipeline{};		// 深度Pipelineの構成
		RenderPipelineAsset pickingPipeline{};		// 選択Pipelineの構成
		RenderPipelineAsset outlinePipeline{};		// 輪郭Pipelineの構成
		RenderPipelineAsset computePipeline{};		// Compute Pipelineの構成
		RenderPipelineAsset rayTracingPipeline{};	// DXR Pipelineの構成
		ShaderGraphCompileOutput compileOutput{};	// 解析とShader生成の結果
	};

	//============================================================================
	//	ShaderGraphArtifactCache class
	//	ShaderGraphの生成と成果物保存をまとめる
	//============================================================================
	class ShaderGraphArtifactCache {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		ShaderGraphArtifactCache() = delete;
		~ShaderGraphArtifactCache() = delete;

		// グラフをコンパイルして派生成果物を更新
		static bool Compile(const ShaderGraphAsset& graph, AssetID graphID, ShaderGraphArtifact& outArtifact,
			AssetDatabase* database = nullptr, std::vector<ShaderGraphDiagnostic>* diagnostics = nullptr);
		// Renderer向けのMaterial構成を生成
		static MaterialAsset CreateMaterial(const ShaderGraphAsset& graph, AssetID graphID);
		// 生成ShaderをMaterialの描画パスへ適用
		static void ApplyToMaterial(const ShaderGraphArtifact& artifact, MaterialAsset& material);
		// コンパイルせずに生成予定の参照IDを取得
		static ShaderGraphArtifact DescribeReferences(const ShaderGraphAsset& graph, AssetID graphID);
		// グラフと用途から決定的な派生IDを生成
		static AssetID MakeDerivedID(AssetID graphID, uint64_t discriminator);
	};
}
