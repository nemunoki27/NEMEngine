#include "MSDFFontGenerator.h"
#include "MSDFAtlasGeneration.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Assets/Database/AssetDocumentPublication.h>
#include <Engine/Core/Assets/Database/AssetDocumentRecovery.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>
#include <Engine/Core/Rendering/Assets/MSDFFontAsset.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonCanonical.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonFile.h>
#include <Engine/Core/Foundation/Serialization/StorageFileUtility.h>

// c++
#include <array>
#include <cctype>
#include <exception>
#include <utility>

namespace {

	// 既存生成物と同じ名前を使う
	constexpr const char* kFontJsonSuffix = "_msdf.font.json";
	constexpr const char* kAtlasSuffix = "_msdf.png";

	// 同じ階層の基底文字集合も読み込む
	std::filesystem::path GameCharsetPath() {

		return Engine::RuntimePaths::GetGameRoot() / "GameAssets" / "Fonts" / "Charset" / "game_charset.txt";
	}

	// 区切りを除き単語の先頭を大文字にする
	std::string MakeFontName(const std::string& stem) {

		std::string result;
		bool nextUpper = true;
		for (char value : stem) {
			if (!std::isalnum(static_cast<unsigned char>(value))) {
				nextUpper = true;
				continue;
			}
			result += nextUpper ? static_cast<char>(std::toupper(static_cast<unsigned char>(value))) : value;
			nextUpper = false;
		}
		return result.empty() ? stem : result;
	}
}

//============================================================================
//	MSDFFontGenerator namespaceMethods
//============================================================================
bool Engine::MSDFFontGenerator::IsFontSourceExtension(const std::filesystem::path& path) {

	const std::string extension = Algorithm::ToLower(path.extension().string());
	return extension == ".ttf" || extension == ".otf";
}

Engine::MSDFFontGenerator::Result Engine::MSDFFontGenerator::EnsureGenerated(
	AssetDatabase& database, const std::filesystem::path& fontSourcePath, bool forceRegenerate) {

	Result result{};
	try {
		if (!IsFontSourceExtension(fontSourcePath)) {
			result.message = "対応していない拡張子です";
			return result;
		}
		if (!std::filesystem::is_regular_file(fontSourcePath)) {
			result.message = "フォントファイルが見つかりません";
			return result;
		}

		// ソースと同じ階層にFontとAtlasを置く
		const std::filesystem::path directory = fontSourcePath.parent_path();
		const std::string stem = fontSourcePath.stem().string();
		const std::filesystem::path fontJsonPath = directory / (stem + kFontJsonSuffix);
		const std::filesystem::path atlasPath = directory / (stem + kAtlasSuffix);
		const std::string fontAssetPath = RuntimePaths::ToAssetPath(fontJsonPath);
		const std::string atlasAssetPath = RuntimePaths::ToAssetPath(atlasPath);
		result.fontAssetPath = fontAssetPath;
		result.atlasAssetPath = atlasAssetPath;
		const bool alreadyGenerated =
			std::filesystem::is_regular_file(fontJsonPath) && std::filesystem::is_regular_file(atlasPath);

		// 登録済みの生成物は全Assetの再走査なしで再利用する
		if (!forceRegenerate && alreadyGenerated) {
			const AssetMeta* existingFont = database.FindByPath(fontAssetPath);
			const AssetMeta* existingAtlas = database.FindByPath(atlasAssetPath);
			if (existingFont && existingAtlas) {
				result.success = true;
				result.fontAssetID = existingFont->guid;
				result.atlasAssetID = existingAtlas->guid;
				return result;
			}
		}

		// 生成開始前の保存先とGUIDを保持する
		const auto scope = AssetDocumentRecovery::MakeScope(AssetDocumentSaveKind::Font);
		if (!scope.isWritable(fontJsonPath) || !scope.isWritable(atlasPath)) {
			result.message = "Fontの保存先が編集可能なAsset範囲にありません";
			return result;
		}
		std::array<AssetDocumentChange, 2> changes;
		auto& atlasChange = changes[0];
		auto& fontChange = changes[1];
		if (!AssetDocumentPublication::Prepare(database, atlasAssetPath, AssetType::Texture, atlasChange, result.message) ||
			!AssetDocumentPublication::Prepare(database, fontAssetPath, AssetType::Font, fontChange, result.message)) {
			return result;
		}

		// 生成途中の画像や文書をファイルへ出さない
		if (forceRegenerate || !alreadyGenerated) {
			MSDFAtlasGeneration::Result generated;
			if (!MSDFAtlasGeneration::Generate(fontSourcePath, GameCharsetPath(), generated)) {
				result.message = "MSDF生成に失敗しました";
				return result;
			}
			fontChange.document = std::move(generated.document);
			fontChange.document["name"] = MakeFontName(stem);
			atlasChange.bytes = std::move(generated.atlasPNG);
		} else {
			// 未登録の既存生成物も同じ保存窓口で登録する
			if (!JsonFile::TryLoad(fontJsonPath, fontChange.document)) {
				result.message = "既存のFont情報を読み込めません";
				return result;
			}
			atlasChange.bytes = StorageFileUtility::ReadVerifiedBytes(atlasPath, atlasChange.fileRevision);
		}

		// 対応するAtlasのGUIDを保存前に確定する
		if (!fontChange.document.is_object()) {
			result.message = "生成したフォントデータの形式が不正です";
			return result;
		}
		fontChange.document["atlasTexture"] = ToString(atlasChange.metadata.guid);
		MSDFFontAsset font;
		if (!FromJson(fontChange.document, font) || font.glyphMap.empty() || atlasChange.bytes->empty()) {
			result.message = "生成したフォントデータの検証に失敗しました";
			return result;
		}
		fontChange.bytes = JsonCanonical::SerializeCanonical(fontChange.document, 2);
		if (fontChange.bytes->empty()) {
			result.message = ".font.jsonの書き出しに失敗しました";
			return result;
		}

		// 本体とmetaと索引をまとめて確定する
		if (!AssetDocumentPublication::Commit(database, changes, scope, result.message)) {
			return result;
		}
		result.fontAssetID = fontChange.metadata.guid;
		result.atlasAssetID = atlasChange.metadata.guid;
		result.success = true;
		return result;
	} catch (const std::exception& error) {
		result.message = error.what();
		return result;
	}
}
