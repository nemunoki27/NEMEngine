#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/AssetTypes.h>
#include <Engine/Core/Rendering/Meshes/GPUResource/MeshShaderSharedTypes.h>
#include <unordered_map>

namespace Engine {

	class GraphicsCore;
	class AssetDatabase;
	class MaterialParameterSet;
	struct MaterialAsset;

	//============================================================================
	//	RaytracingMaterialResolver class
	//	反射用のMaterialとTextureを解決する
	//============================================================================
	class RaytracingMaterialResolver {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		// 標準Materialを反射用データへ変換する
		MeshSubMeshShaderData BuildPrimitiveSubMeshData(GraphicsCore& graphicsCore,
			AssetDatabase& assetDatabase, const MaterialAsset& material,
			const MaterialParameterSet* materialInstance, const Matrix4x4& uvMatrix);
		// TextureをDescriptorへ解決する
		uint32_t ResolveTextureDescriptorIndex(GraphicsCore& graphicsCore,
			AssetDatabase& assetDatabase, AssetID textureAssetID, bool sRGB);
		// 解決cacheを破棄する
		void Clear();
		void ResetPending() { hasPendingTextureDescriptors_ = false; }
		bool HasPendingTextures() const { return hasPendingTextureDescriptors_; }
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		std::unordered_map<AssetID, uint32_t> textureDescriptorIndexCache_{};
		std::unordered_map<AssetID, uint32_t> sRGBTextureDescriptorIndexCache_{};
		// 非同期読込中は静的シーンのマテリアルバッファを次フレームも再構築する
		bool hasPendingTextureDescriptors_ = false;
	};
}
