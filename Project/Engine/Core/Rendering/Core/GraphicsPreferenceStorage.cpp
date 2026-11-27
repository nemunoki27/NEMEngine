#include "GraphicsPreferenceStorage.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Diagnostics/Log.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonFile.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonSerializer.h>
#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>
#include <Engine/Core/Runtime/Paths/ConfigPaths.h>

// c++
#include <algorithm>
#include <cmath>

namespace {

	constexpr const char* kProjectLightingKeys[] = {
		"enableReSTIRDI", "dynamicGI", "dynamicGIQuality", "dynamicGIDebug", "allowDispatchRays",
		"reflectionQuality", "reflectionIntensity", "reflectionMaxDistance", "reflectionParameters", "ambientOcclusionEnabled",
		"ambientOcclusionQuality", "ambientOcclusionRadius", "ambientOcclusionIntensity",
		"indirectIntensity", "emissiveContribution", "giUpdateSpeed"
	};

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

	int32_t ReadQualitySetting(const nlohmann::json& data, const char* key, int32_t fallback) {

		const auto found = data.find(key);
		if (found == data.end() || !found->is_number_integer()) return fallback;
		if (*found < 0) return 0;
		if (*found > 2) return 2;
		return found->get<int32_t>();
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

	nlohmann::json project;
	if (JsonFile::TryLoad(RuntimePaths::GetProjectSettingsPath(ConfigPaths::kGlobalIllumination), project) && project.is_object()) {
		Read(project, preferences);
	}
	const std::filesystem::path path = RuntimePaths::GetUserSettingsPath(kGraphicsFeatureConfigPath);
	if (!JsonAdapter::Check(path)) {
		return;
	}

	nlohmann::json data = JsonAdapter::Load(path);
	if (!data.is_object()) {
		return;
	}

	const bool changed = Migrate(data);
	auto user = data;
	for (const auto* key : kProjectLightingKeys) user.erase(key);
	Read(user, preferences);
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

	preferences.reflectionIntensity = std::clamp(ReadFloatSetting(data, "reflectionIntensity", preferences.reflectionIntensity), 0.0f, 65504.0f);
	preferences.reflectionMaxDistance = std::clamp(ReadFloatSetting(data, "reflectionMaxDistance", preferences.reflectionMaxDistance), 0.01f, 100000.0f);
	preferences.ambientOcclusionRadius = std::clamp(ReadFloatSetting(data, "ambientOcclusionRadius", preferences.ambientOcclusionRadius), 0.001f, 1000.0f);
	preferences.ambientOcclusionIntensity = std::clamp(ReadFloatSetting(data, "ambientOcclusionIntensity", preferences.ambientOcclusionIntensity), 0.0f, 65504.0f);
	preferences.indirectIntensity = std::clamp(ReadFloatSetting(data, "indirectIntensity", preferences.indirectIntensity), 0.0f, 65504.0f);
	preferences.emissiveContribution = std::clamp(ReadFloatSetting(data, "emissiveContribution", preferences.emissiveContribution), 0.0f, 65504.0f);
	preferences.giUpdateSpeed = static_cast<uint32_t>(ReadQualitySetting(data, "giUpdateSpeed", static_cast<int32_t>(preferences.giUpdateSpeed)));
	preferences.reflectionQuality = ReadQualitySetting(data, "reflectionQuality", preferences.reflectionQuality);
	preferences.ambientOcclusionQuality = ReadQualitySetting(data, "ambientOcclusionQuality", preferences.ambientOcclusionQuality);
	preferences.ambientOcclusionEnabled = ReadBoolSetting(data, "ambientOcclusionEnabled", preferences.ambientOcclusionEnabled);
	if (auto it = data.find("reflectionParameters"); it != data.end() && it->is_object()) preferences.reflectionParameters = *it;
	preferences.enableReSTIRDI = ReadBoolSetting(data, "enableReSTIRDI", preferences.enableReSTIRDI);
	preferences.dynamicGI = ReadBoolSetting(data, "dynamicGI", preferences.dynamicGI);
	preferences.dynamicGIQuality = EnumAdapter<DynamicGIQuality>::FromString(
		ReadStringSetting(data, "dynamicGIQuality", EnumAdapter<DynamicGIQuality>::ToString(preferences.dynamicGIQuality)))
		.value_or(preferences.dynamicGIQuality);
	preferences.dynamicGIDebug = EnumAdapter<DynamicGIDebug>::FromString(
		ReadStringSetting(data, "dynamicGIDebug", EnumAdapter<DynamicGIDebug>::ToString(preferences.dynamicGIDebug)))
		.value_or(preferences.dynamicGIDebug);
	preferences.allowMeshShader = ReadBoolSetting(data,
		"allowMeshShader", preferences.allowMeshShader);
	preferences.allowInlineRayTracing = ReadBoolSetting(data,
		"allowInlineRayTracing", preferences.allowInlineRayTracing);
	preferences.reflectionEnabled = ReadBoolSetting(data,
		"allowDispatchRays", preferences.reflectionEnabled);
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
			ReadStringSetting(data, "displayOutputMode", EnumAdapter<DisplayOutputMode>::ToString(preferences.displayOutput.mode)))
		.value_or(DisplayOutputMode::SDR);
	preferences.displayOutput.paperWhiteNits = std::clamp(
		ReadFloatSetting(data, "paperWhiteNits", preferences.displayOutput.paperWhiteNits), 80.0f, 1000.0f);
	preferences.displayOutput.maxLuminanceNits = std::clamp(
		ReadFloatSetting(data, "maxLuminanceNits", preferences.displayOutput.maxLuminanceNits),
		preferences.displayOutput.paperWhiteNits, 10000.0f);
}

void Engine::GraphicsPreferenceStorage::Save(const GraphicsFeaturePreferences& preferences) {

	auto values = Write(preferences);
	for (const auto* key : kProjectLightingKeys) values.erase(key);
	JsonAdapter::Save(RuntimePaths::GetUserSettingsPath(kGraphicsFeatureConfigPath), values);
}

bool Engine::GraphicsPreferenceStorage::SaveProjectLighting(const GraphicsFeaturePreferences& preferences) {

	const auto values = Write(preferences);
	nlohmann::json settings;
	for (const auto* key : kProjectLightingKeys) settings[key] = values.at(key);
	return JsonFile::SaveCanonical(RuntimePaths::GetProjectSettingsPath(ConfigPaths::kGlobalIllumination), settings, 2);
}

nlohmann::json Engine::GraphicsPreferenceStorage::Write(const GraphicsFeaturePreferences& preferences) {

	nlohmann::json data{};
	data["antialiasing"] = EnumAdapter<AntialiasingMethod>::ToString(preferences.antialiasing);
	data["fxaaStrength"] = preferences.fxaa.strength;
	data["fxaaEdgeThreshold"] = preferences.fxaa.edgeThreshold;
	data["fxaaMinimumThreshold"] = preferences.fxaa.minimumThreshold;
	data["fxaaSubpixelStrength"] = preferences.fxaa.subpixelStrength;
	data["fxaaSettingsMigrated"] = preferences.fxaaSettingsMigrated;
	data["reflectionQuality"] = preferences.reflectionQuality;
	data["reflectionIntensity"] = preferences.reflectionIntensity;
	data["reflectionMaxDistance"] = preferences.reflectionMaxDistance;
	data["reflectionParameters"] = preferences.reflectionParameters;
	data["ambientOcclusionEnabled"] = preferences.ambientOcclusionEnabled;
	data["ambientOcclusionQuality"] = preferences.ambientOcclusionQuality;
	data["ambientOcclusionRadius"] = preferences.ambientOcclusionRadius;
	data["ambientOcclusionIntensity"] = preferences.ambientOcclusionIntensity;
	data["indirectIntensity"] = preferences.indirectIntensity;
	data["emissiveContribution"] = preferences.emissiveContribution;
	data["giUpdateSpeed"] = preferences.giUpdateSpeed;
	data["enableReSTIRDI"] = preferences.enableReSTIRDI;
	data["dynamicGI"] = preferences.dynamicGI;
	data["dynamicGIQuality"] = EnumAdapter<DynamicGIQuality>::ToString(preferences.dynamicGIQuality);
	data["dynamicGIDebug"] = EnumAdapter<DynamicGIDebug>::ToString(preferences.dynamicGIDebug);
	data["allowMeshShader"] = preferences.allowMeshShader;
	data["allowInlineRayTracing"] = preferences.allowInlineRayTracing;
	data["allowDispatchRays"] = preferences.reflectionEnabled;
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
