#pragma once

//============================================================================
//	include
//============================================================================
#include "MeshResourceTypes.h"

namespace Engine {

	//============================================================================
	//	MeshUploadPreparation class
	//	GPU転送前のCPU配列を構築する
	//============================================================================
	class MeshUploadPreparation {
	public:
		MeshUploadPreparation() = delete;
		~MeshUploadPreparation() = delete;

		// 描画用配列と境界球を作る
		static MeshUploadData Build(const ImportedMeshAsset& imported);
	};
} // Engine
