#include "MSDFAtlasGeneration.h"
#include "MSDFAtlasDocument.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Diagnostics/Log.h>
#include <Engine/Core/Foundation/Utility/ScopedCleanup.h>

// c++
#include <utility>
#include <vector>

// msdf-atlas-gen
#if defined(NEM_USE_MSDF_ATLAS_GEN)
#include <msdf-atlas-gen/msdf-atlas-gen.h>
// DirectXTex
#include <DirectXTex.h>
// Windows
#include <windows.h>
#endif

namespace Engine::MSDFAtlasGeneration {

	// 固定Atlasの生成設定
	constexpr double kFontSize = 48.0;
	constexpr double kPixelRange = 8.0;
	constexpr double kMaxCornerAngle = 3.0;
	constexpr double kMiterLimit = 1.0;
	constexpr int kAtlasWidth = 2048;
	constexpr int kAtlasHeight = 2048;
	constexpr int kThreadCount = 4;

#if defined(NEM_USE_MSDF_ATLAS_GEN)

	// AtlasをRGBAのPNGへ変換する
	static bool EncodeAtlasPNG(const msdfgen::BitmapConstSection<msdfgen::byte, 3>& bitmap, std::string& out) {

		const int width = bitmap.width;
		const int height = bitmap.height;
		if (width <= 0 || height <= 0) {
			return false;
		}

		// RGBへ不透明なAlphaを補う
		std::vector<uint8_t> rgba(static_cast<size_t>(width) * height * 4);
		for (int y = 0; y < height; ++y) {
			for (int x = 0; x < width; ++x) {
				const msdfgen::byte* source = bitmap(x, y);
				uint8_t* target = &rgba[(static_cast<size_t>(y) * width + x) * 4];
				target[0] = source[0];
				target[1] = source[1];
				target[2] = source[2];
				target[3] = 255;
			}
		}
		DirectX::Image image{};
		image.width = static_cast<size_t>(width);
		image.height = static_cast<size_t>(height);
		image.format = DXGI_FORMAT_R8G8B8A8_UNORM;
		image.rowPitch = static_cast<size_t>(width) * 4;
		image.slicePitch = rgba.size();
		image.pixels = rgba.data();

		// この呼出しで初期化したCOMだけを戻す
		const HRESULT coInit = ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);
		if (FAILED(coInit) && coInit != RPC_E_CHANGED_MODE) {
			return false;
		}
		ScopedCleanup cleanup([coInit]() noexcept {
			if (SUCCEEDED(coInit)) {
				::CoUninitialize();
			}
		});
		DirectX::Blob encoded;
		if (FAILED(DirectX::SaveToWICMemory(
				image, DirectX::WIC_FLAGS_NONE, DirectX::GetWICCodec(DirectX::WIC_CODEC_PNG), encoded))) {
			return false;
		}
		out.assign(static_cast<const char*>(encoded.GetBufferPointer()), encoded.GetBufferSize());
		return true;
	}

	bool Generate(const std::filesystem::path& fontPath, const std::filesystem::path& charsetPath, Result& out) {

		using namespace msdf_atlas;

		// include先も含めて文字集合の読込成功を確認する
		Charset charset;
		if (!charset.load(charsetPath.string().c_str()) || charset.empty()) {
			Logger::Output(LogType::Engine, spdlog::level::warn, "[MSDFFontGenerator] 文字集合を読み込めません path={}",
				charsetPath.string());
			return false;
		}

		// 途中失敗でもFontとFreeTypeを解放する
		msdfgen::FreetypeHandle* freetype = msdfgen::initializeFreetype();
		if (!freetype) {
			Logger::Output(LogType::Engine, spdlog::level::warn, "[MSDFFontGenerator] FreeTypeの初期化に失敗しました");
			return false;
		}
		ScopedCleanup freetypeCleanup([freetype]() noexcept { msdfgen::deinitializeFreetype(freetype); });
		msdfgen::FontHandle* font = msdfgen::loadFont(freetype, fontPath.string().c_str());
		if (!font) {
			Logger::Output(
				LogType::Engine, spdlog::level::warn, "[MSDFFontGenerator] Fontを読み込めません path={}", fontPath.string());
			return false;
		}
		ScopedCleanup fontCleanup([font]() noexcept { msdfgen::destroyFont(font); });
		std::vector<GlyphGeometry> glyphs;
		FontGeometry fontGeometry(&glyphs);

		// Skiaによる前処理を使わず指定文字を読み込む
		if (fontGeometry.loadCharset(font, 1.0, charset, false, true) <= 0) {
			Logger::Output(LogType::Engine, spdlog::level::warn, "[MSDFFontGenerator] 指定文字をFontから取得できません");
			return false;
		}
		// 従来の辺の着色方式を維持する
		for (GlyphGeometry& glyph : glyphs) {
			glyph.edgeColoring(&msdfgen::edgeColoringInkTrap, kMaxCornerAngle, 0);
		}

		// 配置できなかった文字を含むAtlasを公開しない
		TightAtlasPacker packer;
		packer.setDimensions(kAtlasWidth, kAtlasHeight);
		packer.setSpacing(0);
		packer.setScale(kFontSize);
		packer.setPixelRange(msdfgen::Range(kPixelRange));
		packer.setUnitRange(msdfgen::Range(0.0));
		packer.setMiterLimit(kMiterLimit);
		packer.setOriginPixelAlignment(false, false);
		if (packer.pack(glyphs.data(), static_cast<int>(glyphs.size())) != 0) {
			Logger::Output(LogType::Engine, spdlog::level::warn, "[MSDFFontGenerator] 指定文字をAtlasへ配置できません");
			return false;
		}
		int width = 0, height = 0;
		packer.getDimensions(width, height);

		// 前処理なしの重なりと走査線の補正を維持する
		ImmediateAtlasGenerator<float, 3, msdfGenerator, BitmapAtlasStorage<msdfgen::byte, 3>> generator(width, height);
		GeneratorAttributes attributes;
		attributes.config.overlapSupport = true;
		attributes.scanlinePass = true;
		generator.setAttributes(attributes);
		generator.setThreadCount(kThreadCount);
		generator.generate(glyphs.data(), static_cast<int>(glyphs.size()));

		// 上原点の画像と対応する文字配置を揃える
		msdfgen::BitmapConstSection<msdfgen::byte, 3> bitmap = generator.atlasStorage();
		bitmap.reorient(msdfgen::Y_DOWNWARD);
		Result next;
		next.document = MSDFAtlasDocument::Build(fontGeometry, packer);
		if (!EncodeAtlasPNG(bitmap, next.atlasPNG)) {
			return false;
		}
		out = std::move(next);
		return true;
	}

#else

	bool Generate(const std::filesystem::path&, const std::filesystem::path&, Result&) {

		Logger::Output(LogType::Engine, spdlog::level::warn, "[MSDFFontGenerator] msdf-atlas-genが未統合です");
		return false;
	}

#endif
}
