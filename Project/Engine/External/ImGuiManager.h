#pragma once

//============================================================================
//	include
//============================================================================

// windows
#include <Windows.h>
// directX
#include <d3d12.h>
// c++
#include <cstdint>
#include <string>

namespace Engine {

	// front
	class SRVDescriptor;

	//============================================================================
	//	ImGuiManager class
	//	ImGuiの管理クラス、Debug、Developのみで機能する
	//============================================================================
	class ImGuiManager {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		ImGuiManager() = default;
		~ImGuiManager() = default;

		// ImGui機能の初期化
		void Init(HWND hwnd, UINT bufferCount, ID3D12Device* device, ID3D12CommandQueue* commandQueue,
			SRVDescriptor* srvDescriptor, DXGI_FORMAT rtvFormat, DXGI_FORMAT dsvFormat);

		// フレーム開始、終了
		void Begin();
		void End();

		// 描画
		void Draw(ID3D12GraphicsCommandList* commandList);

		// 終了処理
		void Finalize();
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		// 初期化済みか
		bool initialized_ = false;

		SRVDescriptor* srvDescriptor_ = nullptr;
	};
}; // Engine