#pragma once

#include <Engine/Core/Assets/AssetTypes.h>
#include <Engine/Core/Foundation/Math/Color.h>
#include <filesystem>

namespace Engine {

	struct MaterialAsset;

	// ブラシで重ねるPBR素材
	struct PaintMaterialAsset {

		Color4 baseColor = Color4::White();
		Color4 emissiveColor{};
		float emissiveIntensity = 0.0f;
		AssetID emissiveTexture{}, metallicRoughnessTexture{};
		float normalStrength = 1.0f;
		float roughness = 0.5f;
		float metallic = 0.0f;
		float occlusion = 1.0f;
		float height = 0.5f;
		float tiling = 1.0f;
		uint32_t channels = 63;
		AssetID baseColorTexture{};
		AssetID normalTexture{};
		AssetID roughnessTexture{};
		AssetID metallicTexture{};
		AssetID occlusionTexture{};
		AssetID heightTexture{};

		bool operator==(const PaintMaterialAsset&) const = default;
	};

	bool IsPaintSettingParameter(std::string_view name);
	PaintMaterialAsset MakePaintMaterialSample(const MaterialAsset& material);
	MaterialAsset ConvertLegacyPaintMaterial(std::string_view name, const PaintMaterialAsset& paint);
	nlohmann::json ToJson(const PaintMaterialAsset& material);
	bool LoadPaintMaterialAsset(const std::filesystem::path& path, PaintMaterialAsset& material);
}
