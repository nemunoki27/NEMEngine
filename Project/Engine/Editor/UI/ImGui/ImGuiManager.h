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
#include <unordered_map>

struct ImGui_ImplDX12_InitInfo;

namespace Engine {

	// front
	class SRVDescriptor;

	//============================================================================
	//	ImGuiManager class
	//	ImGuiの管理クラス、Debug、Developのみで機能する
	//============================================================================
	class ImGuiManager {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

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
		// ネイティブ外部ウィンドウを描画してPresentする
		void DrawPlatformWindows();

		// 終了処理
		void Finalize();
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		static void AllocateSRVDescriptor(::ImGui_ImplDX12_InitInfo* info,
			D3D12_CPU_DESCRIPTOR_HANDLE* outCPUHandle, D3D12_GPU_DESCRIPTOR_HANDLE* outGPUHandle);
		static void FreeSRVDescriptor(::ImGui_ImplDX12_InitInfo* info,
			D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle, D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle);

		void AllocateImGuiSRV(D3D12_CPU_DESCRIPTOR_HANDLE* outCPUHandle,
			D3D12_GPU_DESCRIPTOR_HANDLE* outGPUHandle);
		void FreeImGuiSRV(D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle);
		// 新しく生成された外部ウィンドウへエディター処理を接続する
		void RegisterPlatformWindows();
		// 外部ウィンドウの元のプロシージャを復元する
		void RestorePlatformWindowProcedures();
		// 指定ウィンドウがエディターの管理対象か
		bool IsEditorWindow(HWND hwnd) const;
		// 外部ウィンドウのメッセージを処理する
		static LRESULT CALLBACK PlatformWindowProc(HWND hwnd, UINT message,
			WPARAM wparam, LPARAM lparam);

		//--------- variables ----------------------------------------------------

		static ImGuiManager* instance_;

		// 初期化済みか
		bool initialized_ = false;

		SRVDescriptor* srvDescriptor_ = nullptr;
		std::unordered_map<uint64_t, uint32_t> imguiSRVIndices_;
		std::unordered_map<HWND, WNDPROC> platformWindowProcedures_;
	};
}; // Engine
