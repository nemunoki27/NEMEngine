#include "EngineFramework.h"

using namespace Engine;

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Diagnostics/Log.h>
#include <Engine/Core/Platform/Input/InputSystem.h>
#include <Engine/Core/Foundation/Time/FrameProfiler.h>

//============================================================================
//	Framework classMethods
//============================================================================
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
	Logger::CreateLogFiles();

	// グラフィックス機能初期化
	graphicsCore_ = std::make_unique<GraphicsCore>();
	graphicsCore_->Init();

	// 入力機能初期化
	Input::GetInstance()->Init(graphicsCore_->GetContext().GetWinApp());

	// エンジンアプリケーション初期化
	engineApplication_ = std::make_unique<EngineApplication>();
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

	// 時間更新
	frameTimer_.Update();
	// プロファイラのフレーム開始
	FrameProfiler::GetInstance().BeginFrame(frameTimer_.GetDeltaTime(), frameTimer_.GetTotalTime());

	// 入力更新
	Input::GetInstance()->Update();

	// エンジン機能更新
	{
		FrameProfiler::ScopedSample updateSample(FrameProfiler::Category::Update);
		engineApplication_->Tick(*graphicsCore_, frameTimer_.GetDeltaTime());
	}
	if (engineApplication_->ConsumeFrameDeltaResetRequest()) {
		frameTimer_.ResetDeltaTimeBase();
	}

	// 描画(開始～終了までを計測)
	{
		FrameProfiler::ScopedSample drawSample(FrameProfiler::Category::Draw);

		// 描画開始
		BeginRenderFrame();

		// 描画
		engineApplication_->Render(*graphicsCore_);

		// 描画終了
		EndRenderFrame();
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
