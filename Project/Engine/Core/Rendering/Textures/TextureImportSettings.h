#pragma once

//============================================================================
//	include
//============================================================================
// c++
#include <cstdint>
// directX
#include <d3d12.h>
// json
#include <json.hpp>

namespace Engine {

	inline constexpr uint32_t kTextureImporterVersion = 2;

	//============================================================================
	//	TextureImportSettings structures
	//============================================================================
	// テクスチャ用途のプリセット
	enum class TextureImportPreset : uint8_t {

		Default,
		Color,
		NormalMap,
		Data,
		PixelArt,
		UI,
		HDR,
	};
	// テクスチャの色空間
	enum class TextureColorSpace : uint8_t {

		Auto,
		SRGB,
		Linear,
	};
	// テクスチャのフィルタ方式
	enum class TextureFilterMode : uint8_t {

		Point,
		Bilinear,
		Trilinear,
		Anisotropic,
	};
	// テクスチャ座標の範囲外参照方式
	enum class TextureAddressMode : uint8_t {

		Wrap,
		Clamp,
		Mirror,
	};
	// 法線マップのY成分規約
	enum class TextureNormalConvention : uint8_t {

		OpenGL,
		DirectX,
	};
	// エディタプレビューで表示するチャンネル
	enum class TexturePreviewChannel : uint8_t {

		Color,
		Red,
		Green,
		Blue,
		Alpha,
		Normal,
	};
	// .metaに保存するテクスチャ取り込み設定
	struct TextureImportSettings {

		TextureImportPreset preset = TextureImportPreset::Default;
		TextureColorSpace colorSpace = TextureColorSpace::Auto;
		TextureFilterMode filter = TextureFilterMode::Trilinear;
		TextureAddressMode addressU = TextureAddressMode::Wrap;
		TextureAddressMode addressV = TextureAddressMode::Wrap;
		bool generateMipmaps = true;
		uint32_t maxAnisotropy = 8;
		TextureNormalConvention normalConvention = TextureNormalConvention::OpenGL;
		bool alphaColorBleed = true;

		bool operator==(const TextureImportSettings&) const noexcept = default;
	};

	// プリセットの推奨設定を作成する
	TextureImportSettings MakeTextureImportSettings(TextureImportPreset preset);
	// JSONから設定を復元し欠落値をプリセット既定値で補う
	TextureImportSettings ParseTextureImportSettings(const nlohmann::json& data);
	// 設定をJSONへ変換する
	nlohmann::json ToJson(const TextureImportSettings& settings);
	// 明示設定と描画側要求から最終色空間を決定する
	TextureColorSpace ResolveTextureColorSpace(const TextureImportSettings& settings,
		TextureColorSpace requestedColorSpace);
	// テクスチャキャッシュ用の安定ハッシュを生成する
	uint64_t HashTextureImportSettings(const TextureImportSettings& settings,
		TextureColorSpace requestedColorSpace = TextureColorSpace::Auto);
	// D3D12のサンプラーフィルタへ変換する
	D3D12_FILTER ToD3D12Filter(const TextureImportSettings& settings);
	// D3D12のアドレス方式へ変換する
	D3D12_TEXTURE_ADDRESS_MODE ToD3D12AddressMode(TextureAddressMode mode);
} // Engine
