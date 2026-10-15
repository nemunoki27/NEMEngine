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
		// 法線と接線に共通の圧縮形式を使う
		static uint32_t PackDirection(const Vector3& direction);
	};
} // Engine
