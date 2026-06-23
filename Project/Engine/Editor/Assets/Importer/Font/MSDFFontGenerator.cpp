#include "MSDFFontGenerator.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>

// c++
#include <fstream>
#include <vector>
#include <cctype>

// json
#include <json.hpp>

// msdf-atlas-gen
// ライブラリ統合が済むまではNEM_USE_MSDF_ATLAS_GENを未定義にして、依存ヘッダ無しでもビルドが通るようにする
#if defined(NEM_USE_MSDF_ATLAS_GEN)
#include <msdf-atlas-gen/msdf-atlas-gen.h>
// アトラスPNGはmsdf側のlodepng/libpng依存を避け、エンジン同梱のDirectXTexで書き出す
#include <DirectXTex.h>
#include <windows.h>
#endif

//============================================================================
//	MSDFFontGenerator anonymous
//============================================================================
namespace {

	// 生成パラメータ、従来のGenerate-MSDF.ps1と同じ値に揃える
	constexpr double kFontSize = 48.0;
	constexpr double kPixelRange = 8.0;
	constexpr double kMaxCornerAngle = 3.0;
	constexpr double kMiterLimit = 1.0;
	constexpr int kAtlasWidth = 2048;
	constexpr int kAtlasHeight = 2048;
	constexpr int kThreadCount = 4;

	// 生成物の命名サフィックス、AssetDatabaseのReconcileFontAtlasReferencesと前提を合わせる
	constexpr const char* kFontJsonSuffix = "_msdf.font.json";
	constexpr const char* kAtlasSuffix = "_msdf.png";
	constexpr const char* kRawJsonSuffix = "_msdf.raw.json";

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

#if defined(NEM_USE_MSDF_ATLAS_GEN)

	// reorient済みアトラスをRGBAへ展開しPNGとして書き出す、PNG出力はDirectXTexに任せる
	bool SaveAtlasPng(const msdfgen::BitmapConstSection<msdfgen::byte, 3>& bitmap, const std::filesystem::path& pngPath) {

		const int width = bitmap.width;
		const int height = bitmap.height;
		if (width <= 0 || height <= 0) {
			return false;
		}

		// MSDFは3chだが保存はRGBA、αは描画で未使用なので255で埋める
		std::vector<uint8_t> rgba(static_cast<size_t>(width) * height * 4);
		for (int y = 0; y < height; ++y) {
			for (int x = 0; x < width; ++x) {

				const msdfgen::byte* src = bitmap(x, y);
				uint8_t* dst = &rgba[(static_cast<size_t>(y) * width + x) * 4];
				dst[0] = src[0];
				dst[1] = src[1];
				dst[2] = src[2];
				dst[3] = 255;
			}
		}

		DirectX::Image image{};
		image.width = static_cast<size_t>(width);
		image.height = static_cast<size_t>(height);
		image.format = DXGI_FORMAT_R8G8B8A8_UNORM;
		image.rowPitch = static_cast<size_t>(width) * 4;
		image.slicePitch = rgba.size();
		image.pixels = rgba.data();

		// WICはCOMを要求する、未初期化スレッドでも動くよう一時的に初期化して後で戻す
		const HRESULT coInit = ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);
		const bool shouldUninit = SUCCEEDED(coInit);
		const HRESULT hr = DirectX::SaveToWICFile(image, DirectX::WIC_FLAGS_NONE,
			DirectX::GetWICCodec(DirectX::WIC_CODEC_PNG), pngPath.wstring().c_str());
		if (shouldUninit) {
			::CoUninitialize();
		}
		return SUCCEEDED(hr);
	}

	// msdf-atlas-genでアトラス画像とraw jsonを書き出す、設定値は標準実装とGenerate-MSDF.ps1に揃える
	bool RunMsdfGeneration(const std::filesystem::path& fontPath, const std::filesystem::path& charsetPath,
		const std::filesystem::path& rawJsonPath, const std::filesystem::path& pngPath) {

		using namespace msdf_atlas;

		msdfgen::FreetypeHandle* freetype = msdfgen::initializeFreetype();
		if (!freetype) {

			Engine::Logger::Output(Engine::LogType::Engine, spdlog::level::warn,
				"[MSDFFontGenerator] failed to initialize freetype");
			return false;
		}

		bool success = false;
		if (msdfgen::FontHandle* font = msdfgen::loadFont(freetype, fontPath.string().c_str())) {

			std::vector<GlyphGeometry> glyphs;
			FontGeometry fontGeometry(&glyphs);

			// 従来と同じcharsetファイルを読む、未指定文字を増やしたいときはgame_charset.txtを編集する
			Charset charset;
			charset.load(charsetPath.string().c_str());
			// Skia無しビルドなのでジオメトリ前処理は行わない、標準実装のSkia無効時と同じ
			fontGeometry.loadCharset(font, 1.0, charset, false, true);

			// エッジ着色は標準既定のインクトラップ方式、角しきい値とseedも既定に合わせる
			for (GlyphGeometry& glyph : glyphs) {
				glyph.edgeColoring(&msdfgen::edgeColoringInkTrap, kMaxCornerAngle, 0);
			}

			// 固定2048正方アトラスへ48px/emで詰める、Generate-MSDF.ps1と同じ設定
			TightAtlasPacker packer;
			packer.setDimensions(kAtlasWidth, kAtlasHeight);
			packer.setSpacing(0);
			packer.setScale(kFontSize);
			packer.setPixelRange(msdfgen::Range(kPixelRange));
			packer.setUnitRange(msdfgen::Range(0.0));
			packer.setMiterLimit(kMiterLimit);
			packer.setOriginPixelAlignment(false, false);
			packer.pack(glyphs.data(), static_cast<int>(glyphs.size()));

			int width = 0;
			int height = 0;
			packer.getDimensions(width, height);

			// 前処理無しなのでオーバーラップ対応とスキャンラインパスを有効化する、標準のSkia無効時と同じ
			ImmediateAtlasGenerator<float, 3, msdfGenerator, BitmapAtlasStorage<msdfgen::byte, 3>> generator(width, height);
			GeneratorAttributes attributes;
			attributes.config.overlapSupport = true;
			attributes.scanlinePass = true;
			generator.setAttributes(attributes);
			generator.setThreadCount(kThreadCount);
			generator.generate(glyphs.data(), static_cast<int>(glyphs.size()));

			// yorigin topへ反転してから保存する、描画側のatlasBounds前提に合わせる
			msdfgen::BitmapConstSection<msdfgen::byte, 3> bitmap = generator.atlasStorage();
			bitmap.reorient(msdfgen::Y_DOWNWARD);

			JsonAtlasMetrics metrics{};
			metrics.distanceRange = packer.getPixelRange();
			metrics.size = packer.getScale();
			metrics.width = width;
			metrics.height = height;
			metrics.yDirection = msdfgen::Y_DOWNWARD;

			if (SaveAtlasPng(bitmap, pngPath) &&
				exportJSON(&fontGeometry, 1, ImageType::MSDF, metrics, rawJsonPath.string().c_str(), true)) {

				success = true;
			}
			msdfgen::destroyFont(font);
		} else {

			Engine::Logger::Output(Engine::LogType::Engine, spdlog::level::warn,
				"[MSDFFontGenerator] failed to load font. path={}", fontPath.string());
		}

		msdfgen::deinitializeFreetype(freetype);
		return success;
	}

#else

	// ライブラリ未統合時は生成不可、呼び出し側は失敗として扱う
	bool RunMsdfGeneration(const std::filesystem::path&, const std::filesystem::path&,
		const std::filesystem::path&, const std::filesystem::path&) {

		Engine::Logger::Output(Engine::LogType::Engine, spdlog::level::warn,
			"[MSDFFontGenerator] msdf-atlas-gen is not integrated. define NEM_USE_MSDF_ATLAS_GEN to enable generation");
		return false;
	}

#endif

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

	const bool alreadyGenerated =
		std::filesystem::exists(fontJsonPath, ec) && std::filesystem::exists(atlasPath, ec);

	const std::string fontAssetPath = RuntimePaths::ToAssetPath(fontJsonPath);

	// 既に生成済みかつDBにも登録済みなら、再走査せずそのまま既存アセットを使う
	// これでドロップのたびにRebuildMeta(全アセット再走査)が走って重くなるのを防ぐ
	if (!forceRegenerate && alreadyGenerated) {
		if (const AssetMeta* existing = database.FindByPath(fontAssetPath)) {

			result.success = true;
			result.fontAssetID = existing->guid;
			result.fontAssetPath = fontAssetPath;
			return result;
		}
	}

	if (forceRegenerate || !alreadyGenerated) {

		const std::filesystem::path charsetPath = GameCharsetPath();
		if (!std::filesystem::exists(charsetPath, ec) || ec) {

			result.message = "charsetファイルが見つかりません: " + charsetPath.string();
			return result;
		}

		// 中間のraw jsonは従来同様使い捨てなので生成後に消す
		const std::filesystem::path rawJsonPath = directory / (stem + kRawJsonSuffix);
		if (!RunMsdfGeneration(fontSourcePath, charsetPath, rawJsonPath, atlasPath)) {

			result.message = "MSDF生成に失敗しました";
			return result;
		}

		const std::string atlasAssetPath = RuntimePaths::ToAssetPath(atlasPath);
		const bool wrapped = WrapRawJsonToFontJson(rawJsonPath, fontJsonPath, MakeFontName(stem), atlasAssetPath);
		std::filesystem::remove(rawJsonPath, ec);
		if (!wrapped) {

			result.message = ".font.jsonの書き出しに失敗しました";
			return result;
		}
	}

	// 新規生成物に.metaを発番し、atlasTextureを隣接アトラスのGUIDへ貼り直す
	database.RebuildMeta();

	const AssetMeta* meta = database.FindByPath(fontAssetPath);
	if (!meta) {

		result.message = "生成した.font.jsonの登録に失敗しました";
		return result;
	}

	result.success = true;
	result.fontAssetID = meta->guid;
	result.fontAssetPath = fontAssetPath;
	return result;
}
