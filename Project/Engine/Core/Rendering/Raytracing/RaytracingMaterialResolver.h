#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/AssetTypes.h>
#include <Engine/Core/Rendering/Meshes/GPUResource/MeshShaderSharedTypes.h>

// c++
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
		// 現在FrameのTexture待ちを解除
		void ResetPending() { hasPendingTextureDescriptors_ = false; }

		//--------- accessor -----------------------------------------------------

		// 読込待ちのTextureが残っているか
		bool HasPendingTextures() const { return hasPendingTextureDescriptors_; }
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		std::unordered_map<AssetID, uint32_t> textureDescriptorIndexCache_{};
		std::unordered_map<AssetID, uint32_t> sRGBTextureDescriptorIndexCache_{};
		// 読込中は次のFrameでもMaterial値を再解決
		bool hasPendingTextureDescriptors_ = false;
	};
}
