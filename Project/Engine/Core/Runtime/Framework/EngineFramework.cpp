#include "EngineFramework.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>
#include <Engine/Core/Foundation/Diagnostics/Assert.h>
#include <Engine/Core/Platform/Input/InputSystem.h>
#include <Engine/Core/Platform/Windows/Win32Window.h>
#include <Engine/Core/Foundation/Time/FrameProfiler.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>
#include <Engine/Core/Audio/AudioSystem.h>
#include <Engine/Core/Foundation/Utility/Algorithm/UTFConversion.h>

// c++
#include <exception>
#include <stdexcept>
#include <cstdlib>
#include <cstdio>

using namespace Engine;

//============================================================================
//	Framework classMethods
//============================================================================
Framework::Framework(std::unique_ptr<IEngineApplication> application) :
	engineApplication_(std::move(application)) {

	// GPU初期化より前に必須Applicationを確認する
	if (!engineApplication_) {
		throw std::invalid_argument("Framework requires an Application");
	}
}

Framework::~Framework() = default;

void Framework::Run() {

	// 終了済みFrameworkは再利用しない
	if (!engineApplication_) {
		throw std::logic_error("Framework has already finished");
	}

	// COMの初期化が成功した場合だけ終了処理と対にする
	HRESULT result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
	if (FAILED(result)) {
		throw std::runtime_error("Framework COM initialization failed");
	}
	std::exception_ptr failure;
	try {
		Init();
		while (isRunning_) {
			Tick();
		}
	} catch (...) {
		failure = std::current_exception();
	}
	// InitとTickの失敗時も所有資源を先に終了する
	try {
		Finalize();
	} catch (...) {
		if (!failure) {
			failure = std::current_exception();
		}
	}
	CoUninitialize();
	if (failure) {
		std::rethrow_exception(failure);
	}
}

int Framework::ReportFailure(const char* detail) noexcept {

	// Logger終了後も診断を残し、例外を実行入口の外へ出さない
	try {
		const std::string message = "NEMEngineの実行に失敗しました\n\n" + std::string(detail ? detail : "不明なエラー");
		const auto wide = Algorithm::ConvertString(message);
		std::fprintf(stderr, "%s\n", message.c_str());
		OutputDebugStringW(wide.c_str());
		MessageBoxW(nullptr, wide.c_str(), L"NEMEngine 実行エラー", MB_OK | MB_ICONERROR);
	} catch (...) {
		OutputDebugStringW(L"NEMEngineのエラー詳細を作成できませんでした\n");
	}
	return EXIT_FAILURE;
}

void Framework::Init() {

	// ログファイルの作成
	Logger::CreateLogFiles(RuntimePaths::GetSavedPath("Logs"));

	// グラフィックス機能初期化
	graphicsCore_ = std::make_unique<GraphicsCore>();
	graphicsCore_->Init(engineApplication_->UsesEditorUI());

	// 入力機能初期化
	Input::GetInstance()->Init(graphicsCore_->GetContext().GetWinApp());

	applicationStarted_ = true;
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
	if (!engineApplication_->UsesEditorUI()) {

		// 製品実行中はF11でウィンドウとフルスクリーンを切り替える
		if (Input::GetInstance()->TriggerKey(DIK_F11)) {
			WinApp::SetFullscreen(!WinApp::IsFullscreen());
		}
	}
	// 実際の入力操作から入力タイプを更新し、マウス範囲制御も適用する
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

	isRunning_ = false;
	std::exception_ptr failure;
	// 一つの終了処理が失敗しても残りの所有者を終了する
	auto cleanup = [&failure](auto&& action) {
		try {
			action();
		} catch (...) {
			if (!failure) {
				failure = std::current_exception();
			}
		}
	};
	cleanup([this]() {
		if (applicationStarted_) {
			engineApplication_->Finalize();
		}
	});
	// Applicationの借用先は所有者を破棄するまで残す
	engineApplication_.reset();
	applicationStarted_ = false;
	cleanup([]() { Input::Finalize(); });
	cleanup([]() { Audio::Finalize(); });
	cleanup([this]() {
		if (graphicsCore_) {
			graphicsCore_->Finalize();
		}
	});
	graphicsCore_.reset();
	cleanup([]() { Logger::Finalize(); });
	if (failure) {
		std::rethrow_exception(failure);
	}
}

//============================================================================
//	EngineFramework classMethods
//============================================================================

namespace Engine {

	Framework::LeakChecker::~LeakChecker() {

		ComPtr<IDXGIDebug1> debug;
		if (SUCCEEDED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(debug.GetAddressOf())))) {

			debug->ReportLiveObjects(DXGI_DEBUG_ALL, DXGI_DEBUG_RLO_ALL);
			debug->ReportLiveObjects(DXGI_DEBUG_APP, DXGI_DEBUG_RLO_ALL);
			debug->ReportLiveObjects(DXGI_DEBUG_D3D12, DXGI_DEBUG_RLO_ALL);
		}
	}
}
