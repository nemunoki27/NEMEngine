#include "ShaderGraphPublication.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Assets/Database/AssetDocumentPublication.h>
#include <Engine/Core/Assets/Database/AssetDocumentRecovery.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonFileJournal.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonFile.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonSerializer.h>
#include <Engine/Core/Foundation/Serialization/StorageFileUtility.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>
#include <Engine/Core/Rendering/Renderer/Pipeline/RenderPipelineRunner.h>
#include <Engine/Core/Rendering/ShaderGraph/ShaderGraphArtifactCache.h>
#include <Engine/Core/Rendering/ShaderGraph/ShaderGraphParameterDefaults.h>
#include <Engine/Core/Rendering/ShaderGraph/ShaderGraphAssetReferences.h>

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
			status = "保存先のグラフと選択中のGUIDが一致しません";
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

		if (artifact.particleGPUShaderID) context.panelContext->renderPipeline->ReloadShader(artifact.particleGPUShaderID);
		Engine::RenderAssetLibrary& library = context.panelContext->renderPipeline->GetRenderAssetLibrary();
		library.RegisterDerivedShader(std::move(artifact.opaqueShader));
		library.RegisterDerivedShader(std::move(artifact.transparentShader));
		library.RegisterDerivedShader(std::move(artifact.particleGPUShader));
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
	if (expectedID) {
		const auto materialPath = path.parent_path() / Algorithm::PathFromUTF8(GraphFileStem(path) + ".material.json");
		nlohmann::json document;
		MaterialAsset previous;
		ShaderGraphAsset source;
		if (std::filesystem::exists(materialPath) && JsonFile::TryLoad(materialPath, document) &&
			!document.contains("graphOverrides") && FromJson(document, previous) && previous.shaderGraph == expectedID &&
			ShaderGraphAssetReferences::LoadSource(expectedID, &database, source) &&
			(WriteMaterialInstance(ShaderGraphParameterDefaults::Build(source)) != WriteMaterialInstance(ShaderGraphParameterDefaults::Build(graph)))) {
			// 元の既定値が変わる前に旧Materialの上書きだけを記録する
			auto& migration = changes.emplace_back();
			if (!AssetDocumentPublication::Prepare(database, RuntimePaths::ToAssetPath(materialPath), AssetType::Material, migration, status))
				return {};
			// Prepare後の内容を読み取り外部で保存された共有値も取り込む
			if (!JsonFile::TryLoad(materialPath, document) || !FromJson(document, previous) || previous.shaderGraph != expectedID) {
				status = "旧マテリアルの公開値を移行できませんでした";
				return {};
			}
			migration.document = std::move(document);
			if (!migration.document.contains("graphOverrides")) {
				ShaderGraphParameterDefaults::ImportLegacyOverrides(source, previous);
				migration.document["graphOverrides"] = ToJson(previous)["graphOverrides"];
			}
		}
	}
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
		status = diagnostics.empty() ? "派生シェーダーを生成できませんでした" : diagnostics.front().message;
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
	if (changes[0].fileRevision != "missing") {
		nlohmann::json document;
		MaterialAsset previous;
		if (!JsonFile::TryLoad(materialPath, document) || !FromJson(document, previous)) {
			status = "既存のマテリアルを読み込めませんでした";
			return false;
		}
		if (previous.shaderGraph == assetID) {
			ShaderGraphAsset source;
			if (ShaderGraphAssetReferences::LoadSource(assetID, &database, source)) {
				if (previous.name != source.name) material.name = previous.name;
				// 旧形式では保存済みの公開値と元グラフの差分を引き継ぐ
				if (!document.contains("graphOverrides")) {
					ShaderGraphParameterDefaults::ImportLegacyOverrides(source, previous);
				}
			}
			ShaderGraphParameterDefaults::RestoreOverrides(graph, previous, material);
		}
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
