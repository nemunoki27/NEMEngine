#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Renderer/Backends/Common/BackendDrawCommon.h>
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Mesh/MeshRenderBackendTypes.h>
#include <Engine/Core/Rendering/Pipelines/Bind/GraphicsRootBinder.h>

//============================================================================
//	MeshDrawPathCommon namespace
//	メッシュ描画パスで共通の処理
//============================================================================
namespace Engine::MeshDrawPathCommon {

	// バッチ内で使用するメッシュアセットIDを解決
	AssetID ResolveBatchMesh(const RenderSceneBatch& batch, std::span<const RenderItem* const> items);

	// サブメッシュのテクスチャアセットIDを解決 (種別ごと)
	AssetID ResolveSubMeshBaseColorTextureAssetID(const MeshGPUResource& gpuMesh,
		const MeshRendererComponent* renderer, uint32_t subMeshIndex);
	AssetID ResolveSubMeshNormalTextureAssetID(const MeshGPUResource& gpuMesh,
		const MeshRendererComponent* renderer, uint32_t subMeshIndex);
	AssetID ResolveSubMeshMetallicRoughnessTextureAssetID(const MeshGPUResource& gpuMesh,
		const MeshRendererComponent* renderer, uint32_t subMeshIndex);
	AssetID ResolveSubMeshEmissiveTextureAssetID(const MeshGPUResource& gpuMesh,
		const MeshRendererComponent* renderer, uint32_t subMeshIndex);
	AssetID ResolveSubMeshOcclusionTextureAssetID(const MeshGPUResource& gpuMesh,
		const MeshRendererComponent* renderer, uint32_t subMeshIndex);
	AssetID ResolveSubMeshSpecularTextureAssetID(const MeshGPUResource& gpuMesh,
		const MeshRendererComponent* renderer, uint32_t subMeshIndex);
} // Engine