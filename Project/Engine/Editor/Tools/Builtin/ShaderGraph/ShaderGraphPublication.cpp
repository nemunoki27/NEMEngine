#include "ShaderGraphPublication.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonSerializer.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>
#include <Engine/Core/Rendering/Renderer/Pipeline/RenderPipelineRunner.h>
#include <Engine/Core/Rendering/ShaderGraph/ShaderGraphArtifactCache.h>

std::string Engine::ShaderGraphPublication::GraphFileStem(const std::filesystem::path& graphPath) {

	return Engine::Algorithm::PathToUTF8(
		graphPath.stem().stem());
}

bool Engine::ShaderGraphPublication::CompileAndPublish(const EditorToolContext& context, AssetDatabase& database, const ShaderGraphAsset& graph,
	AssetID assetID, const std::filesystem::path& graphPath, AssetID& materialID,
	std::vector<ShaderGraphDiagnostic>& diagnostics, std::string& status) {

	ShaderGraphArtifact artifact{};
	if (!ShaderGraphArtifactCache::Compile(
		graph, assetID, artifact, &database)) {
		diagnostics = artifact.compileOutput.diagnostics;
		status =
			artifact.compileOutput.diagnostics.empty() ?
			"派生Shaderを生成できませんでした" :
			artifact.compileOutput.diagnostics.front().message;
		return false;
	}
	diagnostics = artifact.compileOutput.diagnostics;

	const std::string stem = GraphFileStem(graphPath);
	const std::filesystem::path materialPath =
		graphPath.parent_path() /
		Algorithm::PathFromUTF8(stem + ".material.json");
	MaterialAsset material =
		ShaderGraphArtifactCache::CreateMaterial(
			graph, assetID);
	ShaderGraphArtifactCache::ApplyToMaterial(
		artifact, material);
	JsonAdapter::Save(materialPath, ToJson(material));
	materialID = database.ImportOrGet(
		RuntimePaths::ToAssetPath(materialPath),
		AssetType::Material);
	if (!materialID) {
		status =
			"Materialを登録できませんでした";
		return false;
	}

	JsonAdapter::Save(graphPath, ToJson(graph));
	if (context.panelContext &&
		context.panelContext->renderPipeline) {

		RenderPipelineRunner& renderPipeline =
			*context.panelContext->renderPipeline;
		renderPipeline.ReloadMaterial(materialID);
		if (artifact.opaqueShaderID) {
			renderPipeline.ReloadShader(artifact.opaqueShaderID);
		}
		if (artifact.transparentShaderID) {
			renderPipeline.ReloadShader(artifact.transparentShaderID);
		}
		if (artifact.depthShaderID) {
			renderPipeline.ReloadShader(artifact.depthShaderID);
		}
		if (artifact.pickingShaderID) {
			renderPipeline.ReloadShader(artifact.pickingShaderID);
		}
		if (artifact.computeShaderID) {
			renderPipeline.ReloadShader(artifact.computeShaderID);
		}
		if (artifact.rayTracingShaderID) {
			renderPipeline.ReloadShader(artifact.rayTracingShaderID);
		}
		RenderAssetLibrary& library =
			renderPipeline.GetRenderAssetLibrary();
		library.RegisterDerivedShader(
			std::move(artifact.opaqueShader));
		library.RegisterDerivedShader(
			std::move(artifact.transparentShader));
		library.RegisterDerivedShader(
			std::move(artifact.depthShader));
		library.RegisterDerivedShader(
			std::move(artifact.pickingShader));
		library.RegisterDerivedShader(
			std::move(artifact.computeShader));
		library.RegisterDerivedShader(
			std::move(artifact.rayTracingShader));
		library.RegisterDerivedPipeline(
			std::move(artifact.opaquePipeline));
		library.RegisterDerivedPipeline(
			std::move(artifact.transparentPipeline));
		library.RegisterDerivedPipeline(
			std::move(artifact.depthPipeline));
		library.RegisterDerivedPipeline(
			std::move(artifact.pickingPipeline));
		library.RegisterDerivedPipeline(
			std::move(artifact.computePipeline));
		library.RegisterDerivedPipeline(
			std::move(artifact.rayTracingPipeline));
		material.guid = materialID;
		ShaderGraphArtifactCache::ApplyToMaterial(
			artifact, material);
		library.RegisterDerivedMaterial(std::move(material));
	}
	return true;
}
