#include "EngineContext.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Diagnostics/Assert.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonSerializer.h>
#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>
#include <Engine/Core/Runtime/Paths/ConfigPaths.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>

// c++
#include <cmath>

namespace {

	// 文字列設定を安全に取得
	std::string ReadStringSetting(const nlohmann::json& data, const char* key,
		std::string_view fallback) {

		auto found = data.find(key);
		if (found != data.end() && found->is_string()) {
			return found->get<std::string>();
		}
		Engine::Logger::Output(Engine::LogType::Engine, spdlog::level::warn,
			"エンジン設定{}が不正なため既定値を使用します", key);
		return std::string(fallback);
	}

	// 正の数値設定を安全に取得
	float ReadSizeSetting(const nlohmann::json& data, const char* key,
		float fallback) {

		auto found = data.find(key);
		if (found != data.end() && found->is_number()) {
			const float value = found->get<float>();
			if (std::isfinite(value) && 1.0f <= value) {
				return value;
			}
		}
		Engine::Logger::Output(Engine::LogType::Engine, spdlog::level::warn,
			"エンジン設定{}が不正なため既定値{}を使用します", key, fallback);
		return fallback;
	}

	bool ReadBoolSetting(const nlohmann::json& data, const char* key,
		bool fallback) {

		auto found = data.find(key);
		if (found == data.end()) {
			return fallback;
		}
		if (found->is_boolean()) {
			return found->get<bool>();
		}
		Engine::Logger::Output(Engine::LogType::Engine, spdlog::level::warn,
			"エンジン設定{}が不正なため既定値を使用します", key);
		return fallback;
	}

	// DXGI形式設定を安全に取得
	DXGI_FORMAT ReadFormatSetting(const nlohmann::json& data, const char* key,
		DXGI_FORMAT fallback) {

		const std::string name = ReadStringSetting(data, key,
			Engine::EnumAdapter<DXGI_FORMAT>::ToString(fallback));
		const std::optional<DXGI_FORMAT> format =
			Engine::EnumAdapter<DXGI_FORMAT>::FromString(name);
		if (format) {
			return *format;
		}
		Engine::Logger::Output(Engine::LogType::Engine, spdlog::level::warn,
			"エンジン設定{}のDXGI形式{}が不正なため既定値を使用します", key, name);
		return fallback;
	}
}

//============================================================================
//	EngineContext classMethods
//============================================================================
Engine::EngineContext::WindowSetting Engine::EngineContext::windowSetting_ = {};
Engine::EngineContext::GraphicsSetting Engine::EngineContext::graphicsSetting_ = {};

void Engine::EngineContext::InitCoreSettings(bool usesEditorUI) {

	nlohmann::json data = JsonAdapter::Load(
		RuntimePaths::GetEngineAssetPath("Config/windowSettings.exeConfig.json"));
	// ウィンドウ設定
	std::string windowTitle = ReadStringSetting(data, "WindowTitle", "Engine");
	windowSetting_.title = Algorithm::ConvertString(windowTitle);
	windowSetting_.startupFullscreen = false;

	if (!usesEditorUI) {

		// 製品実行ではビルド設定の製品名と起動状態を優先する
		const nlohmann::json gameBuild = JsonAdapter::Load(
			RuntimePaths::GetProjectSettingsPath(ConfigPaths::kGameBuild), false);
		if (gameBuild.is_object()) {

			const std::string gameName = ReadStringSetting(gameBuild, "gameName", {});
			if (!gameName.empty()) {
				windowSetting_.title = Algorithm::ConvertString(gameName);
			}
			windowSetting_.startupFullscreen = ReadBoolSetting(
				gameBuild, "startupFullscreen", false);
		}
	}
	// エンジンウィンドウサイズ
	windowSetting_.engineSizeFloat.x = ReadSizeSetting(data, "EngineWindowSizeX", 1920.0f);
	windowSetting_.engineSizeFloat.y = ReadSizeSetting(data, "EngineWindowSizeY", 1080.0f);
	windowSetting_.engineSize.x = static_cast<uint32_t>(windowSetting_.engineSizeFloat.x);
	windowSetting_.engineSize.y = static_cast<uint32_t>(windowSetting_.engineSizeFloat.y);
	// ゲームウィンドウサイズ
	windowSetting_.gameSizeFloat.x = ReadSizeSetting(data, "GameWindowSizeX", 1920.0f);
	windowSetting_.gameSizeFloat.y = ReadSizeSetting(data, "GameWindowSizeY", 1080.0f);
	windowSetting_.gameSize.x = static_cast<uint32_t>(windowSetting_.gameSizeFloat.x);
	windowSetting_.gameSize.y = static_cast<uint32_t>(windowSetting_.gameSizeFloat.y);
	// グラフィックス設定
	graphicsSetting_.swapChainFormat = ReadFormatSetting(data, "SwapChainDXFORMAT",
		DXGI_FORMAT_R8G8B8A8_UNORM_SRGB);
	graphicsSetting_.renderTextureFormat = ReadFormatSetting(data, "RenderTextureDXFORMAT",
		DXGI_FORMAT_R32G32B32A32_FLOAT);
	graphicsSetting_.clearColor = Color4(graphicsSetting_.kWindowClearColor[0], graphicsSetting_.kWindowClearColor[1],
		graphicsSetting_.kWindowClearColor[2], graphicsSetting_.kWindowClearColor[3]);
}

void Engine::EngineContext::Init(bool usesEditorUI) {

	// 各コア設定の初期化
	InitCoreSettings(usesEditorUI);

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
