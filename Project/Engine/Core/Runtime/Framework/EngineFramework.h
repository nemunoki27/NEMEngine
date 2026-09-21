#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Foundation/Time/FrameTimer.h>

namespace Engine {

	//============================================================================
	//	IEngineApplication class
	//	Frameworkへ注入するEditor/Runtime共通ライフサイクル
	//============================================================================
	class IEngineApplication {
	public:

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
		~Framework() = default;

		void Run();
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- variables ----------------------------------------------------

		// 処理が続いているか
		bool isRunning_ = false;

		// フレーム計測
		FrameTimer frameTimer_;

		// グラフィックス機能
		std::unique_ptr<GraphicsCore> graphicsCore_;

		// エンジンコアアプリケーション
		std::unique_ptr<IEngineApplication> engineApplication_;

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

		//--------- LeakChecker ----------------------------------------------------

		struct LeakChecker {

			~LeakChecker();
		};
		LeakChecker leakChecker_;
	};
}; // Engine
