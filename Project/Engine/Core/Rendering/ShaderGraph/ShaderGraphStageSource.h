#pragma once

//============================================================================
//	include
//============================================================================
#include "ShaderGraphExpressionCompiler.h"

namespace Engine::ShaderGraphStageSource {

	// 表面のShaderを生成する
	std::string BuildSurfaceSource(const ShaderGraphAsset& graph, ShaderGraphExpressionCompiler& context);
	// Mesh描画のShaderを生成する
	std::string BuildMeshPixelSource(std::string_view surfaceIncludeFile, bool transparent);
	// Mesh描画のShaderを生成する
	std::string BuildMeshAuxiliaryPixelSource(std::string_view surfaceIncludeFile, bool picking);
	// Mesh描画のShaderを生成する
	std::string BuildMeshVertexSource(
		const ShaderGraphAsset& graph, std::string_view surfaceIncludeFile, ShaderGraphExpressionCompiler& context);
	// GI用の頂点変形を生成する
	std::string BuildMeshGIVertexSource(const ShaderGraphAsset& graph, std::string_view surfaceIncludeFile,
		ShaderGraphExpressionCompiler& context);
	std::string BuildPrimitiveGIVertexSource(const ShaderGraphAsset& graph, std::string_view surfaceIncludeFile,
		ShaderGraphExpressionCompiler& context);
	// Mesh描画のShaderを生成する
	std::string BuildMeshShaderSource(
		const ShaderGraphAsset& graph, std::string_view surfaceIncludeFile, ShaderGraphExpressionCompiler& context);
	// 形状の頂点のShaderを生成する
	std::string BuildPrimitiveVertexSource(
		const ShaderGraphAsset& graph, std::string_view surfaceIncludeFile, ShaderGraphExpressionCompiler& context);
	// 形状の頂点のShaderを生成する
	std::string BuildPrimitiveMeshShaderSource(
		const ShaderGraphAsset& graph, std::string_view surfaceIncludeFile, ShaderGraphExpressionCompiler& context);
	// 形状の頂点のShaderを生成する
	std::string BuildPrimitive2DVertexSource(
		const ShaderGraphAsset& graph, std::string_view surfaceIncludeFile, ShaderGraphExpressionCompiler& context);
	// 色と輪郭のShaderを生成する
	std::string BuildPrimitivePixelSource(
		std::string_view surfaceIncludeFile, const ShaderGraphExpressionCompiler& context, bool transparent);
	// 色と輪郭のShaderを生成する
	std::string BuildUnlitPixelSource(
		ShaderGraphTarget target, std::string_view surfaceIncludeFile, const ShaderGraphExpressionCompiler& context);
	// 色と輪郭のShaderを生成する
	std::string BuildParticlePixelSource(const ShaderGraphAsset& graph, std::string_view surfaceIncludeFile);
	// 色と輪郭のShaderを生成する
	std::string BuildUnlitOutlinePixelSource(
		ShaderGraphTarget target, std::string_view surfaceIncludeFile, const ShaderGraphExpressionCompiler& context);
	// 色と輪郭のShaderを生成する
	std::string BuildPixelSource(const ShaderGraphAsset& graph, std::string_view surfaceIncludeFile,
		const ShaderGraphExpressionCompiler& context, bool transparent);
	// RayTracingのShaderを生成する
	std::string BuildGIMaterialSource(std::string_view surfaceIncludeFile, const ShaderGraphExpressionCompiler& context);
	std::string BuildRayTracingSource(std::string_view surfaceIncludeFile, const ShaderGraphExpressionCompiler& context);
	// RayTracingのShaderを生成する
	std::string BuildRayTracingEffectSource(const ShaderGraphAsset& graph, ShaderGraphExpressionCompiler& context);
	// 画面効果のShaderを生成する
	std::string BuildPostProcessSource(const ShaderGraphAsset& graph, ShaderGraphExpressionCompiler& context);
} // Engine::ShaderGraphStageSource
