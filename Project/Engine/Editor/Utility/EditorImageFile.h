#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Utility/ScopedCleanup.h>
#include <Externals/DirectXTex/DirectXTex.h>

// c++
#include <stdexcept>
#include <string>
// WIC
#include <wincodec.h>

namespace Engine::EditorImageFile {

	// アルファを保持してPNGへ変換する
	inline std::string EncodePNG(const DirectX::Image& source) {

		const HRESULT initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
		if (FAILED(initialized) && initialized != RPC_E_CHANGED_MODE) throw std::runtime_error("画像出力を初期化できません");
		ScopedCleanup cleanup([initialized]() noexcept { if (SUCCEEDED(initialized)) CoUninitialize(); });
		DirectX::Blob bytes;
		if (FAILED(DirectX::SaveToWICMemory(source, DirectX::WIC_FLAGS_NONE,
			DirectX::GetWICCodec(DirectX::WIC_CODEC_PNG), bytes, &GUID_WICPixelFormat32bppBGRA))) {
			throw std::runtime_error("PNGへ変換できません");
		}
		return { static_cast<const char*>(bytes.GetBufferPointer()), bytes.GetBufferSize() };
	}
}
