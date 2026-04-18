#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Window/WinApp.h>
#include <Engine/Core/Graphics/GraphicsPlatform.h>
#include <Engine/MathLib/Vector2.h>
#include <Engine/MathLib/Color.h>

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
			Color clearColor;
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
		const WindowSetting& GetWindowSetting() const { return windowSetting_; }
		// グラフィックス設定の取得
		const GraphicsSetting& GetGraphicsSetting() const { return graphicsSetting_; }
		// ウィンドウ管理クラスの取得
		WinApp* GetWinApp() const { return winApp_.get(); }
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		// ウィンドウ設定
		WindowSetting windowSetting_;
		// グラフィックス設定
		GraphicsSetting graphicsSetting_;

		// ウィンドウ管理クラス
		std::unique_ptr<WinApp> winApp_;

		//--------- functions ----------------------------------------------------

		// 各コア設定の初期化
		void InitCoreSettings();
	};
} // Engine