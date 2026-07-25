#include "EngineContext.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Diagnostics/Assert.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonSerializer.h>
#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>
#include <Engine/Core/Foundation/Build/BuildConfig.h>
#include <Engine/Core/Runtime/Paths/ConfigPaths.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>

//============================================================================
//	EngineContext classMethods
//============================================================================
Engine::EngineContext::WindowSetting Engine::EngineContext::windowSetting_ = {};
Engine::EngineContext::GraphicsSetting Engine::EngineContext::graphicsSetting_ = {};

void Engine::EngineContext::InitCoreSettings() {

	nlohmann::json data = JsonAdapter::Load(
		RuntimePaths::GetEngineAssetPath("Window/windowSetting.json"));
	// ウィンドウ設定
	std::string windowTitle = data["WindowTitle"];
	windowSetting_.title = Algorithm::ConvertString(windowTitle);
	windowSetting_.startupFullscreen = false;

	if constexpr (!BuildConfig::kEditorEnabled) {

		// 製品ビルドではビルド設定の製品名と起動状態を優先する
		const nlohmann::json gameBuild = JsonAdapter::Load(
			RuntimePaths::GetGameConfigPath(ConfigPaths::kGameBuild), false);
		if (gameBuild.is_object()) {

			const std::string gameName = gameBuild.value("gameName", std::string{});
			if (!gameName.empty()) {
				windowSetting_.title = Algorithm::ConvertString(gameName);
			}
			windowSetting_.startupFullscreen = gameBuild.value("startupFullscreen", false);
		}
	}
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
	graphicsSetting_.clearColor = Color4(graphicsSetting_.kWindowClearColor[0], graphicsSetting_.kWindowClearColor[1],
		graphicsSetting_.kWindowClearColor[2], graphicsSetting_.kWindowClearColor[3]);
}

void Engine::EngineContext::Init() {

	// 各コア設定の初期化
	InitCoreSettings();

	// 表示ウィンドウ作成
	winApp_ = std::make_unique<WinApp>();
	winApp_->Create(windowSetting_.engineSize.ToUInt().front(), windowSetting_.engineSize.ToUInt().back(), windowSetting_.title.c_str());
	if (windowSetting_.startupFullscreen) {
		WinApp::SetFullscreen(true);
	}
}

void Engine::EngineContext::Finalize() {

	// 各機能を破棄

	winApp_.reset();
}
