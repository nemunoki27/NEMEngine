#include "RectLightTests.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Components/Lighting/RectLightComponent.h>
#include <Engine/Core/Rendering/Renderer/Lighting/GPU/LightGPUConversion.h>
#include <Engine/Core/Rendering/Textures/TextureDecoder.h>
#include <Engine/Core/Assets/BuiltinAssetIDs.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>
#include <Engine/Core/Foundation/Serialization/ContentHash.h>

// c++
#include <array>
#include <cstddef>
#include <limits>

bool NEMTests::TestRectLightCPU() {

	using namespace Engine;
	static_assert(sizeof(RectLightGPU) == 112);
	static_assert(sizeof(LightCountsGPU) == 32);
	static_assert(offsetof(RectLightGPU, sourceWidth) == 60);
	static_assert(offsetof(RectLightGPU, sourceHeight) == 76);
	static_assert(offsetof(LightCountsGPU, rectLTCMatrixIndex) == 20);
	static_assert(offsetof(LightCountsGPU, rectLTCAmplitudeIndex) == 24);

	// 廃止した減衰は再保存されず、総出力と形状は維持する
	RectLightComponent light;
	light.intensity = 32.0f;
	light.sourceWidth = 4.0f;
	const nlohmann::json saved = light;
	const auto loaded = saved.get<RectLightComponent>();
	if (saved.contains("decay") || loaded.intensity != light.intensity || loaded.sourceWidth != light.sourceWidth) return false;
	RectLightItem item;
	item.sourceWidth = -1.0f;
	item.sourceHeight = std::numeric_limits<float>::quiet_NaN();
	item.intensity = std::numeric_limits<float>::infinity();
	item.shadowStrength = 2.0f;
	const auto gpu = LightGPUConversion::ToGPU(item);
	if (gpu.sourceWidth != 0.0f || gpu.sourceHeight != 0.0f || gpu.intensity != 0.0f || gpu.shadowStrength != 1.0f) return false;

	// 参照テーブルの版と画像形式を照合する
	const std::array names{"ltcMatrix.dds", "ltcAmplitude.dds"};
	const std::array hashes{
		"6aa389c6c0403a408cf6bb23dcdc543ad3e97f0fdc49e60f034b6a85682ee18d",
		"2ff4f03b10d73e25985216d73a58aa57375f0c775dee0e1bde075d44e2b07200"};
	for (size_t index = 0; index < names.size(); ++index) {

		TextureFileRequestDesc request{};
		request.assetPath = std::string("engine://Textures/Lighting/LTC/") + names[index];
		request.importSettings = MakeTextureImportSettings(TextureImportPreset::Data);
		request.importSettings.generateMipmaps = false;
		request.importSettings.alphaColorBleed = false;
		const auto decoded = TextureDecoder::Decode(request);
		if (!decoded.success || decoded.metadata.width != 64 || decoded.metadata.height != 64 ||
			decoded.metadata.mipLevels != 1 || decoded.metadata.format != DXGI_FORMAT_R16G16B16A16_FLOAT ||
			ContentHash::FileSHA256(RuntimePaths::ResolveAssetPath(request.assetPath)) != hashes[index]) return false;
	}
	return true;
}
