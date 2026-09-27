#include "PrefabJsonDiff.h"

//============================================================================
//	include
//============================================================================

// c++
#include <algorithm>

//============================================================================
//	PrefabJsonDiff internalMethods
//============================================================================
namespace {

	// 経路をスラッシュ区切りでセグメントへ分割する
	std::vector<std::string> SplitPath(const std::string& path) {

		std::vector<std::string> segments;
		std::string current;
		for (char c : path) {

			if (c == '/') {

				// 空セグメントは無視して連続スラッシュや先頭スラッシュに耐える
				if (!current.empty()) {
					segments.emplace_back(current);
					current.clear();
				}
				continue;
			}
			current.push_back(c);
		}
		if (!current.empty()) {
			segments.emplace_back(current);
		}
		return segments;
	}
}

//============================================================================
//	PrefabJsonDiff classMethods
//============================================================================
const nlohmann::json* Engine::PrefabJsonDiff::GetAtPath(const nlohmann::json& root, const std::string& path) {

	const std::vector<std::string> segments = SplitPath(path);
	const nlohmann::json* current = &root;
	for (const std::string& segment : segments) {

		if (!current->is_object() || !current->contains(segment)) {
			return nullptr;
		}
		current = &(*current)[segment];
	}
	return current;
}

void Engine::PrefabJsonDiff::SetAtPath(nlohmann::json& root, const std::string& path, const nlohmann::json& value) {

	const std::vector<std::string> segments = SplitPath(path);
	if (segments.empty()) {
		root = value;
		return;
	}

	nlohmann::json* current = &root;
	for (size_t i = 0; i < segments.size(); ++i) {

		const std::string& segment = segments[i];
		// 最後のセグメントが実際の代入位置
		if (i + 1 == segments.size()) {
			(*current)[segment] = value;
			return;
		}
		// 途中がオブジェクトでなければ作り直して経路を通す
		if (!(*current)[segment].is_object()) {
			(*current)[segment] = nlohmann::json::object();
		}
		current = &(*current)[segment];
	}
}

void Engine::PrefabJsonDiff::CollectLeafDifferences(const std::string& prefix, const nlohmann::json& base,
	const nlohmann::json& instance, std::vector<std::pair<std::string, nlohmann::json>>& out) {

	// 同値なら差分なし
	if (base == instance) {
		return;
	}

	// 両方オブジェクトのときだけ子へ降りる、配列とスカラーは丸ごと1リーフ扱い
	if (base.is_object() && instance.is_object()) {

		// 削除キーと参照identityは親オブジェクトごと保存する
		bool replaceObject = instance.contains("kind") && instance.contains("sourceAsset") && instance.contains("localFileId");
		for (auto it = base.begin(); it != base.end(); ++it) {
			replaceObject |= !instance.contains(it.key());
		}
		for (auto it = instance.begin(); it != instance.end(); ++it) {
			replaceObject |= it.key().empty() || it.key().find('/') != std::string::npos;
		}
		if (replaceObject) {
			out.emplace_back(prefix, instance);
			return;
		}
		for (auto it = instance.begin(); it != instance.end(); ++it) {

			const std::string childPrefix = prefix.empty() ? it.key() : prefix + "/" + it.key();
			// キーの追加と既存のnull値を区別する
			if (!base.contains(it.key())) {
				out.emplace_back(childPrefix, it.value());
			} else {
				CollectLeafDifferences(childPrefix, base[it.key()], it.value(), out);
			}
		}
		return;
	}

	// リーフが異なるのでインスタンス側の値を差分として積む
	out.emplace_back(prefix, instance);
}

Engine::ComponentMapDiff Engine::PrefabJsonDiff::DiffComponentMaps(const nlohmann::json& base,
	const nlohmann::json& instance, const std::vector<std::string>& excludeTypes) {

	ComponentMapDiff diff{};

	auto isExcluded = [&](const std::string& type) {
		return std::find(excludeTypes.begin(), excludeTypes.end(), type) != excludeTypes.end();
		};

	// インスタンス側を基準に、追加コンポーネントと値差分を集める
	if (instance.is_object()) {
		for (auto it = instance.begin(); it != instance.end(); ++it) {

			const std::string& type = it.key();
			if (isExcluded(type)) {
				continue;
			}
			if (base.is_object() && base.contains(type)) {

				CollectLeafDifferences(type, base[type], it.value(), diff.modifications);
			} else {

				diff.addedComponents.emplace_back(type, it.value());
			}
		}
	}

	// ベースにのみ存在する型は削除コンポーネント
	if (base.is_object()) {
		for (auto it = base.begin(); it != base.end(); ++it) {

			const std::string& type = it.key();
			if (isExcluded(type)) {
				continue;
			}
			if (!instance.is_object() || !instance.contains(type)) {

				diff.removedComponents.emplace_back(type);
			}
		}
	}
	return diff;
}
