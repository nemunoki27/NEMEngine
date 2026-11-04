#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Meshes/GPUResource/MeshResourceTypes.h>

// c++
#include <filesystem>

namespace Engine::MeshFileImporter {

	// モデルから頂点・骨と描画用データを構築する
	ImportedMeshAsset ImportFile(AssetID assetID, const std::filesystem::path& fullPath, bool buildGPUData = true);
}
