#pragma once

//============================================================================
//	include
//============================================================================
#include "BakedLightingTypes.h"
#include <Engine/Core/Rendering/Meshes/Import/MeshImportSettings.h>
#include <Engine/Core/Rendering/Assets/MaterialAsset.h>
#include <Engine/Core/World/Components/Rendering/MeshRendererComponent.h>

// c++
#include <atomic>
#include <memory>

namespace Engine {

	struct BakedLightingMeshInput {

		AssetID mesh{};
		std::filesystem::path path;
		MeshImportSettings importSettings;
		Matrix4x4 worldMatrix{};
		uint32_t receiverIndex = 0;
		std::vector<SubMeshMaterial> subMeshes;
		std::vector<uint32_t> materials;
	};
	struct BakedLightingMaterialInput {

		MaterialAsset material;
		MaterialInstanceParameters instance;
		Matrix4x4 uvMatrix = Matrix4x4::Identity();
		MaterialSurfaceMode surfaceMode = MaterialSurfaceMode::Opaque;
		float alphaCutoff = 0.5f;
	};
	struct BakedLightingPreparation {

		BakedLightingData data;
		std::vector<BakedLightingMeshInput> meshes;
		std::vector<BakedLightingMaterialInput> materials;
		std::vector<BakedLightingTexel> texels;
		std::string diagnostic;
	};

	//============================================================================
	//	BakedLightingGeometry namespace
	//	元モデルを変更せずベイク用UVと探索構造を作る
	//============================================================================
	namespace BakedLightingGeometry {

		// CPU作業を中止要求と進捗へ接続する
		void Prepare(BakedLightingPreparation& output, const BakedLightingSettings& settings,
			const std::atomic<bool>& cancelled, std::atomic<float>& progress);
		// 三角形をワールド座標から探索するための階層を作る
		void BuildHierarchy(BakedLightingData& data);
		// 同じChartの中だけ平滑化して余白を埋める
		void FilterLightmaps(BakedLightingData& data, const std::vector<BakedLightingTexel>& texels,
			const std::atomic<bool>& cancelled);
	}
}
