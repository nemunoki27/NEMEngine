#include "ViewportImageFile.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Serialization/StorageFileUtility.h>
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/Renderer/Views/ViewportRenderService.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>
#include <Engine/Editor/Assets/Project/ProjectAssetPath.h>
#include <Engine/Editor/Utility/EditorImageFile.h>

// c++
#include <format>
#include <stdexcept>
// DirectXTex
#include <Externals/DirectXTex/DirectXTex.h>

void Engine::ViewportImageFile::Capture(GraphicsCore& graphicsCore, const RenderTexture2D& texture, DirectX::ScratchImage& captured) {

	const auto state = texture.GetCurrentState();
	if (FAILED(DirectX::CaptureTexture(graphicsCore.GetDXObject().GetCommandQueue()->GetQueue(),
		texture.GetResource(), false, captured, state, state))) {
		throw std::runtime_error("Viewの画像取得に失敗しました");
	}
}

std::string Engine::ViewportImageFile::EncodePNG(const DirectX::Image& source, bool displayColor) {

	const auto format = displayColor ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : DXGI_FORMAT_R8G8B8A8_UNORM;
	const auto* image = &source;
	DirectX::ScratchImage converted;
	if (source.format != format) {
		if (FAILED(DirectX::Convert(source, format, DirectX::TEX_FILTER_DEFAULT, DirectX::TEX_THRESHOLD_DEFAULT, converted))) {
			throw std::runtime_error("Viewの画像変換に失敗しました");
		}
		image = converted.GetImage(0, 0, 0);
	}
	return EditorImageFile::EncodePNG(*image);
}

std::filesystem::path Engine::ViewportImageFile::SaveScreenshot(GraphicsCore& graphicsCore,
	RenderViewKind kind, const RenderTexture2D& texture) {

	DirectX::ScratchImage captured;
	Capture(graphicsCore, texture, captured);
	const std::string png = EncodePNG(*captured.GetImage(0, 0, 0), true);
	SYSTEMTIME time{};
	GetLocalTime(&time);
	const auto filename = std::format("{}_{:04}{:02}{:02}_{:02}{:02}{:02}_{:03}.png",
		ViewportRenderService::GetViewAlias(kind), time.wYear, time.wMonth, time.wDay,
		time.wHour, time.wMinute, time.wSecond, time.wMilliseconds);
	const auto directory = RuntimePaths::GetGameAssetsRoot() / "Screenshots";
	std::filesystem::create_directories(directory);
	const auto path = ProjectAssetPath::MakeUniquePath(directory / filename);
	StorageFileUtility::FileIdentity identity;
	if (path.empty() || !StorageFileUtility::WriteBytesWithoutReplacement(path, png, identity)) {
		throw std::runtime_error("スクリーンショットを書き込めません");
	}
	return path;
}
