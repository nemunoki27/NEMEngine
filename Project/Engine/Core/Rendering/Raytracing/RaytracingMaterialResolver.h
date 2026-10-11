#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/AssetTypes.h>
#include <Engine/Core/Rendering/Meshes/GPUResource/MeshShaderSharedTypes.h>
#include <Engine/Core/Rendering/Textures/GPUTextureResource.h>

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
			AssetDatabase& assetDatabase, AssetID textureAssetID, bool sRGB, bool normalMap = false);
		// 解決cacheを破棄する
		void Clear();
		// 現在FrameのTexture待ちを解除
		void ResetPending();
		// 公開したSceneで使用しないTextureの所有を解除する
		void CommitTextures();

		//--------- accessor -----------------------------------------------------

		// 読込待ちのTextureが残っているか
		bool HasPendingTextures() const { return hasPendingTextureDescriptors_; }
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- structure ----------------------------------------------------

		struct CachedTextureDescriptor {

			uint32_t index = UINT32_MAX;
			TextureOwnership ownership;
			bool used = false;
		};

		//--------- variables ----------------------------------------------------

		std::unordered_map<AssetID, CachedTextureDescriptor> textureDescriptorIndexCache_{};
		std::unordered_map<AssetID, CachedTextureDescriptor> sRGBTextureDescriptorIndexCache_{};
		// 法線用途の変換済みTextureを区別する
		std::unordered_map<AssetID, CachedTextureDescriptor> normalTextureDescriptorIndexCache_{};
		// 読込中は次のFrameでもMaterial値を再解決
		bool hasPendingTextureDescriptors_ = false;
	};
}
