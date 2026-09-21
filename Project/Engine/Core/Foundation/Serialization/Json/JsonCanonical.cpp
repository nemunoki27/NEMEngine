#include "JsonCanonical.h"

//============================================================================
//	include
//============================================================================
#include <cmath>

using namespace Engine;

namespace {

	bool CanonicalizeJson(nlohmann::json& value) {

		if (value.is_number_float()) {

			const double number = value.get<double>();
			if (!std::isfinite(number)) {
				return false;
			}
			if (number == 0.0) {
				value = 0.0;
			}
			return true;
		}
		if (value.is_array()) {
			for (auto& element : value) {
				if (!CanonicalizeJson(element)) {
					return false;
				}
			}
			return true;
		}
		if (value.is_object()) {
			for (auto it = value.begin(); it != value.end(); ++it) {
				if (!CanonicalizeJson(it.value())) {
					return false;
				}
			}
		}
		return true;
	}
}

std::string JsonCanonical::SerializeCanonical(const nlohmann::json& data, int32_t indent) {

	nlohmann::json canonical = data;
	if (!CanonicalizeJson(canonical)) {
		return {};
	}
	return canonical.dump(indent) + '\n';
}
