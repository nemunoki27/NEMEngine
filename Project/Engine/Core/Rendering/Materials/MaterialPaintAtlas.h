#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/AssetTypes.h>
#include <Engine/Core/Foundation/Math/Vector2.h>
#include <Engine/Core/Foundation/Math/Vector3.h>

#include <array>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace Engine {

	class AssetDatabase;
	class ECSWorld;
	struct Entity;
	struct MaterialPaintComponent;
	struct PrimitiveRendererComponent;

	struct MaterialPaintTriangle {

		std::array<Vector3, 3> positions;
		std::array<Vector2, 3> uv;
		uint32_t subMesh = 0;
	};

	struct MaterialPaintAtlas {

		struct TriangleBatch {
			Vector3 min, max;
			uint32_t first = 0, count = 0;
		};
		std::vector<MaterialPaintTriangle> triangles;
		std::vector<uint32_t> meshletTriangles;
		uint64_t geometryHash = 0;
		std::vector<TriangleBatch> batches;
	};

	// 保存済みのUVと対応する面を取得する
	std::shared_ptr<const MaterialPaintAtlas> LoadMaterialPaintAtlas(const AssetDatabase& database, AssetID atlasID);

	// 取り込み後の形状と保存されたペイント領域の対応を確認する
	bool MaterialPaintGeometryMatches(const AssetDatabase& database, const ECSWorld& world,
		Entity entity, const MaterialPaintComponent& paint);

	// 編集対象の形状を照合し、初回のペイント時にUVを作成する
	std::shared_ptr<const MaterialPaintAtlas> ResolveMaterialPaintAtlas(AssetDatabase& database, AssetID mesh,
		const PrimitiveRendererComponent* primitive, AssetID& atlasID, std::string& error, bool* preparing = nullptr);

	// UVの外周を埋めて縮小時の色漏れを抑える
	void DilateMaterialPaintMask(const MaterialPaintAtlas& atlas, uint32_t resolution, std::span<uint8_t> rgbaPixels);
}
