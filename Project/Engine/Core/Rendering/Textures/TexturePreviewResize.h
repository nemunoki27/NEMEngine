#pragma once

//============================================================================
//	include
//============================================================================
#include <DirectXTex.h>
#include <cstdint>

namespace Engine {

	// Preview画像だけを縮小する
	bool ResizeTexturePreview(DirectX::ScratchImage& image, uint32_t maxDimension);
}
