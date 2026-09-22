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
		bool reload = false;
		HRESULT result = E_FAIL;
		const char* failureStage = "Decode";
	};

	namespace TextureDecoder {
		// 要求に従って画像を読み込み加工する
		DecodedTexture Decode(const TextureFileRequestDesc& job);
	}
}
