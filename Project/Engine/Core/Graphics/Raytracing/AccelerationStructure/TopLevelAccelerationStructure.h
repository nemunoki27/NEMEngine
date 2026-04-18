#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Graphics/Raytracing/AccelerationStructure/AccelerationStructureBuffer.h>
#include <Engine/Core/Graphics/Raytracing/RaytracingStructures.h>

// c++
#include <vector>

namespace Engine {

	//============================================================================
	//	TopLevelAccelerationStructure class
	//	高レベル加速化構造クラス
	//============================================================================
	class TopLevelAccelerationStructure {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		TopLevelAccelerationStructure() = default;
		~TopLevelAccelerationStructure() = default;

		// TLASの構築
		void Build(ID3D12Device8* device, ID3D12GraphicsCommandList6* commandList,
			const std::vector<RaytracingTLASInstance>& instances, bool allowUpdate);

		// TLASの更新
		void Update(ID3D12GraphicsCommandList6* commandList,
			const std::vector<RaytracingTLASInstance>& instances);

		//--------- accessor -----------------------------------------------------

		bool IsBuilt() const { return result_.GetResource() != nullptr; }

		ID3D12Resource* GetResource() const { return result_.GetResource(); }
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		ID3D12Device8* device_ = nullptr;

		// 加速化構造バッファ
		AccelerationStructureBuffer instanceDescBuffer_;
		AccelerationStructureBuffer scratch_;
		AccelerationStructureBuffer result_;

		// ビルド記述
		D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs_{};
		D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC buildDesc_{};

		// 更新を許可するか
		bool allowUpdate_ = false;

		//--------- functions ----------------------------------------------------

		// TLASインスタンス記述のアップロード
		void UploadInstanceDescs(const std::vector<RaytracingTLASInstance>& instances);
		// Matrix4x4行列を3x4行列に変換してコピー
		static void CopyMatrix3x4(float(&dst)[3][4], const Matrix4x4& src);
	};
} // Engine