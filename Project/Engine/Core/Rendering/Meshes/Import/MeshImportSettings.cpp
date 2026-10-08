#include "MeshImportSettings.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Serialization/Json/JsonSerializer.h>
#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>

// c++
#include <algorithm>
#include <cmath>

namespace {

	float ParseTargetError(
		const nlohmann::json& data,
		size_t index, float fallback) {

		if (!data.is_array() || data.size() <= index ||
			!data[index].is_number()) {

			return fallback;
		}
		const float value = data[index].get<float>();
		return std::isfinite(value) ?
			(std::clamp)(value, 0.0f, 1.0f) : fallback;
	}
}

Engine::MeshImportSettings Engine::ParseMeshImportSettings(
	const nlohmann::json& data) {

	MeshImportSettings settings{};
	if (!data.is_object()) {
		return settings;
	}
	settings.generateAutomaticLODs = data.value(
		"generateAutomaticLODs", settings.generateAutomaticLODs);
	const auto errors = data.find("lodTargetErrors");
	if (errors != data.end()) {
		for (size_t index = 0;
			index < settings.lodTargetErrors.size(); ++index) {

			settings.lodTargetErrors[index] = ParseTargetError(
				*errors, index, settings.lodTargetErrors[index]);
		}
	}
	const auto manual = data.find("manualLODMeshes");
	if (manual != data.end() && manual->is_array()) {
		for (size_t index = 0;
			index < settings.manualLODMeshes.size() &&
			index < manual->size(); ++index) {

			settings.manualLODMeshes[index] =
				FromString32Hex((*manual)[index].is_string() ?
					(*manual)[index].get<std::string>() : std::string{});
		}
	}
	settings.lodTransition = EnumAdapter<MeshLODTransitionMode>::
		FromString(data.value("lodTransition", std::string("Immediate"))).
		value_or(MeshLODTransitionMode::Immediate);
	return settings;
}

nlohmann::json Engine::ToJson(const MeshImportSettings& settings) {

	nlohmann::json manual = nlohmann::json::array();
	for (AssetID asset : settings.manualLODMeshes) {
		manual.push_back(asset ? ToString(asset) : std::string{});
	}
	return {
		{ "generateAutomaticLODs", settings.generateAutomaticLODs },
		{ "lodTargetErrors", settings.lodTargetErrors },
		{ "manualLODMeshes", std::move(manual) },
		{ "lodTransition", EnumAdapter<MeshLODTransitionMode>::
			ToString(settings.lodTransition) },
	};
}
