#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Graphics/Mesh/GPUResource/MeshResourceTypes.h>
#include <Engine/Core/Graphics/Mesh/GPUResource/MeshShaderSharedTypes.h> 

// directX
#include <d3d12.h> 

namespace Engine {

	//============================================================================
	//	RaytracingStructures structures
	//============================================================================

	// BLAS構築入力
	struct RaytracingBLASInput {

		const MeshGPUResource* meshResource = nullptr;

		// サブメッシュ単位BLAS
		uint32_t subMeshIndex = 0;
		uint32_t indexOffset = 0;
		uint32_t indexCount = 0;

		bool allowUpdate = false;

		// BLAS構築時に頂点データを上書きするか
		D3D12_GPU_VIRTUAL_ADDRESS overrideVertexAddress = 0;
		uint32_t overrideVertexCount = 0;
	};
	// TLASインスタンス
	struct RaytracingTLASInstance {

		Matrix4x4 worldMatrix = Matrix4x4::Identity();
		ID3D12Resource* blas = nullptr;

		uint32_t instanceID = 0;
		uint32_t hitGroupIndex = 0;
		uint8_t mask = 0xFF;
		D3D12_RAYTRACING_INSTANCE_FLAGS flags = D3D12_RAYTRACING_INSTANCE_FLAG_NONE;
	};
	// インスタンス情報
	struct RaytracingInstanceShaderData {

		// 頂点/インデックス
		uint32_t vertexDescriptorIndex = 0;
		uint32_t indexDescriptorIndex = 0;
		// スキニングメッシュを使う時の頂点先頭オフセット
		uint32_t vertexOffset = 0;
		// このTLASインスタンスが参照するサブメッシュデータ
		uint32_t subMeshDataIndex = 0;

		// インデックスバッファのどこからこのサブメッシュが始まるか
		uint32_t indexOffset = 0;
		uint32_t _pad[3] = { 0,0,0 };
	};
	// GPUへ渡すシーン共通データ
	struct RaytracingScene {

		float rayMin = 0.001f;
		float rayMax = 10000.0f;
		uint32_t instanceCount = 0;
		uint32_t _pad = 0;
	};
} // Engine