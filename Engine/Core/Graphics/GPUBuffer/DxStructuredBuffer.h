#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Graphics/DxLib/DxUtils.h>

// c++
#include <vector>
#include <cassert>

namespace Engine {

	//============================================================================
	//	DxStructuredBuffer class
	//	SRV/UAV用の構造化バッファを生成し、データ転送とハンドル提供を行う。
	//============================================================================
	template<typename T>
	class DxStructuredBuffer {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		DxStructuredBuffer() = default;
		virtual ~DxStructuredBuffer() = default;

		// SRV用の構造化バッファを作成する
		void CreateSRVBuffer(ID3D12Device* device, UINT instanceCount);
		// UAV用の構造化バッファを作成する
		void CreateUAVBuffer(ID3D12Device* device, UINT instanceCount);

		// 全要素をGPUへ転送する
		void TransferData(const std::vector<T>& data);
		// 先頭からcount分のみをGPUへ転送する
		void TransferData(const std::vector<T>& data, size_t count);
		// 生ポインタから全要素をGPUへ転送する
		void TransferData(const T* data, size_t count);

		//--------- accessor -----------------------------------------------------

		// SRVのGPUハンドルを設定する
		void SetSRVGPUHandle(const D3D12_GPU_DESCRIPTOR_HANDLE& handle) { srvGPUHandle_ = handle; }
		// UAVのGPUハンドルを設定する
		void SetUAVGPUHandle(const D3D12_GPU_DESCRIPTOR_HANDLE& handle) { uavGPUHandle_ = handle; }

		// 内部リソースを取得する
		ID3D12Resource* GetResource() const { return resource_.Get(); }

		// SRV/UAVのGPUハンドルを取得する
		const D3D12_GPU_DESCRIPTOR_HANDLE& GetSRVGPUHandle() const { return srvGPUHandle_; }
		const D3D12_GPU_DESCRIPTOR_HANDLE& GetUAVGPUHandle() const { return uavGPUHandle_; }

		// SRV/UAVのビュー記述子を生成する
		D3D12_SHADER_RESOURCE_VIEW_DESC GetSRVDesc(UINT instanceCount) const;
		D3D12_UNORDERED_ACCESS_VIEW_DESC GetUAVDesc(UINT instanceCount) const;

		// リソースの作成状態を取得する
		bool IsCreatedResource() const { return isCreated_; }
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		ComPtr<ID3D12Resource> resource_;
		T* mappedData_ = nullptr;

		D3D12_GPU_DESCRIPTOR_HANDLE srvGPUHandle_;
		D3D12_GPU_DESCRIPTOR_HANDLE uavGPUHandle_;

		bool isCreated_ = false;
	};

	//============================================================================
	//	DxStructuredBuffer templateMethods
	//============================================================================

	template<typename T>
	inline void DxStructuredBuffer<T>::CreateSRVBuffer(ID3D12Device* device, UINT instanceCount) {

		DxUtils::CreateBufferResource(device, resource_, sizeof(T) * instanceCount);

		// マッピング
		HRESULT hr = resource_->Map(0, nullptr, reinterpret_cast<void**>(&mappedData_));
		assert(SUCCEEDED(hr));

		isCreated_ = true;
	}

	template<typename T>
	inline void DxStructuredBuffer<T>::CreateUAVBuffer(ID3D12Device* device, UINT instanceCount) {

		DxUtils::CreateUavBufferResource(device, resource_, sizeof(T) * instanceCount);
		// マッピング処理は行わない
		isCreated_ = true;
	}

	template<typename T>
	inline void DxStructuredBuffer<T>::TransferData(const std::vector<T>& data) {

		if (mappedData_) {

			std::memcpy(mappedData_, data.data(), sizeof(T) * data.size());
		}
	}

	template<typename T>
	inline void DxStructuredBuffer<T>::TransferData(const std::vector<T>& data, size_t count) {

		if (mappedData_) {

			std::memcpy(mappedData_, data.data(), sizeof(T) * count);
		}
	}

	template<typename T>
	inline void DxStructuredBuffer<T>::TransferData(const T* data, size_t count) {
	
		if (!mappedData_ || !data || count == 0) {
			return;
		}

		std::memcpy(mappedData_, data, sizeof(T) * count);
	}

	template<typename T>
	inline D3D12_SHADER_RESOURCE_VIEW_DESC DxStructuredBuffer<T>::GetSRVDesc(UINT instanceCount) const {

		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};

		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.Format = DXGI_FORMAT_UNKNOWN;
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
		srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
		srvDesc.Buffer.NumElements = instanceCount;
		srvDesc.Buffer.StructureByteStride = sizeof(T);
		srvDesc.Buffer.FirstElement = 0;

		return srvDesc;
	}

	template<typename T>
	inline D3D12_UNORDERED_ACCESS_VIEW_DESC DxStructuredBuffer<T>::GetUAVDesc(UINT instanceCount) const {

		D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{};

		uavDesc.Format = DXGI_FORMAT_UNKNOWN;
		uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
		uavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;
		uavDesc.Buffer.NumElements = instanceCount;
		uavDesc.Buffer.StructureByteStride = sizeof(T);
		uavDesc.Buffer.CounterOffsetInBytes = 0;
		uavDesc.Buffer.FirstElement = 0;

		return uavDesc;
	}
}; // Engine