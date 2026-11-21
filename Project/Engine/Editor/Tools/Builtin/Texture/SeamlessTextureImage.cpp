#include "SeamlessTextureImage.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Textures/TextureDecoder.h>

// c++
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace {

	using Pixel = std::array<double, 4>;

	Pixel ReadPremultiplied(const uint8_t* p) {

		const double alpha = p[3] / 255.0;
		return { p[0] * alpha, p[1] * alpha, p[2] * alpha, static_cast<double>(p[3]) };
	}

	void ApplyOffset(uint8_t* p, const Pixel& offset, double scale) {

		const Pixel original = ReadPremultiplied(p);
		const double alpha = std::clamp(original[3] + offset[3] * scale, 0.0, 255.0);
		p[3] = static_cast<uint8_t>(std::lround(alpha));
		for (size_t c = 0; c < 3; ++c) {
			const double value = original[c] + offset[c] * scale;
			p[c] = p[3] ? static_cast<uint8_t>(std::lround(std::clamp(value * 255.0 / alpha, 0.0, 255.0))) : 0;
		}
	}

	void BlendEdges(const DirectX::Image& image, size_t length, size_t lines, size_t along, size_t across, int percent) {

		const size_t width = (std::min)(length / 2, (length * percent + 99) / 100);
		if (!width) return;
		std::vector<Pixel> sums(lines + 1);
		bool matched = true;
		for (size_t i = 0; i < lines; ++i) {
			const auto* first = image.pixels + i * across;
			const auto* last = first + (length - 1) * along;
			matched &= std::memcmp(first, last, 4) == 0;
			const Pixel a = ReadPremultiplied(first);
			const Pixel b = ReadPremultiplied(last);
			for (size_t c = 0; c < 4; ++c) sums[i + 1][c] = sums[i][c] + a[c] - b[c];
		}
		if (matched) return;

		for (size_t d = 0; d < width; ++d) {
			const double t = 1.0 - static_cast<double>(d) / width;
			const double weight = 0.5 * t * t * (3.0 - 2.0 * t);
			// 境界から離れるほど低周波の色差だけを補い、各画素のノイズを残す
			const size_t radius = (std::min)(lines - 1, 2 * d + 2);
			for (size_t i = 0; i < lines; ++i) {
				const size_t begin = i > radius ? i - radius : 0;
				const size_t end = (std::min)(lines, i + radius + 1);
				Pixel offset;
				for (size_t c = 0; c < 4; ++c) offset[c] = (sums[end][c] - sums[begin][c]) / (end - begin);
				auto* first = image.pixels + i * across + d * along;
				auto* last = image.pixels + i * across + (length - 1 - d) * along;
				if (first[3] == 0 && last[3] == 0) {
					if (d == 0) std::memcpy(last, first, 4);
					continue;
				}
				ApplyOffset(first, offset, -weight);
				if (d == 0) std::memcpy(last, first, 4);
				else ApplyOffset(last, offset, weight);
			}
		}
	}
}

namespace Engine::SeamlessTextureImage {

	DirectX::ScratchImage Load(const std::string& assetPath, uint32_t previewMaxDimension) {

		TextureFileRequestDesc request{};
		request.assetPath = assetPath;
		request.importSettings.generateMipmaps = false;
		request.importSettings.alphaColorBleed = false;
		request.previewMaxDimension = previewMaxDimension;
		request.requestedColorSpace = TextureColorSpace::Linear;
		auto decoded = TextureDecoder::Decode(request);
		if (!decoded.success || decoded.metadata.dimension != DirectX::TEX_DIMENSION_TEXTURE2D || decoded.metadata.arraySize != 1)
			throw std::runtime_error("2Dテクスチャを読み込めません");

		auto* input = decoded.image.GetImage(0, 0, 0);
		const auto format = DXGI_FORMAT_R8G8B8A8_UNORM;
		if (input->format == format) return std::move(decoded.image);
		DirectX::ScratchImage rgba;
		const HRESULT hr = DirectX::IsCompressed(input->format) ? DirectX::Decompress(*input, format, rgba) :
			DirectX::Convert(*input, format, DirectX::TEX_FILTER_DEFAULT, DirectX::TEX_THRESHOLD_DEFAULT, rgba);
		if (FAILED(hr)) throw std::runtime_error("画像をRGBAに変換できません");
		return rgba;
	}

	DirectX::ScratchImage Blend(const DirectX::Image& source, int widthPercent) {

		DirectX::ScratchImage result;
		if (source.format != DXGI_FORMAT_R8G8B8A8_UNORM || !source.pixels || source.rowPitch < source.width * 4 ||
			widthPercent < 1 || 50 < widthPercent ||
			FAILED(result.Initialize2D(source.format, source.width, source.height, 1, 1)))
			throw std::runtime_error("シームレス画像を作成できません");

		const auto& target = *result.GetImage(0, 0, 0);
		for (size_t y = 0; y < source.height; ++y)
			std::memcpy(target.pixels + y * target.rowPitch, source.pixels + y * source.rowPitch, source.width * 4);
		BlendEdges(target, target.width, target.height, 4, target.rowPitch, widthPercent);
		BlendEdges(target, target.height, target.width, target.rowPitch, 4, widthPercent);
		// 二方向目の補正後も四隅と左右端を一致させる
		for (size_t y = 0; target.width > 1 && y < target.height; ++y) {
			auto* row = target.pixels + y * target.rowPitch;
			std::memcpy(row + (target.width - 1) * 4, row, 4);
		}
		return result;
	}

}
