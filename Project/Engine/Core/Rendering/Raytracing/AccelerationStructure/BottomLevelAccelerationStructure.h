#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Raytracing/AccelerationStructure/AccelerationStructureBuffer.h>
#include <Engine/Core/Rendering/Raytracing/RaytracingStructures.h>
#include <Engine/Core/Rendering/DxObject/Buffers/DxFrameMappedUploadBuffer.h>

// c++
#include <vector>

namespace Engine {

	//============================================================================
	//	BottomLevelAccelerationStructure class
	//	低レベル加速化構造クラス
	//============================================================================
	class BottomLevelAccelerationStructure {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		BottomLevelAccelerationStructure() = default;
		~BottomLevelAccelerationStructure() = default;

		// BLASの構築
		void Build(ID3D12Device8* device, ID3D12GraphicsCommandList6* commandList,
			const RaytracingBLASInput& input);

		// BLASの更新
		void Update(ID3D12GraphicsCommandList6* commandList, const RaytracingBLASInput& input);

		//--------- accessor -----------------------------------------------------

		// ASが構築されているか
		bool IsBuilt() const { return result_.GetResource() != nullptr; }

		ID3D12Resource* GetResource() const { return result_.GetResource(); }
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- variables ----------------------------------------------------

		// 加速化構造バッファ
		AccelerationStructureBuffer scratch_;
		AccelerationStructureBuffer result_;
		// ジオメトリローカル行列のアップロードバッファ
		DxFrameMappedUploadBuffer geometryTransformBuffer_;
		std::vector<ComPtr<ID3D12Resource>> retiredResources_{};

		// ジオメトリ記述
		std::vector<D3D12_RAYTRACING_GEOMETRY_DESC> geometryDescs_{};
		// ビルド記述
		D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs_{};
		D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC buildDesc_{};

		ID3D12Device8* device_ = nullptr;
		// 更新を許可するか
		bool allowUpdate_ = false;
		// 更新時に変えられないジオメトリレイアウト
		uint64_t layoutHash_ = 0;

		//--------- functions ----------------------------------------------------

		// ジオメトリ記述とローカル行列の設定
		void FillGeometryDescs(const RaytracingBLASInput& input);
		// 更新可否を判定するレイアウトHashを計算
		static uint64_t ComputeLayoutHash(const RaytracingBLASInput& input);
	};
} // Engine
