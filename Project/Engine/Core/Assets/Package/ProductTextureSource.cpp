#include "ProductAssetCook.h"

//============================================================================
//	include
//============================================================================
#include "ProductBinary.h"
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>

// directX
#include <dxgiformat.h>
// c++
#include <algorithm>
#include <array>
#include <fstream>
#include <memory>

// 製品形式がCPUやOSの画像コーデックに依存しないよう整数デコーダーを固定する
#define STB_IMAGE_STATIC
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_NO_SIMD
#define STBI_NO_STDIO
#define STBI_NO_LINEAR
#define STBI_MAX_DIMENSIONS 16384
#define STB_IMAGE_IMPLEMENTATION
#pragma warning(push, 0)
#include <Externals/assimp/contrib/stb/stb_image.h>
#pragma warning(pop)

namespace {

	using namespace Engine;
	using Bytes = std::vector<uint8_t>;
	constexpr size_t kDDSHeaderSize = 148;
	constexpr uint32_t kVersion = 1;

	uint32_t ReadWord(std::span<const uint8_t> bytes, size_t offset) {

		uint32_t value;
		std::memcpy(&value, bytes.data() + offset, sizeof(value));
		return value;
	}

	template<class T>
	bool PredictPixels(std::span<const uint8_t> source, uint32_t width, uint32_t height, uint32_t levels,
		size_t channels, bool bgra, size_t expectedSize, Bytes& pixels) {

		int x = 0, y = 0, components = 0;
		const auto length = static_cast<int>(source.size());
		if (!stbi_info_from_memory(source.data(), length, &x, &y, &components) ||
			x != static_cast<int>(width) || y != static_cast<int>(height)) return false;

		T* decoded = nullptr;
		if constexpr (sizeof(T) == 1) decoded = stbi_load_from_memory(source.data(), length, &x, &y, &components, 4);
		else decoded = stbi_load_16_from_memory(source.data(), length, &x, &y, &components, 4);
		const std::unique_ptr<T, decltype(&stbi_image_free)> image(decoded, stbi_image_free);
		if (!image) return false;

		const size_t stride = channels * sizeof(T);
		const size_t baseSize = static_cast<size_t>(width) * height * stride;
		if (baseSize > expectedSize) return false;
		pixels.resize(expectedSize);
		size_t end = baseSize;
		for (size_t i = 0; i < baseSize / stride; ++i) {
			const T* rgba = decoded + i * 4;
			if (bgra) {
				const T swapped[4] = {rgba[2], rgba[1], rgba[0], rgba[3]};
				std::memcpy(pixels.data() + i * stride, swapped, stride);
			} else {
				std::memcpy(pixels.data() + i * stride, rgba, stride);
			}
		}
		size_t begin = 0;
		for (uint32_t mip = 1; mip < levels; ++mip) {
			if (width == 1 && height == 1) return false;
			const uint32_t w = (std::max)(1u, width / 2), h = (std::max)(1u, height / 2);
			const size_t mipSize = static_cast<size_t>(w) * h * stride;
			if (mipSize > expectedSize - end) return false;
			for (uint32_t row = 0; row < h; ++row) {
				for (uint32_t col = 0; col < w; ++col) {
					const size_t top = begin + (static_cast<size_t>(row * 2) * width + col * 2) * stride;
					const size_t bottom = top + (height > 1 ? width * stride : 0);
					const size_t next = width > 1 ? stride : 0;
					for (size_t c = 0; c < channels; ++c) {
						uint32_t sum = 0;
						for (const auto offset : {top, top + next, bottom, bottom + next}) {
							T value;
							std::memcpy(&value, pixels.data() + offset + c * sizeof(T), sizeof(T));
							sum += value;
						}
						const T average = static_cast<T>((sum + 2) / 4);
						std::memcpy(pixels.data() + end + (static_cast<size_t>(row) * w + col) * stride + c * sizeof(T),
							&average, sizeof(T));
					}
				}
			}
			begin = end;
			end += mipSize;
			width = w;
			height = h;
		}
		return end == expectedSize;
	}

	bool Predict(std::span<const uint8_t> header, std::span<const uint8_t> source, size_t size, Bytes& pixels) {

		if (header.size() != kDDSHeaderSize || std::memcmp(header.data(), "DDS ", 4) ||
			std::memcmp(header.data() + 84, "DX10", 4) || ReadWord(header, 132) != 3 ||
			ReadWord(header, 136) != 0 || ReadWord(header, 140) != 1 || source.empty() || source.size() > INT_MAX) return false;
		const uint32_t w = ReadWord(header, 16), h = ReadWord(header, 12), levels = (std::max)(1u, ReadWord(header, 28));
		if (!w || !h || w > 16384 || h > 16384 || levels > 15) return false;
		switch (ReadWord(header, 128)) {
		case DXGI_FORMAT_R8_UNORM: return PredictPixels<uint8_t>(source, w, h, levels, 1, false, size, pixels);
		case DXGI_FORMAT_R16_UNORM: return PredictPixels<uint16_t>(source, w, h, levels, 1, false, size, pixels);
		case DXGI_FORMAT_R8G8B8A8_UNORM:
		case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB: return PredictPixels<uint8_t>(source, w, h, levels, 4, false, size, pixels);
		case DXGI_FORMAT_B8G8R8A8_UNORM:
		case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB: return PredictPixels<uint8_t>(source, w, h, levels, 4, true, size, pixels);
		case DXGI_FORMAT_R16G16B16A16_UNORM: return PredictPixels<uint16_t>(source, w, h, levels, 4, false, size, pixels);
		default: return false;
		}
	}
}

bool Engine::ProductAssetCook::TryTextureSource(const std::filesystem::path& source, const AssetMeta& meta,
	const std::vector<uint8_t>& cooked, size_t limit, std::vector<uint8_t>& payload) {

	if (meta.type != AssetType::Texture || cooked.size() <= kDDSHeaderSize || limit <= kDDSHeaderSize + 8) return false;
	const auto extension = Algorithm::ToLower(Algorithm::PathToUTF8(source.extension()));
	if (extension != ".jpg" && extension != ".jpeg" && extension != ".png") return false;
	std::ifstream file(source, std::ios::binary | std::ios::ate);
	const std::streamoff length = file.tellg();
	if (length <= 0 || length > INT_MAX || static_cast<size_t>(length) >= limit - kDDSHeaderSize - 8) return false;
	Bytes original(static_cast<size_t>(length));
	file.seekg(0);
	if (!file.read(reinterpret_cast<char*>(original.data()), length)) return false;

	const auto header = std::span(cooked).first(kDDSHeaderSize);
	Bytes correction;
	if (!Predict(header, original, cooked.size() - header.size(), correction)) return false;
	bool differs = false;
	for (size_t i = 0; i < correction.size(); ++i) {
		correction[i] = static_cast<uint8_t>(cooked[i + header.size()] - correction[i]);
		differs |= correction[i] != 0;
	}
	Bytes packed;
	if (differs) {
		packed = ProductBinary::CompressSmaller(correction);
		if (packed.empty()) return false;
	}
	if (header.size() + 8 + original.size() + packed.size() >= limit) return false;
	Bytes candidate;
	ProductBinary::Writer writer(candidate);
	writer.Copy(&kVersion, sizeof(kVersion));
	writer.Copy(header.data(), header.size());
	ProductBinary::Array(writer, original);
	writer.Copy(packed.data(), packed.size());

	Bytes restored;
	if (!RestoreTextureSource(candidate, cooked.size(), restored) || restored != cooked) return false;
	payload.swap(candidate);
	return true;
}

bool Engine::ProductAssetCook::RestoreTextureSource(const std::vector<uint8_t>& payload, size_t expectedSize,
	std::vector<uint8_t>& cooked) {

	const auto bytes = std::span(payload);
	if (bytes.size() < kDDSHeaderSize + 8 || expectedSize <= kDDSHeaderSize || ReadWord(bytes, 0) != kVersion) return false;
	const auto header = bytes.subspan(4, kDDSHeaderSize);
	const auto sourceLength = ReadWord(bytes, kDDSHeaderSize + 4);
	const auto body = bytes.subspan(kDDSHeaderSize + 8);
	if (sourceLength > body.size()) return false;
	Bytes pixels;
	if (!Predict(header, body.first(sourceLength), expectedSize - kDDSHeaderSize, pixels)) return false;
	const auto packed = body.subspan(sourceLength);
	if (!packed.empty()) {
		Bytes correction;
		if (!ProductBinary::Decompress(Bytes(packed.begin(), packed.end()), pixels.size(), correction)) return false;
		for (size_t i = 0; i < pixels.size(); ++i) pixels[i] += correction[i];
	}
	Bytes result;
	result.reserve(expectedSize);
	result.insert(result.end(), header.begin(), header.end());
	result.insert(result.end(), pixels.begin(), pixels.end());
	cooked.swap(result);
	return true;
}
