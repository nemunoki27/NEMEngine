#include "EngineContext.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Debug/Assert.h>
#include <Engine/Utility/Algorithm/Algorithm.h>
#include <Engine/Utility/Json/JsonAdapter.h>
#include <Engine/Utility/Enum/EnumAdapter.h>

//============================================================================
//	EngineContext classMethods
//============================================================================

void Engine::EngineContext::InitCoreSettings() {

	nlohmann::json data = JsonAdapter::Load("./Engine/Assets/Window/windowSetting.json");
	// ウィンドウ設定
	std::string windowTitle = data["WindowTitle"];
	windowSetting_.title = Algorithm::ConvertString(windowTitle);
	// エンジンウィンドウサイズ
	windowSetting_.engineSizeFloat.x = data.value("EngineWindowSizeX", 1920.0f);
	windowSetting_.engineSizeFloat.y = data.value("EngineWindowSizeY", 1080.0f);
	windowSetting_.engineSize.x = static_cast<uint32_t>(windowSetting_.engineSizeFloat.x);
	windowSetting_.engineSize.y = static_cast<uint32_t>(windowSetting_.engineSizeFloat.y);
	// ゲームウィンドウサイズ
	windowSetting_.gameSizeFloat.x = data.value("GameWindowSizeX", 1920.0f);
	windowSetting_.gameSizeFloat.y = data.value("GameWindowSizeY", 1080.0f);
	windowSetting_.gameSize.x = static_cast<uint32_t>(windowSetting_.gameSizeFloat.x);
	windowSetting_.gameSize.y = static_cast<uint32_t>(windowSetting_.gameSizeFloat.y);
	// グラフィックス設定
	graphicsSetting_.swapChainFormat = EnumAdapter<DXGI_FORMAT>::FromString(data["SwapChainDXFORMAT"]).value();
	graphicsSetting_.renderTextureFormat = EnumAdapter<DXGI_FORMAT>::FromString(data["RenderTextureDXFORMAT"]).value();
	graphicsSetting_.clearColor = Color(graphicsSetting_.kWindowClearColor[0], graphicsSetting_.kWindowClearColor[1],
		graphicsSetting_.kWindowClearColor[2], graphicsSetting_.kWindowClearColor[3]);
}

void Engine::EngineContext::Init() {

	// 各コア設定の初期化
	InitCoreSettings();

	// 表示ウィンドウ作成
	winApp_ = std::make_unique<WinApp>();
	winApp_->Create(windowSetting_.engineSize.ToUInt().front(), windowSetting_.engineSize.ToUInt().back(), windowSetting_.title.c_str());
}

void Engine::EngineContext::Finalize() {

	// 各機能を破棄

	winApp_.reset();
}