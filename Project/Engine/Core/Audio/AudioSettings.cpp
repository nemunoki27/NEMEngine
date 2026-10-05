#include "AudioSettings.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Serialization/Json/JsonSerializer.h>

bool Engine::AudioSettings::Load(const std::filesystem::path& path) {

	std::error_code error;
	if (!std::filesystem::exists(path, error)) { return !error; }
	nlohmann::json data;
	if (!JsonAdapter::TryLoad(path, data)) { return false; }
	if (!data.is_object() || (data.contains("playInBackground") && !data["playInBackground"].is_boolean())) {
		return false;
	}
	playInBackground = data.value("playInBackground", true);
	return true;
}

bool Engine::AudioSettings::Save(const std::filesystem::path& path) const {

	return JsonAdapter::Save(path, { { "playInBackground", playInBackground } });
}
