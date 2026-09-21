#pragma once

//============================================================================
//	include
//============================================================================

// c++
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

//============================================================================
//	ShaderSourcePathResolver
//	shader.jsonやインラインdescで指定されたシェーダーソース参照を実体パスへ解決する
//	参照は.metaのguidでもShadersルート相対パスでも受け付ける
//============================================================================
namespace Engine::ShaderSourcePath {

	using Index = std::unordered_map<std::string, std::filesystem::path>;

	// 32桁AssetGUIDを索引検索用の小文字表記へ正規化する
	std::optional<std::string> NormalizeGuidReference(std::string_view file);

	// .metaから "guid" の値を取り出す、JSON依存を避けて軽量に文字列抽出する
	std::string ReadGuidFromMeta(const std::filesystem::path& metaPath);

	// ファイル名->実体パスの索引、起動時に一度だけ構築する
	const Index& GetFileNameIndex();

	// GUID->実体パスの索引、シェーダーソースの.hlslや.hlsliの横の.metaから構築する
	const Index& GetGuidIndex();

	// シェーダーソース参照のGUIDまたはパスを実体パスへ解決する
	std::filesystem::path Resolve(const std::string& fileOrGuid);
} // namespace Engine::ShaderSourcePath
