#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Graphics/DxLib/ComPtr.h>

// directX
#include <d3d12.h>
#include <dxgi1_6.h>
// c++
#include <string>
#include <cassert>
#include <cstdint>

namespace Engine {

	//============================================================================
	//	DxDevice class
	//	DXGIファクトリ/アダプタ選定とD3D12デバイス生成を行う
	//============================================================================
	class DxDevice {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		DxDevice() = default;
		~DxDevice() = default;

		// DXGIファクトリ作成→アダプタ列挙/選定→D3D12デバイス生成を行う
		void Create();

		//--------- accessor -----------------------------------------------------

		ID3D12Device8* Get() const { return device_.Get(); }
		IDXGIFactory7* GetDxgiFactory() const { return dxgiFactory_.Get(); }

		const std::string& GetAdapterName() const { return adapterName_; }
		uint64_t GetDedicatedVideoMemoryBytes() const { return dedicatedVideoMemoryBytes_; }
		D3D_FEATURE_LEVEL GetFeatureLevel() const { return featureLevel_; }
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		ComPtr<ID3D12Device8> device_;
		ComPtr<IDXGIFactory7> dxgiFactory_;
		ComPtr<IDXGIAdapter4> useAdapter_;

		std::string adapterName_{};
		uint64_t dedicatedVideoMemoryBytes_ = 0;
		D3D_FEATURE_LEVEL featureLevel_ = D3D_FEATURE_LEVEL_11_0;

		//--------- functions ----------------------------------------------------

		// 内部ヘルパ: ワイド文字列をUTF-8へ変換する
		std::string WStringToString(const std::wstring& wstr);
	};

}; // Engine
