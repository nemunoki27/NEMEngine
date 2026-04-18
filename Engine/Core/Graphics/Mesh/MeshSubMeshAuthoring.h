#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/ECS/Component/Builtin/Render/MeshRendererComponent.h>

// c++
#include <vector>
#include <string>
#include <filesystem>
// assimp
#include <Externals/assimp/include/assimp/Importer.hpp>
#include <Externals/assimp/include/assimp/postprocess.h>
#include <Externals/assimp/include/assimp/scene.h>

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
	};
} // Engine
namespace Engine::MeshSubMeshAuthoring {

	// メッシュアセットからサブメッシュレイアウトを読む
	bool TryBuildLayout(const AssetDatabase* assetDatabase, AssetID meshAssetID,
		std::vector<MeshSubMeshLayoutItem>& outLayout);

	// レイアウトに合わせてサブメッシュを正規化する
	bool SyncComponentToLayout(const std::vector<MeshSubMeshLayoutItem>& layout,
		MeshRendererComponent& renderer, bool preserveOverrides);

	// データベースから直接レイアウトを読んで正規化する
	bool SyncComponent(const AssetDatabase* assetDatabase,
		MeshRendererComponent& renderer, bool preserveOverrides);

	// IDから現在のサブメッシュインデックスを解決する
	int32_t FindSubMeshIndexByStableID(const MeshRendererComponent& renderer, UUID stableID);
} // Engine