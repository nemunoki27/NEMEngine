#include "AssetDependencyResolver.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/Database/AssetFileUtility.h>
#include <Engine/Core/Assets/Database/AssetDependencyScanner.h>
#include <Engine/Core/Assets/Utility/AssetTypeResolver.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>
#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>
#include <Engine/Core/World/Scene/Serialization/SceneAssetStorage.h>
#include <Engine/Core/Rendering/ShaderGraph/ShaderGraphArtifactCache.h>

using namespace Engine;
using AssetFileUtility::LoadJsonFileNoThrow;

std::vector<Engine::AssetID> AssetDependencyResolver::ExtractDependencies(const AssetDatabase& database, const AssetMeta& meta,
	std::vector<AssetDatabaseIssue>& issues) {

	std::vector<AssetID> dependencies;

	// JSONベースのアセットだけが内部に参照を持つ
	if (!AssetTypeResolver::IsJsonAssetType(meta.type)) {
		return dependencies;
	}

	const std::filesystem::path fullPath = database.ResolveAssetPath(meta.assetPath);
	if (fullPath.empty()) {
		return dependencies;
	}
	// Shader種別には.hlsl/.hlsli等の非JSONも含まれるため、実体が.jsonのものだけ解析する
	if (Algorithm::ToLower(Algorithm::PathToUTF8(fullPath.extension())) != ".json") {
		return dependencies;
	}

	nlohmann::json data = LoadJsonFileNoThrow(fullPath);
	if (!data.is_object() && !data.is_array()) {
		return dependencies;
	}
	if (RuntimePaths::IsProductBuild() && data.is_object()) {
		// 製品のシェーダーソース参照はCook済みデータが所有する
		if (meta.type == AssetType::Shader) {
			data.erase("stages");
			data.erase("sourceShader");
		}
		// 製品では編集用ピッキングの依存先を使用しない
		if (meta.type == AssetType::Material &&
			data.contains("passes") && data["passes"].is_array()) {
			auto& passes = data["passes"];
			passes.erase(std::remove_if(passes.begin(), passes.end(),
				[](const nlohmann::json& pass) {
					return pass.is_object() && pass.value("passKind", "") == "EditorPicking";
				}), passes.end());
		}
	}

	// 既知の参照キー配下からGUIDとシェーダー等の論理パスを収集する
	std::unordered_map<AssetID, AssetType> candidates;
	std::unordered_map<std::string, AssetType> pathCandidates;
	AssetDependencyScanner::ScanReferences(data, candidates, pathCandidates);
	// シーンから分離したActor内のスクリプト参照も依存先へ含める
	if (meta.type == AssetType::Scene && data.contains("ExternalActors") && data["ExternalActors"].is_array()) {
		const auto actorRoot = SceneAssetStorage::ResolveActorRoot(fullPath, meta.guid);
		for (const auto& actorID : data["ExternalActors"]) {
			if (!actorID.is_string() || !TryParseUUID16Hex(actorID.get<std::string>())) continue;
			AssetDependencyScanner::ScanReferences(LoadJsonFileNoThrow(actorRoot / (actorID.get<std::string>() + ".actor.json")), candidates, pathCandidates);
		}
	}
	// 編集時の派生参照は元グラフから再生成され、通常アセットには登録されない
	if (!RuntimePaths::IsProductBuild() && meta.type == AssetType::Material) {
		const AssetID graphID = ParseAssetReference(data, "shaderGraph", nullptr, AssetType::ShaderGraph);
		const AssetMeta* graphMeta = database.Find(graphID);
		if (graphMeta && graphMeta->type == AssetType::ShaderGraph) {
			const auto graphData = LoadJsonFileNoThrow(database.ResolveFullPath(graphID));
			if (graphData.is_object() && graphData.contains("domain") && graphData["domain"].is_string() &&
				graphData.contains("target") && graphData["target"].is_string()) {
				// 参照IDに必要な種別だけを読み、ノードの解析はインポーターへ任せる
				ShaderGraphAsset graph;
				graph.domain = EnumAdapter<ShaderGraphDomain>::FromString(
					graphData["domain"].get<std::string>()).value_or(ShaderGraphDomain::Surface);
				graph.target = EnumAdapter<ShaderGraphTarget>::FromString(
					graphData["target"].get<std::string>()).value_or(ShaderGraphTarget::Mesh);
				const auto artifact = ShaderGraphArtifactCache::DescribeReferences(graph, graphID);
				const auto removeGenerated = [&](AssetID id, AssetType type) {
					const auto found = candidates.find(id);
					if (id && found != candidates.end() && found->second == type) {
						candidates.erase(found);
					}
				};
				for (const AssetID id : { artifact.opaqueShaderID, artifact.transparentShaderID,
					artifact.depthShaderID, artifact.pickingShaderID, artifact.computeShaderID,
					artifact.rayTracingShaderID }) {
					removeGenerated(id, AssetType::Shader);
				}
				for (const AssetID id : { artifact.opaquePipelineID, artifact.transparentPipelineID,
					artifact.depthPipelineID, artifact.pickingPipelineID, artifact.computePipelineID,
					artifact.rayTracingPipelineID }) {
					removeGenerated(id, AssetType::RenderPipeline);
				}
			}
		}
	}
	for (const auto& [assetPath, expectedType] : pathCandidates) {

		const AssetMeta* referenced = database.FindByPath(assetPath);
		if (!referenced) {
			issues.push_back({ AssetDatabaseIssueType::MissingReference, meta.guid, {},
				expectedType, AssetType::Unknown, meta.assetPath, assetPath,
				"missing path reference" });
			continue;
		}
		candidates.emplace(referenced->guid, expectedType);
	}

	dependencies.reserve(candidates.size());
	for (const auto& [referencedID, expectedType] : candidates) {

		const AssetMeta* referenced = database.Find(referencedID);
		if (!referenced) {

			issues.push_back({ AssetDatabaseIssueType::MissingReference, meta.guid, referencedID,
				expectedType, AssetType::Unknown, meta.assetPath, {}, "missing reference" });
		} else if (expectedType != AssetType::Unknown && referenced->type != expectedType) {

			issues.push_back({ AssetDatabaseIssueType::ReferenceTypeMismatch, meta.guid, referencedID,
				expectedType, referenced->type, meta.assetPath, referenced->assetPath, "type mismatch" });
		}
		dependencies.emplace_back(referencedID);
	}
	return dependencies;
}
