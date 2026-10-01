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
#include <Engine/Core/Rendering/Meshes/Import/MeshImportSettings.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonFile.h>

// c++
#include <algorithm>
#include <stdexcept>
#include <unordered_set>

using namespace Engine;
using AssetFileUtility::LoadJsonFileNoThrow;

std::vector<Engine::AssetID> AssetDependencyResolver::ExtractDependencies(const AssetDatabase& database, const AssetMeta& meta,
	std::vector<AssetDatabaseIssue>& issues) {

	std::vector<AssetID> dependencies;
	std::unordered_set<AssetID> collected;

	const std::filesystem::path fullPath = database.ResolveAssetPath(meta.assetPath);
	if (fullPath.empty()) {
		return dependencies;
	}
	if (meta.type == AssetType::Mesh) {
		const MeshImportSettings settings =
			ParseMeshImportSettings(meta.importerSettings);
		for (AssetID lod : settings.manualLODMeshes) {
			if (!lod || !collected.insert(lod).second) {
				continue;
			}
			const AssetMeta* referenced = database.Find(lod);
			if (!referenced) {
				issues.push_back({ AssetDatabaseIssueType::MissingReference,
					meta.guid, lod, AssetType::Mesh, AssetType::Unknown,
					meta.assetPath, {}, "missing manual LOD mesh" });
			} else if (referenced->type != AssetType::Mesh) {
				issues.push_back({ AssetDatabaseIssueType::ReferenceTypeMismatch,
					meta.guid, lod, AssetType::Mesh, referenced->type,
					meta.assetPath, referenced->assetPath,
					"manual LOD type mismatch" });
			}
			dependencies.emplace_back(lod);
		}
	}
	// 独自拡張子のJSONも解析し、Font本体やShaderソースは除く
	if (!AssetTypeResolver::IsJsonAssetFile(meta.type, fullPath)) {
		return dependencies;
	}

	nlohmann::json data;
	std::string diagnostic;
	if (!JsonFile::TryLoad(fullPath, data, &diagnostic) || (!data.is_object() && !data.is_array())) {
		throw std::runtime_error("Asset dependency read failed: " + meta.assetPath + " " + diagnostic);
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
	AssetDependencyScanner::IDReferences candidates;
	AssetDependencyScanner::PathReferences pathCandidates;
	AssetDependencyScanner::ScanReferences(data, candidates, pathCandidates);
	// シーンから分離したActor内のスクリプト参照も依存先へ含める
	if (meta.type == AssetType::Scene && data.contains("ExternalActors") && data["ExternalActors"].is_array()) {
		const auto actorRoot = SceneAssetStorage::ResolveActorRoot(fullPath, meta.guid);
		for (const auto& actorID : data["ExternalActors"]) {
			if (!actorID.is_string() || !TryParseUUID16Hex(actorID.get<std::string>())) {
				throw std::runtime_error("Invalid ExternalActor ID: " + meta.assetPath);
			}
			const auto actorPath = actorRoot / (actorID.get<std::string>() + ".actor.json");
			nlohmann::json actor;
			if (!JsonFile::TryLoad(actorPath, actor, &diagnostic) || !actor.is_object()) {
				throw std::runtime_error("ExternalActor read failed: " + Algorithm::PathToUTF8(actorPath) + " " + diagnostic);
			}
			AssetDependencyScanner::ScanReferences(actor, candidates, pathCandidates);
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
					auto [found, end] = candidates.equal_range(id);
					while (found != end) {
						if (found->second == type) {
							found = candidates.erase(found);
						} else {
							++found;
						}
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
		const auto [begin, end] = candidates.equal_range(referenced->guid);
		if (!std::any_of(begin, end, [expectedType](const auto& candidate) { return candidate.second == expectedType; })) {
			candidates.emplace(referenced->guid, expectedType);
		}
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
		if (collected.insert(referencedID).second) {
			dependencies.emplace_back(referencedID);
		}
	}
	return dependencies;
}
