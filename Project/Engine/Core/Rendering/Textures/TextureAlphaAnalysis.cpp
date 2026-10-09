#include "TextureAlphaAnalysis.h"

//============================================================================
//	include
//============================================================================
#include "TextureDecoder.h"
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>

//============================================================================
//	TextureAlphaAnalysis classMethods
//============================================================================
Engine::TextureAlphaContent Engine::TextureAlphaAnalysis::Analyze(const std::filesystem::path& path) {

	if (path.empty()) return TextureAlphaContent::Opaque;
	if (const auto found = cache_.find(path); found != cache_.end()) return found->second;
	// 読込時のAlphaを加工せず基底画像だけを検査
	TextureFileRequestDesc request{};
	request.assetPath = Algorithm::PathToUTF8(path);
	request.importSettings.generateMipmaps = false;
	request.importSettings.alphaColorBleed = false;
	auto decoded = TextureDecoder::Decode(request);
	if (!decoded.success) return TextureAlphaContent::Opaque;
	TextureAlphaContent result = TextureAlphaContent::Opaque;
	const DirectX::Image* image = decoded.image.GetImage(0, 0, 0);
	if (DirectX::HasAlpha(image->format)) {

		DirectX::ScratchImage expanded;
		if (DirectX::IsCompressed(image->format)) {
			if (FAILED(DirectX::Decompress(*image, DXGI_FORMAT_R32G32B32A32_FLOAT, expanded))) return result;
			image = expanded.GetImage(0, 0, 0);
		}
		size_t transparent = 0;
		size_t partial = 0;
		const HRESULT status = DirectX::EvaluateImage(*image, [&](const DirectX::XMVECTOR* pixels, size_t width, size_t) {
			for (size_t x = 0; x < width; ++x) {
				const float alpha = DirectX::XMVectorGetW(pixels[x]);
				if (alpha < 0.98f) ++transparent;
				if (alpha > 0.02f && alpha < 0.98f) ++partial;
			}
		});
		if (FAILED(status)) return result;
		// 切り抜き境界の補間だけで半透明へ切り替えない
		if (transparent > 0) result = partial > transparent / 2 ? TextureAlphaContent::Transparent : TextureAlphaContent::Masked;
	}
	cache_.emplace(path, result);
	return result;
}

void Engine::TextureAlphaAnalysis::Clear() {

	cache_.clear();
}
