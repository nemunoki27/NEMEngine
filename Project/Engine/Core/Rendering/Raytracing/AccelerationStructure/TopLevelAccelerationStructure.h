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
	//	TopLevelAccelerationStructure class
	//	高レベル加速化構造クラス
	//============================================================================
	class TopLevelAccelerationStructure {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		TopLevelAccelerationStructure() = default;
		~TopLevelAccelerationStructure();
		TopLevelAccelerationStructure(const TopLevelAccelerationStructure&) = delete;
		TopLevelAccelerationStructure& operator=(const TopLevelAccelerationStructure&) = delete;
		TopLevelAccelerationStructure(TopLevelAccelerationStructure&& other) noexcept;
		TopLevelAccelerationStructure& operator=(TopLevelAccelerationStructure&& other) noexcept;

		// 回収窓口を接続する
		void SetRetirementQueue(GraphicsResourceRetirement& queue);

		// 所有資源をGPU完了まで退避する
		void Release();

		// TLASの構築
		void Build(ID3D12Device8* device, ID3D12GraphicsCommandList6* commandList,
			const std::vector<RaytracingTLASInstance>& instances, bool allowUpdate);

		// TLASの更新
		void Update(ID3D12GraphicsCommandList6* commandList,
			const std::vector<RaytracingTLASInstance>& instances);
		// 既存バッファを再利用してTLASを完全再構築する
		void Rebuild(ID3D12GraphicsCommandList6* commandList,
			const std::vector<RaytracingTLASInstance>& instances);

		//--------- accessor -----------------------------------------------------

		bool IsBuilt() const { return result_.GetResource() != nullptr; }

		ID3D12Resource* GetResource() const { return result_.GetResource(); }
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- variables ----------------------------------------------------

		ID3D12Device8* device_ = nullptr;

		// 加速化構造バッファ
		DxFrameMappedUploadBuffer instanceDescBuffer_;
		AccelerationStructureBuffer scratch_;
		AccelerationStructureBuffer result_;
		GraphicsResourceRetirement* retirementQueue_ = nullptr;
		// 毎更新で再確保しないTLAS記述作業領域
		std::vector<D3D12_RAYTRACING_INSTANCE_DESC>
			instanceDescScratch_{};

		// ビルド記述
		D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs_{};
		D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC buildDesc_{};

		// 更新を許可するか
		bool allowUpdate_ = false;

		//--------- functions ----------------------------------------------------

		// 所有と構築状態を一組で交換する
		void Swap(TopLevelAccelerationStructure& other) noexcept;

		// TLASインスタンス記述のアップロード
		void UploadInstanceDescs(const std::vector<RaytracingTLASInstance>& instances);
		// Matrix4x4行列を3x4行列に変換してコピー
		static void CopyMatrix3x4(float(&dst)[3][4], const Matrix4x4& src);
	};
} // Engine
