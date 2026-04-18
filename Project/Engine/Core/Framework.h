#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Graphics/GraphicsCore.h>
#include <Engine/Core/Runtime/EngineApplication.h>
#include <Engine/Core/Time/FrameTimer.h>

namespace Engine {

	//============================================================================
	//	Framework class
	//	全てのライフサイクルを管理する
	//============================================================================
	class Framework {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		Framework() = default;
		~Framework() = default;

		void Run();
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		// 処理が続いているか
		bool isRunning_ = false;

		// フレーム計測
		FrameTimer frameTimer_;

		// グラフィックス機能
		std::unique_ptr<GraphicsCore> graphicsCore_;

		// エンジンコアアプリケーション
		std::unique_ptr<EngineApplication> engineApplication_;

		//--------- functions ----------------------------------------------------

		// 初期化
		void Init();
		
		// フレーム更新
		void Tick();

		//  描画開始/終了処理
		void BeginRenderFrame();
		void EndRenderFrame();

		// 終了処理
		void Finalize();

		//--------- LeakChecker ----------------------------------------------------

		struct LeakChecker {

			~LeakChecker() {

				ComPtr<IDXGIDebug1> debug;
				if (SUCCEEDED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(debug.GetAddressOf())))) {

					debug->ReportLiveObjects(DXGI_DEBUG_ALL, DXGI_DEBUG_RLO_ALL);
					debug->ReportLiveObjects(DXGI_DEBUG_APP, DXGI_DEBUG_RLO_ALL);
					debug->ReportLiveObjects(DXGI_DEBUG_D3D12, DXGI_DEBUG_RLO_ALL);
				}
			}
		};
		LeakChecker leakChecker_;
	};
}; // Engine