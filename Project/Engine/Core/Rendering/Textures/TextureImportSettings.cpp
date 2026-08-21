#include "TextureImportSettings.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>

// c++
#include <algorithm>
#include <string>

//============================================================================
//	TextureImportSettings internal
//============================================================================
namespace {

	template<typename T>
	T ParseEnum(const nlohmann::json& data, const char* key, T fallback) {

		const auto found = data.find(key);
		if (found == data.end() || !found->is_string()) {
			return fallback;
		}
		return Engine::EnumAdapter<T>::FromString(found->get<std::string>()).value_or(fallback);
	}

	void HashMix(uint64_t& hash, uint64_t value) {

		hash ^= value;
		hash *= 1099511628211ull;
	}
}

//============================================================================
//	TextureImportSettings functions
//============================================================================
Engine::TextureImportSettings Engine::MakeTextureImportSettings(TextureImportPreset preset) {

	TextureImportSettings settings{};
	settings.preset = preset;
	switch (preset) {
	case TextureImportPreset::Color:
		settings.colorSpace = TextureColorSpace::SRGB;
		settings.filter = TextureFilterMode::Anisotropic;
		break;
	case TextureImportPreset::NormalMap:
		settings.colorSpace = TextureColorSpace::Linear;
		settings.filter = TextureFilterMode::Anisotropic;
		settings.alphaColorBleed = false;
		break;
	case TextureImportPreset::Data:
		settings.colorSpace = TextureColorSpace::Linear;
		settings.filter = TextureFilterMode::Anisotropic;
		settings.alphaColorBleed = false;
		break;
	case TextureImportPreset::PixelArt:
		settings.colorSpace = TextureColorSpace::SRGB;
		settings.filter = TextureFilterMode::Point;
		settings.addressU = TextureAddressMode::Clamp;
		settings.addressV = TextureAddressMode::Clamp;
		settings.generateMipmaps = false;
		settings.maxAnisotropy = 1;
		settings.alphaColorBleed = false;
		break;
	case TextureImportPreset::UI:
		settings.colorSpace = TextureColorSpace::SRGB;
		settings.filter = TextureFilterMode::Bilinear;
		settings.addressU = TextureAddressMode::Clamp;
		settings.addressV = TextureAddressMode::Clamp;
		settings.generateMipmaps = false;
		settings.maxAnisotropy = 1;
		break;
	case TextureImportPreset::HDR:
		settings.colorSpace = TextureColorSpace::Linear;
		settings.filter = TextureFilterMode::Anisotropic;
		settings.alphaColorBleed = false;
		break;
	case TextureImportPreset::Default:
	default:
		break;
	}
	return settings;
}

Engine::TextureImportSettings Engine::ParseTextureImportSettings(const nlohmann::json& data) {

	if (!data.is_object()) {
		return MakeTextureImportSettings(TextureImportPreset::Default);
	}

	const TextureImportPreset preset = ParseEnum(
		data, "preset", TextureImportPreset::Default);
	TextureImportSettings settings = MakeTextureImportSettings(preset);
	settings.colorSpace = ParseEnum(data, "colorSpace", settings.colorSpace);
	settings.filter = ParseEnum(data, "filter", settings.filter);
	settings.addressU = ParseEnum(data, "addressU", settings.addressU);
	settings.addressV = ParseEnum(data, "addressV", settings.addressV);
	settings.generateMipmaps = data.value("generateMipmaps", settings.generateMipmaps);
	settings.maxAnisotropy = static_cast<uint32_t>((std::clamp)(
		data.value("maxAnisotropy", static_cast<int32_t>(settings.maxAnisotropy)), 1, 16));
	settings.normalConvention = ParseEnum(
		data, "normalConvention", settings.normalConvention);
	settings.alphaColorBleed = data.value("alphaColorBleed", settings.alphaColorBleed);
	return settings;
}

nlohmann::json Engine::ToJson(const TextureImportSettings& settings) {

	return {
		{ "preset", EnumAdapter<TextureImportPreset>::ToString(settings.preset) },
		{ "colorSpace", EnumAdapter<TextureColorSpace>::ToString(settings.colorSpace) },
		{ "filter", EnumAdapter<TextureFilterMode>::ToString(settings.filter) },
		{ "addressU", EnumAdapter<TextureAddressMode>::ToString(settings.addressU) },
		{ "addressV", EnumAdapter<TextureAddressMode>::ToString(settings.addressV) },
		{ "generateMipmaps", settings.generateMipmaps },
		{ "maxAnisotropy", settings.maxAnisotropy },
		{ "normalConvention", EnumAdapter<TextureNormalConvention>::ToString(settings.normalConvention) },
		{ "alphaColorBleed", settings.alphaColorBleed },
	};
}

Engine::TextureColorSpace Engine::ResolveTextureColorSpace(
	const TextureImportSettings& settings, TextureColorSpace requestedColorSpace) {

	if (settings.colorSpace != TextureColorSpace::Auto) {
		return settings.colorSpace;
	}
	if (requestedColorSpace != TextureColorSpace::Auto) {
		return requestedColorSpace;
	}
	switch (settings.preset) {
	case TextureImportPreset::Color:
	case TextureImportPreset::PixelArt:
	case TextureImportPreset::UI:
		return TextureColorSpace::SRGB;
	case TextureImportPreset::NormalMap:
	case TextureImportPreset::Data:
	case TextureImportPreset::HDR:
		return TextureColorSpace::Linear;
	case TextureImportPreset::Default:
	default:
		return TextureColorSpace::Auto;
	}
}

uint64_t Engine::HashTextureImportSettings(
	const TextureImportSettings& settings, TextureColorSpace requestedColorSpace) {

	uint64_t hash = 1469598103934665603ull;
	HashMix(hash, static_cast<uint64_t>(settings.preset));
	HashMix(hash, static_cast<uint64_t>(settings.colorSpace));
	HashMix(hash, static_cast<uint64_t>(settings.filter));
	HashMix(hash, static_cast<uint64_t>(settings.addressU));
	HashMix(hash, static_cast<uint64_t>(settings.addressV));
	HashMix(hash, settings.generateMipmaps ? 1ull : 0ull);
	HashMix(hash, settings.maxAnisotropy);
	HashMix(hash, static_cast<uint64_t>(settings.normalConvention));
	HashMix(hash, settings.alphaColorBleed ? 1ull : 0ull);
	HashMix(hash, static_cast<uint64_t>(ResolveTextureColorSpace(settings, requestedColorSpace)));
	return hash;
}

D3D12_FILTER Engine::ToD3D12Filter(const TextureImportSettings& settings) {

	switch (settings.filter) {
	case TextureFilterMode::Point:
		return D3D12_FILTER_MIN_MAG_MIP_POINT;
	case TextureFilterMode::Bilinear:
		return D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT;
	case TextureFilterMode::Anisotropic:
		return D3D12_FILTER_ANISOTROPIC;
	case TextureFilterMode::Trilinear:
	default:
		return D3D12_FILTER_MIN_MAG_MIP_LINEAR;
	}
}

D3D12_TEXTURE_ADDRESS_MODE Engine::ToD3D12AddressMode(TextureAddressMode mode) {

	switch (mode) {
	case TextureAddressMode::Clamp:
		return D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	case TextureAddressMode::Mirror:
		return D3D12_TEXTURE_ADDRESS_MODE_MIRROR;
	case TextureAddressMode::Wrap:
	default:
		return D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	}
}
