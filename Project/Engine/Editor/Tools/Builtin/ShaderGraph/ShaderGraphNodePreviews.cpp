#include "ShaderGraphNodePreviews.h"

//============================================================================
//	include
//============================================================================
#include "ShaderGraphNodePreviewUtility.h"

// c++
#include <algorithm>
// imgui
#include <imgui.h>

using namespace Engine::ShaderGraphNodePreviewUtility;

Engine::ShaderGraphNodePreviews::ShaderGraphNodePreviews() = default;

Engine::ShaderGraphNodePreviews::~ShaderGraphNodePreviews() = default;

void Engine::ShaderGraphNodePreviews::Init() {

	evaluator_.Init();
}

void Engine::ShaderGraphNodePreviews::UpdateNodePreviews(const EditorToolContext& context, const ShaderGraphAsset& graph,
	const ShaderGraphAppearanceSetting& appearance, std::string& status) {

	// 全ノードの時間入力を同じフレームへ揃える
	evaluator_.Update(context, graph, appearance.nodePreviewTextureSize, static_cast<float>(ImGui::GetTime()),
		ImGui::GetIO().DeltaTime, status);
}

void Engine::ShaderGraphNodePreviews::DrawNodePreview(
	ShaderGraphNode& node, float nodeWidth, const ShaderGraphAppearanceSetting& appearance) {

	if (!IsPreviewableNode(node.kind) || !node.previewExpanded) {

		return;
	}

	const EditorToolRenderTexture* texture = evaluator_.FindTexture(PreviewTextureName(node.id));
	const ImTextureID textureID = texture ? texture->GetImTextureID(0) : static_cast<ImTextureID>(0);
	const float displaySize = (std::min)(nodeWidth, appearance.nodePreviewDisplaySize);
	const float offsetX = (nodeWidth - displaySize) * 0.5f;
	const ImVec2 rowMinimum = ImGui::GetCursorScreenPos();
	ImGui::SetCursorScreenPos(ImVec2(rowMinimum.x + offsetX, rowMinimum.y));

	const ImVec2 imageMinimum = ImGui::GetCursorScreenPos();
	const ImVec2 imageMaximum{
		imageMinimum.x + displaySize,
		imageMinimum.y + displaySize,
	};
	ImDrawList* drawList = ImGui::GetWindowDrawList();
	constexpr float checkerSize = 8.0f;
	const ImU32 checkerColors[2]{
		IM_COL32(58, 58, 58, 255),
		IM_COL32(92, 92, 92, 255),
	};
	for (float y = imageMinimum.y; y < imageMaximum.y; y += checkerSize) {

		for (float x = imageMinimum.x; x < imageMaximum.x; x += checkerSize) {

			const int32_t column = static_cast<int32_t>((x - imageMinimum.x) / checkerSize);
			const int32_t row = static_cast<int32_t>((y - imageMinimum.y) / checkerSize);
			drawList->AddRectFilled(ImVec2(x, y),
				ImVec2((std::min)(x + checkerSize, imageMaximum.x), (std::min)(y + checkerSize, imageMaximum.y)),
				checkerColors[(column + row) & 1]);
		}
	}

	if (textureID) {
		ImGui::Image(textureID, ImVec2(displaySize, displaySize));
	} else {
		ImGui::Dummy(ImVec2(displaySize, displaySize));
	}
	drawList->AddRect(imageMinimum, imageMaximum, ImGui::GetColorU32(ImGuiCol_Border));
	ImGui::SetCursorScreenPos(ImVec2(rowMinimum.x, imageMaximum.y));
	ImGui::Dummy(ImVec2(nodeWidth, 1.0f));
}

void Engine::ShaderGraphNodePreviews::InvalidateNodePreviews() {

	evaluator_.InvalidateNodePreviews();
}

void Engine::ShaderGraphNodePreviews::ClearNodePreviews() {

	evaluator_.ClearNodePreviews();
}
