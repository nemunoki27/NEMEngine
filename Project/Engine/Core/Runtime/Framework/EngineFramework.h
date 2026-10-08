#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Time/FrameTimer.h>

// c++
#include <memory>
#include <filesystem>

namespace Engine {

	class GraphicsCore;

	//============================================================================
	//	IEngineApplication class
	//	Frameworkへ注入するEditor/Runtime共通ライフサイクル
	//============================================================================
	class IEngineApplication {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		virtual ~IEngineApplication() = default;

		virtual void Init(GraphicsCore& graphicsCore) = 0;
		virtual void Tick(GraphicsCore& graphicsCore, float deltaTime) = 0;
		virtual void Render(GraphicsCore& graphicsCore) = 0;
		virtual void RenderPlatformWindows(GraphicsCore& graphicsCore) = 0;
		virtual void Finalize() = 0;
		virtual bool ConsumeFrameDeltaResetRequest() = 0;
		virtual bool UsesEditorUI() const = 0;
	};

	//============================================================================
	//	Framework class
	//	全てのライフサイクルを管理する
	//============================================================================
	class Framework {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		explicit Framework(std::unique_ptr<IEngineApplication> application);
		~Framework();

		void Run();
		// 終了処理後に実行エラーを通知する
		static int ReportFailure(const char* detail) noexcept;
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- structure ----------------------------------------------------

		struct LeakChecker {

			~LeakChecker();
		};

		//--------- variables ----------------------------------------------------

		// 処理が続いているか
		bool isRunning_ = false;
		// Applicationの初期化を開始したか
		bool applicationStarted_ = false;
		// 製品実行の明示計測先と通常フレームでの結果回収猶予
		std::filesystem::path profileCapturePath_;
		uint32_t profileCaptureDrainFrames_ = 0;

		// フレーム計測
		FrameTimer frameTimer_;

		// グラフィックス機能
		std::unique_ptr<GraphicsCore> graphicsCore_;

		// エンジンコアアプリケーション
		std::unique_ptr<IEngineApplication> engineApplication_;
		LeakChecker leakChecker_;

		//--------- functions ----------------------------------------------------

		// 初期化
		void Init();

		// フレーム更新
		void Tick();

		// 描画開始/終了処理
		void BeginRenderFrame();
		void EndRenderFrame();

		// 終了処理
		void Finalize();
		// 環境設定で指定された製品計測を開始する
		void InitProfileCapture();
		// 通常の描画で取得した結果を保存する
		void UpdateProfileCapture();
		void SaveProfileCapture();

	};
}; // Engine
