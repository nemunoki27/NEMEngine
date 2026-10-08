#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Meshes/GPUResource/MeshResourceTypes.h>
#include <Engine/Core/Rendering/Meshes/Import/MeshImportSettings.h>

// c++
#include <array>
#include <filesystem>

namespace Engine::MeshFileImporter {

	// モデルから頂点・骨・LODと描画用データを構築する
	ImportedMeshAsset ImportFile(AssetID assetID, const std::filesystem::path& fullPath, const MeshImportSettings& settings,
		const std::array<std::filesystem::path, 3>& manualLODPaths, bool buildGPUData = true);
}
