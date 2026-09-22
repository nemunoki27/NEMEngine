#include "TextureDecoder.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>

#include <algorithm>
#include <deque>
#include <vector>
#include <DirectXMath.h>

namespace {

	// 読み込み要求に応じたWICフラグを取得する
	DirectX::WIC_FLAGS ResolveWICFlags(Engine::TextureColorSpace colorSpace) {

		switch (colorSpace) {
		case Engine::TextureColorSpace::SRGB:
			return DirectX::WIC_FLAGS_FORCE_SRGB;
		case Engine::TextureColorSpace::Linear:
			return DirectX::WIC_FLAGS_IGNORE_SRGB;
		case Engine::TextureColorSpace::Auto:
		default:
			return DirectX::WIC_FLAGS_NONE;
		}
	}

	// DDSやTGAのフォーマットへ明示色空間を反映する
	void OverrideColorSpace(DirectX::ScratchImage& image,
		Engine::TextureColorSpace colorSpace) {

		if (colorSpace == Engine::TextureColorSpace::Auto) {
			return;
		}
		const DXGI_FORMAT source = image.GetMetadata().format;
		const DXGI_FORMAT target = colorSpace == Engine::TextureColorSpace::SRGB ?
			DirectX::MakeSRGB(source) : DirectX::MakeLinear(source);
		if (target != DXGI_FORMAT_UNKNOWN && target != source) {
			image.OverrideFormat(target);
		}
	}

	// 近傍の不透明色を透明ピクセルへ伝播して線形補間時の色滲みを防ぐ
	bool BleedTransparentPixels(DirectX::ScratchImage& image) {

		const DirectX::TexMetadata& metadata = image.GetMetadata();
		if (metadata.dimension != DirectX::TEX_DIMENSION_TEXTURE2D ||
			metadata.arraySize != 1 || metadata.mipLevels != 1) {
			return false;
		}

		const bool sRGB = DirectX::IsSRGB(metadata.format);
		DirectX::ScratchImage converted{};
		const DXGI_FORMAT format = sRGB ?
			DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : DXGI_FORMAT_R8G8B8A8_UNORM;
		DirectX::ScratchImage* workingImage = &image;
		if (metadata.format != format) {

			const HRESULT hr = DirectX::Convert(
				image.GetImages(), image.GetImageCount(), metadata,
				format, DirectX::TEX_FILTER_DEFAULT,
				DirectX::TEX_THRESHOLD_DEFAULT, converted);
			if (FAILED(hr)) {
				return false;
			}
			workingImage = &converted;
		}

		const DirectX::Image* base = workingImage->GetImage(0, 0, 0);
		if (!base || base->width == 0 || base->height == 0) {
			return false;
		}

		const size_t pixelCount = base->width * base->height;
		std::vector<int32_t> source(pixelCount, -1);
		std::deque<size_t> pending{};
		for (size_t y = 0; y < base->height; ++y) {

			const uint8_t* row = base->pixels + y * base->rowPitch;
			for (size_t x = 0; x < base->width; ++x) {

				const size_t index = y * base->width + x;
				if (row[x * 4 + 3] != 0) {
					source[index] = static_cast<int32_t>(index);
					pending.emplace_back(index);
				}
			}
		}
		if (pending.empty() || pending.size() == pixelCount) {
			if (workingImage == &converted) {
				image = std::move(converted);
			}
			return true;
		}

		constexpr int32_t kOffsetX[4]{ -1, 1, 0, 0 };
		constexpr int32_t kOffsetY[4]{ 0, 0, -1, 1 };
		while (!pending.empty()) {

			const size_t index = pending.front();
			pending.pop_front();
			const int32_t x = static_cast<int32_t>(index % base->width);
			const int32_t y = static_cast<int32_t>(index / base->width);
			for (uint32_t direction = 0; direction < 4; ++direction) {

				const int32_t nextX = x + kOffsetX[direction];
				const int32_t nextY = y + kOffsetY[direction];
				if (nextX < 0 || nextY < 0 ||
					nextX >= static_cast<int32_t>(base->width) ||
					nextY >= static_cast<int32_t>(base->height)) {
					continue;
				}

				const size_t next = static_cast<size_t>(nextY) * base->width +
					static_cast<size_t>(nextX);
				if (source[next] >= 0) {
					continue;
				}
				source[next] = source[index];
				pending.emplace_back(next);
			}
		}

		for (size_t y = 0; y < base->height; ++y) {

			uint8_t* row = base->pixels + y * base->rowPitch;
			for (size_t x = 0; x < base->width; ++x) {

				uint8_t* pixel = row + x * 4;
				if (pixel[3] != 0) {
					continue;
				}
				const size_t index = y * base->width + x;
				const size_t colorIndex = static_cast<size_t>(source[index]);
				const size_t colorX = colorIndex % base->width;
				const size_t colorY = colorIndex / base->width;
				const uint8_t* color = base->pixels + colorY * base->rowPitch + colorX * 4;
				pixel[0] = color[0];
				pixel[1] = color[1];
				pixel[2] = color[2];
			}
		}
		if (workingImage == &converted) {
			image = std::move(converted);
		}
		return true;
	}

	// 法線を正規化し必要ならOpenGLのY成分をDirectX規約へ変換する
	bool NormalizeNormalMap(DirectX::ScratchImage& image, bool invertGreen) {

		const DirectX::TexMetadata& metadata = image.GetMetadata();
		DirectX::ScratchImage normalized{};
		const HRESULT hr = DirectX::TransformImage(
			image.GetImages(), image.GetImageCount(), metadata,
			[invertGreen](DirectX::XMVECTOR* output, const DirectX::XMVECTOR* input,
				size_t width, size_t) {

				const DirectX::XMVECTOR one = DirectX::XMVectorReplicate(1.0f);
				const DirectX::XMVECTOR half = DirectX::XMVectorReplicate(0.5f);
				for (size_t x = 0; x < width; ++x) {

					DirectX::XMVECTOR normal = DirectX::XMVectorSubtract(
						DirectX::XMVectorScale(input[x], 2.0f), one);
					if (invertGreen) {
						normal = DirectX::XMVectorSetY(normal,
							-DirectX::XMVectorGetY(normal));
					}
					normal = DirectX::XMVector3Normalize(normal);
					DirectX::XMVECTOR encoded = DirectX::XMVectorMultiplyAdd(
						normal, half, half);
					output[x] = DirectX::XMVectorSetW(
						encoded, DirectX::XMVectorGetW(input[x]));
				}
			}, normalized);
		if (FAILED(hr)) {
			return false;
		}
		image = std::move(normalized);
		return true;
	}

	// 2D画像へフルMipチェーンを生成する
	bool GenerateMipChain(DirectX::ScratchImage& image) {

		const DirectX::TexMetadata& metadata = image.GetMetadata();
		if (metadata.dimension != DirectX::TEX_DIMENSION_TEXTURE2D ||
			metadata.mipLevels != 1 ||
			(metadata.width == 1 && metadata.height == 1)) {
			return false;
		}

		DirectX::ScratchImage mipChain{};
		DirectX::TEX_FILTER_FLAGS filter = DirectX::TEX_FILTER_DEFAULT;
		if (DirectX::IsSRGB(metadata.format)) {
			filter = static_cast<DirectX::TEX_FILTER_FLAGS>(
				filter | DirectX::TEX_FILTER_SRGB);
		}
		const HRESULT hr = DirectX::GenerateMipMaps(
			image.GetImages(), image.GetImageCount(), metadata,
			filter, 0, mipChain);
		if (FAILED(hr)) {
			return false;
		}
		image = std::move(mipChain);
		return true;
	}

	// インスペクター向けに指定チャンネルだけを可視化する
	bool ApplyPreviewChannel(DirectX::ScratchImage& image,
		Engine::TexturePreviewChannel channel) {

		if (channel == Engine::TexturePreviewChannel::Color) {
			return true;
		}

		DirectX::ScratchImage preview{};
		const HRESULT hr = DirectX::TransformImage(
			image.GetImages(), image.GetImageCount(), image.GetMetadata(),
			[channel](DirectX::XMVECTOR* output, const DirectX::XMVECTOR* input,
				size_t width, size_t) {

				for (size_t x = 0; x < width; ++x) {

					float value = 0.0f;
					switch (channel) {
					case Engine::TexturePreviewChannel::Red:
						value = DirectX::XMVectorGetX(input[x]);
						break;
					case Engine::TexturePreviewChannel::Green:
						value = DirectX::XMVectorGetY(input[x]);
						break;
					case Engine::TexturePreviewChannel::Blue:
						value = DirectX::XMVectorGetZ(input[x]);
						break;
					case Engine::TexturePreviewChannel::Alpha:
						value = DirectX::XMVectorGetW(input[x]);
						break;
					case Engine::TexturePreviewChannel::Normal:
						output[x] = DirectX::XMVectorSetW(input[x], 1.0f);
						continue;
					case Engine::TexturePreviewChannel::Color:
					default:
						output[x] = input[x];
						continue;
					}
					output[x] = DirectX::XMVectorSet(value, value, value, 1.0f);
				}
			}, preview);
		if (FAILED(hr)) {
			return false;
		}
		image = std::move(preview);
		return true;
	}

}

Engine::DecodedTexture Engine::TextureDecoder::Decode(const TextureFileRequestDesc& job) {

	DecodedTexture result{};
	result.key = job.key;
	result.reload = job.reload;

	// ファイルパスからテクスチャをデコードする
	const std::filesystem::path fullPath = RuntimePaths::ResolveAssetPath(job.assetPath);
	const std::string extension = Algorithm::ToLower(
		Algorithm::PathToUTF8(fullPath.extension()));
	const std::wstring fullPathW = fullPath.wstring();
	const TextureColorSpace colorSpace = job.overrideImportColorSpace &&
		job.requestedColorSpace != TextureColorSpace::Auto ?
		job.requestedColorSpace : ResolveTextureColorSpace(
			job.importSettings, job.requestedColorSpace);

	DirectX::ScratchImage loaded{};
	HRESULT hr = E_FAIL;
	const char* failureStage = "Decode";
	DirectX::TexMetadata loadedMeta{};
	if (extension == ".dds") {

		hr = DirectX::LoadFromDDSFile(fullPathW.c_str(), DirectX::DDS_FLAGS_NONE, &loadedMeta, loaded);
	} else if (extension == ".tga") {

		hr = DirectX::LoadFromTGAFile(fullPathW.c_str(), &loadedMeta, loaded);
	} else if (extension == ".hdr") {

		hr = DirectX::LoadFromHDRFile(fullPathW.c_str(), &loadedMeta, loaded);
	} else {

		hr = DirectX::LoadFromWICFile(fullPathW.c_str(), ResolveWICFlags(colorSpace),
			&loadedMeta, loaded);
	}
	if (SUCCEEDED(hr)) {

		OverrideColorSpace(loaded, colorSpace);
		bool processingSucceeded = true;
		const bool isNormalMap = job.importSettings.preset == TextureImportPreset::NormalMap;
		if (isNormalMap) {

			failureStage = "NormalConvert";
			if (loaded.GetMetadata().format != DXGI_FORMAT_R8G8B8A8_UNORM) {

				DirectX::ScratchImage linearImage{};
				hr = DirectX::Convert(loaded.GetImages(), loaded.GetImageCount(), loaded.GetMetadata(),
					DXGI_FORMAT_R8G8B8A8_UNORM, DirectX::TEX_FILTER_DEFAULT,
					DirectX::TEX_THRESHOLD_DEFAULT, linearImage);
				if (SUCCEEDED(hr)) {
					loaded = std::move(linearImage);
				} else {
					processingSucceeded = false;
				}
			}
			if (processingSucceeded) {

				failureStage = "NormalNormalize";
				processingSucceeded = NormalizeNormalMap(loaded,
					job.importSettings.normalConvention == TextureNormalConvention::OpenGL);
			}
		} else if (job.importSettings.alphaColorBleed &&
			(job.importSettings.preset == TextureImportPreset::Color ||
				job.importSettings.preset == TextureImportPreset::UI)) {

			BleedTransparentPixels(loaded);
		}

		const DirectX::TexMetadata& metadata = loaded.GetMetadata();
		const bool authoredDDSMips = extension == ".dds" && 1 < metadata.mipLevels;
		const bool needsMipChain = job.importSettings.generateMipmaps &&
			!authoredDDSMips && metadata.dimension == DirectX::TEX_DIMENSION_TEXTURE2D &&
			metadata.mipLevels == 1 && (1 < metadata.width || 1 < metadata.height);
		if (processingSucceeded && needsMipChain) {

			failureStage = "MipGeneration";
			processingSucceeded = GenerateMipChain(loaded);
		}
		if (processingSucceeded && isNormalMap) {

			// Mip補間後に各法線を再正規化する、Y反転は基底Mipで完了している
			failureStage = "NormalMipNormalize";
			processingSucceeded = NormalizeNormalMap(loaded, false);
		}
		if (processingSucceeded) {
			failureStage = "PreviewChannel";
			processingSucceeded = ApplyPreviewChannel(loaded, job.previewChannel);
		}

		if (processingSucceeded) {
			result.image = std::move(loaded);
			result.metadata = result.image.GetMetadata();
			result.success = true;
		} else {
			hr = E_FAIL;
		}
	}
	result.result = hr;
	result.failureStage = failureStage;
	return result;
}
