#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Graphics/Raytracing/AccelerationStructure/AccelerationStructureBuffer.h>
#include <Engine/Core/Graphics/Raytracing/RaytracingStructures.h>

namespace Engine {

	//============================================================================
	//	BottomLevelAccelerationStructure class
	//	低レベル加速化構造クラス
	//============================================================================
	class BottomLevelAccelerationStructure {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

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
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		// 加速化構造バッファ
		AccelerationStructureBuffer scratch_;
		AccelerationStructureBuffer result_;

		// ジオメトリ記述
		D3D12_RAYTRACING_GEOMETRY_DESC geometryDesc_{};
		// ビルド記述
		D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs_{};
		D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC buildDesc_{};

		// 更新を許可するか
		bool allowUpdate_ = false;

		//--------- functions ----------------------------------------------------

		// ジオメトリ記述の設定
		void FillGeometryDesc(const RaytracingBLASInput& input);
	};
} // Engine