#include "ShaderGraphNodePreviews.h"

//============================================================================
//	include
//============================================================================
#include "ShaderGraphNodePreviewUtility.h"
#include <Engine/Editor/UI/ImGui/EditorPalette.h>

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

	if (!IsPreviewableNode(node) || !node.previewExpanded) {

		return;
	}

	const ShaderGraphNodePreview* preview = evaluator_.FindPreview(node.id);
	const ImTextureID textureID = preview ? preview->textureID : static_cast<ImTextureID>(0);
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
	if (textureID) {
		constexpr float checkerSize = 8.0f;
		const ImU32 checkerColors[2]{EditorPalette::U32("Panels.Checker.Dark"), EditorPalette::U32("Panels.Checker.Light")};
		for (float y = imageMinimum.y; y < imageMaximum.y; y += checkerSize) {
			for (float x = imageMinimum.x; x < imageMaximum.x; x += checkerSize) {
				const int32_t column = static_cast<int32_t>((x - imageMinimum.x) / checkerSize);
				const int32_t row = static_cast<int32_t>((y - imageMinimum.y) / checkerSize);
				drawList->AddRectFilled(ImVec2(x, y),
					ImVec2((std::min)(x + checkerSize, imageMaximum.x), (std::min)(y + checkerSize, imageMaximum.y)),
					checkerColors[(column + row) & 1]);
			}
		}
		ImGui::Image(textureID, ImVec2(displaySize, displaySize),
			ImVec2(preview->uvMinimum.x, preview->uvMinimum.y), ImVec2(preview->uvMaximum.x, preview->uvMaximum.y));
	} else {
		drawList->AddRectFilled(imageMinimum, imageMaximum, EditorPalette::U32("Graph.previewBackground"));
		ImGui::InvisibleButton("##NodePreviewStatus", ImVec2(displaySize, displaySize));
		const char* message = preview && !preview->diagnostic.empty() ? "プレビューエラー" : "プレビューを準備中";
		const float textWidth = (std::max)(1.0f, displaySize - 8.0f);
		const ImVec2 textSize = ImGui::CalcTextSize(message, nullptr, false, textWidth);
		drawList->AddText(nullptr, 0.0f, ImVec2(imageMinimum.x + (displaySize - textSize.x) * 0.5f,
			imageMinimum.y + (displaySize - textSize.y) * 0.5f),
			ImGui::GetColorU32(ImGuiCol_TextDisabled), message, nullptr, textWidth);
	}
	if (preview && !preview->diagnostic.empty() && ImGui::IsItemHovered()) {
		hoveredDiagnostic_ = preview->diagnostic;
	}
	drawList->AddRect(imageMinimum, imageMaximum, ImGui::GetColorU32(ImGuiCol_Border));
	ImGui::SetCursorScreenPos(ImVec2(rowMinimum.x, imageMaximum.y));
	ImGui::Dummy(ImVec2(nodeWidth, 1.0f));
}

void Engine::ShaderGraphNodePreviews::DrawTooltip() {

	if (hoveredDiagnostic_.empty()) return;
	ImGui::BeginTooltip();
	ImGui::PushTextWrapPos(480.0f);
	ImGui::TextUnformatted(hoveredDiagnostic_.c_str());
	ImGui::PopTextWrapPos();
	ImGui::EndTooltip();
	hoveredDiagnostic_.clear();
}

void Engine::ShaderGraphNodePreviews::InvalidateNodePreviews() {

	evaluator_.InvalidateNodePreviews();
}

void Engine::ShaderGraphNodePreviews::ClearNodePreviews() {

	evaluator_.ClearNodePreviews();
	hoveredDiagnostic_.clear();
}
