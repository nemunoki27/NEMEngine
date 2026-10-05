#include "ProjectPanel.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Runtime/Paths/ConfigPaths.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonSerializer.h>
#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>

namespace {

	// ProjectPanelの表示状態を保存するパスを返す
	std::filesystem::path GetProjectPanelStatePath() {

		return Engine::RuntimePaths::GetUserSettingsPath(Engine::ConfigPaths::kProjectPanel);
	}
}

nlohmann::json Engine::ProjectPanel::SaveLayoutState() const {

	return {
		{"displayName", displayName_},
		{"assetSource", EnumAdapter<ProjectAssetSource>::ToString(assetSource_)},
		{"selectedDirectory", selectedDirectory_},
	};
}

void Engine::ProjectPanel::LoadLayoutState(const nlohmann::json& state) {

	if (!state.is_object()) {
		return;
	}

	if (auto value = state.find("displayName"); value != state.end() && value->is_string()) {
		displayName_ = value->get<std::string>();
	}
	if (auto value = state.find("assetSource"); value != state.end() && value->is_string()) {
		assetSource_ = EnumAdapter<ProjectAssetSource>::FromString(value->get<std::string>()).value_or(assetSource_);
	}
	if (auto value = state.find("selectedDirectory"); value != state.end() && value->is_string()) {
		selectedDirectory_ = value->get<std::string>();
	}
	selectedAsset_ = {};
	dirty_ = true;
}

nlohmann::json Engine::ProjectPanel::MakeDuplicateState([[maybe_unused]] const EditorPanelContext& context) const {

	nlohmann::json state = SaveLayoutState();
	state.erase("displayName");
	return state;
}

void Engine::ProjectPanel::LoadPersistentState() {

	const std::filesystem::path path = GetProjectPanelStatePath();
	if (!JsonAdapter::Check(path)) {
		return;
	}

	const nlohmann::json data = JsonAdapter::Load(path);
	if (!data.is_object()) {
		return;
	}

	std::string assetSourceName = "Engine";
	auto assetSourceValue = data.find("assetSource");
	if (assetSourceValue != data.end() && assetSourceValue->is_string()) {
		assetSourceName = assetSourceValue->get<std::string>();
	} else if (assetSourceValue != data.end()) {
		Logger::Output(LogType::Engine, spdlog::level::warn, "ProjectPanelのアセット表示元の型が不正なためEngineへ戻します");
	}
	const std::optional<ProjectAssetSource> assetSource = EnumAdapter<ProjectAssetSource>::FromString(assetSourceName);
	if (!assetSource) {
		Logger::Output(LogType::Engine, spdlog::level::warn, "ProjectPanelのアセット表示元が不正なためEngineへ戻します");
	}
	assetSource_ = assetSource.value_or(ProjectAssetSource::Engine);

	const std::string defaultDirectory = assetSource_ == ProjectAssetSource::Game ? "GameAssets" : "Engine/Assets";
	auto selectedDirectory = data.find("selectedDirectory");
	if (selectedDirectory != data.end() && selectedDirectory->is_string()) {
		selectedDirectory_ = selectedDirectory->get<std::string>();
	} else {
		selectedDirectory_ = defaultDirectory;
		if (selectedDirectory != data.end()) {
			Logger::Output(
				LogType::Engine, spdlog::level::warn, "ProjectPanelの選択Directoryが不正なため表示元のRootへ戻します");
		}
	}
	if (selectedDirectory_.empty()) {

		selectedDirectory_ = defaultDirectory;
	}
	selectedAsset_ = {};
}

void Engine::ProjectPanel::SavePersistentState() const {

	nlohmann::json data = nlohmann::json::object();
	data["assetSource"] = EnumAdapter<ProjectAssetSource>::ToString(assetSource_);
	data["selectedDirectory"] = selectedDirectory_;

	JsonAdapter::Save(GetProjectPanelStatePath(), data);
}
