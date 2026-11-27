#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Assets/MaterialAsset.h>

namespace Engine {
	class AssetDatabase;
	class RenderAssetLibrary;
	struct MeshSubMeshRenderState;
	enum class DefaultMaterialSlot : uint8_t;
}

namespace Engine::MaterialRenderPolicy {

	// 出力内容を保証できる標準のSurface Shaderか
	bool IsStandardSurface(const MaterialAsset& material);
	// Alpha判定なしで遮蔽を確定できるか
	bool IsOpaqueGeometry(const MaterialAsset* material, MaterialSurfaceMode mode);
	std::string GetMaterialPaintUnsupportedReason(const MaterialAsset& material);
	// 描画とブラシで同じ下地マテリアルを判定する
	const MaterialAsset* ResolveMaterialPaintSurface(const AssetDatabase& database, RenderAssetLibrary& assets,
		const MeshSubMeshRenderState& state, DefaultMaterialSlot slot);
	bool IsMaterialPaintSurface(const AssetDatabase& database, RenderAssetLibrary& assets,
		const MeshSubMeshRenderState& state, DefaultMaterialSlot slot);
	// 既定Materialの切替をcacheへ伝える
	uint64_t GetDefaultMaterialHash();
}
