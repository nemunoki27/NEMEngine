#pragma once

//============================================================================
//	include
//============================================================================
#include <DirectXTex.h>
#include <filesystem>
#include <string>

namespace Engine::EXRTextureDecoder {

	// 画像の指定がある場合だけ画素を展開する
	HRESULT Read(const std::filesystem::path& path, const std::string* snapshot, DirectX::TexMetadata& metadata,
		DirectX::ScratchImage* image, const char*& diagnostic);
}
