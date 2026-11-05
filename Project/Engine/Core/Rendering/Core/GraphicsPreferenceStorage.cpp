#include "GraphicsPreferenceStorage.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Diagnostics/Log.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonSerializer.h>
#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>
#include <Engine/Core/Runtime/Paths/ConfigPaths.h>

// c++
#include <algorithm>
#include <cmath>

namespace {

	constexpr const char* kGraphicsFeatureConfigPath = Engine::ConfigPaths::kGraphicsFeatureSettings;

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
			"グラフィックス設定{}の型が不正なため既定値を使用します", key);
		return fallback;
	}

	uint32_t ReadUIntSetting(const nlohmann::json& data, const char* key,
		uint32_t fallback) {

		auto found = data.find(key);
		if (found == data.end()) {
			return fallback;
		}
		if (found->is_number_unsigned()) {
			return found->get<uint32_t>();
		}
		Engine::Logger::Output(Engine::LogType::Engine, spdlog::level::warn,
			"グラフィックス設定{}の型が不正なため既定値を使用します", key);
		return fallback;
	}

	float ReadFloatSetting(const nlohmann::json& data, const char* key,
		float fallback) {

		auto found = data.find(key);
		if (found == data.end()) {
			return fallback;
		}
		if (found->is_number()) {
			const float value = found->get<float>();
			if (std::isfinite(value)) {
				return value;
			}
		}
		Engine::Logger::Output(Engine::LogType::Engine, spdlog::level::warn,
			"グラフィックス設定{}の値が不正なため既定値を使用します", key);
		return fallback;
	}

	std::string ReadStringSetting(const nlohmann::json& data, const char* key,
		std::string_view fallback) {

		auto found = data.find(key);
		if (found == data.end()) {
			return std::string(fallback);
		}
		if (found->is_string()) {
			return found->get<std::string>();
		}
		Engine::Logger::Output(Engine::LogType::Engine, spdlog::level::warn,
			"グラフィックス設定{}の型が不正なため既定値を使用します", key);
		return std::string(fallback);
	}
}

void Engine::GraphicsPreferenceStorage::Load(GraphicsFeaturePreferences& preferences) {

	const std::filesystem::path path = RuntimePaths::GetUserSettingsPath(kGraphicsFeatureConfigPath);
	if (!JsonAdapter::Check(path)) {
		return;
	}

	nlohmann::json data = JsonAdapter::Load(path);
	if (!data.is_object()) {
		return;
	}

	const bool changed = Migrate(data);
	Read(data, preferences);
	if (changed) {
		JsonAdapter::Save(path, data);
	}
}

bool Engine::GraphicsPreferenceStorage::Migrate(nlohmann::json& data) {

	if (!data.is_object()) return false;
	GraphicsFeaturePreferences preferences{};
	Read(data, preferences);
	const auto method = EnumAdapter<AntialiasingMethod>::ToString(preferences.antialiasing);
	bool changed = data.contains("antialiasing") &&
		ReadStringSetting(data, "antialiasing", std::string{}) != method;
	if (changed) data["antialiasing"] = method;
	for (const char* key : { "dlssQuality", "rayReconstruction", "allowMeshLOD",
		"meshLOD0PixelThreshold", "meshLOD1PixelThreshold", "meshLOD2PixelThreshold" }) {
		changed |= data.erase(key) != 0;
	}
	return changed;
}

void Engine::GraphicsPreferenceStorage::Read(const nlohmann::json& data, GraphicsFeaturePreferences& preferences) {

	const auto antialiasing =
		ReadStringSetting(data, "antialiasing", EnumAdapter<AntialiasingMethod>::ToString(preferences.antialiasing));
	preferences.antialiasing = antialiasing == "DLSSSuperResolution" || antialiasing == "DLAA" ? AntialiasingMethod::None :
		EnumAdapter<AntialiasingMethod>::FromString(antialiasing).value_or(preferences.antialiasing);
	preferences.fxaa.strength = std::clamp(ReadFloatSetting(data, "fxaaStrength", preferences.fxaa.strength), 0.0f, 1.0f);
	preferences.fxaa.edgeThreshold = std::clamp(
		ReadFloatSetting(data, "fxaaEdgeThreshold", preferences.fxaa.edgeThreshold), 0.0f, 1.0f);
	preferences.fxaa.minimumThreshold = std::clamp(
		ReadFloatSetting(data, "fxaaMinimumThreshold", preferences.fxaa.minimumThreshold), 0.0f, 1.0f);
	preferences.fxaa.subpixelStrength = std::clamp(
		ReadFloatSetting(data, "fxaaSubpixelStrength", preferences.fxaa.subpixelStrength), 0.0f, 1.0f);
	preferences.fxaaSettingsMigrated = ReadBoolSetting(data, "fxaaSettingsMigrated", preferences.fxaaSettingsMigrated);

	preferences.allowMeshShader = ReadBoolSetting(data,
		"allowMeshShader", preferences.allowMeshShader);
	preferences.allowInlineRayTracing = ReadBoolSetting(data,
		"allowInlineRayTracing", preferences.allowInlineRayTracing);
	preferences.allowDispatchRays = ReadBoolSetting(data,
		"allowDispatchRays", preferences.allowDispatchRays);
	preferences.allowRaytracingDownsampling = ReadBoolSetting(data,
		"allowRaytracingDownsampling",
		preferences.allowRaytracingDownsampling);
	const uint32_t shadowSamples = ReadUIntSetting(data,
		"softShadowSampleCount", preferences.softShadowSampleCount);
	preferences.softShadowSampleCount = shadowSamples <= 1u ?
		1u : shadowSamples <= 2u ? 2u : 4u;
	preferences.allowFrustumCulling = ReadBoolSetting(data,
		"allowFrustumCulling", preferences.allowFrustumCulling);
	preferences.allowOcclusionCulling = ReadBoolSetting(data,
		"allowOcclusionCulling",
		preferences.allowOcclusionCulling);
	preferences.useGameViewCameraForSceneCulling = ReadBoolSetting(data,
		"useGameViewCameraForSceneCulling", preferences.useGameViewCameraForSceneCulling);
	preferences.allowContributionCulling = ReadBoolSetting(data,
		"allowContributionCulling", preferences.allowContributionCulling);
	preferences.allowNormalConeCulling = ReadBoolSetting(data,
		"allowNormalConeCulling", preferences.allowNormalConeCulling);
	const uint32_t savedFrameCount = ReadUIntSetting(data, "frameContextCount", preferences.frameContextCount);
	preferences.frameContextCount = std::clamp(savedFrameCount, 1u, 2u);
	preferences.displayOutput.mode =
		EnumAdapter<DisplayOutputMode>::FromString(
			ReadStringSetting(data, "displayOutputMode", "SDR"))
		.value_or(DisplayOutputMode::SDR);
	preferences.displayOutput.paperWhiteNits = std::clamp(
		ReadFloatSetting(data, "paperWhiteNits", 200.0f), 80.0f, 1000.0f);
	preferences.displayOutput.maxLuminanceNits = std::clamp(
		ReadFloatSetting(data, "maxLuminanceNits", 1000.0f),
		preferences.displayOutput.paperWhiteNits, 10000.0f);
}

void Engine::GraphicsPreferenceStorage::Save(const GraphicsFeaturePreferences& preferences) {

	JsonAdapter::Save(RuntimePaths::GetUserSettingsPath(kGraphicsFeatureConfigPath), Write(preferences));
}

nlohmann::json Engine::GraphicsPreferenceStorage::Write(const GraphicsFeaturePreferences& preferences) {

	nlohmann::json data{};
	data["antialiasing"] = EnumAdapter<AntialiasingMethod>::ToString(preferences.antialiasing);
	data["fxaaStrength"] = preferences.fxaa.strength;
	data["fxaaEdgeThreshold"] = preferences.fxaa.edgeThreshold;
	data["fxaaMinimumThreshold"] = preferences.fxaa.minimumThreshold;
	data["fxaaSubpixelStrength"] = preferences.fxaa.subpixelStrength;
	data["fxaaSettingsMigrated"] = preferences.fxaaSettingsMigrated;
	data["allowMeshShader"] = preferences.allowMeshShader;
	data["allowInlineRayTracing"] = preferences.allowInlineRayTracing;
	data["allowDispatchRays"] = preferences.allowDispatchRays;
	data["allowRaytracingDownsampling"] =
		preferences.allowRaytracingDownsampling;
	data["softShadowSampleCount"] =
		preferences.softShadowSampleCount;
	data["allowFrustumCulling"] = preferences.allowFrustumCulling;
	data["allowOcclusionCulling"] = preferences.allowOcclusionCulling;
	data["useGameViewCameraForSceneCulling"] = preferences.useGameViewCameraForSceneCulling;
	data["allowContributionCulling"] = preferences.allowContributionCulling;
	data["allowNormalConeCulling"] = preferences.allowNormalConeCulling;
	data["frameContextCount"] =
		preferences.frameContextCount;
	data["displayOutputMode"] = EnumAdapter<DisplayOutputMode>::ToString(
		preferences.displayOutput.mode);
	data["paperWhiteNits"] = preferences.displayOutput.paperWhiteNits;
	data["maxLuminanceNits"] = preferences.displayOutput.maxLuminanceNits;

	return data;
}
