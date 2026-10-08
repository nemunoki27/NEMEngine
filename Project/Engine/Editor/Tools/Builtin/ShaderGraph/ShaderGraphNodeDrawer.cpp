#include "ShaderGraphNodeDrawer.h"

//============================================================================
//	include
//============================================================================
#include "ShaderGraphCanvasID.h"
#include "ShaderGraphNodePreviewUtility.h"
#include <Engine/Core/Rendering/ShaderGraph/ShaderGraphNodeRegistry.h>

// c++
#include <algorithm>
#include <string>
// imgui
#include <imgui.h>
#include <imgui_node_editor.h>

using namespace Engine::ShaderGraphCanvasID;
using namespace Engine::ShaderGraphNodePreviewUtility;

namespace {

	namespace ed = ax::NodeEditor;
	constexpr float kNodePinColumnGap = 32.0f;
}

Engine::ShaderGraphNodeDrawer::ShaderGraphNodeDrawer(
	ShaderGraphEditSession& session, const ShaderGraphAppearanceSetting& settings, ShaderGraphNodePreviews& previews)
	: session_(session), settings_(settings), previews_(previews) {
}

void Engine::ShaderGraphNodeDrawer::DrawNode(ShaderGraphNode& node) {

	ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * settings_.nodeTextScale);
	ed::BeginNode(ed::NodeId(ToNodeEditorID(node.id)));
	ImGui::PushID(static_cast<int>(node.id.value));
	const float nodeWidth = CalculateNodeWidth(node);
	const ImVec2 headerMinimum = ImGui::GetCursorScreenPos();
	ImGui::TextUnformatted(GetShaderGraphNodeName(node.kind).data());
	float headerHeight = ImGui::GetItemRectSize().y;
	if (IsPreviewableNode(node.kind)) {

		const float buttonSize = ImGui::GetFrameHeight();
		ImGui::SetCursorScreenPos(ImVec2(headerMinimum.x + nodeWidth - buttonSize, headerMinimum.y));
		if (ImGui::ArrowButton("##NodePreview", node.previewExpanded ? ImGuiDir_Down : ImGuiDir_Right)) {

			node.previewExpanded = !node.previewExpanded;
			session_.MarkDirty();
		}
		headerHeight = (std::max)(headerHeight, ImGui::GetItemRectSize().y);
	}
	ImGui::SetCursorScreenPos(ImVec2(headerMinimum.x, headerMinimum.y + headerHeight));
	DrawNodeSeparator(nodeWidth);

	if (node.kind == ShaderGraphNodeKind::Parameter) {
		const auto parameter = std::find_if(session_.GetDraft().parameters.begin(), session_.GetDraft().parameters.end(),
			[&](const ShaderGraphParameter& value) { return value.id == node.parameterID; });
		if (parameter != session_.GetDraft().parameters.end()) {
			ImGui::TextDisabled("%s", parameter->name.c_str());
		}
	}
	if (node.kind == ShaderGraphNodeKind::Keyword) {
		const auto keyword = std::find_if(session_.GetDraft().keywords.begin(), session_.GetDraft().keywords.end(),
			[&](const ShaderGraphKeyword& value) { return value.id == node.keywordID; });
		if (keyword != session_.GetDraft().keywords.end()) {
			ImGui::TextDisabled("%s", keyword->name.c_str());
		}
	}
	if (node.kind == ShaderGraphNodeKind::CustomFunction && !node.functionName.empty()) {
		ImGui::TextDisabled("%s", node.functionName.c_str());
	}
	if (node.kind == ShaderGraphNodeKind::Constant || node.kind == ShaderGraphNodeKind::TextureSample) {

		nodeValueEditor_.DrawValue(session_, node, nodeWidth);
	}
	DrawNodePins(node, nodeWidth);
	previews_.DrawNodePreview(node, nodeWidth, settings_);
	ImGui::PopID();
	ed::EndNode();
	ImGui::PopFont();
}

void Engine::ShaderGraphNodeDrawer::DrawNodePins(const ShaderGraphNode& node, float nodeWidth) {

	const uint32_t inputCount = GetShaderGraphInputCount(node);
	const uint32_t outputCount = GetShaderGraphOutputCount(node);
	const uint32_t rowCount = (std::max)(inputCount, outputCount);
	if (rowCount == 0) {
		ImGui::Dummy(ImVec2(nodeWidth, 1.0f));
		return;
	}

	const ImVec2 rowsMinimum = ImGui::GetCursorScreenPos();
	const float rowHeight = ImGui::GetTextLineHeightWithSpacing();
	for (uint32_t row = 0; row < rowCount; ++row) {

		const float rowY = rowsMinimum.y + rowHeight * row;
		if (row < inputCount) {
			const std::string name = "> " + std::string(GetShaderGraphInputName(node, row));
			ImGui::SetCursorScreenPos(ImVec2(rowsMinimum.x, rowY));

			const uintptr_t pinID = MakePinID(node.id, true, row);
			pinAddresses_[pinID] = ShaderGraphPinAddress{node.id, row, true};
			ed::BeginPin(ed::PinId(pinID), ed::PinKind::Input);
			ImGui::TextUnformatted(name.c_str());
			const ImVec2 pinMinimum = ImGui::GetItemRectMin();
			const ImVec2 pinMaximum = ImGui::GetItemRectMax();
			const ImVec2 pinCenter{
				pinMinimum.x + settings_.linkEndOffset.x,
				(pinMinimum.y + pinMaximum.y) * 0.5f + settings_.linkEndOffset.y,
			};
			ed::PinPivotRect(pinCenter, pinCenter);
			ed::EndPin();
		}
		if (row < outputCount) {
			const std::string name = std::string(GetShaderGraphOutputName(node, row)) + " >";
			const float textWidth = ImGui::CalcTextSize(name.c_str()).x;
			ImGui::SetCursorScreenPos(ImVec2(rowsMinimum.x + nodeWidth - textWidth, rowY));

			const uintptr_t pinID = MakePinID(node.id, false, row);
			pinAddresses_[pinID] = ShaderGraphPinAddress{node.id, row, false};
			ed::BeginPin(ed::PinId(pinID), ed::PinKind::Output);
			ImGui::TextUnformatted(name.c_str());
			const ImVec2 pinMinimum = ImGui::GetItemRectMin();
			const ImVec2 pinMaximum = ImGui::GetItemRectMax();
			const ImVec2 pinCenter{
				pinMaximum.x + settings_.linkStartOffset.x,
				(pinMinimum.y + pinMaximum.y) * 0.5f + settings_.linkStartOffset.y,
			};
			ed::PinPivotRect(pinCenter, pinCenter);
			ed::EndPin();
		}
	}

	ImGui::SetCursorScreenPos(ImVec2(rowsMinimum.x, rowsMinimum.y + rowHeight * rowCount));
	ImGui::Dummy(ImVec2(nodeWidth, 1.0f));
}

void Engine::ShaderGraphNodeDrawer::DrawNodeSeparator(float nodeWidth) const {

	const ImVec2 minimum = ImGui::GetCursorScreenPos();
	const float height = ImGui::GetStyle().ItemSpacing.y;
	const float lineY = minimum.y + height * 0.5f;
	ImGui::GetWindowDrawList()->AddLine(
		ImVec2(minimum.x, lineY), ImVec2(minimum.x + nodeWidth, lineY), ImGui::GetColorU32(ImGuiCol_Separator));
	ImGui::Dummy(ImVec2(nodeWidth, height));
}

float Engine::ShaderGraphNodeDrawer::CalculateNodeWidth(const ShaderGraphNode& node) const {

	float nodeWidth = (std::max)(settings_.nodeMinimumWidth,
		ImGui::CalcTextSize(GetShaderGraphNodeName(node.kind).data()).x +
			(IsPreviewableNode(node.kind) ? ImGui::GetStyle().ItemSpacing.x + ImGui::GetFrameHeight() : 0.0f));
	if (node.kind == ShaderGraphNodeKind::Parameter) {
		const auto parameter = std::find_if(session_.GetDraft().parameters.begin(), session_.GetDraft().parameters.end(),
			[&](const ShaderGraphParameter& value) { return value.id == node.parameterID; });
		if (parameter != session_.GetDraft().parameters.end()) {
			nodeWidth = (std::max)(nodeWidth, ImGui::CalcTextSize(parameter->name.c_str()).x);
		}
	}
	if (IsPreviewableNode(node.kind) && node.previewExpanded) {
		nodeWidth = (std::max)(nodeWidth, settings_.nodePreviewDisplaySize);
	}

	float inputWidth = 0.0f;
	for (uint32_t slot = 0; slot < GetShaderGraphInputCount(node); ++slot) {

		const std::string name = "> " + std::string(GetShaderGraphInputName(node, slot));
		inputWidth = (std::max)(inputWidth, ImGui::CalcTextSize(name.c_str()).x);
	}
	float outputWidth = 0.0f;
	for (uint32_t slot = 0; slot < GetShaderGraphOutputCount(node); ++slot) {

		const std::string name = std::string(GetShaderGraphOutputName(node, slot)) + " >";
		outputWidth = (std::max)(outputWidth, ImGui::CalcTextSize(name.c_str()).x);
	}
	if (inputWidth != 0.0f && outputWidth != 0.0f) {

		nodeWidth = (std::max)(nodeWidth, inputWidth + kNodePinColumnGap + outputWidth);
	} else {
		nodeWidth = (std::max)(nodeWidth, (std::max)(inputWidth, outputWidth));
	}
	return nodeWidth;
}

void Engine::ShaderGraphNodeDrawer::DrawPopup() {

	nodeValueEditor_.DrawPopup(session_);
}

void Engine::ShaderGraphNodeDrawer::ClearPins() {

	pinAddresses_.clear();
}

void Engine::ShaderGraphNodeDrawer::Reset() {

	// Graph切替後へPinとpopupの参照を持ち越さない
	pinAddresses_.clear();
	nodeValueEditor_.Reset();
}
