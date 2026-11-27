#pragma once

#include <Engine/Core/Assets/AssetTypes.h>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace Engine {

	struct MaterialPaintAtlas;
	class ECSWorld;
	struct Entity;
	bool EncodeMaterialPaintMask(uint32_t resolution, const std::vector<uint8_t>& rgba, std::string& png);
	AssetID QueueMaterialPaintMask(AssetDatabase& database, uint32_t resolution,
		std::vector<uint8_t>&& rgba, std::shared_ptr<const MaterialPaintAtlas> atlas);
	bool FindMaterialPaintMaskPixels(const AssetDatabase& database, AssetID mask, uint32_t resolution,
		std::vector<uint8_t>& rgba, bool forStroke);
	bool PublishMaterialPaintMasks(AssetDatabase& database, bool wait);
	bool PublishMaterialPaintMasks(AssetDatabase& database, const ECSWorld& world, std::span<const Entity> objects);
	bool MaterialPaintSaveQueueFull();
	bool LoadMaterialPaintMask(const AssetDatabase& database, AssetID mask, uint32_t resolution,
		std::vector<uint8_t>& rgba, bool* preparing = nullptr);
	AssetID SaveMaterialPaintMask(AssetDatabase& database, uint32_t resolution, const std::vector<uint8_t>& rgba);
}
