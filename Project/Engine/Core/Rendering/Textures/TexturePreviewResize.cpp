#include "TexturePreviewResize.h"

//============================================================================
//	include
//============================================================================
#include <algorithm>

bool Engine::ResizeTexturePreview(DirectX::ScratchImage& image, uint32_t maxDimension) {

	const auto& metadata = image.GetMetadata();
	if (maxDimension == 0 || metadata.dimension != DirectX::TEX_DIMENSION_TEXTURE2D || metadata.arraySize != 1 ||
		(metadata.width <= maxDimension && metadata.height <= maxDimension)) {
		return true;
	}
	// 縦横比を保ち、圧縮画像はworker上で展開する
	const float scale = static_cast<float>(maxDimension) / static_cast<float>((std::max)(metadata.width, metadata.height));
	const size_t width = (std::max)(size_t{ 1 }, static_cast<size_t>(metadata.width * scale));
	const size_t height = (std::max)(size_t{ 1 }, static_cast<size_t>(metadata.height * scale));
	const DirectX::Image* source = image.GetImage(0, 0, 0);
	DirectX::ScratchImage expanded;
	if (DirectX::IsCompressed(metadata.format)) {
		if (FAILED(DirectX::Decompress(*source, DXGI_FORMAT_UNKNOWN, expanded))) { return false; }
		source = expanded.GetImage(0, 0, 0);
	}
	DirectX::ScratchImage resized;
	if (FAILED(DirectX::Resize(*source, width, height, DirectX::TEX_FILTER_DEFAULT, resized))) { return false; }
	image = std::move(resized);
	return true;
}
