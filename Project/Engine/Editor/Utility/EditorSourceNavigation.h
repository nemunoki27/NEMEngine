#pragma once

//============================================================================
//	include
//============================================================================
// c++
#include <filesystem>
#include <string>

namespace Engine {

	class AssetDatabase;
	struct ScriptEntry;
}

//============================================================================
//	EditorSourceNavigation namespace
//	編集中のプロジェクトからソースファイルを解決して開く
//============================================================================
namespace Engine::EditorSourceNavigation {

	// エンジンソース配下の開発プロジェクトか判定する
	bool IsEngineSourceProject();
	// スクリプト参照に対応する定義元を解決する
	std::filesystem::path ResolveScriptFile(const ScriptEntry& entry, const AssetDatabase* database);
	// 登録された相対パスからコンポーネントの編集元を解決する
	std::filesystem::path ResolveComponentEditorFile(const std::string& sourceFile);

	// スクリプトの定義元をVisual Studioで開く
	bool OpenScript(const ScriptEntry& entry, const AssetDatabase* database);
	// 開発環境でのみコンポーネントの編集元をVisual Studioで開く
	bool OpenComponentEditor(const std::string& sourceFile);
}
