#include "BillboardComponent.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>

//============================================================================
//	BillboardComponent classMethods
//============================================================================
namespace {

	// 軸をビットへ変換する
	uint8_t ToAxisBit(Engine::Axis axis) {

		switch (axis) {
		case Engine::Axis::X:
			return 1u << 0;
		case Engine::Axis::Y:
			return 1u << 1;
		case Engine::Axis::Z:
			return 1u << 2;
		default:
			return 0;
		}
	}

	// ビルボードで使用できる軸か
	bool IsValidAxis(Engine::Axis axis) {

		return ToAxisBit(axis) != 0;
	}
}

void Engine::from_json(const nlohmann::json& in, BillboardComponent& component) {

	component.axisMask = 0;
	if (const auto it = in.find("axes"); it != in.end() && it->is_array()) {
		for (const nlohmann::json& axisJson : *it) {
			if (!axisJson.is_string()) {
				continue;
			}
			if (auto axis = EnumAdapter<Axis>::FromString(axisJson.get<std::string>())) {
				component.axisMask |= ToAxisBit(*axis);
			}
		}
	}
	SanitizeBillboardAxes(component);
}

void Engine::to_json(nlohmann::json& out, const BillboardComponent& component) {

	out["axes"] = nlohmann::json::array();
	for (Axis axis : { Axis::X, Axis::Y, Axis::Z }) {
		if (HasBillboardAxis(component, axis)) {
			out["axes"].push_back(EnumAdapter<Axis>::ToString(axis));
		}
	}
}

bool Engine::HasBillboardAxis(const BillboardComponent& component, Axis axis) {

	return (component.axisMask & ToAxisBit(axis)) != 0;
}

void Engine::SetBillboardAxis(BillboardComponent& component, Axis axis, bool enabled) {

	if (!IsValidAxis(axis)) {
		return;
	}

	if (enabled) {
		component.axisMask |= ToAxisBit(axis);
	} else {
		component.axisMask &= static_cast<uint8_t>(~ToAxisBit(axis));
	}
	SanitizeBillboardAxes(component);
}

void Engine::SetBillboardAllAxes(BillboardComponent& component) {

	component.axisMask = 0x07;
}

void Engine::SanitizeBillboardAxes(BillboardComponent& component) {

	component.axisMask &= 0x07;
}

bool Engine::HasAnyBillboardAxis(const BillboardComponent& component) {

	return component.axisMask != 0;
}
