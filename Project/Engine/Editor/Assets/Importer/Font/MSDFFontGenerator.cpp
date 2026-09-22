#include "MSDFFontGenerator.h"
#include "MSDFAtlasGeneration.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>
#include <Engine/Core/Rendering/Assets/MSDFFontAsset.h>

// c++
#include <chrono>
#include <fstream>
#include <vector>
#include <cctype>

// json
#include <json.hpp>

//============================================================================
//	MSDFFontGenerator anonymous
//============================================================================
namespace {

	// 生成物の命名サフィックス、AssetDatabaseのReconcileFontAtlasReferencesと前提を合わせる
	constexpr const char* kFontJsonSuffix = "_msdf.font.json";
	constexpr const char* kAtlasSuffix = "_msdf.png";
	constexpr const char* kRawJsonSuffix = "_msdf.raw.json";

	// 一時生成ディレクトリをスコープ終了時に削除する
	struct GenerationDirectoryCleanup {

		std::filesystem::path path;

		~GenerationDirectoryCleanup() {

			if (path.empty()) {
				return;
			}
			std::error_code ec{};
			std::filesystem::remove_all(path, ec);
		}
	};

	// 既存生成物を一時ディレクトリへ退避する
	bool BackupGeneratedFile(const std::filesystem::path& target,
		const std::filesystem::path& backup, bool& outExisted) {

		std::error_code ec{};
		outExisted = std::filesystem::exists(target, ec);
		if (ec) {
			return false;
		}
		if (!outExisted) {
			return true;
		}
		return std::filesystem::copy_file(target, backup,
			std::filesystem::copy_options::overwrite_existing, ec) && !ec;
	}

	// 退避した生成物を元へ戻す
	bool RestoreGeneratedFile(const std::filesystem::path& target,
		const std::filesystem::path& backup, bool existed) {

		std::error_code ec{};
		if (existed) {
			return std::filesystem::copy_file(backup, target,
				std::filesystem::copy_options::overwrite_existing, ec) && !ec;
		} else {
			std::filesystem::remove(target, ec);
			return !ec;
		}
	}

	// フォント情報とアトラスを退避時点へ戻す
	bool RestoreGeneratedFiles(const std::filesystem::path& fontJsonPath,
		const std::filesystem::path& fontJsonBackupPath, bool fontJsonExisted,
		const std::filesystem::path& atlasPath,
		const std::filesystem::path& atlasBackupPath, bool atlasExisted) {

		const bool fontRestored = RestoreGeneratedFile(
			fontJsonPath, fontJsonBackupPath, fontJsonExisted);
		const bool atlasRestored = RestoreGeneratedFile(
			atlasPath, atlasBackupPath, atlasExisted);
		return fontRestored && atlasRestored;
	}

	// 一時領域の生成物が描画に必要な内容を持つか検証する
	bool ValidateGeneratedFiles(const std::filesystem::path& fontJsonPath,
		const std::filesystem::path& atlasPath) {

		std::error_code ec{};
		if (!std::filesystem::is_regular_file(atlasPath, ec) || ec ||
			std::filesystem::file_size(atlasPath, ec) == 0 || ec) {

			return false;
		}

		std::ifstream ifs(fontJsonPath, std::ios::binary);
		if (!ifs.is_open()) {
			return false;
		}
		const nlohmann::json data = nlohmann::json::parse(ifs, nullptr, false);
		Engine::MSDFFontAsset font{};
		return !data.is_discarded() && Engine::FromJson(data, font) &&
			!font.glyphMap.empty();
	}

	// game_charsetの場所、@includeでbase_charsetを取り込むため同階層にbase_charsetを置く
	std::filesystem::path GameCharsetPath() {

		return Engine::RuntimePaths::GetGameRoot() / "GameAssets" / "Fonts" / "Charset" / "game_charset.txt";
	}

	// JSONの"name"に使う名前をソースファイル名から作る、区切りを除いて各語の先頭だけ大文字にする
	std::string MakeFontName(const std::string& stem) {

		std::string result;
		bool nextUpper = true;
		for (char c : stem) {

			// 英数字以外は語の区切りとして読み飛ばす、次の文字を大文字始まりにする
			if (!std::isalnum(static_cast<unsigned char>(c))) {
				nextUpper = true;
				continue;
			}
			result += nextUpper ? static_cast<char>(std::toupper(static_cast<unsigned char>(c))) : c;
			nextUpper = false;
		}
		return result.empty() ? stem : result;
	}

	// msdf-atlas-genのraw jsonを、エンジンが読む.font.jsonへ包み直す
	bool WrapRawJsonToFontJson(const std::filesystem::path& rawJsonPath, const std::filesystem::path& fontJsonPath,
		const std::string& fontName, const std::string& atlasAssetPath) {

		std::ifstream ifs(rawJsonPath, std::ios::binary);
		if (!ifs.is_open()) {
			return false;
		}
		nlohmann::json raw = nlohmann::json::parse(ifs, nullptr, false);
		if (raw.is_discarded() || !raw.is_object()) {
			return false;
		}

		// name と atlasTexture を補ってから既存スキーマへ詰め直す、atlasTextureはRebuildMetaがGUIDへ貼り直す
		nlohmann::json fontJson;
		fontJson["name"] = fontName;
		fontJson["atlasTexture"] = atlasAssetPath;
		fontJson["atlas"] = raw.value("atlas", nlohmann::json::object());
		fontJson["metrics"] = raw.value("metrics", nlohmann::json::object());
		fontJson["glyphs"] = raw.value("glyphs", nlohmann::json::array());
		fontJson["kerning"] = raw.value("kerning", nlohmann::json::array());

		std::ofstream ofs(fontJsonPath, std::ios::binary | std::ios::trunc);
		if (!ofs.is_open()) {
			return false;
		}
		ofs << fontJson.dump(2);
		return true;
	}
}

//============================================================================
//	MSDFFontGenerator methods
//============================================================================
bool Engine::MSDFFontGenerator::IsFontSourceExtension(const std::filesystem::path& path) {

	const std::string extension = Algorithm::ToLower(path.extension().string());
	return extension == ".ttf" || extension == ".otf";
}

Engine::MSDFFontGenerator::Result Engine::MSDFFontGenerator::EnsureGenerated(
	AssetDatabase& database, const std::filesystem::path& fontSourcePath, bool forceRegenerate) {

	Result result{};

	if (!IsFontSourceExtension(fontSourcePath)) {

		result.message = "対応していない拡張子です";
		return result;
	}
	std::error_code ec{};
	if (!std::filesystem::exists(fontSourcePath, ec) || ec) {

		result.message = "フォントファイルが見つかりません";
		return result;
	}

	// 生成物はソースと同じ階層に同名プレフィックスで置く、Reconcileが隣接アトラスを探せるようにするため
	const std::filesystem::path directory = fontSourcePath.parent_path();
	const std::string stem = fontSourcePath.stem().string();
	const std::filesystem::path fontJsonPath = directory / (stem + kFontJsonSuffix);
	const std::filesystem::path atlasPath = directory / (stem + kAtlasSuffix);
	const std::string fontAssetPath = RuntimePaths::ToAssetPath(fontJsonPath);
	const std::string atlasAssetPath = RuntimePaths::ToAssetPath(atlasPath);
	result.fontAssetPath = fontAssetPath;
	result.atlasAssetPath = atlasAssetPath;

	const bool alreadyGenerated =
		std::filesystem::exists(fontJsonPath, ec) && std::filesystem::exists(atlasPath, ec);

	// 既に生成済みかつDBにも登録済みなら、再走査せずそのまま既存アセットを使う
	// これでドロップのたびにRebuildMeta(全アセット再走査)が走って重くなるのを防ぐ
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

	bool generatedFilesReplaced = false;
	bool fontJsonExisted = false;
	bool atlasExisted = false;
	std::filesystem::path fontJsonBackupPath{};
	std::filesystem::path atlasBackupPath{};
	GenerationDirectoryCleanup generationDirectory{};
	if (forceRegenerate || !alreadyGenerated) {

		const std::filesystem::path charsetPath = GameCharsetPath();
		if (!std::filesystem::exists(charsetPath, ec) || ec) {

			result.message = "charsetファイルが見つかりません: " + charsetPath.string();
			return result;
		}

		// 生成途中のファイルをAssetWatchに見せないようLibrary内の一時領域を使う
		const auto uniqueValue = std::chrono::steady_clock::now().time_since_epoch().count();
		generationDirectory.path = RuntimePaths::GetLibraryPath("FontGeneration") /
			(stem + "_" + std::to_string(uniqueValue));
		std::filesystem::create_directories(generationDirectory.path, ec);
		if (ec) {

			result.message = "フォント生成用の一時ディレクトリを作成できません";
			return result;
		}

		const std::filesystem::path generatedRawJsonPath =
			generationDirectory.path / (stem + kRawJsonSuffix);
		const std::filesystem::path generatedFontJsonPath =
			generationDirectory.path / (stem + kFontJsonSuffix);
		const std::filesystem::path generatedAtlasPath =
			generationDirectory.path / (stem + kAtlasSuffix);
		if (!MSDFAtlasGeneration::Generate(fontSourcePath, charsetPath,
			generatedRawJsonPath, generatedAtlasPath)) {

			result.message = "MSDF生成に失敗しました";
			return result;
		}

		if (!WrapRawJsonToFontJson(generatedRawJsonPath, generatedFontJsonPath,
			MakeFontName(stem), atlasAssetPath)) {

			result.message = ".font.jsonの書き出しに失敗しました";
			return result;
		}
		if (!ValidateGeneratedFiles(generatedFontJsonPath, generatedAtlasPath)) {

			result.message = "生成したフォントデータの検証に失敗しました";
			return result;
		}

		// 置換失敗時に元の生成物へ戻せるよう一時領域へ退避する
		fontJsonBackupPath = generationDirectory.path / "font.backup";
		atlasBackupPath = generationDirectory.path / "atlas.backup";
		if (!BackupGeneratedFile(fontJsonPath, fontJsonBackupPath, fontJsonExisted) ||
			!BackupGeneratedFile(atlasPath, atlasBackupPath, atlasExisted)) {

			result.message = "既存フォントデータの退避に失敗しました";
			return result;
		}

		// 描画フレームを挟まずアトラスとフォント情報を続けて置換する
		std::filesystem::copy_file(generatedAtlasPath, atlasPath,
			std::filesystem::copy_options::overwrite_existing, ec);
		if (!ec) {
			std::filesystem::copy_file(generatedFontJsonPath, fontJsonPath,
				std::filesystem::copy_options::overwrite_existing, ec);
		}
		if (ec) {

			if (!RestoreGeneratedFiles(fontJsonPath, fontJsonBackupPath, fontJsonExisted,
				atlasPath, atlasBackupPath, atlasExisted)) {

				Logger::Output(LogType::Engine, spdlog::level::err,
					"[MSDFFontGenerator] フォントデータを元へ戻せません path={}",
					fontSourcePath.string());
			}
			result.message = "生成したフォントデータの置換に失敗しました";
			return result;
		}
		generatedFilesReplaced = true;
	}

	// 新規生成物に.metaを発番し、atlasTextureを隣接アトラスのGUIDへ貼り直す
	database.RebuildMeta();

	const AssetMeta* fontMeta = database.FindByPath(fontAssetPath);
	const AssetMeta* atlasMeta = database.FindByPath(atlasAssetPath);
	if (!fontMeta || !atlasMeta) {

		if (generatedFilesReplaced) {
			if (!RestoreGeneratedFiles(fontJsonPath, fontJsonBackupPath, fontJsonExisted,
				atlasPath, atlasBackupPath, atlasExisted)) {

				Logger::Output(LogType::Engine, spdlog::level::err,
					"[MSDFFontGenerator] フォントデータを元へ戻せません path={}",
					fontSourcePath.string());
			}
			database.RebuildMeta();
		}

		result.message = "生成したフォントデータの登録に失敗しました";
		return result;
	}

	result.success = true;
	result.fontAssetID = fontMeta->guid;
	result.atlasAssetID = atlasMeta->guid;
	return result;
}
