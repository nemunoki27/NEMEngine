#include "BottomLevelAccelerationStructure.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Diagnostics/Assert.h>

//============================================================================
//	BottomLevelAccelerationStructure classMethods
//============================================================================
void Engine::BottomLevelAccelerationStructure::FillGeometryDesc(const RaytracingBLASInput& input) {

	D3D12_GPU_VIRTUAL_ADDRESS vertexBaseAddress = 0;
	UINT vertexCount = 0;
	UINT vertexStride = 0;
	DXGI_FORMAT vertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
	D3D12_GPU_VIRTUAL_ADDRESS indexBaseAddress = 0;
	DXGI_FORMAT indexFormat = DXGI_FORMAT_R32_UINT;
	// 通常のMeshRenderer経由描画
	if (input.meshResource) {

		// メッシュリソースの取得と検査
		const MeshGPUResource& mesh = *input.meshResource;

		Assert::Call(input.subMeshIndex < mesh.subMeshes.size(), "Invalid subMeshIndex.");
		Assert::Call((input.indexOffset + input.indexCount) <= mesh.indexCount, "SubMesh index range out of bounds.");

		// スキニング結果の頂点データを使用するか
		const bool useOverrideVertexBuffer = input.overrideVertexAddress != 0 && input.overrideVertexCount != 0;

		// 頂点はMeshVertexのtepositionを先頭にしてstrideはMeshVertexサイズ
		vertexBaseAddress = (useOverrideVertexBuffer ? input.overrideVertexAddress :
			mesh.vertexSRV.buffer->GetResource()->GetGPUVirtualAddress()) + offsetof(MeshVertex, position);
		vertexCount = useOverrideVertexBuffer ? static_cast<UINT>(input.overrideVertexCount) : static_cast<UINT>(mesh.vertexCount);
		vertexStride = static_cast<UINT>(sizeof(MeshVertex));
		vertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;

		// インデックスGPUアドレスはサブメッシュ範囲まで、BLAS側にもIBVと同じFormatを渡す
		indexBaseAddress = mesh.indexBuffer.GetResource()->GetGPUVirtualAddress() +
			mesh.indexBuffer.GetIndexSizeInBytes() * static_cast<uint64_t>(input.indexOffset);
		indexFormat = mesh.indexBuffer.GetFormat();
	}
	// カスタムメッシュ描画の設定
	else {

		Assert::Call(input.customVertexAddress != 0 && input.customVertexStride != 0, "Custom BLAS geometry requires a vertex buffer.");

		// 入力設定をそのままセット
		vertexBaseAddress = input.customVertexAddress;
		vertexCount = static_cast<UINT>(input.customVertexCount);
		vertexStride = static_cast<UINT>(input.customVertexStride);
		vertexFormat = input.customVertexFormat;
		indexBaseAddress = input.customIndexAddress;
		indexFormat = input.customIndexFormat;
	}

	// ジオメトリ記述の設定
	geometryDesc_ = {};
	geometryDesc_.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
	geometryDesc_.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;
	geometryDesc_.Triangles.Transform3x4 = 0;
	// 頂点
	geometryDesc_.Triangles.VertexFormat = vertexFormat;
	geometryDesc_.Triangles.VertexCount = vertexCount;
	geometryDesc_.Triangles.VertexBuffer.StartAddress = vertexBaseAddress;
	geometryDesc_.Triangles.VertexBuffer.StrideInBytes = vertexStride;
	// インデックス
	geometryDesc_.Triangles.IndexFormat = indexFormat;
	geometryDesc_.Triangles.IndexCount = static_cast<UINT>(input.indexCount);
	geometryDesc_.Triangles.IndexBuffer = indexBaseAddress;
}

void Engine::BottomLevelAccelerationStructure::Build(ID3D12Device8* device,
	ID3D12GraphicsCommandList6* commandList, const RaytracingBLASInput& input) {

	allowUpdate_ = input.allowUpdate;

	// ジオメトリ記述の設定
	FillGeometryDesc(input);

	// ASビルド記述の設定
	inputs_ = {};
	inputs_.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
	inputs_.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
	inputs_.NumDescs = 1;
	inputs_.pGeometryDescs = &geometryDesc_;
	inputs_.Flags = allowUpdate_ ? D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE
		: D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
	D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO prebuild{};
	device->GetRaytracingAccelerationStructurePrebuildInfo(&inputs_, &prebuild);

	// スクラッチと結果のバッファを作成
	scratch_.Create(device, prebuild.ScratchDataSizeInBytes,
		D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON);
	result_.Create(device, prebuild.ResultDataMaxSizeInBytes,
		D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE);

	// ASの構築
	buildDesc_ = {};
	buildDesc_.Inputs = inputs_;
	buildDesc_.DestAccelerationStructureData = result_.GetGPUAddress();
	buildDesc_.ScratchAccelerationStructureData = scratch_.GetGPUAddress();
	buildDesc_.SourceAccelerationStructureData = 0;
	commandList->BuildRaytracingAccelerationStructure(&buildDesc_, 0, nullptr);

	// UAVバリアを挿入してASの構築完了を待つ
	D3D12_RESOURCE_BARRIER uavBarrier{};
	uavBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
	uavBarrier.UAV.pResource = result_.GetResource();
	commandList->ResourceBarrier(1, &uavBarrier);
}

void Engine::BottomLevelAccelerationStructure::Update(ID3D12GraphicsCommandList6* commandList, const RaytracingBLASInput& input) {

	// 更新が許可されていない場合やASが構築されていない場合は何もしない
	if (!allowUpdate_ || !result_.GetResource()) {
		return;
	}

	// 毎フレーム、最新の頂点アドレスで組み直す
	FillGeometryDesc(input);

	buildDesc_.Inputs.pGeometryDescs = &geometryDesc_;
	buildDesc_.SourceAccelerationStructureData = result_.GetGPUAddress();
	buildDesc_.Inputs.Flags |= D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PERFORM_UPDATE;
	commandList->BuildRaytracingAccelerationStructure(&buildDesc_, 0, nullptr);

	// UAVバリアを挿入してASの更新完了を待つ
	D3D12_RESOURCE_BARRIER uavBarrier{};
	uavBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
	uavBarrier.UAV.pResource = result_.GetResource();
	commandList->ResourceBarrier(1, &uavBarrier);
}
