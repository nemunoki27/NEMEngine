#pragma once

#include <DirectXTex.h>

// c++
#include <string>

namespace Engine::SeamlessTextureImage {

	// 画像の色成分とアルファをRGBAへ展開する
	DirectX::ScratchImage Load(const std::string& assetPath, uint32_t previewMaxDimension);
	// 縦横の境界を指定幅でなじませる
	DirectX::ScratchImage Blend(const DirectX::Image& source, int widthPercent);
}
