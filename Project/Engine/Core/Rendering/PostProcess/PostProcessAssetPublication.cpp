#include "PostProcessAssetPublication.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Assets/Database/AssetDocumentPublication.h>
#include <Engine/Core/Assets/Database/AssetDocumentRecovery.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonFile.h>
#include <Engine/Core/Foundation/Utility/Algorithm/StringUtility.h>

// c++
#include <exception>
#include <vector>

namespace {

	using namespace Engine;
	constexpr const char* kGeneratedBy = "PostProcessAssetGenerator";

	// 標準効果の既定値を設定する
	nlohmann::json DefaultParameters(const PostProcessAssetSource& source) {

		if (!source.builtin) {
			return nlohmann::json::object();
		}
		const auto name = Algorithm::ToLower(source.name);
		if (name == "invert" || name == "grayscale") {
			return {{"strength", 1.0f}};
		}
		if (name == "vignette") {
			return {{"intensity", 0.45f}, {"radius", 0.75f}, {"softness", 0.35f}};
		}
		if (name == "blurhorizontal" || name == "blurvertical") {
			return {{"radius", 1.0f}};
		}
		if (name == "bloomprefilter") {
			return {{"threshold", 1.0f}, {"knee", 0.5f}, {"intensity", 1.0f}};
		}
		if (name == "bloomcomposite") {
			return {{"intensity", 0.75f}};
		}
		return nlohmann::json::object();
	}

	// 生成元を文書へ記録する
	nlohmann::json Metadata(nlohmann::json document, const PostProcessAssetSource& source, bool shader) {

		document["generated"] = true;
		document["generatedBy"] = kGeneratedBy;
		if (source.builtin || shader) {
			document["sourceShader"] = source.sourceShader;
		}
		return document;
	}

	// 読込時の状態を保持し、手書き文書は変更しない
	bool Prepare(AssetDatabase& database, const std::string& path, AssetType type, nlohmann::json document,
		AssetDocumentChange& change, std::string& diagnostic, bool preserve = false) {

		if (!AssetDocumentPublication::Prepare(database, path, type, change, diagnostic)) {
			return false;
		}
		if (preserve && change.fileRevision == "missing") {
			diagnostic = "生成元のShader文書が見つかりません";
			return false;
		}
		// 既存文書の生成元を確認
		if (change.fileRevision != "missing") {
			nlohmann::json current;
			if (!JsonFile::TryLoad(change.filePath, current) || !current.is_object()) {
				diagnostic = "生成先のAsset文書を読み込めません";
				return false;
			}
			if (preserve || !current.value("generated", false) || current.value("generatedBy", std::string{}) != kGeneratedBy) {
				document = std::move(current);
			}
		}
		change.document = std::move(document);
		return true;
	}
}

//============================================================================
//	PostProcessAssetPublication namespaceMethods
//============================================================================
namespace {

	// 保存対象を一組にして成功後に結果を渡す
	bool PublishSources(AssetDatabase& database, std::span<const PostProcessAssetSource> sources, std::vector<AssetID>& output,
		std::string& diagnostic) {

		diagnostic.clear();
		try {
			// 全文書の識別子を保存前に確定
			std::vector<AssetDocumentChange> changes;
			changes.reserve(sources.size() * 3);
			std::vector<AssetID> identifiers;
			identifiers.reserve(sources.size());
			for (const auto& source : sources) {
				AssetID shader{};
				// Shaderの保存候補を作成
				if (!source.shaderPath.empty()) {
					AssetDocumentChange change;
					auto document =
						Metadata({{"name", source.name + "Shader"},
									 {"stages", nlohmann::json::array({{{"stage", "CS"}, {"file", source.shaderFile},
													{"entry", "main"}, {"profile", "cs_6_0"}}})}},
							source, true);
					if (!Prepare(database, source.shaderPath, AssetType::Shader, std::move(document), change, diagnostic,
							source.shaderFile.empty())) {
						return false;
					}
					shader = change.metadata.guid;
					changes.push_back(std::move(change));
				}
				if (!shader) {
					diagnostic = "生成元のShaderを取得できません";
					return false;
				}
				// Shaderの候補GUIDでPipelineを作成
				AssetDocumentChange pipeline;
				auto pipelineDocument =
					Metadata({{"name", source.name + "Pipeline"},
								 {"variants", nlohmann::json::array({{{"kind", "Compute"}, {"pipelineType", "Compute"},
												  {"shader", ToAssetReferenceJson(shader)}}})}},
						source, false);
				if (!Prepare(database, source.pipelinePath, AssetType::RenderPipeline, std::move(pipelineDocument), pipeline,
						diagnostic)) {
					return false;
				}
				const auto pipelineID = pipeline.metadata.guid;
				changes.push_back(std::move(pipeline));
				// Pipelineの候補GUIDでMaterialを作成
				AssetDocumentChange material;
				auto materialDocument = Metadata(
					{{"name", source.name + "Material"}, {"domain", "Compute"},
						{"passes", nlohmann::json::array({{{"passKind", "PostProcess"},
									   {"pipeline", ToAssetReferenceJson(pipelineID)}, {"preferredVariant", "Compute"}}})},
						{"parameters", DefaultParameters(source)}},
					source, false);
				if (!Prepare(database, source.materialPath, AssetType::Material, std::move(materialDocument), material,
						diagnostic)) {
					return false;
				}
				const auto materialID = material.metadata.guid;
				changes.push_back(std::move(material));
				identifiers.push_back(materialID);
			}

			// 文書とmetaと索引を同じ操作で確定
			const auto scope = AssetDocumentRecovery::MakeScope(AssetDocumentSaveKind::GeneratedRender);
			if (!AssetDocumentPublication::Commit(database, changes, scope, diagnostic)) {
				return false;
			}
			// 成功した識別子だけを公開
			output = std::move(identifiers);
			return true;
		} catch (const std::exception& error) {
			diagnostic = error.what();
			return false;
		}
	}

}

bool Engine::PostProcessAssetPublication::PublishBatch(AssetDatabase& database, std::span<const PostProcessAssetSource> sources,
	std::vector<AssetID>& output, std::string& diagnostic) {

	return PublishSources(database, sources, output, diagnostic);
}

Engine::AssetID Engine::PostProcessAssetPublication::Publish(
	AssetDatabase& database, const PostProcessAssetSource& source, std::string& diagnostic) {

	std::vector<AssetID> identifiers;
	return PublishSources(database, std::span(&source, 1), identifiers, diagnostic) ? identifiers.front() : AssetID{};
}
