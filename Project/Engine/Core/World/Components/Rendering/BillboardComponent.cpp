#include "BillboardComponent.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>

// c++
#include <algorithm>

//============================================================================
//	BillboardComponent classMethods
//============================================================================

namespace {

	bool IsValidAxis(Engine::Axis axis) {

		return axis == Engine::Axis::X || axis == Engine::Axis::Y || axis == Engine::Axis::Z;
	}
}

void Engine::from_json(const nlohmann::json& in, BillboardComponent& component) {

	component.axes.clear();
	if (const auto it = in.find("axes"); it != in.end() && it->is_array()) {
		for (const nlohmann::json& axisJson : *it) {
			if (!axisJson.is_string()) {
				continue;
			}
			if (auto axis = EnumAdapter<Axis>::FromString(axisJson.get<std::string>())) {
				component.axes.emplace_back(*axis);
			}
		}
	}
	SanitizeBillboardAxes(component);
}

void Engine::to_json(nlohmann::json& out, const BillboardComponent& component) {

	out["axes"] = nlohmann::json::array();
	for (Axis axis : component.axes) {
		if (!IsValidAxis(axis)) {
			continue;
		}
		out["axes"].push_back(EnumAdapter<Axis>::ToString(axis));
	}
}

bool Engine::HasBillboardAxis(const BillboardComponent& component, Axis axis) {

	return std::find(component.axes.begin(), component.axes.end(), axis) != component.axes.end();
}

void Engine::SetBillboardAxis(BillboardComponent& component, Axis axis, bool enabled) {

	if (!IsValidAxis(axis)) {
		return;
	}

	const auto it = std::find(component.axes.begin(), component.axes.end(), axis);
	if (enabled) {
		if (it == component.axes.end()) {
			component.axes.emplace_back(axis);
		}
	} else if (it != component.axes.end()) {
		component.axes.erase(it);
	}
	SanitizeBillboardAxes(component);
}

void Engine::SetBillboardAllAxes(BillboardComponent& component) {

	component.axes = { Axis::X, Axis::Y, Axis::Z };
}

void Engine::SanitizeBillboardAxes(BillboardComponent& component) {

	std::vector<Axis> sanitized;
	sanitized.reserve(3);
	for (Axis axis : { Axis::X, Axis::Y, Axis::Z }) {
		if (HasBillboardAxis(component, axis)) {
			sanitized.emplace_back(axis);
		}
	}
	component.axes = std::move(sanitized);
}
