#pragma once

//============================================================================
//	include
//============================================================================
#include <filesystem>
#include <string>

namespace DirectX {
	struct Image;
	class ScratchImage;
}

namespace Engine {
	class GraphicsCore;
	class RenderTexture2D;
	enum class RenderViewKind;
}

namespace Engine::ViewportImageFile {

	// 描画の提出後にViewのテクスチャを読み戻す
	void Capture(GraphicsCore& graphicsCore, const RenderTexture2D& texture, DirectX::ScratchImage& captured);
	// 表示色または中間バッファの値をPNGへ変換する
	std::string EncodePNG(const DirectX::Image& image, bool displayColor);
	// 連続撮影でも既存の画像を上書きしない
	std::filesystem::path SaveScreenshot(GraphicsCore& graphicsCore, RenderViewKind kind, const RenderTexture2D& texture);
}
