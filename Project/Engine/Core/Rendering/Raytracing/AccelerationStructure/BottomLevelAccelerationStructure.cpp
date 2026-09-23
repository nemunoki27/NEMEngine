#include "BottomLevelAccelerationStructure.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Diagnostics/Assert.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>

//============================================================================
//	BottomLevelAccelerationStructure classMethods
//============================================================================
namespace {

	struct GeometryTransform3x4 {

		float value[3][4]{};
	};
}

void Engine::BottomLevelAccelerationStructure::FillGeometryDescs(
	const RaytracingBLASInput& input) {

	geometryDescs_.resize(input.geometries.size());

	std::vector<GeometryTransform3x4> transforms(input.geometries.size());

	for (uint32_t index = 0;
		index < static_cast<uint32_t>(input.geometries.size()); ++index) {

		const RaytracingBLASGeometryInput& source = input.geometries[index];
		Assert::Call(source.vertexAddress != 0 && source.vertexStride != 0 &&
			source.vertexCount != 0, "BLAS Geometryに頂点データが必要です");
		Assert::Call(source.indexAddress != 0 && source.indexCount != 0,
			"BLAS Geometryにインデックスデータが必要です");

		// 行ベクトル行列をDXRの3x4行列へ変換
		const Matrix4x4 matrix = Matrix4x4::Transpose(source.localMatrix);
		for (uint32_t row = 0; row < 3; ++row) {
			for (uint32_t column = 0; column < 4; ++column) {
				transforms[index].value[row][column] =
					matrix.m[row][column];
			}
		}

		D3D12_RAYTRACING_GEOMETRY_DESC& geometry = geometryDescs_[index];
		geometry = {};
		geometry.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
		geometry.Flags = source.flags;
		geometry.Triangles.Transform3x4 =
			geometryTransformBuffer_.GetGPUAddress() +
			sizeof(GeometryTransform3x4) * static_cast<uint64_t>(index);
		geometry.Triangles.VertexFormat = source.vertexFormat;
		geometry.Triangles.VertexCount = source.vertexCount;
		geometry.Triangles.VertexBuffer.StartAddress = source.vertexAddress;
		geometry.Triangles.VertexBuffer.StrideInBytes = source.vertexStride;
		geometry.Triangles.IndexFormat = source.indexFormat;
		geometry.Triangles.IndexCount = source.indexCount;
		geometry.Triangles.IndexBuffer = source.indexAddress;
	}
	geometryTransformBuffer_.Write(
		transforms.data(), transforms.size() * sizeof(GeometryTransform3x4));
}

uint64_t Engine::BottomLevelAccelerationStructure::ComputeLayoutHash(
	const RaytracingBLASInput& input) {

	uint64_t hash = static_cast<uint64_t>(input.geometries.size());
	for (const RaytracingBLASGeometryInput& geometry : input.geometries) {

		Algorithm::HashCombine(hash, geometry.vertexStride);
		Algorithm::HashCombine(hash, geometry.vertexCount);
		Algorithm::HashCombine(hash, static_cast<uint32_t>(geometry.vertexFormat));
		Algorithm::HashCombine(hash, geometry.indexCount);
		Algorithm::HashCombine(hash, static_cast<uint32_t>(geometry.indexFormat));
		Algorithm::HashCombine(hash, static_cast<uint32_t>(geometry.flags));
	}
	return hash;
}

void Engine::BottomLevelAccelerationStructure::Build(ID3D12Device8* device,
	ID3D12GraphicsCommandList6* commandList, const RaytracingBLASInput& input) {

	Assert::Call(retirementQueue_ != nullptr, "ASの回収窓口が設定されていません");

	Assert::Call(!input.geometries.empty(), "BLASにGeometryが必要です");

	device_ = device;
	allowUpdate_ = input.allowUpdate;
	layoutHash_ = ComputeLayoutHash(input);

	// ジオメトリローカル行列の領域を作成
	geometryTransformBuffer_.EnsureCapacity(device,
		sizeof(GeometryTransform3x4) * input.geometries.size(),
		"BLASGeometryTransforms", 256);
	FillGeometryDescs(input);

	// ASビルド記述の設定
	inputs_ = {};
	inputs_.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
	inputs_.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
	inputs_.NumDescs = static_cast<UINT>(geometryDescs_.size());
	inputs_.pGeometryDescs = geometryDescs_.data();
	inputs_.Flags =
		D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
	if (allowUpdate_) {
		inputs_.Flags |=
			D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE;
	}
	D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO prebuild{};
	device->GetRaytracingAccelerationStructurePrebuildInfo(&inputs_, &prebuild);

	if (scratch_.GetResource()) {
		retirementQueue_->Retire(scratch_.TakeResource());
	}
	if (result_.GetResource()) {
		retirementQueue_->Retire(result_.TakeResource());
	}
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
	if (layoutHash_ != ComputeLayoutHash(input)) {
		Build(device_, commandList, input);
		return;
	}

	// 毎フレーム、最新の頂点アドレスで組み直す
	FillGeometryDescs(input);

	buildDesc_.Inputs.pGeometryDescs = geometryDescs_.data();
	buildDesc_.SourceAccelerationStructureData = result_.GetGPUAddress();
	buildDesc_.Inputs.Flags |= D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PERFORM_UPDATE;
	commandList->BuildRaytracingAccelerationStructure(&buildDesc_, 0, nullptr);

	// UAVバリアを挿入してASの更新完了を待つ
	D3D12_RESOURCE_BARRIER uavBarrier{};
	uavBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
	uavBarrier.UAV.pResource = result_.GetResource();
	commandList->ResourceBarrier(1, &uavBarrier);
}

void Engine::BottomLevelAccelerationStructure::Rebuild(
	ID3D12GraphicsCommandList6* commandList,
	const RaytracingBLASInput& input) {

	if (!result_.GetResource() ||
		layoutHash_ != ComputeLayoutHash(input)) {
		Build(device_, commandList, input);
		return;
	}

	// refitで劣化したBVHを、GPUアドレスを変えずに初期品質へ戻す
	FillGeometryDescs(input);
	inputs_.pGeometryDescs = geometryDescs_.data();
	inputs_.Flags =
		D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
	if (allowUpdate_) {
		inputs_.Flags |=
			D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE;
	}

	buildDesc_.Inputs = inputs_;
	buildDesc_.DestAccelerationStructureData = result_.GetGPUAddress();
	buildDesc_.ScratchAccelerationStructureData = scratch_.GetGPUAddress();
	buildDesc_.SourceAccelerationStructureData = 0;
	commandList->BuildRaytracingAccelerationStructure(
		&buildDesc_, 0, nullptr);

	D3D12_RESOURCE_BARRIER uavBarrier{};
	uavBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
	uavBarrier.UAV.pResource = result_.GetResource();
	commandList->ResourceBarrier(1, &uavBarrier);
}

Engine::BottomLevelAccelerationStructure::~BottomLevelAccelerationStructure() {

	Release();
}

Engine::BottomLevelAccelerationStructure::BottomLevelAccelerationStructure(BottomLevelAccelerationStructure&& other) noexcept {

	Swap(other);
}

Engine::BottomLevelAccelerationStructure& Engine::BottomLevelAccelerationStructure::operator=(
	BottomLevelAccelerationStructure&& other) noexcept {

	if (this != &other) {
		Release();
		Swap(other);
	}
	return *this;
}

void Engine::BottomLevelAccelerationStructure::Swap(BottomLevelAccelerationStructure& other) noexcept {

	std::swap(scratch_, other.scratch_);
	std::swap(result_, other.result_);
	std::swap(geometryTransformBuffer_, other.geometryTransformBuffer_);
	std::swap(retirementQueue_, other.retirementQueue_);
	std::swap(inputs_, other.inputs_);
	std::swap(buildDesc_, other.buildDesc_);
	std::swap(device_, other.device_);
	std::swap(allowUpdate_, other.allowUpdate_);
	std::swap(geometryDescs_, other.geometryDescs_);
	std::swap(layoutHash_, other.layoutHash_);
}

void Engine::BottomLevelAccelerationStructure::SetRetirementQueue(GraphicsResourceRetirement& queue) {

	Assert::Call(!IsBuilt() || retirementQueue_ == &queue, "使用中のASの回収窓口は変更できません");
	geometryTransformBuffer_.SetRetirementQueue(queue);
	retirementQueue_ = &queue;
}

void Engine::BottomLevelAccelerationStructure::Release() {

	if (scratch_.GetResource()) retirementQueue_->Retire(scratch_.TakeResource());
	if (result_.GetResource()) retirementQueue_->Retire(result_.TakeResource());
	geometryTransformBuffer_.Release();
	inputs_ = {};
	buildDesc_ = {};
	device_ = nullptr;
	allowUpdate_ = false;
	geometryDescs_.clear();
	layoutHash_ = 0;
}
