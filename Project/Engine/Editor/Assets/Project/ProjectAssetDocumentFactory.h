#pragma once

//============================================================================
//	include
//============================================================================
#include "ProjectAssetFileTypes.h"

namespace Engine::ProjectAssetDocumentFactory {

	// ファイル作成用の拡張子を取得する
	const char* GetFileSuffix(ProjectAssetFileKind kind);

	// 作成するファイルの中身を構築する
	std::string BuildFileContent(ProjectAssetFileKind kind, const std::string& assetName);

	// 既存ファイルを置き換えず新しい文書を作成する
	bool CreateTextFile(const std::filesystem::path& path, const std::string& content);

	// ゲームスクリプトのルート名前空間をcsprojから取得する
	std::string LoadGameScriptRootNamespace();
}
