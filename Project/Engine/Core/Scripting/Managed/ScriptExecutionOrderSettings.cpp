#include "ScriptExecutionOrderSettings.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Diagnostics/Log.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonSerializer.h>
#include <Engine/Core/Runtime/Paths/ConfigPaths.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>

// c++
#include <algorithm>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

//============================================================================
//	ScriptExecutionOrderSettings anonymous
//============================================================================
namespace {

	constexpr int32_t kSchemaVersion = 1;

	std::unordered_map<std::string, int32_t> g_overrides;
	bool g_loaded = false;

	std::filesystem::path SettingsPath() {
		return Engine::RuntimePaths::GetProjectSettingsPath(
			Engine::ConfigPaths::kScriptExecutionOrder);
	}

	void LoadFromDisk() {

		g_overrides.clear();
		const nlohmann::json data = Engine::JsonAdapter::Load(SettingsPath(), false);
		if (data.is_object() && data.contains("entries") && data["entries"].is_array()) {

			for (const nlohmann::json& entry : data["entries"]) {

				if (!entry.is_object()) {
					continue;
				}
				const std::string scriptTypeID = entry.value("scriptTypeID", std::string{});
				if (scriptTypeID.empty() || !entry.contains("order") || !entry["order"].is_number_integer()) {
					continue;
				}
				g_overrides[scriptTypeID] = entry["order"].get<int32_t>();
			}
		}
		g_loaded = true;
	}

	void EnsureLoaded() {

		if (!g_loaded) {
			LoadFromDisk();
		}
	}
}

//============================================================================
//	ScriptExecutionOrderSettings functions
//============================================================================
int32_t Engine::ScriptExecutionOrderSettings::Resolve(
	const std::string_view& scriptTypeID, int32_t defaultOrder) {

	EnsureLoaded();
	const auto it = g_overrides.find(std::string(scriptTypeID));
	return it == g_overrides.end() ? defaultOrder : it->second;
}

bool Engine::ScriptExecutionOrderSettings::HasOverride(const std::string_view& scriptTypeID) {

	EnsureLoaded();
	return g_overrides.contains(std::string(scriptTypeID));
}

bool Engine::ScriptExecutionOrderSettings::SetOverride(
	const std::string_view& scriptTypeID, int32_t order) {

	EnsureLoaded();
	if (scriptTypeID.empty()) {
		return false;
	}
	const std::string key(scriptTypeID);
	const auto it = g_overrides.find(key);
	if (it != g_overrides.end() && it->second == order) {
		return false;
	}
	g_overrides[key] = order;
	return true;
}

bool Engine::ScriptExecutionOrderSettings::RemoveOverride(const std::string_view& scriptTypeID) {

	EnsureLoaded();
	return g_overrides.erase(std::string(scriptTypeID)) > 0;
}

bool Engine::ScriptExecutionOrderSettings::Save() {

	EnsureLoaded();
	std::vector<std::pair<std::string, int32_t>> sortedOverrides(
		g_overrides.begin(), g_overrides.end());
	std::sort(sortedOverrides.begin(), sortedOverrides.end(),
		[](const auto& lhs, const auto& rhs) { return lhs.first < rhs.first; });

	nlohmann::json entries = nlohmann::json::array();
	for (const auto& [scriptTypeID, order] : sortedOverrides) {
		entries.push_back({
			{ "scriptTypeID", scriptTypeID },
			{ "order", order },
			});
	}

	nlohmann::json data;
	data["schemaVersion"] = kSchemaVersion;
	data["entries"] = std::move(entries);
	if (!JsonAdapter::SaveCanonical(SettingsPath(), data)) {
		Logger::Output(LogType::Engine, spdlog::level::err,
			"ScriptExecutionOrderSettings: 実行順設定を保存できません path={}", SettingsPath().string());
		return false;
	}
	return true;
}

void Engine::ScriptExecutionOrderSettings::Reload() {

	g_loaded = false;
	LoadFromDisk();
}
