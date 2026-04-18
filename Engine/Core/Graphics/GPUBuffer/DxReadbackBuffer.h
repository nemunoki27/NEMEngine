#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Graphics/DxLib/DxUtils.h>

// directX
#include <d3d12.h>

namespace Engine {

	//============================================================================
	//	DxReadbackBuffer class
	//	GPU→CPUの読み戻し用リードバックバッファを管理するテンプレート。
	//============================================================================
	template <typename T>
	class DxReadbackBuffer {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		DxReadbackBuffer() = default;
		~DxReadbackBuffer() = default;

		// リードバック用のリソースを確保し、CPUアクセス可能にする
		void CreateBuffer(ID3D12Device* device);

		//--------- accessor -----------------------------------------------------

		// 内部リソースを取得する
		ID3D12Resource* GetResource() const { return resource_.Get(); }

		// 読み戻し結果を参照で取得する
		const T& GetReadbackData() { return *mappedData_; }
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		ComPtr<ID3D12Resource> resource_;
		T* mappedData_ = nullptr;
	};

	//============================================================================
	//	DxReadbackBuffer templateMethods
	//============================================================================

	template<typename T>
	inline void DxReadbackBuffer<T>::CreateBuffer(ID3D12Device* device) {

		DxUtils::CreateReadbackBufferResource(device, resource_, sizeof(T));

		// マッピング
		HRESULT hr = resource_->Map(0, nullptr, reinterpret_cast<void**>(&mappedData_));
		assert(SUCCEEDED(hr));
	}
}; // Engine