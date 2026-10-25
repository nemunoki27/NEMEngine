#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Core/DisplayOutputState.h>
#include <Engine/Core/Rendering/Renderer/RenderTargets/RenderTexture2D.h>

// c++
#include <memory>
#include <unordered_map>

// imgui
#include <imgui.h>
#include <imgui_impl_dx12.h>

namespace Engine {

	class GraphicsCore;
	class RenderPipelineRunner;

	//============================================================================
	//	ImGuiDisplayOutput class
	// EditorのWindow別出力とHDR映像のUI合成を管理する
	//============================================================================
	class ImGuiDisplayOutput {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		// 描画基盤とBackendの出力窓口を接続する
		void Init(GraphicsCore& graphicsCore, ImGui_ImplDX12_InitInfo& info);
		// 次の描画に使用する希望設定を固定する
		void Begin();
		// 合成画像の出力先を接続する
		void SetRenderer(RenderPipelineRunner& renderer) { renderer_ = &renderer; }
		// メインWindowへUIを描画する
		void Draw(ID3D12GraphicsCommandList* commandList);
		// Windowの借用と合成資源を解除する
		void Finalize();
		// 指定Windowの実際の表示設定を取得する
		DisplayOutputSettings GetSettings(HWND hwnd) const;
		// View画像を映像の基準白で表示する
		static void DrawViewImage(ImTextureID texture, const ImVec2& size, float paperWhiteNits);
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- structure ----------------------------------------------------

		struct WindowOutput {

			DisplayOutputState state;
			DisplayOutputState previous;
			HWND hwnd = nullptr;
			std::unique_ptr<RenderTexture2D> composition;
		};

		//--------- variables ----------------------------------------------------

		GraphicsCore* graphicsCore_ = nullptr;
		RenderPipelineRunner* renderer_ = nullptr;
		DisplayOutputSettings requested_{};
		std::unordered_map<ImGuiID, WindowOutput> windows_;

		//--------- functions ----------------------------------------------------

		// 出力形式とUI輝度をBackendへ渡す
		static ImGui_ImplDX12_OutputConfig BuildConfig(const WindowOutput& window);
		// HDR10の合成画像だけを必要なサイズで保持する
		void SyncComposition(WindowOutput& window, uint32_t width, uint32_t height);
		// 指定WindowにUIを合成して出力する
		void DrawWindow(ImGuiViewport* viewport, ID3D12GraphicsCommandList* commandList, const RenderTarget& target);
		// Backendから表示設定の解決と適用を受け取る
		static bool ConfigureWindow(ImGui_ImplDX12_InitInfo* info, ImGuiViewport* viewport,
			IDXGISwapChain3* swapChain, ImGui_ImplDX12_OutputConfig* config, int operation);
		// 外部Windowの描画を受け取る
		static void RenderWindow(ImGui_ImplDX12_InitInfo* info, ImGuiViewport* viewport,
			ID3D12GraphicsCommandList* commandList, D3D12_CPU_DESCRIPTOR_HANDLE target);
		// 閉じたWindowの資源を回収する
		static void DestroyWindow(ImGui_ImplDX12_InitInfo* info, ImGuiViewport* viewport);
		// View画像の輝度だけを変更する
		static void SetImageWhiteLevel(const ImDrawList* drawList, const ImDrawCmd* command);
	};
} // Engine
