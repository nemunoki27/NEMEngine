#include "RenderPhase.h"

// c++
#include <optional>

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
