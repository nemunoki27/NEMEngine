#include "BottomLevelAccelerationStructure.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Logger/Assert.h>

//============================================================================
//	BottomLevelAccelerationStructure classMethods
//============================================================================

void Engine::BottomLevelAccelerationStructure::FillGeometryDesc(const RaytracingBLASInput& input) {

	// メッシュリソースの取得と検査
	const MeshGPUResource& mesh = *input.meshResource;

	Assert::Call(input.subMeshIndex < mesh.subMeshes.size(), "Invalid subMeshIndex.");
	Assert::Call((input.indexOffset + input.indexCount) <= mesh.indexCount, "SubMesh index range out of bounds.");

	// スキニング結果の頂点データを使用するか
	bool useOverrideVertexBuffer = input.overrideVertexAddress != 0 && input.overrideVertexCount != 0;

	// 頂点GPUアドレス
	D3D12_GPU_VIRTUAL_ADDRESS vertexBaseAddress = useOverrideVertexBuffer ?
		(input.overrideVertexAddress + offsetof(MeshVertex, position)) :
		(mesh.vertexSRV.buffer->GetResource()->GetGPUVirtualAddress() + offsetof(MeshVertex, position));
	// 頂点数
	UINT vertexCount = useOverrideVertexBuffer ?
		static_cast<UINT>(input.overrideVertexCount) :
		static_cast<UINT>(mesh.vertexCount);

	// インデックスGPUアドレスはサブメッシュ範囲まで
	D3D12_GPU_VIRTUAL_ADDRESS indexBaseAddress = mesh.indexBuffer.GetResource()->GetGPUVirtualAddress() +
		sizeof(uint32_t) * static_cast<uint64_t>(input.indexOffset);

	// ジオメトリ記述の設定
	geometryDesc_ = {};
	geometryDesc_.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
	geometryDesc_.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;
	geometryDesc_.Triangles.Transform3x4 = 0;
	// 頂点
	geometryDesc_.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
	geometryDesc_.Triangles.VertexCount = vertexCount;
	geometryDesc_.Triangles.VertexBuffer.StartAddress = vertexBaseAddress;
	geometryDesc_.Triangles.VertexBuffer.StrideInBytes = sizeof(MeshVertex);
	// インデックス
	geometryDesc_.Triangles.IndexFormat = DXGI_FORMAT_R32_UINT;
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