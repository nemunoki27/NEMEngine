#include "EngineContext.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Diagnostics/Assert.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonSerializer.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonFile.h>
#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>
#include <Engine/Core/Runtime/Paths/ConfigPaths.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>

// c++
#include <cmath>
#include <filesystem>
#include <system_error>

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
Engine::Color4 Engine::EngineContext::projectSceneClearColor_ = Color4::FromHex(0x303030ff);

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
	const nlohmann::json gameSettings = JsonAdapter::Load(
		RuntimePaths::GetProjectSettingsPath(ConfigPaths::kGameBuild), false);
	if (gameSettings.is_object()) {
		windowSetting_.gameSizeFloat.x = ReadSizeSetting(
			gameSettings, "gameWidth", windowSetting_.gameSizeFloat.x);
		windowSetting_.gameSizeFloat.y = ReadSizeSetting(
			gameSettings, "gameHeight", windowSetting_.gameSizeFloat.y);
	}
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
	LoadRenderingSettings();

	// 表示ウィンドウ作成
	winApp_ = std::make_unique<WinApp>();
	const Vector2I initialSize = usesEditorUI ? windowSetting_.engineSize : windowSetting_.gameSize;
	winApp_->Create(initialSize.ToUInt().front(), initialSize.ToUInt().back(), windowSetting_.title.c_str());
	if (windowSetting_.startupFullscreen) {
		WinApp::SetFullscreen(true);
	}
}

void Engine::EngineContext::Finalize() {

	// 各機能を破棄

	winApp_.reset();
}

void Engine::EngineContext::SetGameSize(const Vector2I& size) {

	windowSetting_.gameSize.x = (std::max)(size.x, 1);
	windowSetting_.gameSize.y = (std::max)(size.y, 1);
	windowSetting_.gameSizeFloat = windowSetting_.gameSize.GetFloat();
}

bool Engine::EngineContext::IsSceneClearColorValid(const Color4& color) {

	return std::isfinite(color.r) && std::isfinite(color.g) && std::isfinite(color.b) &&
		color.r >= 0.0f && color.r <= 1.0f && color.g >= 0.0f && color.g <= 1.0f && color.b >= 0.0f && color.b <= 1.0f;
}

bool Engine::EngineContext::SetRuntimeSceneClearColor(const Color4& color) {

	if (!IsSceneClearColorValid(color)) return false;
	graphicsSetting_.sceneClearColor = Color4(color.r, color.g, color.b, 1.0f);
	return true;
}

bool Engine::EngineContext::SetSceneClearColor(const Color4& color) {

	if (!IsSceneClearColorValid(color)) return false;
	const auto path = RuntimePaths::GetProjectSettingsPath(ConfigPaths::kRendering);
	nlohmann::json data = nlohmann::json::object();
	std::error_code error;
	const bool exists = std::filesystem::exists(path, error);
	if (error || (exists && (!JsonFile::TryLoad(path, data) || !data.is_object()))) return false;
	const Color4 value(color.r, color.g, color.b, 1.0f);
	data["clearColor"] = value.ToJson();
	// 保存に成功した色を共有値と実行値へ渡す
	if (!JsonFile::Save(path, data)) return false;
	projectSceneClearColor_ = value;
	return SetRuntimeSceneClearColor(value);
}

void Engine::EngineContext::LoadRenderingSettings() {

	projectSceneClearColor_ = Color4::FromHex(0x303030ff);
	const auto path = RuntimePaths::GetProjectSettingsPath(ConfigPaths::kRendering);
	nlohmann::json data;
	if (JsonFile::TryLoad(path, data) && data.is_object() && data.contains("clearColor")) {
		const auto& value = data["clearColor"];
		const bool valid = value.is_object() && value.contains("r") && value["r"].is_number() &&
			value.contains("g") && value["g"].is_number() && value.contains("b") && value["b"].is_number();
		if (valid) {
			const Color4 color(value["r"].get<float>(), value["g"].get<float>(), value["b"].get<float>(), 1.0f);
			if (IsSceneClearColorValid(color)) projectSceneClearColor_ = color;
		}
	}
	SetRuntimeSceneClearColor(projectSceneClearColor_);
}
