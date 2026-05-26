#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Platform/Windows/Win32Window.h>
#include <Engine/Core/Rendering/Core/RenderingPlatform.h>
#include <Engine/Core/Foundation/Math/Vector2.h>
#include <Engine/Core/Foundation/Math/Color.h>

namespace Engine {

	//============================================================================
	//	EngineContext class
	//	エンジン全体の実行コンテキストを管理するクラス
	//============================================================================
	class EngineContext {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		//--------- structure ----------------------------------------------------

		// ウィンドウ設定
		struct WindowSetting {

			// タイトル
			std::wstring title;

			// エンジン表示サイズ
			Vector2 engineSizeFloat{};
			Vector2I engineSize{};
			// ゲーム表示サイズ
			Vector2 gameSizeFloat{};
			Vector2I gameSize{};
		};
		// グラフィックス設定
		struct GraphicsSetting {

			// 画面クリアカラー
			const float kWindowClearColor[4] = { 0.016f, 0.016f, 0.016f, 1.0f };
			Color4 clearColor;
			// 描画フォーマット
			DXGI_FORMAT swapChainFormat;
			DXGI_FORMAT renderTextureFormat;
		};
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		EngineContext() = default;
		~EngineContext() = default;

		// 初期化
		void Init();

		// 終了
		void Finalize();

		//--------- accessor -----------------------------------------------------

		// ウィンドウ設定の取得
		static const WindowSetting& GetWindowSetting() { return windowSetting_; }
		// グラフィックス設定の取得
		static const GraphicsSetting& GetGraphicsSetting() { return graphicsSetting_; }
		// ウィンドウ管理クラスの取得
		WinApp* GetWinApp() const { return winApp_.get(); }
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		// ウィンドウ設定
		static WindowSetting windowSetting_;
		// グラフィックス設定
		static GraphicsSetting graphicsSetting_;

		// ウィンドウ管理クラス
		std::unique_ptr<WinApp> winApp_;

		//--------- functions ----------------------------------------------------

		// 各コア設定の初期化
		void InitCoreSettings();
	};
} // Engine