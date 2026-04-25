#include "LambdaEditorTool.h"

//============================================================================
//	include
//============================================================================
#include <utility>

//============================================================================
//	LambdaEditorTool classMethods
//============================================================================

Engine::LambdaEditorTool::LambdaEditorTool(ToolDescriptor descriptor, DrawFunc drawFunc,
	TickFunc tickFunc, EnabledFunc enabledFunc) :
	descriptor_(std::move(descriptor)),
	drawFunc_(std::move(drawFunc)),
	tickFunc_(std::move(tickFunc)),
	enabledFunc_(std::move(enabledFunc)) {
}

void Engine::LambdaEditorTool::Tick(ToolContext& context) {

	if (tickFunc_) {
		tickFunc_(context);
	}
}

void Engine::LambdaEditorTool::DrawEditorTool(const EditorToolContext& context) {

	if (drawFunc_) {
		drawFunc_(context);
	}
}

bool Engine::LambdaEditorTool::IsEnabled(const ToolContext& context) const {

	if (!ITool::IsEnabled(context)) {
		return false;
	}
	return enabledFunc_ ? enabledFunc_(context) : true;
}
