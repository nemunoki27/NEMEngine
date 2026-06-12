#include "ScriptExecutionOrderTable.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>

// c++
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <system_error>

// json
#include <json.hpp>

namespace {

	constexpr int kSchemaVersion = 1;
}

Engine::ScriptExecutionOrderTable& Engine::ScriptExecutionOrderTable::GetInstance() {

	static ScriptExecutionOrderTable instance;
	return instance;
}

std::string Engine::ScriptExecutionOrderTable::SettingsPath() {

	// ProjectSettingsはGameAssetsと同階層に置く運用でInputActions.jsonと同様
	const std::filesystem::path path = RuntimePaths::GetGameRoot() / "ProjectSettings" / "ScriptExecutionOrder.json";
	return path.string();
}

void Engine::ScriptExecutionOrderTable::EnsureLoaded() {

	if (loaded_) {
		return;
	}
	loaded_ = true;
	Reload();
}

void Engine::ScriptExecutionOrderTable::Reload() {

	loaded_ = true;

	const std::string path = SettingsPath();
	std::error_code ec;
	if (!std::filesystem::exists(path, ec)) {
		// 設定ファイルが無い場合はoverride無しでmalformedではない、空tableにして良い
		entries_.clear();
		orderByGuid_.clear();
		return;
	}

	std::ifstream file(path);
	if (!file.is_open()) {
		// 開けない場合はmalformedと同様に直前のusable tableを保持する
		Logger::Output(LogType::Engine, spdlog::level::warn,
			"ScriptExecutionOrderTable: cannot open {}. keeping previous table.", path);
		return;
	}

	// parseはtempへ行い成功時のみswapする、malformed JSONで旧tableを失わない
	nlohmann::json root;
	try {
		file >> root;
	}
	catch (const std::exception& e) {
		Logger::Output(LogType::Engine, spdlog::level::warn,
			"ScriptExecutionOrderTable: failed to parse {} ({}). keeping previous table.", path, e.what());
		return;
	}

	if (!root.is_object() || !root.contains("entries") || !root["entries"].is_array()) {
		// 構造が不正な場合はmalformed扱いで直前のusable tableを保持する
		Logger::Output(LogType::Engine, spdlog::level::warn,
			"ScriptExecutionOrderTable: malformed structure in {}. keeping previous table.", path);
		return;
	}

	std::vector<Entry> parsed;
	std::unordered_map<std::string, int32_t> parsedLookup;
	for (const auto& entryJson : root["entries"]) {

		if (!entryJson.is_object()) {
			continue;
		}
		Entry entry{};
		entry.scriptTypeId = entryJson.value("scriptTypeId", std::string{});
		entry.displayName = entryJson.value("displayName", std::string{});
		entry.executionOrder = entryJson.value("executionOrder", 0);
		if (entry.scriptTypeId.empty()) {
			continue;
		}
		// duplicate scriptTypeIdは最初のものを採用し、以降は診断する
		if (parsedLookup.find(entry.scriptTypeId) != parsedLookup.end()) {
			Logger::Output(LogType::Engine, spdlog::level::warn,
				"ScriptExecutionOrderTable: duplicate scriptTypeId '{}' ignored.", entry.scriptTypeId);
			continue;
		}
		parsedLookup.emplace(entry.scriptTypeId, entry.executionOrder);
		parsed.emplace_back(std::move(entry));
	}

	// parse全体が成功したのでここで初めてswapする
	entries_ = std::move(parsed);
	RebuildLookup();
}

void Engine::ScriptExecutionOrderTable::RebuildLookup() {

	// scriptTypeIdで安定ソートしておく、UI表示と決定的な書き出しのため
	std::sort(entries_.begin(), entries_.end(), [](const Entry& lhs, const Entry& rhs) {
		return lhs.scriptTypeId < rhs.scriptTypeId;
		});
	orderByGuid_.clear();
	for (const Entry& entry : entries_) {
		orderByGuid_[entry.scriptTypeId] = entry.executionOrder;
	}
}

int32_t Engine::ScriptExecutionOrderTable::GetOrder(const std::string_view& scriptTypeId) const {

	if (scriptTypeId.empty()) {
		return 0;
	}
	const auto it = orderByGuid_.find(std::string(scriptTypeId));
	return it != orderByGuid_.end() ? it->second : 0;
}

bool Engine::ScriptExecutionOrderTable::TryGetOverride(const std::string_view& scriptTypeId, int32_t& outOrder) const {

	// override無しと、明示的に0を設定した状態を区別する、GetOrderはどちらも0を返すため
	if (scriptTypeId.empty()) {
		return false;
	}
	const auto it = orderByGuid_.find(std::string(scriptTypeId));
	if (it == orderByGuid_.end()) {
		return false;
	}
	outOrder = it->second;
	return true;
}

void Engine::ScriptExecutionOrderTable::SetOrder(const std::string_view& scriptTypeId, const std::string_view& displayName, int32_t order) {

	if (scriptTypeId.empty()) {
		return;
	}
	const std::string guid(scriptTypeId);
	for (Entry& entry : entries_) {
		if (entry.scriptTypeId == guid) {
			entry.executionOrder = order;
			if (!displayName.empty()) {
				entry.displayName = std::string(displayName);
			}
			RebuildLookup();
			return;
		}
	}
	entries_.emplace_back(Entry{ guid, std::string(displayName), order });
	RebuildLookup();
}

void Engine::ScriptExecutionOrderTable::Remove(const std::string_view& scriptTypeId) {

	const std::string guid(scriptTypeId);
	entries_.erase(std::remove_if(entries_.begin(), entries_.end(), [&](const Entry& entry) {
		return entry.scriptTypeId == guid;
		}), entries_.end());
	RebuildLookup();
}

bool Engine::ScriptExecutionOrderTable::Save() const {

	nlohmann::json root;
	root["schemaVersion"] = kSchemaVersion;
	nlohmann::json arr = nlohmann::json::array();
	for (const Entry& entry : entries_) {

		nlohmann::json item;
		item["scriptTypeId"] = entry.scriptTypeId;
		item["displayName"] = entry.displayName;
		item["executionOrder"] = entry.executionOrder;
		arr.emplace_back(std::move(item));
	}
	root["entries"] = std::move(arr);

	const std::filesystem::path target = SettingsPath();
	std::error_code ec;
	std::filesystem::create_directories(target.parent_path(), ec);

	// temp fileへflushしてからbackupとsafe replaceとrollbackで置換する
	const std::filesystem::path temp = target.string() + ".tmp";
	{
		std::ofstream file(temp, std::ios::binary | std::ios::trunc);
		if (!file.is_open()) {
			Logger::Output(LogType::Engine, spdlog::level::err,
				"ScriptExecutionOrderTable: failed to open temp file for save: {}", temp.string());
			return false;
		}
		file << root.dump(2);
		file.flush();
		if (!file.good()) {
			file.close();
			std::filesystem::remove(temp, ec);
			Logger::Output(LogType::Engine, spdlog::level::err,
				"ScriptExecutionOrderTable: failed to write temp file: {}", temp.string());
			return false;
		}
	}

	const bool targetExists = std::filesystem::exists(target, ec);
	const std::filesystem::path backup = target.string() + ".bak";
	if (targetExists) {
		// 既存をbackupへ退避する、Windowsでtempからtargetの直接renameが失敗しても元を失わない
		std::filesystem::remove(backup, ec);
		std::filesystem::rename(target, backup, ec);
		if (ec) {
			std::filesystem::remove(temp, ec);
			Logger::Output(LogType::Engine, spdlog::level::err,
				"ScriptExecutionOrderTable: failed to back up {} ({}). keeping existing file.", target.string(), ec.message());
			return false;
		}
	}

	std::filesystem::rename(temp, target, ec);
	if (ec) {
		// 置換失敗時はbackupからrollbackして元のusable fileを復元する
		std::error_code rollbackEc;
		if (targetExists) {
			std::filesystem::rename(backup, target, rollbackEc);
		}
		std::filesystem::remove(temp, rollbackEc);
		Logger::Output(LogType::Engine, spdlog::level::err,
			"ScriptExecutionOrderTable: failed to replace {} ({}). rolled back.", target.string(), ec.message());
		return false;
	}

	// 置換成功、backupのcleanup失敗はwarningに留める、usable fileは既に正
	if (targetExists) {
		std::filesystem::remove(backup, ec);
		if (ec) {
			Logger::Output(LogType::Engine, spdlog::level::warn,
				"ScriptExecutionOrderTable: failed to remove backup {} ({}).", backup.string(), ec.message());
		}
	}
	return true;
}
