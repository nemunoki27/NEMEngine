#include "ProjectSettingsStorage.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Diagnostics/Log.h>

// c++
#include <fstream>
#include <system_error>

nlohmann::json Engine::ProjectSettingsStorage::Load(const std::filesystem::path& path) {

	std::ifstream file(path, std::ios::binary);
	if (!file.is_open()) {
		return {};
	}
	return nlohmann::json::parse(file, nullptr, false);
}

bool Engine::ProjectSettingsStorage::SaveTags(const std::filesystem::path& target, const std::vector<std::string>& tags) {

	nlohmann::json root;
	root["tags"] = tags;

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

bool Engine::ProjectSettingsStorage::SaveLayers(const std::filesystem::path& target,
	const std::array<std::string, ProjectRenderingLayerSettings::kLayerCount>& names) {

	std::error_code ec;
	std::filesystem::create_directories(target.parent_path(), ec);
	if (ec) {
		Logger::Output(LogType::Engine, spdlog::level::err,
			"Rendering Layer設定のDirectoryを作成できません path={} 内容={}",
			target.parent_path().string(), ec.message());
		return false;
	}
	nlohmann::json data{};
	data["layers"] = names;
	const std::filesystem::path temporary = target.string() + ".tmp";
	{
		std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
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
