#include "ITool.h"

//============================================================================
//	ITool classMethods
//============================================================================

bool Engine::ITool::IsEnabled(const ToolContext& context) const {

	const ToolDescriptor& desc = GetDescriptor();
	if (HasToolFlag(desc.flags, ToolFlags::EditOnly) && context.isPlaying) {
		return false;
	}
	if (!HasToolFlag(desc.flags, ToolFlags::AllowPlayMode) && context.isPlaying) {
		return false;
	}
	return true;
}

void Engine::ITool::Tick([[maybe_unused]] ToolContext& context) {
}

void Engine::ITool::OnRegistered() {
}

void Engine::ITool::OnUnregistered() {
}
