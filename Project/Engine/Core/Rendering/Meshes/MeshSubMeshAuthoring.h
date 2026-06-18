#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Components/Rendering/MeshRendererComponent.h>
#include <Engine/Core/Rendering/Meshes/GPUResource/MeshResourceTypes.h>

// c++
#include <vector>
#include <string>
#include <filesystem>
// assimp
#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

namespace Engine {

	// front
	class AssetDatabase;

	//============================================================================
	//	MeshSubMeshLayoutItem structures
	//============================================================================
	// メッシュアセットから読み取った、正規のSubMesh並び
	struct MeshSubMeshLayoutItem {

		uint32_t sourceSubMeshIndex = 0;
		std::string name{};

		// 頂点座標から計算したピボット
		Vector3 sourcePivot = Vector3::AnyInit(0.0f);

		// モデルファイルから読み取ったデフォルトテクスチャ
		ImportedMeshTextureAssetSet defaultTextureAssets{};

		// モデルファイルから読み取ったマテリアル係数、has付きで設定済みのものだけ再適用する
		Color4 baseColorFactor = Color4::White();
		Color4 emissiveFactor = Color4(0.0f, 0.0f, 0.0f, 1.0f);
		float metallicFactor = 0.0f;
		float roughnessFactor = 1.0f;
		bool hasBaseColorFactor = false;
		bool hasEmissiveFactor = false;
		bool hasMetallicFactor = false;
		bool hasRoughnessFactor = false;
	};
} // Engine
namespace Engine::MeshSubMeshAuthoring {

	// メッシュアセットからサブメッシュレイアウトを読む
	bool TryBuildLayout(AssetDatabase* assetDatabase, AssetID meshAssetID,
		std::vector<MeshSubMeshLayoutItem>& outLayout);

	// レイアウトに合わせてサブメッシュを正規化する
	bool SyncComponentToLayout(const std::vector<MeshSubMeshLayoutItem>& layout,
		MeshRendererComponent& renderer, bool preserveOverrides);

	// データベースから直接レイアウトを読んで正規化する
	bool SyncComponent(AssetDatabase* assetDatabase,
		MeshRendererComponent& renderer, bool preserveOverrides);

	// モデルのマテリアル係数とテクスチャをparameterOverridesへ再適用する、reload用に上書きする
	void ApplyModelMaterialParameters(const std::vector<MeshSubMeshLayoutItem>& layout,
		MeshRendererComponent& renderer);

	// IDから現在のサブメッシュインデックスを解決する
	int32_t FindSubMeshIndexByStableID(const MeshRendererComponent& renderer, UUID stableID);
} // Engine
