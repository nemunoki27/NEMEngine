#include "ScriptExecutionOrderTable.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>

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

std::filesystem::path Engine::ScriptExecutionOrderTable::SettingsPath() {

	return RuntimePaths::GetProjectSettingsPath("ScriptExecutionOrder.json");
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

	const std::filesystem::path path = SettingsPath();
	std::error_code ec;
	if (!std::filesystem::exists(path, ec)) {
		// 設定ファイルが無い場合は上書き無しで不正ではない、空テーブルにして良い
		entries_.clear();
		orderByGuid_.clear();
		return;
	}

	std::ifstream file(path);
	if (!file.is_open()) {
		// 開けない場合は不正と同様に直前の有効なテーブルを保持する
		Logger::Output(LogType::Engine, spdlog::level::warn,
			"ScriptExecutionOrderTable: cannot open {}. keeping previous table.",
			Algorithm::PathToUTF8(path));
		return;
	}

	// 解析は一時へ行い成功時のみ入れ替える、不正JSONで旧テーブルを失わない
	nlohmann::json root;
	try {
		file >> root;
	}
	catch (const std::exception& e) {
		Logger::Output(LogType::Engine, spdlog::level::warn,
			"ScriptExecutionOrderTable: failed to parse {} ({}). keeping previous table.",
			Algorithm::PathToUTF8(path), e.what());
		return;
	}

	if (!root.is_object() || !root.contains("entries") || !root["entries"].is_array()) {
		// 構造が不正な場合は直前の有効なテーブルを保持する
		Logger::Output(LogType::Engine, spdlog::level::warn,
			"ScriptExecutionOrderTable: malformed structure in {}. keeping previous table.",
			Algorithm::PathToUTF8(path));
		return;
	}

	std::vector<Entry> parsed;
	std::unordered_map<std::string, int32_t> parsedLookup;
	for (const auto& entryJson : root["entries"]) {

		if (!entryJson.is_object()) {
			continue;
		}
		Entry entry{};
		entry.scriptTypeID = entryJson.value("scriptTypeId", std::string{});
		entry.displayName = entryJson.value("displayName", std::string{});
		entry.executionOrder = entryJson.value("executionOrder", 0);
		if (entry.scriptTypeID.empty()) {
			continue;
		}
		// 重複するscriptTypeIDは最初のものを採用し以降は診断する
		if (parsedLookup.find(entry.scriptTypeID) != parsedLookup.end()) {
			Logger::Output(LogType::Engine, spdlog::level::warn,
				"ScriptExecutionOrderTable: duplicate scriptTypeID '{}' ignored.", entry.scriptTypeID);
			continue;
		}
		parsedLookup.emplace(entry.scriptTypeID, entry.executionOrder);
		parsed.emplace_back(std::move(entry));
	}

	// parse全体が成功したのでここで初めてswapする
	entries_ = std::move(parsed);
	RebuildLookup();
}

void Engine::ScriptExecutionOrderTable::RebuildLookup() {

	// scriptTypeIDで安定ソートしておく、UI表示と決定的な書き出しのため
	std::sort(entries_.begin(), entries_.end(), [](const Entry& lhs, const Entry& rhs) {
		return lhs.scriptTypeID < rhs.scriptTypeID;
		});
	orderByGuid_.clear();
	for (const Entry& entry : entries_) {
		orderByGuid_[entry.scriptTypeID] = entry.executionOrder;
	}
}

int32_t Engine::ScriptExecutionOrderTable::GetOrder(const std::string_view& scriptTypeID) const {

	if (scriptTypeID.empty()) {
		return 0;
	}
	const auto it = orderByGuid_.find(std::string(scriptTypeID));
	return it != orderByGuid_.end() ? it->second : 0;
}

bool Engine::ScriptExecutionOrderTable::TryGetOverride(const std::string_view& scriptTypeID, int32_t& outOrder) const {

	// 上書き無しと明示的に0を設定した状態を区別する、GetOrderはどちらも0を返すため
	if (scriptTypeID.empty()) {
		return false;
	}
	const auto it = orderByGuid_.find(std::string(scriptTypeID));
	if (it == orderByGuid_.end()) {
		return false;
	}
	outOrder = it->second;
	return true;
}

void Engine::ScriptExecutionOrderTable::SetOrder(const std::string_view& scriptTypeID, const std::string_view& displayName, int32_t order) {

	if (scriptTypeID.empty()) {
		return;
	}
	const std::string guid(scriptTypeID);
	for (Entry& entry : entries_) {
		if (entry.scriptTypeID == guid) {
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

void Engine::ScriptExecutionOrderTable::Remove(const std::string_view& scriptTypeID) {

	const std::string guid(scriptTypeID);
	entries_.erase(std::remove_if(entries_.begin(), entries_.end(), [&](const Entry& entry) {
		return entry.scriptTypeID == guid;
		}), entries_.end());
	RebuildLookup();
}

bool Engine::ScriptExecutionOrderTable::Save() const {

	nlohmann::json root;
	root["schemaVersion"] = kSchemaVersion;
	nlohmann::json arr = nlohmann::json::array();
	for (const Entry& entry : entries_) {

		nlohmann::json item;
		item["scriptTypeId"] = entry.scriptTypeID;
		item["displayName"] = entry.displayName;
		item["executionOrder"] = entry.executionOrder;
		arr.emplace_back(std::move(item));
	}
	root["entries"] = std::move(arr);

	const std::filesystem::path target = SettingsPath();
	std::error_code ec;
	std::filesystem::create_directories(target.parent_path(), ec);

	// 一時ファイルへ書き出してからバックアップと安全な置換とロールバックで置換する
	std::filesystem::path temp = target;
	temp += L".tmp";
	{
		std::ofstream file(temp, std::ios::binary | std::ios::trunc);
		if (!file.is_open()) {
			Logger::Output(LogType::Engine, spdlog::level::err,
				"ScriptExecutionOrderTable: failed to open temp file for save: {}",
				Algorithm::PathToUTF8(temp));
			return false;
		}
		file << root.dump(2);
		file.flush();
		if (!file.good()) {
			file.close();
			std::filesystem::remove(temp, ec);
			Logger::Output(LogType::Engine, spdlog::level::err,
				"ScriptExecutionOrderTable: failed to write temp file: {}",
				Algorithm::PathToUTF8(temp));
			return false;
		}
	}

	const bool targetExists = std::filesystem::exists(target, ec);
	std::filesystem::path backup = target;
	backup += L".bak";
	if (targetExists) {
		// 既存をバックアップへ退避する、Windowsで一時から対象への直接renameが失敗しても元を失わない
		std::filesystem::remove(backup, ec);
		std::filesystem::rename(target, backup, ec);
		if (ec) {
			std::filesystem::remove(temp, ec);
			Logger::Output(LogType::Engine, spdlog::level::err,
				"ScriptExecutionOrderTable: failed to back up {} ({}). keeping existing file.",
				Algorithm::PathToUTF8(target), ec.message());
			return false;
		}
	}

	std::filesystem::rename(temp, target, ec);
	if (ec) {
		// 置換失敗時はバックアップからロールバックして元の有効なファイルを復元する
		std::error_code rollbackEc;
		if (targetExists) {
			std::filesystem::rename(backup, target, rollbackEc);
		}
		std::filesystem::remove(temp, rollbackEc);
		Logger::Output(LogType::Engine, spdlog::level::err,
			"ScriptExecutionOrderTable: failed to replace {} ({}). rolled back.",
			Algorithm::PathToUTF8(target), ec.message());
		return false;
	}

	// 置換成功、バックアップの掃除失敗は警告に留める、有効なファイルは既に正
	if (targetExists) {
		std::filesystem::remove(backup, ec);
		if (ec) {
			Logger::Output(LogType::Engine, spdlog::level::warn,
				"ScriptExecutionOrderTable: failed to remove backup {} ({}).",
				Algorithm::PathToUTF8(backup), ec.message());
		}
	}
	return true;
}
