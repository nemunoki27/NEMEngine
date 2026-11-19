#include "ShaderGraphNodeSettings.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>

// c++
#include <algorithm>

using namespace Engine;

void Engine::ReadShaderGraphNodeSettings(const nlohmann::json& data, ShaderGraphNodeSettings& settings) {

	if (!data.is_object()) { return; }
	settings.mathBase = EnumAdapter<ShaderGraphMathBase>::FromString(data.value("mathBase", ""))
		.value_or(settings.mathBase);
	settings.comparison = EnumAdapter<ShaderGraphComparison>::FromString(data.value("comparison", ""))
		.value_or(settings.comparison);
	settings.matrixAxis = EnumAdapter<ShaderGraphMatrixAxis>::FromString(data.value("matrixAxis", ""))
		.value_or(settings.matrixAxis);
	settings.normalBlend = EnumAdapter<ShaderGraphNormalBlend>::FromString(data.value("normalBlend", ""))
		.value_or(settings.normalBlend);
	settings.colorFrom = EnumAdapter<ShaderGraphColorSpace>::FromString(data.value("colorFrom", ""))
		.value_or(settings.colorFrom);
	settings.colorTo = EnumAdapter<ShaderGraphColorSpace>::FromString(data.value("colorTo", ""))
		.value_or(settings.colorTo);
	settings.spaceFrom = EnumAdapter<ShaderGraphCoordinateSpace>::FromString(data.value("spaceFrom", ""))
		.value_or(settings.spaceFrom);
	settings.spaceTo = EnumAdapter<ShaderGraphCoordinateSpace>::FromString(data.value("spaceTo", ""))
		.value_or(settings.spaceTo);
	settings.transformType = EnumAdapter<ShaderGraphTransformType>::FromString(data.value("transformType", ""))
		.value_or(settings.transformType);
	settings.transformationMatrix = EnumAdapter<ShaderGraphTransformationMatrix>::FromString(data.value("transformationMatrix", ""))
		.value_or(settings.transformationMatrix);
	settings.blend = EnumAdapter<ShaderGraphBlend>::FromString(data.value("blend", ""))
		.value_or(settings.blend);
	settings.gradientMode = EnumAdapter<ShaderGraphGradientMode>::FromString(data.value("gradientMode", ""))
		.value_or(settings.gradientMode);
	settings.channelMask = data.value("channelMask", settings.channelMask) & 15u;
	settings.normalSpace = EnumAdapter<ShaderGraphNormalSpace>::FromString(data.value("normalSpace", ""))
		.value_or(settings.normalSpace);
	settings.dielectricMaterial = EnumAdapter<ShaderGraphDielectricMaterial>::FromString(data.value("dielectricMaterial", ""))
		.value_or(settings.dielectricMaterial);
	settings.metalMaterial = EnumAdapter<ShaderGraphMetalMaterial>::FromString(data.value("metalMaterial", ""))
		.value_or(settings.metalMaterial);
	settings.degrees = data.value("degrees", settings.degrees);
	settings.normalize = data.value("normalize", settings.normalize);
	settings.normalMap = data.value("normalMap", settings.normalMap);
	if (data.contains("swizzle") && data["swizzle"].is_array()) {
		for (size_t i = 0; i < settings.swizzle.size() && i < data["swizzle"].size(); ++i) {
			settings.swizzle[i] = std::min(data["swizzle"][i].get<uint32_t>(), 3u);
		}
	}
	if (data.contains("matrixRows") && data["matrixRows"].is_array()) {
		for (size_t i = 0; i < settings.matrixRows.size() && i < data["matrixRows"].size(); ++i) {
			settings.matrixRows[i] = Vector4::FromJson(data["matrixRows"][i]);
		}
	}
	if (data.contains("gradientColors") && data["gradientColors"].is_array() && !data["gradientColors"].empty()) {
		settings.gradientColors.clear();
		for (const auto& item : data["gradientColors"]) {
			if (settings.gradientColors.size() == 8) { break; }
			Vector4 key = Vector4::FromJson(item);
			key.w = std::clamp(key.w, 0.0f, 1.0f);
			settings.gradientColors.emplace_back(key);
		}
		std::stable_sort(settings.gradientColors.begin(), settings.gradientColors.end(),
			[](const Vector4& a, const Vector4& b) { return a.w < b.w; });
	}
	if (data.contains("gradientAlphas") && data["gradientAlphas"].is_array() && !data["gradientAlphas"].empty()) {
		settings.gradientAlphas.clear();
		for (const auto& item : data["gradientAlphas"]) {
			if (settings.gradientAlphas.size() == 8) { break; }
			Vector2 key = Vector2::FromJson(item);
			key.y = std::clamp(key.y, 0.0f, 1.0f);
			settings.gradientAlphas.emplace_back(key);
		}
		std::stable_sort(settings.gradientAlphas.begin(), settings.gradientAlphas.end(),
			[](const Vector2& a, const Vector2& b) { return a.y < b.y; });
	}
}

nlohmann::json Engine::WriteShaderGraphNodeSettings(const ShaderGraphNodeSettings& settings) {

	nlohmann::json data{
		{ "mathBase", EnumAdapter<decltype(settings.mathBase)>::ToString(settings.mathBase) },
		{ "comparison", EnumAdapter<decltype(settings.comparison)>::ToString(settings.comparison) },
		{ "matrixAxis", EnumAdapter<decltype(settings.matrixAxis)>::ToString(settings.matrixAxis) },
		{ "normalBlend", EnumAdapter<decltype(settings.normalBlend)>::ToString(settings.normalBlend) },
		{ "colorFrom", EnumAdapter<decltype(settings.colorFrom)>::ToString(settings.colorFrom) },
		{ "colorTo", EnumAdapter<decltype(settings.colorTo)>::ToString(settings.colorTo) },
		{ "spaceFrom", EnumAdapter<decltype(settings.spaceFrom)>::ToString(settings.spaceFrom) },
		{ "spaceTo", EnumAdapter<decltype(settings.spaceTo)>::ToString(settings.spaceTo) },
		{ "transformType", EnumAdapter<decltype(settings.transformType)>::ToString(settings.transformType) },
		{ "transformationMatrix", EnumAdapter<decltype(settings.transformationMatrix)>::ToString(settings.transformationMatrix) },
		{ "blend", EnumAdapter<decltype(settings.blend)>::ToString(settings.blend) },
		{ "gradientMode", EnumAdapter<decltype(settings.gradientMode)>::ToString(settings.gradientMode) },
		{ "channelMask", settings.channelMask },
		{ "normalSpace", EnumAdapter<ShaderGraphNormalSpace>::ToString(settings.normalSpace) },
		{ "dielectricMaterial", EnumAdapter<ShaderGraphDielectricMaterial>::ToString(settings.dielectricMaterial) },
		{ "metalMaterial", EnumAdapter<ShaderGraphMetalMaterial>::ToString(settings.metalMaterial) },
		{ "swizzle", settings.swizzle },
		{ "degrees", settings.degrees },
		{ "normalize", settings.normalize },
		{ "normalMap", settings.normalMap },
		{ "matrixRows", nlohmann::json::array() },
		{ "gradientColors", nlohmann::json::array() },
		{ "gradientAlphas", nlohmann::json::array() },
	};
	for (const Vector4& row : settings.matrixRows) { data["matrixRows"].push_back(row.ToJson()); }
	for (const Vector4& key : settings.gradientColors) { data["gradientColors"].push_back(key.ToJson()); }
	for (const Vector2& key : settings.gradientAlphas) { data["gradientAlphas"].push_back(key.ToJson()); }
	return data;
}
