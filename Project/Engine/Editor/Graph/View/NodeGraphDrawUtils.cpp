#include "NodeGraphDrawUtils.h"

// imgui
#include <imgui.h>

//============================================================================
//	NodeGraphDrawUtils functions
//============================================================================

void Engine::NodeGraphDrawUtils::DrawPinIcon(const GraphPin& pin, const NodeGraphStyle& style) {

	// Flowは向きが分かりやすい記号、それ以外は小さい丸で表示する
	const ImVec4 color = style.GetPinColor(pin.valueType);
	ImGui::TextColored(color, pin.valueType == GraphValueType::Flow ? ">" : "o");
}
