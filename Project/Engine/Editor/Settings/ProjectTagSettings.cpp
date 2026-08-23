#include "ProjectTagSettings.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>

// c++
#include <fstream>
#include <algorithm>
#include <filesystem>
#include <system_error>

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

	std::vector<std::string> g_tags;
	bool g_loaded = false;

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

	// 既に同名タグを持っているか、完全一致で判定する
	bool ContainsTag(const std::string& tag) {
		return std::find(g_tags.begin(), g_tags.end(), tag) != g_tags.end();
	}

	void LoadFromDisk() {

		g_tags.clear();
		std::ifstream ifs(TagSettingsPath(), std::ios::binary);
		if (ifs.is_open()) {

			nlohmann::json data = nlohmann::json::parse(ifs, nullptr, false);
			if (data.is_object() && data.contains("tags") && data["tags"].is_array()) {
				for (const auto& tag : data["tags"]) {
					if (tag.is_string()) {
						g_tags.push_back(tag.get<std::string>());
					}
				}
			}
		}

		// 読み込めなかった、もしくはUntaggedが無い場合は既定で補う
		if (g_tags.empty()) {
			g_tags = kDefaultTags;
		} else if (!ContainsTag(kUntagged)) {
			g_tags.insert(g_tags.begin(), kUntagged);
		}
		g_loaded = true;
	}

	// 未ロードならファイルから読み込む、編集系から呼ぶ
	void EnsureLoaded() {

		if (!g_loaded) {
			LoadFromDisk();
		}
	}
}

//============================================================================
//	ProjectTagSettings methods
//============================================================================
const std::vector<std::string>& Engine::ProjectTagSettings::GetTags() {

	if (!g_loaded) {
		LoadFromDisk();
	}
	return g_tags;
}

void Engine::ProjectTagSettings::Reload() {

	g_loaded = false;
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
	g_tags.push_back(Trim(tag));
	return true;
}

bool Engine::ProjectTagSettings::RemoveTag(const std::string& tag) {

	EnsureLoaded();

	// Untaggedは既定タグとして常に残す
	if (tag == kUntagged) {
		return false;
	}
	const auto it = std::find(g_tags.begin(), g_tags.end(), tag);
	if (it == g_tags.end()) {
		return false;
	}
	g_tags.erase(it);
	return true;
}

bool Engine::ProjectTagSettings::RenameTag(const std::string& from, const std::string& to) {

	EnsureLoaded();

	// Untaggedの改名は不可で、変更後が無効や重複の場合も不可
	if (from == kUntagged || !IsValidNewTag(to)) {
		return false;
	}
	const auto it = std::find(g_tags.begin(), g_tags.end(), from);
	if (it == g_tags.end()) {
		return false;
	}
	// 順序を維持したまま名前だけ置き換える
	*it = Trim(to);
	return true;
}

bool Engine::ProjectTagSettings::Save() {

	EnsureLoaded();

	nlohmann::json root;
	root["tags"] = g_tags;

	const std::filesystem::path target = TagSettingsPath();
	std::error_code ec;
	std::filesystem::create_directories(target.parent_path(), ec);

	// 一時ファイルへ書き出してからバックアップと安全な置換とロールバックで置換する
	const std::filesystem::path temp = target.string() + ".tmp";
	{
		std::ofstream file(temp, std::ios::binary | std::ios::trunc);
		if (!file.is_open()) {
			Logger::Output(LogType::Engine, spdlog::level::err,
				"ProjectTagSettings: 保存用一時ファイルを開けません: {}", temp.string());
			return false;
		}
		file << root.dump(2);
		file.flush();
		if (!file.good()) {
			file.close();
			std::filesystem::remove(temp, ec);
			Logger::Output(LogType::Engine, spdlog::level::err,
				"ProjectTagSettings: 一時ファイルへ書き込めません: {}", temp.string());
			return false;
		}
	}

	const bool targetExists = std::filesystem::exists(target, ec);
	const std::filesystem::path backup = target.string() + ".bak";
	if (targetExists) {
		// 既存をバックアップへ退避する、Windowsで一時から対象への直接renameが失敗しても元を失わない
		std::filesystem::remove(backup, ec);
		std::filesystem::rename(target, backup, ec);
		if (ec) {
			std::filesystem::remove(temp, ec);
			Logger::Output(LogType::Engine, spdlog::level::err,
				"ProjectTagSettings: Backup作成に失敗したため既存ファイルを維持します path={} 内容={}",
				target.string(), ec.message());
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
			"ProjectTagSettings: ファイル置換に失敗したため元へ戻しました path={} 内容={}",
			target.string(), ec.message());
		return false;
	}

	// 置換成功、バックアップの掃除失敗は警告に留める、有効なファイルは既に正
	if (targetExists) {
		std::filesystem::remove(backup, ec);
		if (ec) {
			Logger::Output(LogType::Engine, spdlog::level::warn,
				"ProjectTagSettings: Backupを削除できません path={} 内容={}", backup.string(), ec.message());
		}
	}
	return true;
}
