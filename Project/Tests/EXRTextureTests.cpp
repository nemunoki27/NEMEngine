#include "EditorAssetWorkflowTests.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/Package/ProductAssetCook.h>
#include <Engine/Core/Foundation/Utility/Algorithm/PathUtility.h>
#include <Engine/Core/Rendering/Textures/TextureDecoder.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>

// c++
#include <array>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iterator>

namespace {

	bool CheckPixel(const DirectX::Image& image, size_t x, size_t y, const std::array<float, 4>& expected) {

		DirectX::ScratchImage converted;
		const DirectX::Image* pixels = &image;
		if (pixels->format != DXGI_FORMAT_R32G32B32A32_FLOAT) {
			if (FAILED(DirectX::Convert(image, DXGI_FORMAT_R32G32B32A32_FLOAT, DirectX::TEX_FILTER_DEFAULT,
				DirectX::TEX_THRESHOLD_DEFAULT, converted))) return false;
			pixels = converted.GetImage(0, 0, 0);
		}
		const auto* row = reinterpret_cast<const float*>(pixels->pixels + y * pixels->rowPitch) + x * 4;
		for (size_t i = 0; i < expected.size(); ++i) {
			if (!(std::abs(row[i] - expected[i]) < 0.002f)) return false;
		}
		return true;
	}

	bool CheckTexture(const std::filesystem::path& path, bool luminance) {

		using namespace Engine;
		TextureFileRequestDesc request{};
		request.assetPath = Algorithm::PathToUTF8(path);
		request.importSettings = MakeTextureImportSettings(luminance ? TextureImportPreset::Data : TextureImportPreset::HDR);
		DirectX::TexMetadata metadata{};
		if (FAILED(TextureDecoder::ReadMetadata(request.assetPath, metadata)) || metadata.width != 33 || metadata.height != 17) return false;
		const auto file = TextureDecoder::Decode(request);
		if (!file.success || file.metadata.mipLevels != 6) return false;
		const std::array<float, 4> gray{0.5f, 0.5f, 0.5f, 1.0f};
		if (!CheckPixel(*file.image.GetImage(0, 0, 0), 0, 0, luminance ? gray : std::array{4.0f, -0.125f, 0.5f, 0.25f}) ||
			!CheckPixel(*file.image.GetImage(0, 0, 0), 32, 16, luminance ? gray : std::array{8.0f, -0.25f, 0.75f, 0.5f})) return false;

		std::ifstream input(path, std::ios::binary);
		request.snapshotBytes = std::make_shared<const std::string>(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
		request.assetPath = "GameAssets/MissingSource.exr";
		const auto memory = TextureDecoder::Decode(request);
		if (!memory.success || file.image.GetPixelsSize() != memory.image.GetPixelsSize() ||
			std::memcmp(file.image.GetPixels(), memory.image.GetPixels(), file.image.GetPixelsSize()) != 0) return false;

		AssetMeta meta{};
		meta.type = AssetType::Texture;
		meta.importerSettings = ToJson(request.importSettings);
		std::vector<uint8_t> bytes;
		std::string error;
		if (!ProductAssetCook::Convert(path, meta, bytes, error)) return false;
		DirectX::ScratchImage cooked;
		if (FAILED(DirectX::LoadFromDDSMemory(bytes.data(), bytes.size(), DirectX::DDS_FLAGS_NONE, &metadata, cooked)) ||
			metadata.format != file.metadata.format || metadata.mipLevels != file.metadata.mipLevels ||
			cooked.GetPixelsSize() != file.image.GetPixelsSize() ||
			std::memcmp(cooked.GetPixels(), file.image.GetPixels(), file.image.GetPixelsSize()) != 0) return false;

		request.snapshotBytes = std::make_shared<const std::string>(request.snapshotBytes->substr(0, 64));
		return !TextureDecoder::Decode(request).success;
	}
}

bool NEMTests::TestEXRTextures() {

	const auto root = Engine::RuntimePaths::GetEngineProjectRoot() / "Tests/Fixtures/Textures";
	return CheckTexture(root / "Luminance.exr", true) && CheckTexture(root / "TiledHDR.exr", false);
}

