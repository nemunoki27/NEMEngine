#include "TestImage.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Utility/ScopedCleanup.h>
#include <DirectXTex.h>

bool NEMTests::SaveTestImage(const DirectX::Image& image, const std::filesystem::path& path) {

	DirectX::ScratchImage converted;
	const DirectX::Image* source = &image;
	if (image.format != DXGI_FORMAT_R8G8B8A8_UNORM) {
		if (FAILED(DirectX::Convert(image, DXGI_FORMAT_R8G8B8A8_UNORM, DirectX::TEX_FILTER_DEFAULT, 0.5f, converted))) return false;
		source = converted.GetImage(0, 0, 0);
	}
	std::filesystem::create_directories(path.parent_path());
	const HRESULT initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
	if (FAILED(initialized) && initialized != RPC_E_CHANGED_MODE) return false;
	const Engine::ScopedCleanup cleanup([=]() noexcept { if (SUCCEEDED(initialized)) CoUninitialize(); });
	return SUCCEEDED(DirectX::SaveToWICFile(*source, DirectX::WIC_FLAGS_NONE,
		DirectX::GetWICCodec(DirectX::WIC_CODEC_PNG), path.c_str()));
}
