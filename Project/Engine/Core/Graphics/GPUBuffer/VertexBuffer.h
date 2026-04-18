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
	//	VertexBuffer class
	//	頂点バッファ(VB)の作成/転送/ビュー提供を行うテンプレートラッパー。
	//============================================================================
	template<typename T>
	class VertexBuffer {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		VertexBuffer() = default;
		virtual ~VertexBuffer() = default;

		// 指定頂点数でVBリソースを確保し、ビュー情報を初期化する
		void CreateBuffer(ID3D12Device* device, UINT vertexCount);

		// CPU側の頂点配列をGPUへ転送する
		void TransferData(const std::vector<T>& data);

		//--------- accessor -----------------------------------------------------

		// VBVを取得する
		const D3D12_VERTEX_BUFFER_VIEW& GetVertexBufferView() const { return vertexBufferView_; }

		// リソースの作成状態を取得する
		bool IsCreatedResource() const { return isCreated_; }
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		ComPtr<ID3D12Resource> resource_;
		T* mappedData_ = nullptr;

		D3D12_VERTEX_BUFFER_VIEW vertexBufferView_;

		bool isCreated_ = false;
	};

	//============================================================================
	//	VertexBuffer templateMethods
	//============================================================================

	template<typename T>
	inline void VertexBuffer<T>::CreateBuffer(ID3D12Device* device, UINT vertexCount) {

		HRESULT hr;

		if (vertexCount > 0) {

			// 頂点データサイズ
			UINT sizeVB = static_cast<UINT>(sizeof(T) * vertexCount);

			// 定数バッファーのリソース作成
			DxUtils::CreateBufferResource(device, resource_, sizeVB);

			// 頂点バッファビューの作成
			vertexBufferView_.BufferLocation = resource_->GetGPUVirtualAddress();
			vertexBufferView_.SizeInBytes = sizeVB;
			vertexBufferView_.StrideInBytes = sizeof(T);

			// マッピング
			hr = resource_->Map(0, nullptr, reinterpret_cast<void**>(&mappedData_));
			assert(SUCCEEDED(hr));

			// 作成済みにする
			isCreated_ = true;
		}
	}

	template<typename T>
	inline void VertexBuffer<T>::TransferData(const std::vector<T>& data) {

		if (mappedData_) {

			std::memcpy(mappedData_, data.data(), sizeof(T) * data.size());
		}
	}
}; // Engine