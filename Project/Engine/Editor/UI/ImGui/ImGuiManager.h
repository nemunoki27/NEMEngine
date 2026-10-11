#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Core/EditorShortcutState.h>

// c++
#include <cstdint>
#include <unordered_map>

// directX
#include <d3d12.h>

#include <Windows.h>

struct ImGui_ImplDX12_InitInfo;

namespace Engine {

	// 前方宣言
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

		// フレームを開始する
		void Begin();
		// 保持した表示切替要求を取り出す
		bool ConsumeHidePanelsShortcut();
		// 描画データを確定する
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

		//--------- variables ----------------------------------------------------

		// 外部Windowのメッセージ接続先
		static ImGuiManager* instance_;

		// 初期化済みか
		bool initialized_ = false;
		EditorShortcutState shortcutState_;
		// 初期化できた段階だけ終了する
		bool contextCreated_ = false;
		bool platformInitialized_ = false;
		bool rendererInitialized_ = false;
		bool gpuFailed_ = false;

		// Graphicsが所有するDescriptorの借用
		SRVDescriptor* srvDescriptor_ = nullptr;
		// Backendへ貸し出したDescriptor番号
		std::unordered_map<uint64_t, uint32_t> imguiSRVIndices_;
		// 外部Windowの差替え前の処理
		std::unordered_map<HWND, WNDPROC> platformWindowProcedures_;

		//--------- functions ----------------------------------------------------

		// 待機結果を保持し、描画処理から失敗を伝える
		static bool WaitForGPU(::ImGui_ImplDX12_InitInfo* info, ID3D12Fence* fence, UINT64 value, HANDLE event);
		// GPU処理の失敗を呼出し元へ伝える
		void CheckGPUFailure() const;

		// GPU使用中のBackend資源を回収窓口へ渡す
		static void RetireResource(::ImGui_ImplDX12_InitInfo* info, ID3D12Object* resource);
		// BackendへDescriptorを貸し出す
		static void AllocateSRVDescriptor(::ImGui_ImplDX12_InitInfo* info, D3D12_CPU_DESCRIPTOR_HANDLE* outCPUHandle,
			D3D12_GPU_DESCRIPTOR_HANDLE* outGPUHandle);
		// Backendから返されたDescriptorを回収する
		static void FreeSRVDescriptor(
			::ImGui_ImplDX12_InitInfo* info, D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle, D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle);

		// Descriptorを確保して使用番号を記録する
		void AllocateImGuiSRV(D3D12_CPU_DESCRIPTOR_HANDLE* outCPUHandle, D3D12_GPU_DESCRIPTOR_HANDLE* outGPUHandle);
		// 使用番号からDescriptorを回収窓口へ渡す
		void FreeImGuiSRV(D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle);
		// 新しく生成された外部ウィンドウへエディター処理を接続する
		void RegisterPlatformWindows();
		// 外部ウィンドウの元のプロシージャを復元する
		void RestorePlatformWindowProcedures();
		// 指定ウィンドウがエディターの管理対象か
		bool IsEditorWindow(HWND hwnd) const;
		// メインWindowの入力を受け取る
		static LRESULT ForwardWindowMessage(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);
		// 外部ウィンドウのメッセージを処理する
		static LRESULT CALLBACK PlatformWindowProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);
	};
}; // Engine
