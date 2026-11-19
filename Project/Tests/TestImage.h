#pragma once

//============================================================================
//	include
//============================================================================
#include <filesystem>

namespace DirectX { struct Image; }

namespace NEMTests {

	// 検証用画像をPNGへ保存する
	bool SaveTestImage(const DirectX::Image& image, const std::filesystem::path& path);
}
