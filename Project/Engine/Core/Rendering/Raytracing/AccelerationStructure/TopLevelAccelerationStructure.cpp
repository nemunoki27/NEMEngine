#include "TopLevelAccelerationStructure.h"

//============================================================================
//	TopLevelAccelerationStructure classMethods
//============================================================================
void Engine::TopLevelAccelerationStructure::Build(ID3D12Device8* device, ID3D12GraphicsCommandList6* commandList,
	const std::vector<RaytracingTLASInstance>& instances, bool allowUpdate) {

	Assert::Call(retirementQueue_ != nullptr, "ASの回収窓口が設定されていません");

	device_ = device;
	allowUpdate_ = allowUpdate;

	// インスタンス記述はCPU上書き競合を避けるためフレーム別に保持する
	const UINT64 descBytes = sizeof(D3D12_RAYTRACING_INSTANCE_DESC) * static_cast<UINT64>(instances.size());
	instanceDescBuffer_.EnsureCapacity(device,
		static_cast<size_t>(descBytes), "TLASInstanceDescs", 256);

	// インスタンス記述のアップロード
	UploadInstanceDescs(instances);

	// ASビルド記述の設定
	inputs_ = {};
	inputs_.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
	inputs_.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
	inputs_.NumDescs = static_cast<UINT>(instances.size());
	inputs_.InstanceDescs = instanceDescBuffer_.GetGPUAddress();
	inputs_.Flags =
		D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
	if (allowUpdate_) {
		inputs_.Flags |=
			D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE;
	}
	D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO prebuild{};
	device->GetRaytracingAccelerationStructurePrebuildInfo(&inputs_, &prebuild);

	// 再構築前のASは実行中フレームから参照されるため所有を保持する
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

void Engine::TopLevelAccelerationStructure::Update(ID3D12GraphicsCommandList6* commandList,
	const std::vector<RaytracingTLASInstance>& instances) {

	// 更新が許可されていない、またはASが構築されていない場合は何もしない
	if (!allowUpdate_ || !result_.GetResource()) {
		return;
	}

	// インスタンス数が変わっている場合は再構築する
	if (instances.size() != inputs_.NumDescs) {
		Build(device_, commandList, instances, allowUpdate_);
		return;
	}

	// インスタンス記述のアップロード
	UploadInstanceDescs(instances);
	inputs_.InstanceDescs = instanceDescBuffer_.GetGPUAddress();
	buildDesc_.Inputs.InstanceDescs = inputs_.InstanceDescs;

	// ASの更新
	buildDesc_.SourceAccelerationStructureData = result_.GetGPUAddress();
	buildDesc_.Inputs.Flags |= D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PERFORM_UPDATE;
	commandList->BuildRaytracingAccelerationStructure(&buildDesc_, 0, nullptr);

	// UAVバリアを挿入してASの更新完了を待つ
	D3D12_RESOURCE_BARRIER uavBarrier{};
	uavBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
	uavBarrier.UAV.pResource = result_.GetResource();
	commandList->ResourceBarrier(1, &uavBarrier);
}

void Engine::TopLevelAccelerationStructure::Rebuild(
	ID3D12GraphicsCommandList6* commandList,
	const std::vector<RaytracingTLASInstance>& instances) {

	if (!result_.GetResource() ||
		instances.size() != inputs_.NumDescs) {
		Build(device_, commandList, instances, allowUpdate_);
		return;
	}

	// refitで劣化したBVHを、GPUアドレスを変えずに初期品質へ戻す
	UploadInstanceDescs(instances);
	inputs_.InstanceDescs = instanceDescBuffer_.GetGPUAddress();
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

void Engine::TopLevelAccelerationStructure::UploadInstanceDescs(
	const std::vector<RaytracingTLASInstance>& instances) {

	instanceDescScratch_.resize(instances.size());
	for (size_t i = 0; i < instances.size(); ++i) {

		const auto& src = instances[i];
		auto& dst = instanceDescScratch_[i];

		std::memset(&dst, 0, sizeof(dst));

		// 転置ワールド行列をコピー
		CopyMatrix3x4(dst.Transform, Matrix4x4::Transpose(src.worldMatrix));

		// インスタンス情報を設定
		dst.InstanceID = src.instanceID;
		dst.InstanceContributionToHitGroupIndex = src.hitGroupIndex;
		dst.InstanceMask = src.mask;
		dst.Flags = src.flags;
		dst.AccelerationStructure = src.blas->GetGPUVirtualAddress();
	}
	instanceDescBuffer_.Write(instanceDescScratch_.data(),
		instanceDescScratch_.size() *
		sizeof(D3D12_RAYTRACING_INSTANCE_DESC));
}

void Engine::TopLevelAccelerationStructure::CopyMatrix3x4(float(&dst)[3][4], const Matrix4x4& src) {

	for (int r = 0; r < 3; ++r) {
		for (int c = 0; c < 4; ++c) {
			dst[r][c] = src.m[r][c];
		}
	}
}

Engine::TopLevelAccelerationStructure::~TopLevelAccelerationStructure() {

	Release();
}

Engine::TopLevelAccelerationStructure::TopLevelAccelerationStructure(TopLevelAccelerationStructure&& other) noexcept {

	Swap(other);
}

Engine::TopLevelAccelerationStructure& Engine::TopLevelAccelerationStructure::operator=(
	TopLevelAccelerationStructure&& other) noexcept {

	if (this != &other) {
		Release();
		Swap(other);
	}
	return *this;
}

void Engine::TopLevelAccelerationStructure::Swap(TopLevelAccelerationStructure& other) noexcept {

	std::swap(scratch_, other.scratch_);
	std::swap(result_, other.result_);
	std::swap(instanceDescBuffer_, other.instanceDescBuffer_);
	std::swap(retirementQueue_, other.retirementQueue_);
	std::swap(inputs_, other.inputs_);
	std::swap(buildDesc_, other.buildDesc_);
	std::swap(device_, other.device_);
	std::swap(allowUpdate_, other.allowUpdate_);
	std::swap(instanceDescScratch_, other.instanceDescScratch_);
}

void Engine::TopLevelAccelerationStructure::SetRetirementQueue(GraphicsResourceRetirement& queue) {

	Assert::Call(!IsBuilt() || retirementQueue_ == &queue, "使用中のASの回収窓口は変更できません");
	instanceDescBuffer_.SetRetirementQueue(queue);
	retirementQueue_ = &queue;
}

void Engine::TopLevelAccelerationStructure::Release() {

	if (scratch_.GetResource()) retirementQueue_->Retire(scratch_.TakeResource());
	if (result_.GetResource()) retirementQueue_->Retire(result_.TakeResource());
	instanceDescBuffer_.Release();
	inputs_ = {};
	buildDesc_ = {};
	device_ = nullptr;
	allowUpdate_ = false;
	instanceDescScratch_.clear();
}
