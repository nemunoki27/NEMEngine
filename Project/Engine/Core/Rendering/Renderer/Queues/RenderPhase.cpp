#include "RenderPhase.h"

//============================================================================
//	include
//============================================================================
#include <Externals/nlohmann/json.hpp>

// c++
#include <optional>
#include <string>

//============================================================================
//	RenderPhase functions
//============================================================================
std::string_view Engine::ToString(RenderPhase phase) {

	if (phase == RenderPhase::Count) {
		return "Opaque";
	}
	return Engine::EnumAdapter<Engine::RenderPhase>::ToString(phase);
}

bool Engine::TryParseRenderPhase(std::string_view value, RenderPhase& outPhase) {

	const std::optional<RenderPhase> phase = Engine::EnumAdapter<Engine::RenderPhase>::FromString(value);
	if (!phase || *phase == RenderPhase::Count) {
		return false;
	}
	outPhase = *phase;
	return true;
}

Engine::RenderPhase Engine::RenderPhaseFromString(std::string_view value, RenderPhase fallback) {

	RenderPhase phase = fallback;
	TryParseRenderPhase(value, phase);
	return phase;
}

void Engine::ReadRenderCommonFields(const nlohmann::json& in,
	int32_t& layer, int32_t& order, bool& visible, BlendMode& blendMode, RenderPhase& queue) {

	queue = RenderPhaseFromString(in.value("queue", std::string(ToString(queue))), queue);
	layer = in.value("layer", layer);
	order = in.value("order", order);
	visible = in.value("visible", visible);
	blendMode = EnumAdapter<BlendMode>::FromString(in.value("blendMode", "Normal")).value_or(blendMode);
}

void Engine::WriteRenderCommonFields(nlohmann::json& out,
	int32_t layer, int32_t order, bool visible, BlendMode blendMode, RenderPhase queue) {

	out["queue"] = std::string(ToString(queue));
	out["layer"] = layer;
	out["order"] = order;
	out["visible"] = visible;
	out["blendMode"] = EnumAdapter<BlendMode>::ToString(blendMode);
}
