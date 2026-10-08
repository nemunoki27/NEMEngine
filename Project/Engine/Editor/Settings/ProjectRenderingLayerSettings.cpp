#include "ProjectRenderingLayerSettings.h"
#include "ProjectSettingsStorage.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>

// c++
#include <algorithm>
#include <filesystem>

// json
#include <json.hpp>

namespace {

	constexpr const char* kSettingsFile = "RenderingLayers.json";

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

}

const std::array<std::string,
	Engine::ProjectRenderingLayerSettings::kLayerCount>&
	Engine::ProjectRenderingLayerSettings::GetNames() {

	EnsureLoaded();
	return names_;
}

uint32_t Engine::ProjectRenderingLayerSettings::GetDefinedMask() {

	EnsureLoaded();
	uint32_t mask = 0u;
	for (uint32_t index = 0u; index < kLayerCount; ++index) {
		if (!names_[index].empty()) {
			mask |= 1u << index;
		}
	}
	return mask;
}

bool Engine::ProjectRenderingLayerSettings::IsValidNewLayer(const std::string& name) {

	EnsureLoaded();
	const std::string trimmed = Trim(name);
	if (trimmed.empty() || ContainsName(trimmed)) {
		return false;
	}
	return std::ranges::any_of(names_.begin() + 1, names_.end(),
		[](const std::string& current) {

			return current.empty();
		});
}

bool Engine::ProjectRenderingLayerSettings::AddLayer(const std::string& name) {

	if (!IsValidNewLayer(name)) {
		return false;
	}
	const std::string trimmed = Trim(name);
	for (uint32_t index = 1u; index < kLayerCount; ++index) {
		if (names_[index].empty()) {
			names_[index] = trimmed;
			dirty_ = true;
			return true;
		}
	}
	return false;
}

bool Engine::ProjectRenderingLayerSettings::SetName(uint32_t index, const std::string& name) {

	EnsureLoaded();
	const std::string trimmed = Trim(name);
	if (index == 0u || kLayerCount <= index || trimmed.empty() ||
		ContainsName(trimmed, index)) {

		return false;
	}
	names_[index] = trimmed;
	dirty_ = true;
	return true;
}

bool Engine::ProjectRenderingLayerSettings::RemoveLayer(uint32_t index) {

	EnsureLoaded();
	if (index == 0u || kLayerCount <= index || names_[index].empty()) {
		return false;
	}
	names_[index].clear();
	dirty_ = true;
	return true;
}

void Engine::ProjectRenderingLayerSettings::Reload() {

	dirty_ = false;
	loaded_ = false;
	LoadFromDisk();
}

bool Engine::ProjectRenderingLayerSettings::Save() {

	EnsureLoaded();
	if (!ProjectSettingsStorage::SaveLayers(GetSettingsPath(), names_)) {
		return false;
	}
	dirty_ = false;
	return true;
}

void Engine::ProjectRenderingLayerSettings::SetDefaults() {

	names_.fill({});
	names_[0] = "Default";
}

bool Engine::ProjectRenderingLayerSettings::ContainsName(const std::string& name, uint32_t ignoredIndex) {

	for (uint32_t index = 0u;
		index < Engine::ProjectRenderingLayerSettings::kLayerCount; ++index) {

		if (index != ignoredIndex && names_[index] == name) {
			return true;
		}
	}
	return false;
}

void Engine::ProjectRenderingLayerSettings::LoadFromDisk() {

	SetDefaults();
	const nlohmann::json data = ProjectSettingsStorage::Load(GetSettingsPath());
	if (!data.is_discarded()) {
		if (data.is_object()) {
			const auto layers = data.find("layers");
			if (layers != data.end() && layers->is_array()) {
				const size_t count = (std::min)(
					layers->size(), names_.size());
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

							names_[index] = name;
						}
					}
				}
			}
		}
	}
	loaded_ = true;
}

void Engine::ProjectRenderingLayerSettings::EnsureLoaded() {

	if (!loaded_) {
		LoadFromDisk();
	}
}
