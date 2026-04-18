#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Graphics/Mesh/GPUResource/MeshResourceTypes.h>

// meshoptimizer
#include <meshoptimizer.h>

namespace Engine {

	//============================================================================
	//	MeshletBuilder class
	//	メッシュレットを構築するクラス
	//============================================================================
	class MeshletBuilder {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		MeshletBuilder() = default;
		~MeshletBuilder() = default;

		void Build(ImportedMeshAsset& mesh) const;
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		// メッシュレットあたりの最大頂点数と最大プリミティブ数
		static constexpr size_t kMaxMeshletVertices = 64;
		static constexpr size_t kMaxMeshletPrimitives = 124;

		//--------- functions ----------------------------------------------------

		// 3つの頂点インデックスを1つの32ビット整数にパックする
		static uint32_t PackPrimitive(uint32_t i0, uint32_t i1, uint32_t i2);
		// サブメッシュの頂点とインデックスをもとにメッシュレットを構築する
		void BuildSubMeshMeshlets(ImportedMeshAsset& mesh, uint32_t subMeshIndex) const;
	};
} // Engine