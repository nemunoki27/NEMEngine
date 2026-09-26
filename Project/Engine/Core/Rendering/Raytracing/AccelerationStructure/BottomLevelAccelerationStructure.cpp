#include "BottomLevelAccelerationStructure.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/DxObject/Debug/DxDredDiagnostics.h>
#include <stdexcept>
#include <algorithm>
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
		if (!source.vertexAddress || !source.vertexStride || !source.vertexCount || !source.indexAddress || !source.indexCount) {
			throw std::invalid_argument("BLAS Geometryに頂点とインデックスが必要です");
		}

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

		geometry.Triangles.VertexFormat = source.vertexFormat;
		geometry.Triangles.VertexCount = source.vertexCount;
		geometry.Triangles.VertexBuffer.StartAddress = source.vertexAddress;
		geometry.Triangles.VertexBuffer.StrideInBytes = source.vertexStride;
		geometry.Triangles.IndexFormat = source.indexFormat;
		geometry.Triangles.IndexCount = source.indexCount;
		geometry.Triangles.IndexBuffer = source.indexAddress;
	}
	const auto allocation = geometryTransformBuffer_.AllocateAndUploadBytes(*retirementQueue_, device_,
		std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(transforms.data()), transforms.size() * sizeof(GeometryTransform3x4)));
	for (size_t index = 0; index < geometryDescs_.size(); ++index) {
		geometryDescs_[index].Triangles.Transform3x4 = allocation.gpuAddress + sizeof(GeometryTransform3x4) * index;
	}
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

	if (!retirementQueue_) throw std::logic_error("ASの回収窓口が設定されていません");
	if (!device || !commandList || input.geometries.empty() || input.geometries.size() > UINT32_MAX) {
		throw std::invalid_argument("BLASの構築引数が不正です");
	}
	if (!DxDredDiagnostics::CheckDeviceState(device, "BLAS::Build")) throw std::runtime_error("Deviceが失われました");
	// 候補の作成と記録が成功してから公開する
	retirementQueue_->ReservePending(2 + kGraphicsFrameContextCount);
	BottomLevelAccelerationStructure candidate;
	candidate.SetRetirementQueue(*retirementQueue_);
	candidate.BuildResources(device, commandList, input);
	Swap(candidate);
}

void Engine::BottomLevelAccelerationStructure::BuildResources(ID3D12Device8* device,
	ID3D12GraphicsCommandList6* commandList, const RaytracingBLASInput& input) {

	device_ = device;
	allowUpdate_ = input.allowUpdate;
	layoutHash_ = ComputeLayoutHash(input);

	// 構築ごとに異なる変換行列の領域を使う
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

	// スクラッチと結果のバッファを作成
	const UINT64 scratchSize = allowUpdate_ ?
		(std::max)(prebuild.ScratchDataSizeInBytes, prebuild.UpdateScratchDataSizeInBytes) : prebuild.ScratchDataSizeInBytes;
	scratch_.Create(device, scratchSize,
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
	result_.InsertUAVBarrier(commandList);
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
	// 先行描画と前回構築の読み書きを終えてから更新する
	result_.InsertUAVBarrier(commandList);
	scratch_.InsertUAVBarrier(commandList);
	commandList->BuildRaytracingAccelerationStructure(&buildDesc_, 0, nullptr);

	// UAVバリアを挿入してASの更新完了を待つ
	result_.InsertUAVBarrier(commandList);
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
	// 先行描画と前回構築の読み書きを終えてから更新する
	result_.InsertUAVBarrier(commandList);
	scratch_.InsertUAVBarrier(commandList);
	commandList->BuildRaytracingAccelerationStructure(
		&buildDesc_, 0, nullptr);

	result_.InsertUAVBarrier(commandList);
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

	if (IsBuilt() && retirementQueue_ != &queue) throw std::logic_error("使用中のASの回収窓口は変更できません");
	retirementQueue_ = &queue;
}

void Engine::BottomLevelAccelerationStructure::Release() {

	if (retirementQueue_) retirementQueue_->ReservePending(2);
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
