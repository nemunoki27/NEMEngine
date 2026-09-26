#include "JsonSemanticMerge.h"

//============================================================================
//	include
//============================================================================
// c++
#include <map>
#include <optional>
#include <set>
#include <string_view>

//============================================================================
//	JsonSemanticMerge classMethods
//============================================================================
namespace {

	using Json = nlohmann::json;

	bool SameValue(const Json* lhs, const Json* rhs) {

		if (!lhs || !rhs) {
			return lhs == rhs;
		}
		return *lhs == *rhs;
	}

	Json MissingValue() {

		return Json{ { "$missing", true } };
	}

	Json CopyValue(const Json* value) {

		return value ? *value : MissingValue();
	}

	std::string ReadString(const Json& item, const char* name) {

		const auto found = item.find(name);
		return found != item.end() && found->is_string() ? found->get<std::string>() : std::string{};
	}

	std::optional<std::string> ArrayItemKey(
		std::string_view memberName, const Json& item) {

		if (item.is_string() &&
			(memberName == "ExternalActors" ||
				memberName == "RemovedEntities" || memberName == "RemovedNestedSlots")) {
			return item.get<std::string>();
		}
		if (!item.is_object()) {
			return std::nullopt;
		}

		if (memberName == "Entities") {
			const std::string key =
				ReadString(item, "LocalFileID");
			return key.empty() ? std::nullopt : std::optional<std::string>{ key };
		}
		if (memberName == "NestedPrefabInstances" || memberName == "NestedInstances") {
			const std::string key = ReadString(item, "NestedSlotID");
			return key.empty() ? std::nullopt : std::optional<std::string>{ key };
		}
		if (memberName == "PrefabInstances") {
			const std::string key = ReadString(item, "InstanceID");
			return key.empty() ? std::nullopt : std::optional<std::string>{ key };
		}
		if (memberName == "EntityMap") {
			const std::string key = ReadString(item, "S");
			return key.empty() ? std::nullopt : std::optional<std::string>{ key };
		}
		if (memberName == "Modifications") {
			const std::string target = ReadString(item, "Target");
			const std::string path = ReadString(item, "Path");
			return target.empty() || path.empty() ? std::nullopt :
				std::optional<std::string>{ target + "/" + path };
		}
		if (memberName == "AddedComponents" ||
			memberName == "RemovedComponents") {
			const std::string target = ReadString(item, "Target");
			const std::string type = ReadString(item, "Type");
			return target.empty() || type.empty() ? std::nullopt :
				std::optional<std::string>{ target + "/" + type };
		}
		if (memberName == "HierarchyMods") {
			const std::string key = ReadString(item, "Target");
			return key.empty() ? std::nullopt : std::optional<std::string>{ key };
		}
		if (memberName == "AddedEntities") {
			const std::string key =
				ReadString(item, "SceneLocalFileID");
			return key.empty() ? std::nullopt : std::optional<std::string>{ key };
		}
		if (memberName == "subScenes") {
			const std::string key = ReadString(item, "slotID");
			return key.empty() ? std::nullopt : std::optional<std::string>{ key };
		}
		return std::nullopt;
	}

	bool BuildArrayMap(std::string_view memberName, const Json& array,
		std::map<std::string, Json>& outItems) {

		if (!array.is_array()) {
			return false;
		}
		for (const Json& item : array) {

			const std::optional<std::string> key =
				ArrayItemKey(memberName, item);
			if (!key || !outItems.emplace(*key, item).second) {
				return false;
			}
		}
		return true;
	}

	const Json* FindValue(const std::map<std::string, Json>& values,
		const std::string& key) {

		const auto it = values.find(key);
		return it == values.end() ? nullptr : &it->second;
	}

	std::optional<Json> MergeValue(const Json* base, const Json* ours,
		const Json* theirs, const std::string& path,
		std::vector<Engine::JsonMergeConflict>& conflicts);

	std::optional<Json> MergeArray(const Json& base, const Json& ours,
		const Json& theirs, const std::string& path,
		std::vector<Engine::JsonMergeConflict>& conflicts) {

		const size_t separator = path.find_last_of('/');
		const std::string memberName =
			separator == std::string::npos ? path : path.substr(separator + 1);
		std::map<std::string, Json> baseItems;
		std::map<std::string, Json> ourItems;
		std::map<std::string, Json> theirItems;
		if (!BuildArrayMap(memberName, base, baseItems) ||
			!BuildArrayMap(memberName, ours, ourItems) ||
			!BuildArrayMap(memberName, theirs, theirItems)) {
			return std::nullopt;
		}

		std::set<std::string> keys;
		for (const auto& [key, value] : baseItems) { keys.insert(key); }
		for (const auto& [key, value] : ourItems) { keys.insert(key); }
		for (const auto& [key, value] : theirItems) { keys.insert(key); }

		Json result = Json::array();
		for (const std::string& key : keys) {

			std::optional<Json> merged = MergeValue(
				FindValue(baseItems, key),
				FindValue(ourItems, key),
				FindValue(theirItems, key),
				path + "/" + key, conflicts);
			if (merged) {
				result.push_back(std::move(*merged));
			}
		}
		return result;
	}

	std::optional<Json> MergeObject(const Json& base, const Json& ours,
		const Json& theirs, const std::string& path,
		std::vector<Engine::JsonMergeConflict>& conflicts) {

		std::set<std::string> keys;
		for (auto it = base.begin(); it != base.end(); ++it) { keys.insert(it.key()); }
		for (auto it = ours.begin(); it != ours.end(); ++it) { keys.insert(it.key()); }
		for (auto it = theirs.begin(); it != theirs.end(); ++it) { keys.insert(it.key()); }

		Json result = Json::object();
		for (const std::string& key : keys) {

			const Json* baseValue = base.contains(key) ? &base.at(key) : nullptr;
			const Json* ourValue = ours.contains(key) ? &ours.at(key) : nullptr;
			const Json* theirValue = theirs.contains(key) ? &theirs.at(key) : nullptr;
			std::optional<Json> merged = MergeValue(
				baseValue, ourValue, theirValue, path + "/" + key, conflicts);
			if (merged) {
				result[key] = std::move(*merged);
			}
		}
		return result;
	}

	std::optional<Json> MergeValue(const Json* base, const Json* ours,
		const Json* theirs, const std::string& path,
		std::vector<Engine::JsonMergeConflict>& conflicts) {

		if (SameValue(ours, theirs)) {
			return ours ? std::optional<Json>{ *ours } : std::nullopt;
		}
		if (SameValue(base, ours)) {
			return theirs ? std::optional<Json>{ *theirs } : std::nullopt;
		}
		if (SameValue(base, theirs)) {
			return ours ? std::optional<Json>{ *ours } : std::nullopt;
		}

		if (base && ours && theirs &&
			base->is_object() && ours->is_object() && theirs->is_object()) {
			return MergeObject(*base, *ours, *theirs, path, conflicts);
		}
		if (base && ours && theirs &&
			base->is_array() && ours->is_array() && theirs->is_array()) {

			if (std::optional<Json> merged =
				MergeArray(*base, *ours, *theirs, path, conflicts)) {
				return merged;
			}
		}

		conflicts.push_back({
			.path = path,
			.base = CopyValue(base),
			.ours = CopyValue(ours),
			.theirs = CopyValue(theirs),
			});
		return ours ? std::optional<Json>{ *ours } : std::nullopt;
	}
}

Engine::JsonMergeResult Engine::JsonSemanticMerge::Merge(
	const nlohmann::json& base, const nlohmann::json& ours,
	const nlohmann::json& theirs) {

	JsonMergeResult result{};
	const std::optional<nlohmann::json> merged =
		MergeValue(&base, &ours, &theirs, "", result.conflicts);
	result.merged = merged.value_or(nlohmann::json::object());
	return result;
}
