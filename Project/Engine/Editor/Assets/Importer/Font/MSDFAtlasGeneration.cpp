#include "MSDFAtlasGeneration.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Diagnostics/Log.h>

// c++
#include <vector>

// msdf-atlas-gen
// ライブラリ統合が済むまではNEM_USE_MSDF_ATLAS_GENを未定義にして、依存ヘッダ無しでもビルドが通るようにする
#if defined(NEM_USE_MSDF_ATLAS_GEN)
#include <msdf-atlas-gen/msdf-atlas-gen.h>
// アトラスPNGはmsdf側のlodepng/libpng依存を避け、エンジン同梱のDirectXTexで書き出す
#include <DirectXTex.h>
#include <windows.h>
#endif

namespace Engine::MSDFAtlasGeneration {

	// 生成パラメータ、従来のGenerate-MSDF.ps1と同じ値に揃える
	constexpr double kFontSize = 48.0;
	constexpr double kPixelRange = 8.0;
	constexpr double kMaxCornerAngle = 3.0;
	constexpr double kMiterLimit = 1.0;
	constexpr int kAtlasWidth = 2048;
	constexpr int kAtlasHeight = 2048;
	constexpr int kThreadCount = 4;

#if defined(NEM_USE_MSDF_ATLAS_GEN)

	// reorient済みアトラスをRGBAへ展開しPNGとして書き出す、PNG出力はDirectXTexに任せる
	static bool SaveAtlasPNG(const msdfgen::BitmapConstSection<msdfgen::byte, 3>& bitmap, const std::filesystem::path& pngPath) {

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
	bool Generate(const std::filesystem::path& fontPath, const std::filesystem::path& charsetPath,
		const std::filesystem::path& rawJsonPath, const std::filesystem::path& pngPath) {

		using namespace msdf_atlas;

		msdfgen::FreetypeHandle* freetype = msdfgen::initializeFreetype();
		if (!freetype) {

			Engine::Logger::Output(Engine::LogType::Engine, spdlog::level::warn,
				"[MSDFFontGenerator] FreeTypeの初期化に失敗しました");
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

			if (SaveAtlasPNG(bitmap, pngPath) &&
				exportJSON(&fontGeometry, 1, ImageType::MSDF, metrics, rawJsonPath.string().c_str(), true)) {

				success = true;
			}
			msdfgen::destroyFont(font);
		} else {

			Engine::Logger::Output(Engine::LogType::Engine, spdlog::level::warn,
				"[MSDFFontGenerator] Fontを読み込めません path={}", fontPath.string());
		}

		msdfgen::deinitializeFreetype(freetype);
		return success;
	}

#else

	// ライブラリ未統合時は生成不可、呼び出し側は失敗として扱う
	bool Generate(const std::filesystem::path&, const std::filesystem::path&,
		const std::filesystem::path&, const std::filesystem::path&) {

		Engine::Logger::Output(Engine::LogType::Engine, spdlog::level::warn,
			"[MSDFFontGenerator] msdf-atlas-genが未統合です 生成を有効にするにはNEM_USE_MSDF_ATLAS_GENを定義してください");
		return false;
	}

#endif
}
