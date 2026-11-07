#pragma once

//============================================================================
//	include
//============================================================================
#include "EditorTemplateTypes.h"

namespace Engine::EditorTemplateStorage {

	// エンジン開発環境の配布元を取得する
	std::filesystem::path Repository();
	// 標準またはサンプルの保存先を取得する
	std::filesystem::path ResolveRoot(const std::string& id);
	// 配布元のメタデータを読み込む
	bool Read(const std::filesystem::path& root, EditorTemplateEntry& entry);
	// 保存済みサンプルを名前順に取得する
	std::vector<EditorTemplateEntry> ListSamples();
	// 通常のテンプレート情報を作成する
	EditorTemplateEntry MakeEntry(const std::string& kind);
	// 保存した配布元を独立した編集用プロジェクトで開く
	EditorTemplateResult OpenWorkspace(const EditorTemplateEntry& entry);
	// 編集用プロジェクトが由来とする配布元を取得する
	bool ReadWorkspaceEntry(EditorTemplateEntry& entry);
	// 配布元の範囲を確認して削除する
	EditorTemplateResult RemoveSample(const std::string& id);
	// リンクや範囲外のファイルを含めずにコピーする
	void CopyTree(const std::filesystem::path& source, const std::filesystem::path& target);
	// 同じ保存先を退避して完成済みの内容へ置き換える
	void ReplaceDirectory(const std::filesystem::path& staged, const std::filesystem::path& target);
	// 保存元のメタデータを書き出す
	bool WriteEntry(const EditorTemplateEntry& entry, const std::filesystem::path& root);
}
