#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Meshes/GPUResource/MeshResourceTypes.h>
#include <Engine/Core/Rendering/Meshes/GPUResource/MeshShaderSharedTypes.h> 

// directX
#include <d3d12.h> 

namespace Engine {

	//============================================================================
	//	RaytracingStructures structures
	//============================================================================
	// TLASインスタンスマスク、レイの用途ごとに当たるインスタンスを分ける
	static constexpr uint8_t kRaytracingMaskShadowCaster = 1u;
	static constexpr uint8_t kRaytracingMaskReflectionCaster = 1u << 1;
	// ピックなど常に当てたいレイ用、全インスタンスで必ず立てる
	static constexpr uint8_t kRaytracingMaskAlwaysHit = 1u << 2;

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

		// 汎用ジオメトリ入力
		D3D12_GPU_VIRTUAL_ADDRESS customVertexAddress = 0;
		uint32_t customVertexStride = 0;
		uint32_t customVertexCount = 0;
		DXGI_FORMAT customVertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
		D3D12_GPU_VIRTUAL_ADDRESS customIndexAddress = 0;
		DXGI_FORMAT customIndexFormat = DXGI_FORMAT_R32_UINT;
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