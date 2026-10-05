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

	const nlohmann::json data = JsonAdapter::Load(path);
	if (!data.is_object()) {
		return;
	}

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
	preferences.allowMeshLOD = ReadBoolSetting(data,
		"allowMeshLOD", preferences.allowMeshLOD);
	const float lod0 = ReadFloatSetting(data,
		"meshLOD0PixelThreshold",
		preferences.meshLOD0PixelThreshold);
	const float lod1 = ReadFloatSetting(data,
		"meshLOD1PixelThreshold",
		preferences.meshLOD1PixelThreshold);
	const float lod2 = ReadFloatSetting(data,
		"meshLOD2PixelThreshold",
		preferences.meshLOD2PixelThreshold);
	if (GraphicsMeshLOD::ArePixelThresholdsValid(
		lod0, lod1, lod2)) {

		preferences.meshLOD0PixelThreshold = lod0;
		preferences.meshLOD1PixelThreshold = lod1;
		preferences.meshLOD2PixelThreshold = lod2;
	} else {

		preferences.meshLOD0PixelThreshold =
			GraphicsMeshLOD::kDefaultPixelThresholds[0];
		preferences.meshLOD1PixelThreshold =
			GraphicsMeshLOD::kDefaultPixelThresholds[1];
		preferences.meshLOD2PixelThreshold =
			GraphicsMeshLOD::kDefaultPixelThresholds[2];
		Logger::Output(LogType::Engine, spdlog::level::warn,
			"メッシュLODの閾値が不正なため既定値へ戻しました");
	}
	preferences.frameContextCount = std::clamp(
		ReadUIntSetting(data, "frameContextCount",
			preferences.frameContextCount), 1u, 3u);
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

	nlohmann::json data{};
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
	data["allowMeshLOD"] = preferences.allowMeshLOD;
	data["meshLOD0PixelThreshold"] =
		preferences.meshLOD0PixelThreshold;
	data["meshLOD1PixelThreshold"] =
		preferences.meshLOD1PixelThreshold;
	data["meshLOD2PixelThreshold"] =
		preferences.meshLOD2PixelThreshold;
	data["frameContextCount"] =
		preferences.frameContextCount;
	data["displayOutputMode"] = EnumAdapter<DisplayOutputMode>::ToString(
		preferences.displayOutput.mode);
	data["paperWhiteNits"] = preferences.displayOutput.paperWhiteNits;
	data["maxLuminanceNits"] = preferences.displayOutput.maxLuminanceNits;

	JsonAdapter::Save(RuntimePaths::GetUserSettingsPath(kGraphicsFeatureConfigPath), data);
}
