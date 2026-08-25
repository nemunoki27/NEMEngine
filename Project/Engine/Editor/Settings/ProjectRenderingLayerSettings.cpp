#include "ProjectRenderingLayerSettings.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Diagnostics/Log.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>

// c++
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <system_error>

// json
#include <json.hpp>

namespace {

	constexpr const char* kSettingsFile = "RenderingLayers.json";

	std::array<std::string,
		Engine::ProjectRenderingLayerSettings::kLayerCount> g_names{};
	bool g_loaded = false;

	std::filesystem::path GetSettingsPath() {

		return Engine::RuntimePaths::GetProjectSettingsPath(kSettingsFile);
	}

	std::string GetLegacyDefaultName(uint32_t index) {

		return index == 0u ? "Default" : "Layer " + std::to_string(index);
	}

	std::string Trim(const std::string& text) {

		const size_t begin = text.find_first_not_of(" \t\r\n");
		if (begin == std::string::npos) {
			return {};
		}
		const size_t end = text.find_last_not_of(" \t\r\n");
		return text.substr(begin, end - begin + 1u);
	}

	void SetDefaults() {

		g_names.fill({});
		g_names[0] = "Default";
	}

	bool ContainsName(const std::string& name, uint32_t ignoredIndex =
		Engine::ProjectRenderingLayerSettings::kLayerCount) {

		for (uint32_t index = 0u;
			index < Engine::ProjectRenderingLayerSettings::kLayerCount; ++index) {

			if (index != ignoredIndex && g_names[index] == name) {
				return true;
			}
		}
		return false;
	}

	void LoadFromDisk() {

		SetDefaults();
		std::ifstream file(GetSettingsPath(), std::ios::binary);
		if (file.is_open()) {
			const nlohmann::json data = nlohmann::json::parse(
				file, nullptr, false);
			if (data.is_object()) {
				const auto layers = data.find("layers");
				if (layers != data.end() && layers->is_array()) {
					const size_t count = (std::min)(
						layers->size(), g_names.size());
					for (size_t index = 0u; index < count; ++index) {
						if ((*layers)[index].is_string()) {
							const std::string name = Trim(
								(*layers)[index].get<std::string>());
							// 旧方式の自動生成名は未使用枠として移行する
							if (index != 0u &&
								name == GetLegacyDefaultName(
									static_cast<uint32_t>(index))) {

								continue;
							}
							if (index != 0u && !name.empty() &&
								!ContainsName(name)) {

								g_names[index] = name;
							}
						}
					}
				}
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

const std::array<std::string,
	Engine::ProjectRenderingLayerSettings::kLayerCount>&
	Engine::ProjectRenderingLayerSettings::GetNames() {

	EnsureLoaded();
	return g_names;
}

uint32_t Engine::ProjectRenderingLayerSettings::GetDefinedMask() {

	EnsureLoaded();
	uint32_t mask = 0u;
	for (uint32_t index = 0u; index < kLayerCount; ++index) {
		if (!g_names[index].empty()) {
			mask |= 1u << index;
		}
	}
	return mask;
}

bool Engine::ProjectRenderingLayerSettings::IsValidNewLayer(
	const std::string& name) {

	EnsureLoaded();
	const std::string trimmed = Trim(name);
	if (trimmed.empty() || ContainsName(trimmed)) {
		return false;
	}
	return std::ranges::any_of(g_names.begin() + 1, g_names.end(),
		[](const std::string& current) {

			return current.empty();
		});
}

bool Engine::ProjectRenderingLayerSettings::AddLayer(
	const std::string& name) {

	if (!IsValidNewLayer(name)) {
		return false;
	}
	const std::string trimmed = Trim(name);
	for (uint32_t index = 1u; index < kLayerCount; ++index) {
		if (g_names[index].empty()) {
			g_names[index] = trimmed;
			return true;
		}
	}
	return false;
}

bool Engine::ProjectRenderingLayerSettings::SetName(
	uint32_t index, const std::string& name) {

	EnsureLoaded();
	const std::string trimmed = Trim(name);
	if (index == 0u || kLayerCount <= index || trimmed.empty() ||
		ContainsName(trimmed, index)) {

		return false;
	}
	g_names[index] = trimmed;
	return true;
}

bool Engine::ProjectRenderingLayerSettings::RemoveLayer(uint32_t index) {

	EnsureLoaded();
	if (index == 0u || kLayerCount <= index || g_names[index].empty()) {
		return false;
	}
	g_names[index].clear();
	return true;
}

void Engine::ProjectRenderingLayerSettings::Reload() {

	g_loaded = false;
	LoadFromDisk();
}

bool Engine::ProjectRenderingLayerSettings::Save() {

	EnsureLoaded();
	const std::filesystem::path target = GetSettingsPath();
	std::error_code ec;
	std::filesystem::create_directories(target.parent_path(), ec);
	if (ec) {
		Logger::Output(LogType::Engine, spdlog::level::err,
			"Rendering Layer設定のDirectoryを作成できません path={} 内容={}",
			target.parent_path().string(), ec.message());
		return false;
	}
	nlohmann::json data{};
	data["layers"] = g_names;
	const std::filesystem::path temporary = target.string() + ".tmp";
	{
		std::ofstream file(temporary,
			std::ios::binary | std::ios::trunc);
		if (!file.is_open()) {
			Logger::Output(LogType::Engine, spdlog::level::err,
				"Rendering Layer設定を保存できません path={}",
				temporary.string());
			return false;
		}
		file << data.dump(2);
		file.flush();
		if (!file.good()) {
			file.close();
			std::filesystem::remove(temporary, ec);
			Logger::Output(LogType::Engine, spdlog::level::err,
				"Rendering Layer設定の書き込みに失敗しました path={}",
				temporary.string());
			return false;
		}
	}

	const bool targetExists = std::filesystem::exists(target, ec);
	const std::filesystem::path backup = target.string() + ".bak";
	if (targetExists) {
		std::filesystem::remove(backup, ec);
		std::filesystem::rename(target, backup, ec);
		if (ec) {
			std::filesystem::remove(temporary, ec);
			Logger::Output(LogType::Engine, spdlog::level::err,
				"Rendering Layer設定のBackupを作成できません path={} 内容={}",
				target.string(), ec.message());
			return false;
		}
	}
	std::filesystem::rename(temporary, target, ec);
	if (ec) {
		std::error_code rollbackError;
		if (targetExists) {
			std::filesystem::rename(backup, target, rollbackError);
		}
		std::filesystem::remove(temporary, rollbackError);
		Logger::Output(LogType::Engine, spdlog::level::err,
			"Rendering Layer設定を置換できません path={} 内容={}",
			target.string(), ec.message());
		return false;
	}
	if (targetExists) {
		std::filesystem::remove(backup, ec);
		if (ec) {
			Logger::Output(LogType::Engine, spdlog::level::warn,
				"Rendering Layer設定のBackupを削除できません path={} 内容={}",
				backup.string(), ec.message());
		}
	}
	return true;
}
