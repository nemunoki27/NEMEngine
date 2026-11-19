#include "ShaderGraphPreviewDependencies.h"
#include <Engine/Core/Rendering/ShaderGraph/ShaderGraphAssetReferences.h>

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Serialization/Json/JsonSerializer.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>
#include <Engine/Core/Foundation/Utility/Algorithm/HashUtility.h>

// c++
#include <algorithm>
#include <fstream>
#include <regex>
#include <unordered_set>

using namespace Engine;

	// 関数から参照するローカルincludeも更新判定へ含める
	uint64_t HashFunctionFile(const std::filesystem::path& path, std::unordered_set<std::string>& visited) {

		const std::string key = Algorithm::PathToUTF8(path.lexically_normal());
		if (!visited.insert(key).second) return 0;
		std::ifstream file(path, std::ios::binary);
		const std::string bytes{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
		uint64_t hash = Algorithm::MixHashString(14695981039346656037ull, bytes);
		static const std::regex includePattern("#\\s*include\\s*\"([^\"]+)\"");
		for (auto found = std::sregex_iterator(bytes.begin(), bytes.end(), includePattern);
			found != std::sregex_iterator(); ++found) {
			const auto included = path.parent_path() / Algorithm::PathFromUTF8((*found)[1].str());
			hash = Algorithm::MixHash(hash, HashFunctionFile(included, visited));
		}
		return hash;
	}

namespace {

	// 循環参照を避けてファイル内容を更新判定へ加える
	uint64_t HashDependencies(const ShaderGraphAsset& graph, const AssetDatabase* database,
		std::unordered_set<AssetID>& visited) {

		uint64_t hash = 14695981039346656037ull;
		for (const ShaderGraphNode& node : graph.nodes) {
			if (node.kind == ShaderGraphNodeKind::CustomFunction &&
				node.customFunctionSource == ShaderGraphCustomFunctionSource::File) {
				std::unordered_set<std::string> files;
				hash = Algorithm::MixHash(hash, HashFunctionFile(Algorithm::PathFromUTF8(node.functionFile), files));
			}
			if (node.kind == ShaderGraphNodeKind::SubGraph && node.subGraph && visited.insert(node.subGraph).second) {
				ShaderGraphAsset child{};
				if (ShaderGraphPreviewDependencies::LoadGraph(node.subGraph, database, child)) {
					hash = Algorithm::MixHashString(hash, ToJson(child).dump());
					hash = Algorithm::MixHash(hash, HashDependencies(child, database, visited));
				}
			}
		}
		return hash;
	}
}

ShaderGraphAsset Engine::ShaderGraphPreviewDependencies::ResolveFunctions(const ShaderGraphAsset& graph,
	const AssetDatabase* database) {

	return ShaderGraphAssetReferences::ResolveFunctions(graph, database);
}

bool Engine::ShaderGraphPreviewDependencies::LoadGraph(AssetID id, const AssetDatabase* database,
	ShaderGraphAsset& graph) {

	return ShaderGraphAssetReferences::Load(id, database, graph);
}

uint64_t Engine::ShaderGraphPreviewDependencies::CalculateHash(const ShaderGraphAsset& graph,
	const AssetDatabase* database) {

	std::unordered_set<AssetID> visited;
	return HashDependencies(ResolveFunctions(graph, database), database, visited);
}
