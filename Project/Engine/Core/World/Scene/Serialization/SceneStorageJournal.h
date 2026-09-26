#pragma once

//============================================================================
//	include
//============================================================================

// c++
#include <filesystem>
#include <functional>
#include <string>
#include <vector>
// engine
#include <Engine/Core/Foundation/Serialization/Json/JsonFileJournal.h>

namespace Engine {

	// ファイル単位の書込または削除要求
	using SceneStorageChange = JsonFileChange;

	namespace SceneStorageJournal {

		using RecoveryCheck = std::function<void(const std::filesystem::path&)>;
		using RecoveryAction = std::function<bool(const std::filesystem::path&, std::string&)>;

		// ファイル変更を退避して適用し、失敗時は復旧処理へ渡す
		bool Commit(const std::vector<SceneStorageChange>& changes, const std::string& label,
			std::string& error, const RecoveryAction& recover);
		// 操作記録の一覧を取得する
		std::vector<std::filesystem::path> GetRecoveries(bool unfinishedOnly = false);
		// 復旧対象の使用状態を確認してから逆順に復旧する
		bool Recover(const std::filesystem::path& directory, std::string& error, const RecoveryCheck& check);
	}
} // Engine
