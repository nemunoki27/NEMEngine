#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Platform/Windows/Win32Window.h>
#include <Engine/Core/Rendering/Core/RenderingPlatform.h>
#include <Engine/Core/Foundation/Math/Vector2.h>
#include <Engine/Core/Foundation/Math/Color.h>

namespace Engine {
	class RuntimeProjectSettings;

	//============================================================================
	//	EngineContext class
	//	エンジン全体の実行コンテキストを管理するクラス
	//============================================================================
	class EngineContext {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

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
			// 製品起動時にフルスクリーンへ切り替えるか
			bool startupFullscreen = false;
		};
		// グラフィックス設定
		struct GraphicsSetting {

			// 画面クリアカラー
			const float kWindowClearColor[4] = { 0.016f, 0.016f, 0.016f, 1.0f };
			Color4 clearColor;
			// SceneとGameの背景色
			Color4 sceneClearColor = Color4::FromHex(0x303030ff);
			// 描画フォーマット
			DXGI_FORMAT swapChainFormat = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
			DXGI_FORMAT renderTextureFormat = DXGI_FORMAT_R32G32B32A32_FLOAT;
		};
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		EngineContext() = default;
		~EngineContext() = default;

		// 初期化
		void Init(bool usesEditorUI);

		// 終了
		void Finalize();

		//--------- accessor -----------------------------------------------------

		// ウィンドウ設定の取得
		static const WindowSetting& GetWindowSetting() { return windowSetting_; }
		// Editorで使用するゲーム画像サイズを更新する
		static void SetGameSize(const Vector2I& size);
		// グラフィックス設定の取得
		static const GraphicsSetting& GetGraphicsSetting() { return graphicsSetting_; }
		// 全ユーザー共通の背景色を保存する
		static bool SetSceneClearColor(const Color4& color);
		// Projectへ保存された背景色を取得する
		static const Color4& GetProjectSceneClearColor() { return projectSceneClearColor_; }
		// Scriptの変更は実行中だけに反映する
		static bool SetRuntimeSceneClearColor(const Color4& color);
		// 背景色の範囲と有限値を確認する
		static bool IsSceneClearColorValid(const Color4& color);
		// ウィンドウ管理クラスの取得
		WinApp* GetWinApp() const { return winApp_.get(); }
		// GraphicsCoreが所有する実行中設定を参照する
		RuntimeProjectSettings* GetRuntimeSettings() const { return runtimeSettings_; }
		void SetRuntimeSettings(RuntimeProjectSettings* settings) { runtimeSettings_ = settings; }
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- variables ----------------------------------------------------

		// ウィンドウ設定
		static WindowSetting windowSetting_;
		// グラフィックス設定
		static GraphicsSetting graphicsSetting_;
		// ユーザー別設定に含めない共有色
		static Color4 projectSceneClearColor_;

		// ウィンドウ管理クラス
		std::unique_ptr<WinApp> winApp_;
		RuntimeProjectSettings* runtimeSettings_ = nullptr;

		//--------- functions ----------------------------------------------------

		// 各コア設定の初期化
		void InitCoreSettings(bool usesEditorUI);
		// Project単位の背景色を復元する
		void LoadRenderingSettings();
	};
} // Engine
