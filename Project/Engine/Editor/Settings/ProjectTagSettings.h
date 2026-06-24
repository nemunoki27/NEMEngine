#pragma once

//============================================================================
//	include
//============================================================================
// c++
#include <string>
#include <vector>

namespace Engine::ProjectTagSettings {

	// ProjectSettings/TagSettings.jsonのタグ一覧を取得する、未作成なら既定リストを使う
	const std::vector<std::string>& GetTags();

	// ファイルから読み直す
	void Reload();

	// 新規タグとして使える文字列か、空や前後空白除去後の重複や予約タグは不可
	bool IsValidNewTag(const std::string& tag);

	// タグを末尾へ追加する、追加できた場合のみtrue
	bool AddTag(const std::string& tag);

	// タグを削除する、Untaggedは削除不可で削除できた場合のみtrue
	bool RemoveTag(const std::string& tag);

	// タグ名を変更する、Untaggedは変更不可で順序は維持する
	bool RenameTag(const std::string& from, const std::string& to);

	// 現在のタグ一覧をTagSettings.jsonへ原子的に書き出す
	bool Save();

} // Engine::ProjectTagSettings
