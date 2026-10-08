#pragma once

//============================================================================
//	include
//============================================================================
#include "ProjectAssetFileTypes.h"

#include <utility>
#include <vector>

namespace Engine::ProjectAssetPath {

	// ファイル名として使えない文字を除去・置換する
	std::string SanitizeFileName(std::string text);

	// ファイル名からC#クラス名として有効な文字列を生成する
	std::string MakeCSharpClassName(const std::string& fileName);

	// 複合拡張子を優先してファイル名を分割する
	std::pair<std::string, std::string> SplitAssetFileName(const std::filesystem::path& path);

	// 本体とmetaの両方に衝突しないパスを作る
	std::filesystem::path MakeUniquePath(const std::filesystem::path& preferredPath);

	// 末尾のアセット拡張子を取り除く
	std::string RemoveTypedSuffix(std::string name, const char* suffix);

	// 絶対指定と親階層への移動を含まない相対パスを判定する
	bool IsSafeRelativePath(const std::filesystem::path& path);

	// 実パスを所属するAssetルートの論理パスへ変換する
	std::string ToAssetPath(const std::filesystem::path& fullPath);

	// ソースに対応する実ファイル上のルートを取得する
	std::filesystem::path GetSourceRoot(ProjectAssetSource source);

	// 仮想ディレクトリパスから実ディレクトリパスを取得する
	std::filesystem::path ResolveVirtualDirectory(ProjectAssetSource source, const std::string& directoryVirtualPath);

	// 仮想ルート名を取得する
	const char* GetSourceVirtualRoot(ProjectAssetSource source);

	// あるパスが指定の親パスと同一または配下かを判定する
	bool IsSameOrChildPath(const std::filesystem::path& path, const std::filesystem::path& parent);

	// 同じstemへ指定した拡張子を付ける
	std::filesystem::path MakeSiblingPath(const std::filesystem::path& targetPath, const std::filesystem::path& extension);

	// アセットパスに対応する.metaファイルのパスを作る
	std::filesystem::path MakeMetaPath(const std::filesystem::path& path);
}
