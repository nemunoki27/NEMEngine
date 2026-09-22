#include "ProjectTagSettings.h"
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

//============================================================================
//	ProjectTagSettings anonymous
//============================================================================
namespace {

	// 予約タグ、先頭固定で削除やリネームの対象にしない
	const std::string kUntagged = "Untagged";

	// 既定タグ
	const std::vector<std::string> kDefaultTags = {
		"Untagged", "Player"
	};

	std::filesystem::path TagSettingsPath() {
		return Engine::RuntimePaths::GetProjectSettingsPath("TagSettings.json");
	}

	// 前後の空白を取り除いた文字列を返す
	std::string Trim(const std::string& text) {

		const auto begin = text.find_first_not_of(" \t\r\n");
		if (begin == std::string::npos) {
			return std::string{};
		}
		const auto end = text.find_last_not_of(" \t\r\n");
		return text.substr(begin, end - begin + 1);
	}

}

//============================================================================
//	ProjectTagSettings methods
//============================================================================
const std::vector<std::string>& Engine::ProjectTagSettings::GetTags() {

	if (!loaded_) {
		LoadFromDisk();
	}
	return tags_;
}

void Engine::ProjectTagSettings::Reload() {

	dirty_ = false;
	loaded_ = false;
	LoadFromDisk();
}

bool Engine::ProjectTagSettings::IsValidNewTag(const std::string& tag) {

	EnsureLoaded();

	// 空や前後空白のみは不可、重複も不可でUntaggedは常に存在するため重複扱いで弾かれる
	const std::string trimmed = Trim(tag);
	if (trimmed.empty()) {
		return false;
	}
	return !ContainsTag(trimmed);
}

bool Engine::ProjectTagSettings::AddTag(const std::string& tag) {

	if (!IsValidNewTag(tag)) {
		return false;
	}
	tags_.push_back(Trim(tag));
	dirty_ = true;
	return true;
}

bool Engine::ProjectTagSettings::RemoveTag(const std::string& tag) {

	EnsureLoaded();

	// Untaggedは既定タグとして常に残す
	if (tag == kUntagged) {
		return false;
	}
	const auto it = std::find(tags_.begin(), tags_.end(), tag);
	if (it == tags_.end()) {
		return false;
	}
	tags_.erase(it);
	dirty_ = true;
	return true;
}

bool Engine::ProjectTagSettings::RenameTag(const std::string& from, const std::string& to) {

	EnsureLoaded();

	// Untaggedの改名は不可で、変更後が無効や重複の場合も不可
	if (from == kUntagged || !IsValidNewTag(to)) {
		return false;
	}
	const auto it = std::find(tags_.begin(), tags_.end(), from);
	if (it == tags_.end()) {
		return false;
	}
	// 順序を維持したまま名前だけ置き換える
	*it = Trim(to);
	dirty_ = true;
	return true;
}

bool Engine::ProjectTagSettings::Save() {

	EnsureLoaded();
	if (!ProjectSettingsStorage::SaveTags(TagSettingsPath(), tags_)) {
		return false;
	}
	dirty_ = false;
	return true;
}

bool Engine::ProjectTagSettings::ContainsTag(const std::string& tag) {
	return std::find(tags_.begin(), tags_.end(), tag) != tags_.end();
}

void Engine::ProjectTagSettings::LoadFromDisk() {

	tags_.clear();
	const nlohmann::json data = ProjectSettingsStorage::Load(TagSettingsPath());
	if (!data.is_discarded()) {
		if (data.is_object() && data.contains("tags") && data["tags"].is_array()) {
			for (const auto& tag : data["tags"]) {
				if (tag.is_string()) {
					tags_.push_back(tag.get<std::string>());
				}
			}
		}
	}

	// 読み込めなかった、もしくはUntaggedが無い場合は既定で補う
	if (tags_.empty()) {
		tags_ = kDefaultTags;
	} else if (!ContainsTag(kUntagged)) {
		tags_.insert(tags_.begin(), kUntagged);
	}
	loaded_ = true;
}

void Engine::ProjectTagSettings::EnsureLoaded() {

	if (!loaded_) {
		LoadFromDisk();
	}
}
