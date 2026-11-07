#pragma once

//============================================================================
//	include
//============================================================================
#include "TextureFileRequest.h"

// directX
#include <DirectXTex.h>

namespace Engine {

	// CPUで準備した画像と転送要求
	struct DecodedTexture {

		std::string key;
		bool isSolidColor = false;
		uint8_t solidRGBA[4]{};
		DirectX::ScratchImage image;
		DirectX::TexMetadata metadata{};
		bool success = false;
		HRESULT result = E_FAIL;
		const char* failureStage = "Decode";
	};

	namespace TextureDecoder {
		// GPU転送や画像展開をせず元画像の情報を取得する
		HRESULT ReadMetadata(const std::string& assetPath, DirectX::TexMetadata& metadata);
		// 要求に従って画像を読み込み加工する
		DecodedTexture Decode(const TextureFileRequestDesc& job);
	}
}
