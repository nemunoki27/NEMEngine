#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Meshes/GPUResource/MeshResourceTypes.h>
#include <Engine/Core/Rendering/Meshes/GPUResource/MeshShaderSharedTypes.h>

// c++
#include <span>

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

	// BLASへ登録する1つのジオメトリ
	struct RaytracingBLASGeometryInput {

		D3D12_GPU_VIRTUAL_ADDRESS vertexAddress = 0;
		uint32_t vertexStride = 0;
		uint32_t vertexCount = 0;
		DXGI_FORMAT vertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;

		D3D12_GPU_VIRTUAL_ADDRESS indexAddress = 0;
		uint32_t indexCount = 0;
		DXGI_FORMAT indexFormat = DXGI_FORMAT_R32_UINT;

		// ジオメトリ単位のローカル行列
		Matrix4x4 localMatrix = Matrix4x4::Identity();
		D3D12_RAYTRACING_GEOMETRY_FLAGS flags =
			D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;
	};
	// BLAS構築入力
	struct RaytracingBLASInput {

		std::span<const RaytracingBLASGeometryInput> geometries{};
		bool allowUpdate = false;
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
		// このTLASインスタンスが参照するジオメトリデータの先頭
		uint32_t geometryDataOffset = 0;

		// ライティングやIBLをヒット地点で再現するための描画フラグ
		uint32_t renderFlags = 0;
		uint32_t _pad[3] = { 0, 0, 0 };
	};
	static_assert(sizeof(RaytracingInstanceShaderData) == 32,
		"RaytracingInstanceShaderData must match HLSL layout");
	// BLAS内ジオメトリ情報
	struct RaytracingGeometryShaderData {

		// このジオメトリが参照するサブメッシュデータ
		uint32_t subMeshDataIndex = 0;
		// インデックスバッファ内のサブメッシュ先頭
		uint32_t indexOffset = 0;
		// エディターピック記録のインデックス
		uint32_t pickRecordIndex = 0;
		uint32_t _pad = 0;
	};
	// GPUへ渡すシーン共通データ
	struct RaytracingScene {

		float rayMin = 0.001f;
		float rayMax = 10000.0f;
		uint32_t instanceCount = 0;
		uint32_t _pad = 0;
	};
} // Engine
