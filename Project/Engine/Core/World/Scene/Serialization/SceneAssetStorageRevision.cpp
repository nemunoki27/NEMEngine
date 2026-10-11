#include "SceneAssetStorage.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Scene/Serialization/SceneStorageFiles.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonSerializer.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>

// c++
#include <algorithm>
#include <stdexcept>
#include <utility>

using namespace Engine::SceneStorageFiles;

//============================================================================
//	SceneAssetStorage revisionMethods
//============================================================================
std::string Engine::SceneAssetStorage::ReadRevision(const std::filesystem::path& path, AssetID asset) {

	const std::string revision = FileRevision(path);
	const std::string key = PathKey(path);
	auto found = revisionSources_.find(key);
	if (found == revisionSources_.end() || found->second.revision != revision) {

		RevisionSource source;
		source.revision = revision;
		const auto document = JsonAdapter::Load(path, false);
		if (document.contains("ExternalActors") && document["ExternalActors"].is_array()) {

			for (const auto& actor : document["ExternalActors"]) {
				if (!actor.is_string() || !TryParseUUID16Hex(actor.get<std::string>())) {
					source.valid = false;
					break;
				}
				source.actorIDs.push_back(actor.get<std::string>());
			}
		}
		// 解析中に保存された文書は照合へ使わない
		if (FileRevision(path) != revision) {
			throw std::runtime_error("シーンの照合中に内容が変わりました: " + Algorithm::PathToUTF8(path));
		}
		// 使用が古い一覧を外してCacheの容量を保つ
		if (revisionSources_.size() >= 16u && found == revisionSources_.end()) {

			const auto oldest = std::min_element(revisionSources_.begin(), revisionSources_.end(),
				[](const auto& left, const auto& right) { return left.second.lastAccess < right.second.lastAccess; });
			revisionSources_.erase(oldest);
		}
		found = revisionSources_.insert_or_assign(key, std::move(source)).first;
	}
	found->second.lastAccess = ++revisionAccessSerial_;
	if (!found->second.valid) return "invalid";
	std::string result = revision + FileRevision(std::filesystem::path(path.wstring() + L".meta"));
	if (found->second.actorIDs.empty()) return result;
	// Actor本体の変更は毎回ハッシュで検出する
	const auto root = ResolveActorRoot(path, asset);
	for (const auto& actor : found->second.actorIDs) result += FileRevision(root / (actor + ".actor.json"));
	return result;
}
