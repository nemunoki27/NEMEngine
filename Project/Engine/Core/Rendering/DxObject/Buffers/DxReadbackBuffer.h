#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Core/GraphicsFrameContext.h>
#include <Engine/Core/Rendering/DxObject/Debug/DxDredDiagnostics.h>
#include <stdexcept>
#include <Engine/Core/Rendering/DxObject/Common/DxUtils.h>

// directX
#include <d3d12.h>

namespace Engine {

	//============================================================================
	//	DxReadbackBuffer class
	// GPU→CPUの読み戻し用リードバックバッファを管理するテンプレート
	//============================================================================
	template <typename T>
	class DxReadbackBuffer {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		DxReadbackBuffer() = default;
		~DxReadbackBuffer() { if (resource_) retirement_->Retire(std::move(resource_)); }
		DxReadbackBuffer(const DxReadbackBuffer&) = delete;
		DxReadbackBuffer& operator=(const DxReadbackBuffer&) = delete;

		// リードバック用のリソースを確保し、CPUアクセス可能にする
		void CreateBuffer(GraphicsResourceRetirement& retirement, ID3D12Device* device);

		//--------- accessor -----------------------------------------------------

		// 内部リソースを取得する
		ID3D12Resource* GetResource() const { return resource_.Get(); }

		// 読み戻し結果を参照で取得する
		const T& GetReadbackData() { return *mappedData_; }
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- variables ----------------------------------------------------

		ComPtr<ID3D12Resource> resource_;
		GraphicsResourceRetirement* retirement_ = nullptr;
		T* mappedData_ = nullptr;
	};

	//============================================================================
	//	DxReadbackBuffer templateMethods
	//============================================================================
	template<typename T>
	inline void DxReadbackBuffer<T>::CreateBuffer(GraphicsResourceRetirement& retirement, ID3D12Device* device) {

		if (retirement_ && retirement_ != &retirement) throw std::logic_error("Readback Bufferの回収先は変更できません");
		ComPtr<ID3D12Resource> candidate;
		DxUtils::CreateReadbackBufferResource(device, candidate, sizeof(T));
		T* mapped = nullptr;
		const HRESULT result = candidate->Map(0, nullptr, reinterpret_cast<void**>(&mapped));
		if (!DxDredDiagnostics::CheckHRESULT(device, result, "DxReadbackBuffer::Map")) {
			throw std::runtime_error("Readback BufferのMapに失敗しました");
		}
		// 読み戻し途中の旧ResourceはGPU完了まで保持する
		if (resource_) retirement.Retire(resource_);
		retirement_ = &retirement;
		resource_ = std::move(candidate);
		mappedData_ = mapped;
	}
}; // Engine
