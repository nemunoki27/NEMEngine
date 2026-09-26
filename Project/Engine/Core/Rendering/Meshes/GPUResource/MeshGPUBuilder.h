#pragma once

//============================================================================
//	include
//============================================================================
#include "MeshResourceTypes.h"
#include <Engine/Core/Rendering/Meshes/MeshImportService.h>

namespace Engine {

	class AssetDatabase;
	class BufferUploadService;
	class SRVDescriptor;

	namespace MeshGPUBuilder {
		// Import結果をGPU資源へ変換して転送を提出する
		MeshGPUResource Create(const ImportedMeshAsset& imported, AssetDatabase* assetDatabase,
			ID3D12Device* device, BufferUploadService* uploadService, SRVDescriptor* srvDescriptor);
		// Meshが所有するSRVを解放する
		void Release(MeshGPUResource& mesh);
	}
}
