#include "ParticleValue.h"

//============================================================================
//	ParticleValue classMethods
//============================================================================

namespace Engine {

	template <>
	void to_json(nlohmann::json& out, const ParticleValue<Vector3>& value) {

		out["type"] = value.type == ParticleValueType::Random ? "Random" : "Constant";
		out["constant"] = value.constant.ToJson();
		out["min"] = value.min.ToJson();
		out["max"] = value.max.ToJson();
	}

	template <>
	void from_json(const nlohmann::json& in, ParticleValue<Vector3>& value) {

		if (!in.is_object()) {
			return;
		}
		value.type = in.value("type", "Constant") == "Random" ?
			ParticleValueType::Random : ParticleValueType::Constant;
		if (const auto it = in.find("constant"); it != in.end()) { value.constant = Vector3::FromJson(*it); }
		if (const auto it = in.find("min"); it != in.end()) { value.min = Vector3::FromJson(*it); }
		if (const auto it = in.find("max"); it != in.end()) { value.max = Vector3::FromJson(*it); }
	}
}
