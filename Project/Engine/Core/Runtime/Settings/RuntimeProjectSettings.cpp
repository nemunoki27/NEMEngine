#include "RuntimeProjectSettings.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/Core/GraphicsPreferenceStorage.h>
#include <Engine/Core/Foundation/Time/FrameRateSettings.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonFile.h>
#include <Engine/Core/Foundation/Serialization/StorageFileUtility.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>
#include <Engine/Core/Platform/Input/InputSystem.h>
#include <Engine/Core/Audio/AudioSystem.h>
#include <Engine/Core/Physics/Collision/CollisionSettings.h>

// c++
#include <algorithm>
#include <cmath>

//============================================================================
//	RuntimeProjectSettings classMethods
//============================================================================
Engine::RuntimeProjectSettings::RuntimeProjectSettings(GraphicsCore& graphics, bool editor) : graphics_(graphics), editor_(editor) {
}

void Engine::RuntimeProjectSettings::Initialize() {

	defaults_ = Capture();
	// Editorの表示設定はPlayを開始するまで変更しない
	if (!editor_) LoadUserSettings();
}

nlohmann::json Engine::RuntimeProjectSettings::Capture() const {

	auto data = GraphicsPreferenceStorage::Write(graphics_.GetDXObject().GetFeatureController().GetPreferences());
	data["sceneClearColor"] = EngineContext::GetGraphicsSetting().sceneClearColor.ToJson();
	data["targetFrameRate"] = FrameRateSettings::GetInstance().GetTargetFps();
	data["runInBackground"] = Input::GetInstance()->IsBackgroundInputEnabled();
	const auto size = graphics_.GetContext().GetWindowSetting().gameSize;
	data["width"] = size.x;
	data["height"] = size.y;
	const auto& input = *Input::GetInstance();
	data["inputDeadZone"] = input.GetDeadZone() / input.GetMaxStickValue();
	data["playerGamepads"] = nlohmann::json::array();
	data["playerKeyboardMouse"] = nlohmann::json::array();
	for (uint32_t player = 0; player < Input::kMaxPlayers; ++player) {
		data["playerGamepads"].push_back(input.GetPlayerGamepadIndex(player));
		data["playerKeyboardMouse"].push_back(input.IsPlayerKeyboardMouseEnabled(player));
	}
	data["audioPlayInBackground"] = Audio::GetInstance()->IsPlayInBackgroundEnabled();
	auto& collision = CollisionSettings::GetInstance();
	collision.EnsureLoaded();
	data["queriesHitTriggers"] = collision.GetQueriesHitTriggers();
	data["collisionPairs"] = collision.GetMatrixRows();
	return data;
}

nlohmann::json Engine::RuntimeProjectSettings::Read() const {

	return hasPending_ ? pending_ : Capture();
}

nlohmann::json Engine::RuntimeProjectSettings::ReadEffective() const {

	const auto& controller = graphics_.GetDXObject().GetFeatureController();
	const auto& support = controller.GetSupport();
	const auto& features = controller.GetRuntimeFeatures();
	return {{"meshShaderSupported", support.SupportsMeshShaderPath()}, {"rayTracingSupported", support.SupportsRayTracingPath()},
		{"inlineRayTracingSupported", support.SupportsInlineRayTracingPath()}, {"meshShaderEnabled", features.useMeshShader},
		{"rayTracingShadowsEnabled", features.useInlineRayTracing}, {"rayTracingReflectionsEnabled", features.useDispatchRays},
		{"doubleBufferingEnabled", GraphicsFrameState::GetActiveCount() == 2},
		{"displayOutputMode", static_cast<int>(graphics_.GetDisplayOutputSettings().mode)},
		{"presentTargetFrameRate", FrameRateSettings::GetInstance().GetPresentTargetFps()}};
}

bool Engine::RuntimeProjectSettings::Request(const nlohmann::json& settings) {

	if (!settings.is_object()) return false;
	auto candidate = Read();
	// 型の違う入力と未知の設定は公開前に拒否する
	for (auto it = settings.begin(); it != settings.end(); ++it) {
		if (!candidate.contains(it.key())) return false;
		const auto& old = candidate[it.key()];
		if ((old.is_boolean() && !it->is_boolean()) || (old.is_string() && !it->is_string()) ||
			(old.is_number_integer() && !it->is_number_integer()) ||
			(old.is_number() && (!it->is_number() || !std::isfinite(it->get<double>())))) return false;
		candidate[it.key()] = it.value();
	}
	const auto& clearColor = candidate["sceneClearColor"];
	if (!clearColor.is_object()) return false;
	for (const auto* channel : {"r", "g", "b"}) {
		if (!clearColor.contains(channel) || !clearColor[channel].is_number()) return false;
	}
	const Color4 color(clearColor["r"].get<float>(), clearColor["g"].get<float>(), clearColor["b"].get<float>(), 1.0f);
	if (!EngineContext::IsSceneClearColorValid(color)) return false;
	candidate["sceneClearColor"] = color.ToJson();
	if (candidate["targetFrameRate"].get<int64_t>() < 0 || candidate["targetFrameRate"].get<int64_t>() > 1000) return false;
	if (candidate["frameContextCount"] != 1 && candidate["frameContextCount"] != 2) return false;
	if (!candidate["inputDeadZone"].is_number() || candidate["inputDeadZone"].get<float>() < 0 ||
		candidate["inputDeadZone"].get<float>() >= 1) return false;
	if (!candidate["playerGamepads"].is_array() || candidate["playerGamepads"].size() != Input::kMaxPlayers ||
		!candidate["playerKeyboardMouse"].is_array() || candidate["playerKeyboardMouse"].size() != Input::kMaxPlayers) return false;
	for (uint32_t player = 0; player < Input::kMaxPlayers; ++player) {
		const auto& gamepad = candidate["playerGamepads"][player];
		if (!gamepad.is_number_integer() || gamepad.get<int64_t>() < -1 || gamepad.get<int64_t>() > 3 ||
			!candidate["playerKeyboardMouse"][player].is_boolean()) return false;
	}
	const auto& pairs = candidate["collisionPairs"];
	if (!pairs.is_array() || pairs.size() != kMaxCollisionTypes) return false;
	for (uint32_t row = 0; row < kMaxCollisionTypes; ++row) {
		if (!pairs[row].is_number_integer() || pairs[row].get<int64_t>() < 0 || pairs[row].get<uint64_t>() > UINT32_MAX) return false;
		for (uint32_t column = 0; column < row; ++column) {
			if (((pairs[row].get<uint32_t>() >> column) & 1) != ((pairs[column].get<uint32_t>() >> row) & 1)) return false;
		}
	}
	const auto maximum = WinApp::GetMaximumClientSize();
	const bool changesSize = candidate["width"] != Read()["width"] || candidate["height"] != Read()["height"];
	if (candidate["width"].get<int>() < 1 || candidate["height"].get<int>() < 1 || (changesSize &&
		(candidate["width"].get<int>() > maximum.x || candidate["height"].get<int>() > maximum.y))) return false;
	const auto mode = candidate["displayOutputMode"].get<std::string>();
	if (mode != "SDR" && mode != "HDR10" && mode != "ScRGB") return false;
	GraphicsFeaturePreferences preferences = graphics_.GetDXObject().GetFeatureController().GetPreferences();
	GraphicsPreferenceStorage::Read(candidate, preferences);
	// 表示形式と閾値を既存の検証結果へ揃える
	auto validated = GraphicsPreferenceStorage::Write(preferences);
	for (auto it = validated.begin(); it != validated.end(); ++it) candidate[it.key()] = it.value();
	pending_ = std::move(candidate);
	hasPending_ = true;
	return true;
}

void Engine::RuntimeProjectSettings::ApplyPending() {

	if (!hasPending_) return;
	const auto settings = std::move(pending_);
	hasPending_ = false;
	auto& controller = graphics_.GetDXObject().GetFeatureController();
	auto preferences = controller.GetPreferences();
	GraphicsPreferenceStorage::Read(settings, preferences);
	EngineContext::SetRuntimeSceneClearColor(Color4::FromJson(settings["sceneClearColor"]));
	controller.SetRuntimeOverrideActive(true);
	controller.ApplyRuntimePreferences(preferences);
	graphics_.RequestDoubleBuffering(preferences.frameContextCount == 2);
	FrameRateSettings::GetInstance().SetRuntimeTargetFps(settings["targetFrameRate"].get<uint32_t>());
	Input::GetInstance()->SetBackgroundInputEnabled(settings["runInBackground"].get<bool>());
	auto& input = *Input::GetInstance();
	input.SetDeadZone(settings["inputDeadZone"].get<float>() * input.GetMaxStickValue());
	for (uint32_t player = 0; player < Input::kMaxPlayers; ++player) {
		input.SetPlayerGamepadIndex(player, settings["playerGamepads"][player].get<int>());
		input.SetPlayerKeyboardMouseEnabled(player, settings["playerKeyboardMouse"][player].get<bool>());
	}
	Audio::GetInstance()->SetRuntimePlayInBackground(settings["audioPlayInBackground"].get<bool>());
	auto& collision = CollisionSettings::GetInstance();
	collision.SetQueriesHitTriggers(settings["queriesHitTriggers"].get<bool>());
	for (uint32_t row = 0; row < kMaxCollisionTypes; ++row) {
		for (uint32_t column = 0; column <= row; ++column)
			collision.SetPairEnabled(row, column, (settings["collisionPairs"][row].get<uint32_t>() & (1u << column)) != 0);
	}
	const Vector2I size(settings["width"].get<int>(), settings["height"].get<int>());
	if (size != graphics_.GetContext().GetWindowSetting().gameSize) {
		EngineContext::SetGameSize(size);
		if (!editor_) WinApp::ResizeClient(size);
	}
}

void Engine::RuntimeProjectSettings::BeginPlay() {

	if (playing_) return;
	original_ = Capture();
	defaults_ = original_;
	playing_ = true;
	LoadUserSettings();
}

void Engine::RuntimeProjectSettings::EndPlay() {

	if (!playing_) return;
	// Scriptの終了処理を終えてから編集用の設定へ戻す
	pending_ = original_;
	// Play中に変更した共有色はStop後も維持する
	pending_["sceneClearColor"] = EngineContext::GetProjectSceneClearColor().ToJson();
	hasPending_ = true;
	ApplyPending();
	FrameRateSettings::GetInstance().SetRuntimeTargetFps(std::nullopt);
	graphics_.GetDXObject().GetFeatureController().SetRuntimeOverrideActive(false);
	playing_ = false;
}

bool Engine::RuntimeProjectSettings::SaveUserSettings() const {

	auto settings = Read();
	// 共通の背景色をユーザー別設定で上書きしない
	settings.erase("sceneClearColor");
	return StorageFileUtility::WriteBytes(RuntimePaths::GetUserSettingsPath("Runtime/GameUserSettings.json"), settings.dump(2));
}

bool Engine::RuntimeProjectSettings::LoadUserSettings() {

	auto settings = JsonFile::Load(RuntimePaths::GetUserSettingsPath("Runtime/GameUserSettings.json"), false);
	if (settings.is_object()) settings.erase("sceneClearColor");
	return settings.is_object() && Request(settings);
}

void Engine::RuntimeProjectSettings::ResetToProjectDefaults() {

	auto settings = defaults_;
	settings["sceneClearColor"] = EngineContext::GetProjectSceneClearColor().ToJson();
	Request(settings);
}
