#include "ShaderGraphCanvasView.h"

//============================================================================
//	include
//============================================================================
#include "ShaderGraphEditSession.h"
#include "ShaderGraphCanvasContext.h"
#include "ShaderGraphAppearanceEditor.h"
#include "ShaderGraphNodePreviews.h"
#include "ShaderGraphNodeDrawer.h"
#include "ShaderGraphGroupEditor.h"
#include "ShaderGraphCanvasMenu.h"
#include "ShaderGraphCanvasID.h"
#include "ShaderGraphEditOperations.h"
#include <Engine/Editor/Tools/Core/IEditorTool.h>

// c++
#include <algorithm>
// imgui
#include <imgui.h>
#include <imgui_node_editor.h>

using namespace Engine::ShaderGraphCanvasID;
using namespace Engine::ShaderGraphAppearance;

namespace {

	namespace ed = ax::NodeEditor;
}

Engine::ShaderGraphCanvasView::ShaderGraphCanvasView(ShaderGraphEditSession& editSession, ShaderGraphCanvasContext& canvas,
	ShaderGraphAppearanceEditor& appearanceEditor, ShaderGraphNodePreviews& nodePreviews, ShaderGraphNodeDrawer& nodeDrawer,
	ShaderGraphGroupEditor& groupEditor, ShaderGraphCanvasMenu& canvasMenu)
	: editSession_(editSession), canvas_(canvas), appearanceEditor_(appearanceEditor), nodePreviews_(nodePreviews),
	  nodeDrawer_(nodeDrawer), groupEditor_(groupEditor), canvasMenu_(canvasMenu) {
}

std::vector<Engine::ShaderGraphCanvasCommand> Engine::ShaderGraphCanvasView::Draw(
	const EditorToolContext& context, bool commandPanelFocused) {

	std::vector<ShaderGraphCanvasCommand> commands;
	if (!editSession_.IsLoaded()) {
		return commands;
	}
	canvas_.EnsureCreated();

	nodePreviews_.UpdateNodePreviews(
		context, editSession_.GetDraft(), appearanceEditor_.GetSettings(), editSession_.GetStatusMessage());
	ed::SetCurrentEditor(canvas_.GetEditor());
	appearanceEditor_.Apply();
	ed::Begin("ShaderGraphNodeEditor");
	nodeDrawer_.ClearPins();
	for (ShaderGraphNode& node : editSession_.GetDraft().nodes) {
		nodeDrawer_.DrawNode(node);
	}
	for (ShaderGraphGroup& group : editSession_.GetDraft().groups) {
		groupEditor_.DrawGroup(group);
	}
	for (const ShaderGraphLink& link : editSession_.GetDraft().links) {
		ed::Link(ed::LinkId(ToNodeEditorID(link.id)), ed::PinId(MakePinID(link.outputNode, false, link.outputSlot)),
			ed::PinId(MakePinID(link.inputNode, true, link.inputSlot)), ToImVec4(appearanceEditor_.GetSettings().link),
			appearanceEditor_.GetSettings().linkThickness);
	}

	canvas_.RestorePositions(editSession_.GetDraft());

	const ImVec4 linkColor = ToImVec4(appearanceEditor_.GetSettings().link);
	if (ed::BeginCreate(linkColor, appearanceEditor_.GetSettings().linkThickness)) {

		ed::PinId firstPin{};
		ed::PinId secondPin{};
		if (ed::QueryNewLink(&firstPin, &secondPin, linkColor, appearanceEditor_.GetSettings().linkThickness) && firstPin &&
			secondPin) {

			const auto first = nodeDrawer_.GetPinAddresses().find(firstPin.Get());
			const auto second = nodeDrawer_.GetPinAddresses().find(secondPin.Get());
			if (first != nodeDrawer_.GetPinAddresses().end() && second != nodeDrawer_.GetPinAddresses().end() &&
				first->second.input != second->second.input) {

				const ShaderGraphPinAddress& input = first->second.input ? first->second : second->second;
				const ShaderGraphPinAddress& output = first->second.input ? second->second : first->second;
				if (ed::AcceptNewItem(linkColor, appearanceEditor_.GetSettings().linkThickness)) {

					std::erase_if(editSession_.GetDraft().links, [&](const ShaderGraphLink& link) {
						return link.inputNode == input.node && link.inputSlot == input.slot;
					});
					editSession_.GetDraft().links.emplace_back(ShaderGraphLink{
						.id = UUID::New(),
						.outputNode = output.node,
						.outputSlot = output.slot,
						.inputNode = input.node,
						.inputSlot = input.slot,
					});
					editSession_.MarkDirty();
				}
			} else {
				ed::RejectNewItem(ImVec4(1.0f, 0.25f, 0.25f, 1.0f), appearanceEditor_.GetSettings().linkThickness);
			}
		}
	}
	ed::EndCreate();

	if (commandPanelFocused) {
		if (ed::BeginDelete()) {
			ed::LinkId linkID{};
			while (ed::QueryDeletedLink(&linkID)) {
				if (ed::AcceptDeletedItem()) {
					const uint64_t id = static_cast<uint64_t>(linkID.Get());
					std::erase_if(
						editSession_.GetDraft().links, [&](const ShaderGraphLink& link) { return link.id.value == id; });
					editSession_.MarkDirty();
				}
			}

			ed::NodeId nodeID{};
			while (ed::QueryDeletedNode(&nodeID)) {
				const UUID id{static_cast<uint64_t>(nodeID.Get())};
				if (id == editSession_.GetDraft().outputNode) {
					ed::RejectDeletedItem();
				} else if (ed::AcceptDeletedItem()) {
					const auto group = std::find_if(editSession_.GetDraft().groups.begin(),
						editSession_.GetDraft().groups.end(), [&](const ShaderGraphGroup& value) { return value.id == id; });
					if (group != editSession_.GetDraft().groups.end()) {
						groupEditor_.RemoveGroup(id);
					} else {
						editSession_.RemoveNode(id);
					}
				}
			}
		}
		ed::EndDelete();
	}

	const ImVec2 canvasMousePosition = ImGui::GetMousePos();
	ed::Suspend();
	nodeDrawer_.DrawPopup();
	ed::NodeId contextNodeID{};
	if (ed::ShowNodeContextMenu(&contextNodeID)) {
		canvasMenu_.OpenNode(UUID{static_cast<uint64_t>(contextNodeID.Get())});
	} else if (ed::ShowBackgroundContextMenu()) {
		canvasMenu_.OpenCreate(Vector2(canvasMousePosition.x, canvasMousePosition.y));
	}
	canvasMenu_.Draw();
	ed::Resume();
	ed::End();
	const ImGuiIO& io = ImGui::GetIO();
	if (commandPanelFocused && !io.WantTextInput && !ImGui::IsAnyItemActive() && io.KeyCtrl) {
		if (!io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_C, false)) {
			commands.emplace_back(ShaderGraphCanvasCommand::Copy);
		}
		if (!io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_V, false)) {
			commands.emplace_back(ShaderGraphCanvasCommand::Paste);
		}
		if (!io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_D, false)) {
			commands.emplace_back(ShaderGraphCanvasCommand::Duplicate);
		}
		if (!io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_S, false)) {
			commands.emplace_back(ShaderGraphCanvasCommand::Save);
		}
		if (ImGui::IsKeyPressed(ImGuiKey_Z, false)) {
			if (io.KeyShift) {
				commands.emplace_back(ShaderGraphCanvasCommand::Redo);
			} else {
				commands.emplace_back(ShaderGraphCanvasCommand::Undo);
			}
		}
		if (!io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_Y, false)) {
			commands.emplace_back(ShaderGraphCanvasCommand::Redo);
		}
	}
	return commands;
}

void Engine::ShaderGraphCanvasView::DrawDiagnostics() {

	if (editSession_.GetDiagnostics().empty()) {
		return;
	}
	ImGui::SeparatorText("コンパイル診断");
	for (uint32_t index = 0; index < editSession_.GetDiagnostics().size(); ++index) {

		const ShaderGraphDiagnostic& diagnostic = editSession_.GetDiagnostics()[index];
		const char* prefix = diagnostic.severity == ShaderGraphDiagnosticSeverity::Error
								 ? "エラー"
								 : (diagnostic.severity == ShaderGraphDiagnosticSeverity::Warning ? "警告" : "情報");
		ImGui::PushID(static_cast<int>(index));
		if (ImGui::Selectable((std::string(prefix) + ": " + diagnostic.message).c_str())) {

			canvas_.NavigateToNode(diagnostic.node);
		}
		ImGui::PopID();
	}
}
