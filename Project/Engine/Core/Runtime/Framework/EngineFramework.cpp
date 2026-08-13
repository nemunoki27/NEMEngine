#include "EngineFramework.h"

using namespace Engine;

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Diagnostics/Log.h>
#include <Engine/Core/Foundation/Diagnostics/Assert.h>
#include <Engine/Core/Foundation/Build/BuildConfig.h>
#include <Engine/Core/Platform/Input/InputSystem.h>
#include <Engine/Core/Platform/Windows/Win32Window.h>
#include <Engine/Core/Foundation/Time/FrameProfiler.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>

//============================================================================
//	Framework classMethods
//============================================================================
Framework::Framework(std::unique_ptr<IEngineApplication> application) :
	engineApplication_(std::move(application)) {
}

void Framework::Run() {

	// Comオブジェクト初期化
	CoInitializeEx(nullptr, COINIT_MULTITHREADED);

	// 初期化
	Init();

	// メインループ
	while (isRunning_) {

		Tick();
	}

	// 終了処理
	Finalize();

	// Comオブジェクト終了
	CoUninitialize();
}

void Framework::Init() {

	// ログファイルの作成
	Logger::CreateLogFiles(RuntimePaths::GetSavedPath("Logs"));

	// グラフィックス機能初期化
	graphicsCore_ = std::make_unique<GraphicsCore>();
	graphicsCore_->Init(engineApplication_->UsesEditorUI());

	// 入力機能初期化
	Input::GetInstance()->Init(graphicsCore_->GetContext().GetWinApp());

	Assert::Call(engineApplication_ != nullptr, "FrameworkへApplicationを設定してください");
	engineApplication_->Init(*graphicsCore_);

	// フレーム初期化
	frameTimer_.Init();
	// メインループ開始
	isRunning_ = true;
}

void Framework::Tick() {

	// メッセージ処理、ウィンドウが閉じられたらループ終了
	if (graphicsCore_->GetProcessMessage()) {
		isRunning_ = false;
		return;
	}
	// ウィンドウ変更を描画と入力更新より前に反映する
	graphicsCore_->SyncWindowSize();

	// 時間更新
	frameTimer_.Update();
	// プロファイラのフレーム開始
	FrameProfiler::GetInstance().BeginFrame(frameTimer_.GetDeltaTime(), frameTimer_.GetTotalTime());

	// 入力更新
	Input::GetInstance()->Update();
	if constexpr (!BuildConfig::kEditorEnabled) {

		// 製品実行中はF11でウィンドウとフルスクリーンを切り替える
		if (Input::GetInstance()->TriggerKey(DIK_F11)) {
			WinApp::SetFullscreen(!WinApp::IsFullscreen());
		}
	}
	// 検知トリガから入力タイプを自動更新し、マウス範囲制御も適用する
	Input::GetInstance()->UpdateInputDevice();

	// エンジン機能更新
	{
		FrameProfiler::ScopedSample updateSample(FrameProfiler::Category::Update);
		engineApplication_->Tick(*graphicsCore_, frameTimer_.GetDeltaTime());
	}

	// 描画(開始～終了までを計測)
	{
		FrameProfiler::ScopedSample drawSample(FrameProfiler::Category::Draw);

		// 描画開始
		BeginRenderFrame();

		// 描画
		engineApplication_->Render(*graphicsCore_);

		// メイン描画を先に提出して外部ウィンドウから同一フレームの結果を参照する
		graphicsCore_->SubmitRenderFrame();
		engineApplication_->RenderPlatformWindows(*graphicsCore_);

		// 描画終了
		EndRenderFrame();
	}
	// Play開始やシーン切り替え直後の初回描画時間を、次フレームのdeltaTimeへ含めない
	if (engineApplication_->ConsumeFrameDeltaResetRequest()) {
		frameTimer_.ResetDeltaTimeBase();
	}
}

void Framework::BeginRenderFrame() {

	// 描画開始処理
	graphicsCore_->BeginRenderFrame();
}

void Framework::EndRenderFrame() {

	// 描画終了処理
	graphicsCore_->EndRenderFrame();
}

void Framework::Finalize() {

	// 終了処理
	if (engineApplication_) {
		engineApplication_->Finalize();
		engineApplication_.reset();
	}
	Input::Finalize();
	if (graphicsCore_) {
		graphicsCore_->Finalize();
		graphicsCore_.reset();
	}
	Logger::Finalize();
}
