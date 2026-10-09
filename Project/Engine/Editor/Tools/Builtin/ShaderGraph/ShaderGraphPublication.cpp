#include "ShaderGraphPublication.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Assets/Database/AssetDocumentPublication.h>
#include <Engine/Core/Assets/Database/AssetDocumentRecovery.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonFileJournal.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonSerializer.h>
#include <Engine/Core/Foundation/Serialization/StorageFileUtility.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>
#include <Engine/Core/Rendering/Renderer/Pipeline/RenderPipelineRunner.h>
#include <Engine/Core/Rendering/ShaderGraph/ShaderGraphArtifactCache.h>

// c++
#include <utility>
#include <vector>

namespace {

	// 保存先と選択中のGraphを照合する
	bool PrepareGraphDocument(Engine::AssetDatabase& database, const std::filesystem::path& path, Engine::AssetID expectedID,
		Engine::AssetDocumentChange& change, std::string& status) {

		if (!Engine::AssetDocumentPublication::Prepare(
				database, Engine::RuntimePaths::ToAssetPath(path), Engine::AssetType::ShaderGraph, change, status)) {
			return false;
		}
		if (expectedID && change.metadata.guid != expectedID) {
			status = "保存先のGraphと選択中のGUIDが一致しません";
			return false;
		}
		if (!expectedID && change.fileRevision != "missing") {
			status = "同名のグラフが存在します";
			return false;
		}
		return true;
	}

	// 完成したShaderとPipelineを描画側へ渡す
	void PublishShaderGraphArtifact(
		const Engine::EditorToolContext& context, Engine::ShaderGraphArtifact artifact, Engine::MaterialAsset material) {

		if (!context.panelContext || !context.panelContext->renderPipeline) {
			return;
		}

		Engine::RenderAssetLibrary& library = context.panelContext->renderPipeline->GetRenderAssetLibrary();
		library.RegisterDerivedShader(std::move(artifact.opaqueShader));
		library.RegisterDerivedShader(std::move(artifact.transparentShader));
		library.RegisterDerivedShader(std::move(artifact.depthShader));
		library.RegisterDerivedShader(std::move(artifact.pickingShader));
		library.RegisterDerivedShader(std::move(artifact.outlineShader));
		library.RegisterDerivedShader(std::move(artifact.computeShader));
		library.RegisterDerivedShader(std::move(artifact.rayTracingShader));
		// PreviewのGIも編集後のGraphへ差し替える
		library.RegisterDerivedShader(std::move(artifact.giMaterialShader));
		library.RegisterDerivedShader(std::move(artifact.giVertexShader));
		library.RegisterDerivedShader(std::move(artifact.giReflectionShader));
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

	return Engine::Algorithm::PathToUTF8(graphPath.stem().stem());
}

Engine::AssetID Engine::ShaderGraphPublication::SaveGraph(AssetDatabase& database, const ShaderGraphAsset& graph,
	const std::filesystem::path& path, AssetID expectedID, std::string& status) {

	// 文書とmetaを揃えてから索引を公開する
	std::vector<AssetDocumentChange> changes(1);
	if (!PrepareGraphDocument(database, path, expectedID, changes.front(), status)) {
		return {};
	}
	changes.front().document = ToJson(graph);
	if (!AssetDocumentPublication::Commit(
			database, changes, AssetDocumentRecovery::MakeScope(AssetDocumentSaveKind::ShaderGraph), status)) {
		return {};
	}
	return changes.front().metadata.guid;
}

bool Engine::ShaderGraphPublication::CompileAndPublish(const EditorToolContext& context, AssetDatabase& database,
	const ShaderGraphAsset& graph, AssetID assetID, const std::filesystem::path& graphPath, AssetID& materialID,
	std::vector<ShaderGraphDiagnostic>& diagnostics, std::string& status) {

	ShaderGraphArtifact artifact{};
	if (!ShaderGraphArtifactCache::Compile(graph, assetID, artifact, &database, &diagnostics)) {
		status = diagnostics.empty() ? "派生Shaderを生成できませんでした" : diagnostics.front().message;
		return false;
	}

	const std::string stem = GraphFileStem(graphPath);
	const std::filesystem::path materialPath = graphPath.parent_path() / Algorithm::PathFromUTF8(stem + ".material.json");
	MaterialAsset material = ShaderGraphArtifactCache::CreateMaterial(graph, assetID);
	ShaderGraphArtifactCache::ApplyToMaterial(artifact, material);
	// Graphと生成MaterialのGUIDを保存前に揃える
	std::vector<AssetDocumentChange> changes(2);
	if (!AssetDocumentPublication::Prepare(
			database, RuntimePaths::ToAssetPath(materialPath), AssetType::Material, changes[0], status) ||
		!PrepareGraphDocument(database, graphPath, assetID, changes[1], status)) {
		return false;
	}
	changes[0].document = ToJson(material);
	changes[1].document = ToJson(graph);

	// 文書とmetaを確定してから描画用の成果物を公開する
	if (!AssetDocumentPublication::Commit(
			database, changes, AssetDocumentRecovery::MakeScope(AssetDocumentSaveKind::ShaderGraph), status)) {
		return false;
	}
	materialID = changes[0].metadata.guid;

	if (context.panelContext && context.panelContext->renderPipeline) {

		RenderPipelineRunner& renderPipeline = *context.panelContext->renderPipeline;
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
		ShaderGraphArtifactCache::ApplyToMaterial(artifact, material);
		PublishShaderGraphArtifact(context, std::move(artifact), std::move(material));
	}
	return true;
}

bool Engine::ShaderGraphPublication::CompileAndPublishPreview(const EditorToolContext& context, AssetDatabase& database,
	const ShaderGraphAsset& graph, AssetID assetID, AssetID& materialID, std::vector<ShaderGraphDiagnostic>& diagnostics,
	std::string& status) {

	const AssetID previewGraphID = ShaderGraphArtifactCache::MakeDerivedID(assetID, 0x5052455649455747ull);
	ShaderGraphArtifact artifact{};
	if (!ShaderGraphArtifactCache::Compile(graph, previewGraphID, artifact, &database, &diagnostics)) {

		status = diagnostics.empty() ? "プレビューを生成できませんでした" : diagnostics.front().message;
		return false;
	}

	MaterialAsset material = ShaderGraphArtifactCache::CreateMaterial(graph, previewGraphID);
	ShaderGraphArtifactCache::ApplyToMaterial(artifact, material);
	materialID = ShaderGraphArtifactCache::MakeDerivedID(assetID, 0x505245564945574dull);
	material.guid = materialID;
	PublishShaderGraphArtifact(context, std::move(artifact), std::move(material));
	status = "プレビューを更新しました";
	return true;
}
