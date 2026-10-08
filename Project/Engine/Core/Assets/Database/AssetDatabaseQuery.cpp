#include "AssetDatabase.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>

// c++
#include <unordered_set>

//============================================================================
//	AssetDatabase queryMethods
//============================================================================
const Engine::AssetMeta* Engine::AssetDatabase::Find(AssetID id) const {

	// GUIDが一致するmetaを借用する
	auto it = guidToMeta_.find(id);
	return (it == guidToMeta_.end()) ? nullptr : &it->second;
}

const Engine::AssetMeta* Engine::AssetDatabase::FindByPath(const std::string& assetPath) const {

	// 論理パスを正規化してGUIDへ対応付ける
	auto it = pathToGuid_.find(NormalizeLookupKey(assetPath));
	return (it == pathToGuid_.end()) ? nullptr : Find(it->second);
}

std::filesystem::path Engine::AssetDatabase::ResolveFullPath(AssetID id) const {

	// 登録済みAssetの実パスを解決する
	const auto* meta = Find(id);
	if (!meta) {
		return {};
	}
	return ResolveAssetPath(meta->assetPath);
}

std::filesystem::path Engine::AssetDatabase::ResolveAssetPath(const std::string& assetPath) const {

	// ProjectとPackage共通のパス解決を使う
	return RuntimePaths::ResolveAssetPath(assetPath);
}

const std::vector<Engine::AssetID>& Engine::AssetDatabase::FindDependencies(AssetID id) const {

	// 依存一覧を借用し、未登録なら空の一覧を返す
	static const std::vector<AssetID> kEmpty;
	auto it = guidToMeta_.find(id);
	return (it == guidToMeta_.end()) ? kEmpty : it->second.dependencies;
}

const std::vector<Engine::AssetID>& Engine::AssetDatabase::FindReferencers(AssetID id) const {

	// 参照元の一覧を借用する
	static const std::vector<AssetID> kEmpty;
	auto it = referencersByGuid_.find(id);
	return (it == referencersByGuid_.end()) ? kEmpty : it->second;
}

std::vector<Engine::AssetID> Engine::AssetDatabase::FindReferencersRecursive(AssetID id) const {

	// 循環参照を除いて参照元を走査する
	std::vector<AssetID> result;
	std::vector<AssetID> pending{id};
	std::unordered_set<AssetID> visited{id};
	for (size_t index = 0; index < pending.size(); ++index) {

		for (AssetID referencer : FindReferencers(pending[index])) {
			if (!visited.insert(referencer).second) {
				continue;
			}
			result.emplace_back(referencer);
			pending.emplace_back(referencer);
		}
	}
	return result;
}

bool Engine::AssetDatabase::HasReferencers(AssetID id) const {

	// 参照元が1件以上あるか確認する
	auto it = referencersByGuid_.find(id);
	return it != referencersByGuid_.end() && !it->second.empty();
}
