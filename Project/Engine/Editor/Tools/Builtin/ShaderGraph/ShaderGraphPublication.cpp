#include "ShaderGraphPublication.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonFileJournal.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonSerializer.h>
#include <Engine/Core/Foundation/Serialization/StorageFileUtility.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>
#include <Engine/Core/Rendering/Renderer/Pipeline/RenderPipelineRunner.h>
#include <Engine/Core/Rendering/ShaderGraph/ShaderGraphArtifactCache.h>

// c++
#include <algorithm>

namespace {

	Engine::JsonFileJournal::Scope MakeShaderGraphSaveScope() {

		return {
			Engine::RuntimePaths::GetSavedRoot() /
				"ShaderGraphAssetRecovery",
			[](const std::filesystem::path& path) {
				return Engine::StorageFileUtility::IsInside(
					path, Engine::RuntimePaths::GetGameAssetsRoot()) ||
					Engine::StorageFileUtility::IsInside(
						path, Engine::RuntimePaths::GetEngineAssetsRoot());
			},
		};
	}

	bool RecoverShaderGraphFiles(
		const std::filesystem::path& recovery,
		std::string& error) {

		return Engine::JsonFileJournal::Recover(
			MakeShaderGraphSaveScope(), recovery, error,
			[](const std::filesystem::path&) {});
	}

	void PublishShaderGraphArtifact(
		const Engine::EditorToolContext& context,
		Engine::ShaderGraphArtifact artifact,
		Engine::MaterialAsset material) {

		if (!context.panelContext ||
			!context.panelContext->renderPipeline) {
			return;
		}

		Engine::RenderAssetLibrary& library =
			context.panelContext->renderPipeline->GetRenderAssetLibrary();
		library.RegisterDerivedShader(std::move(artifact.opaqueShader));
		library.RegisterDerivedShader(std::move(artifact.transparentShader));
		library.RegisterDerivedShader(std::move(artifact.depthShader));
		library.RegisterDerivedShader(std::move(artifact.pickingShader));
		library.RegisterDerivedShader(std::move(artifact.outlineShader));
		library.RegisterDerivedShader(std::move(artifact.computeShader));
		library.RegisterDerivedShader(std::move(artifact.rayTracingShader));
		library.RegisterDerivedPipeline(std::move(artifact.opaquePipeline));
		library.RegisterDerivedPipeline(std::move(artifact.transparentPipeline));
		library.RegisterDerivedPipeline(std::move(artifact.depthPipeline));
		library.RegisterDerivedPipeline(std::move(artifact.pickingPipeline));
		library.RegisterDerivedPipeline(std::move(artifact.outlinePipeline));
		library.RegisterDerivedPipeline(std::move(artifact.computePipeline));
		library.RegisterDerivedPipeline(std::move(artifact.rayTracingPipeline));
		library.RegisterDerivedMaterial(std::move(material));
	}
}

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
	// Graphと生成Materialを同じ保存操作で確定
	std::string saveError;
	const std::vector<JsonFileChange> changes = {
		{ materialPath, ToJson(material) },
		{ graphPath, ToJson(graph) },
	};
	const std::vector<std::filesystem::path> recoveriesBefore =
		JsonFileJournal::GetRecoveries(MakeShaderGraphSaveScope());
	if (!JsonFileJournal::Commit(
		MakeShaderGraphSaveScope(), changes,
		"ShaderGraphの保存", saveError,
		RecoverShaderGraphFiles)) {

		status = saveError.empty() ?
			"GraphとMaterialを保存できませんでした" :
			saveError;
		return false;
	}
	materialID = database.ImportOrGet(
		RuntimePaths::ToAssetPath(materialPath),
		AssetType::Material);
	if (!materialID) {
		// 登録失敗時は同じ保存操作で確定したファイルを戻す
		const std::vector<std::filesystem::path> recoveriesAfter =
			JsonFileJournal::GetRecoveries(MakeShaderGraphSaveScope());
		for (auto it = recoveriesAfter.rbegin();
			it != recoveriesAfter.rend(); ++it) {

			if (std::find(recoveriesBefore.begin(),
				recoveriesBefore.end(), *it) != recoveriesBefore.end()) {
				continue;
			}
			std::string recoveryError;
			if (!RecoverShaderGraphFiles(*it, recoveryError)) {
				status = "Materialを登録できず、保存前の状態へ戻せませんでした: " +
					recoveryError;
				return false;
			}
			break;
		}
		status =
			"Materialを登録できませんでした";
		return false;
	}

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
		if (artifact.outlineShaderID) {
			renderPipeline.ReloadShader(artifact.outlineShaderID);
		}
		if (artifact.computeShaderID) {
			renderPipeline.ReloadShader(artifact.computeShaderID);
		}
		if (artifact.rayTracingShaderID) {
			renderPipeline.ReloadShader(artifact.rayTracingShaderID);
		}
		material.guid = materialID;
		ShaderGraphArtifactCache::ApplyToMaterial(
			artifact, material);
		PublishShaderGraphArtifact(
			context, std::move(artifact), std::move(material));
	}
	return true;
}

bool Engine::ShaderGraphPublication::CompileAndPublishPreview(
	const EditorToolContext& context, AssetDatabase& database,
	const ShaderGraphAsset& graph, AssetID assetID, AssetID& materialID,
	std::vector<ShaderGraphDiagnostic>& diagnostics, std::string& status) {

	const AssetID previewGraphID =
		ShaderGraphArtifactCache::MakeDerivedID(
			assetID, 0x5052455649455747ull);
	ShaderGraphArtifact artifact{};
	if (!ShaderGraphArtifactCache::Compile(
		graph, previewGraphID, artifact, &database)) {

		diagnostics = artifact.compileOutput.diagnostics;
		status = diagnostics.empty() ?
			"プレビューを生成できませんでした" :
			diagnostics.front().message;
		return false;
	}
	diagnostics = artifact.compileOutput.diagnostics;

	MaterialAsset material =
		ShaderGraphArtifactCache::CreateMaterial(
			graph, previewGraphID);
	ShaderGraphArtifactCache::ApplyToMaterial(artifact, material);
	materialID = ShaderGraphArtifactCache::MakeDerivedID(
		assetID, 0x505245564945574dull);
	material.guid = materialID;
	PublishShaderGraphArtifact(
		context, std::move(artifact), std::move(material));
	status = "プレビューを更新しました";
	return true;
}
