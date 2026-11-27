#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Renderer/Backends/Common/DefaultStructuredInstanceBuffer.h>
#include <Engine/Core/Rendering/Core/GraphicsFrameResourceArray.h>
#include <Engine/Core/Rendering/Materials/PaintMaterialAsset.h>
#include <Engine/Core/Rendering/Materials/MaterialPaintAtlas.h>
#include <Engine/Core/Rendering/Textures/RuntimeTextureResolver.h>
#include <Engine/Core/World/ECS/Entity/Entity.h>

// c++
#include <array>
#include <unordered_map>

namespace Engine {

	class RenderAssetLibrary;
	class RenderBufferRegistry;
	class ECSWorld;
	struct SystemContext;

	// Inspectorで対象の表面全体に適用する編集中の素材
	struct PaintMaterialSample {
		Entity subject = Entity::Null();
		const PaintMaterialAsset* material = nullptr;
	};

	// 描画とレイトレーシングで共有するペイント入力
	class MaterialPaintScene {
	public:
		void Prepare(GraphicsCore& graphics, ECSWorld* world, const AssetDatabase* database, RenderAssetLibrary& assets,
			PaintMaterialSample sample = {}, const SystemContext* system = nullptr);
		uint64_t Publish(RenderBufferRegistry& registry) const;
	private:
		using Word = std::array<uint32_t, 4>;
		struct CachedMaterial {
			PaintMaterialAsset asset{};
			uint64_t revision = UINT64_MAX;
			uint64_t libraryRevision = UINT64_MAX;
			bool valid = false;
			bool surface = false;
		};
		struct AtlasRange {
			Word range{};
			uint32_t subMeshCount = 0;
		};

		StructuredInstanceBuffer<Word> sceneBuffer_{"gMaterialPaintScene"};
		DefaultStructuredInstanceBuffer<Word> geometryBuffer_{"gMaterialPaintAtlas"};
		std::vector<Word> geometry_;
		std::vector<AssetID> atlasIDs_;
		std::unordered_map<AssetID, AtlasRange> ranges_;
		std::unordered_map<AssetID, CachedMaterial> materials_;
		std::vector<std::shared_ptr<const MaterialPaintAtlas>> atlases_;
		GraphicsFrameResourceArray<std::vector<TextureOwnership>> owners_{};
		uint64_t ownershipFrame_ = UINT64_MAX;
		uint64_t materialRevision_ = 0;
		const AssetDatabase* database_ = nullptr;
		bool initialized_ = false;
	};
}

