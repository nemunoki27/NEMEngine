#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Graphics/Render/Backend/Common/BackendDrawCommon.h>
#include <Engine/Core/Graphics/Render/Backend/Builtin/Mesh/MeshRenderBackendTypes.h>
#include <Engine/Core/Graphics/Pipeline/Bind/GraphicsRootBinder.h>

//============================================================================
//	MeshDrawPathCommon namespace
//	メッシュ描画パスで共通の処理
//============================================================================
namespace Engine::MeshDrawPathCommon {

	// バッチ内で使用するメッシュアセットIDを解決
	AssetID ResolveBatchMesh(const RenderSceneBatch& batch, std::span<const RenderItem* const> items);

	// サブメッシュのテクスチャアセットIDを解決
	AssetID ResolveSubMeshBaseColorTextureAssetID(const MeshGPUResource& gpuMesh,
		const MeshRendererComponent* renderer, uint32_t subMeshIndex);
} // Engine