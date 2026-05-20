#include "NodeGraphView.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Graph/View/NodeGraphDrawUtils.h>
#include <Engine/Editor/Graph/View/NodeGraphInteraction.h>

// c++
#include <algorithm>
#include <cstring>
// imgui
#include <imgui.h>
#include <imgui_node_editor.h>

//============================================================================
//	NodeGraphView classMethods
//============================================================================

namespace {

	namespace ed = ax::NodeEditor;

	ed::NodeId ToNodeID(Engine::GraphID id) {

		// imgui-node-editorは専用ID型を使うため、GraphIDをそのまま詰める
		return ed::NodeId(static_cast<uintptr_t>(id));
	}

	ed::PinId ToPinID(Engine::GraphID id) {

		// Pin IDもGraphIDと同じ値で管理する
		return ed::PinId(static_cast<uintptr_t>(id));
	}

	ed::LinkId ToLinkID(Engine::GraphID id) {

		// Link IDもGraphDocument側のIDを使用する
		return ed::LinkId(static_cast<uintptr_t>(id));
	}

	Engine::GraphID FromPinID(ed::PinId id) {

		return static_cast<Engine::GraphID>(id.Get());
	}

	Engine::GraphID FromNodeID(ed::NodeId id) {

		return static_cast<Engine::GraphID>(id.Get());
	}

	Engine::GraphID FromLinkID(ed::LinkId id) {

		return static_cast<Engine::GraphID>(id.Get());
	}

	bool ContainsText(const std::string& text, const char* filter) {

		// 検索文字列が空なら全て表示する
		if (!filter || filter[0] == '\0') {
			return true;
		}
		return text.find(filter) != std::string::npos;
	}
}

bool Engine::NodeGraphView::Draw(NodeGraphContext& context, GraphDocument& document, const NodeGraphViewDesc& desc) {

	bool changed = false;

	// NodeEditorのContextをこのViewに切り替える
	ed::SetCurrentEditor(context.Get());
	ed::PushStyleVar(ed::StyleVar_NodeRounding, 7.0f);
	ed::PushStyleVar(ed::StyleVar_NodePadding, ImVec4(10.0f, 8.0f, 10.0f, 8.0f));
	ed::PushStyleVar(ed::StyleVar_LinkStrength, 85.0f);
	ed::Begin(desc.editorId, ImVec2(0.0f, 0.0f));

	for (GraphNode& node : document.nodes) {
		if (!context.IsNodePlaced(node.id)) {
			// Import直後やResetLayout後の初回だけ保存座標をNodeEditorへ反映する
			ed::SetNodePosition(ToNodeID(node.id), node.position);
			context.MarkNodePlaced(node.id);
		}
		DrawNode(document, node, desc);
		// Drag後の位置はDocumentへ戻して、Export時に保存できるようにする
		node.position = ed::GetNodePosition(ToNodeID(node.id));
		node.size = ed::GetNodeSize(ToNodeID(node.id));
	}

	// Node描画後にLinkと操作系を処理する
	DrawLinks(document);
	DrawCreateLink(document);
	DrawDelete(document);
	DrawBackgroundMenu(desc);

	ed::End();
	ed::PopStyleVar(3);
	ed::SetCurrentEditor(nullptr);

	return changed;
}

void Engine::NodeGraphView::DrawNode(GraphDocument& document, GraphNode& node, const NodeGraphViewDesc& desc) {

	(void)document;

	ed::BeginNode(ToNodeID(node.id));

	const ImVec4 accentColor = style_.GetNodeAccentColor(node.type);
	ImGui::PushStyleVar(ImGuiStyleVar_Alpha, node.enabled ? 1.0f : 0.45f);

	// タイトル部。Unity系GraphViewに寄せて、種類色を左に出す
	ImGui::TextColored(accentColor, "■");
	ImGui::SameLine();
	ImGui::TextUnformatted(node.displayName.empty() ? node.type.c_str() : node.displayName.c_str());

	if (!node.type.empty()) {
		ImGui::TextDisabled("%s", node.type.c_str());
	}

	if (desc.drawNodeProperty) {
		// Tool固有の簡易PropertyはNode内部へ表示する
		ImGui::Separator();
		desc.drawNodeProperty(node);
	}

	// 入力ピンと出力ピンは左右に並べる
	ImGui::Separator();
	const size_t lineCount = std::max(node.inputs.size(), node.outputs.size());
	for (size_t i = 0; i < lineCount; ++i) {

		ImGui::PushID(static_cast<int>(i));

		if (i < node.inputs.size()) {
			GraphPin& pin = node.inputs[i];
			ed::BeginPin(ToPinID(pin.id), ed::PinKind::Input);
			NodeGraphDrawUtils::DrawPinIcon(pin, style_);
			ImGui::SameLine();
			ImGui::TextUnformatted(pin.name.c_str());
			ed::EndPin();
		} else {
			ImGui::Dummy(ImVec2(80.0f, ImGui::GetTextLineHeight()));
		}

		ImGui::SameLine(180.0f);

		if (i < node.outputs.size()) {
			GraphPin& pin = node.outputs[i];
			ed::BeginPin(ToPinID(pin.id), ed::PinKind::Output);
			ImGui::TextUnformatted(pin.name.c_str());
			ImGui::SameLine();
			NodeGraphDrawUtils::DrawPinIcon(pin, style_);
			ed::EndPin();
		}

		ImGui::PopID();
	}

	if (desc.drawNodeDropTarget) {
		// Materialなど、Node上へ直接Dropする処理はTool側へ委譲する
		desc.drawNodeDropTarget(node);
	}

	// Validationのメッセージはノード下部に出して、どのノードの問題かすぐ分かるようにする
	if (!node.validationMessages.empty()) {
		ImGui::Separator();
		for (const std::string& message : node.validationMessages) {
			ImGui::TextColored(style_.GetErrorColor(), "! %s", message.c_str());
		}
	}

	ImGui::PopStyleVar();
	ed::EndNode();
}

void Engine::NodeGraphView::DrawLinks(const GraphDocument& document) {

	for (const GraphLink& link : document.links) {

		// 接続元Pinの型でLink色と太さを決める
		const GraphPin* pin = document.FindPin(link.fromPinID);
		const GraphValueType valueType = pin ? pin->valueType : GraphValueType::Unknown;
		const float thickness = valueType == GraphValueType::Flow ? 3.0f : 2.0f;
		ed::Link(ToLinkID(link.id), ToPinID(link.fromPinID), ToPinID(link.toPinID),
			style_.GetLinkColor(valueType), thickness);
	}
}

void Engine::NodeGraphView::DrawCreateLink(GraphDocument& document) {

	// imgui-node-editorはBeginCreateがfalseを返すフレームでもEndCreateが必要。
	// ここで早期returnすると内部のCreateItemActionが閉じず、次フレームのBeginCreateでassertする。
	const bool creating = ed::BeginCreate(style_.GetLinkColor(GraphValueType::Flow), 2.0f);
	if (creating) {

		ed::PinId startId{};
		ed::PinId endId{};
		if (ed::QueryNewLink(&startId, &endId)) {

			// QueryNewLinkはドラッグ方向を問わず返すため、あとでOutput -> Inputへ揃える
			GraphID startPin = FromPinID(startId);
			GraphID endPin = FromPinID(endId);
			const GraphPin* start = document.FindPin(startPin);
			const GraphPin* end = document.FindPin(endPin);

			// ドラッグ方向が逆でもOutput -> Inputに正規化する
			if (start && end && start->kind == GraphPinKind::Input && end->kind == GraphPinKind::Output) {
				std::swap(startPin, endPin);
			}

			std::string reason{};
			if (document.CanCreateLink(startPin, endPin, &reason)) {
				// Acceptされた瞬間だけDocumentへLinkを追加する
				if (ed::AcceptNewItem(style_.GetLinkColor(document.FindPin(startPin)->valueType), 2.0f)) {
					NodeGraphInteraction::TryCreateLink(document, startPin, endPin);
				}
			} else {
				ed::RejectNewItem(style_.GetErrorColor(), 2.0f);
			}
		}
	}
	ed::EndCreate();
}

void Engine::NodeGraphView::DrawDelete(GraphDocument& document) {

	// BeginDeleteもBeginCreateと同じくBegin/Endの対を守る
	const bool deleting = ed::BeginDelete();
	if (deleting) {

		ed::LinkId linkID{};
		while (ed::QueryDeletedLink(&linkID)) {
			// Deleteキーなどで削除されたLinkをDocumentへ反映する
			if (ed::AcceptDeletedItem()) {
				NodeGraphInteraction::TryDeleteLink(document, FromLinkID(linkID));
			}
		}

		ed::NodeId nodeID{};
		while (ed::QueryDeletedNode(&nodeID)) {
			// Node削除時はDocument側で関連Linkもまとめて削除する
			if (ed::AcceptDeletedItem()) {
				NodeGraphInteraction::TryDeleteNode(document, FromNodeID(nodeID));
			}
		}
	}
	ed::EndDelete();
}

void Engine::NodeGraphView::DrawBackgroundMenu(const NodeGraphViewDesc& desc) {

	if (!desc.registry || !desc.addNodeRequested) {
		return;
	}

	ed::Suspend();
	if (ed::ShowBackgroundContextMenu()) {
		// NodeEditorの描画を一時停止してImGui Popupを表示する
		ImGui::OpenPopup("NodeGraphAddNodeMenu");
	}

	if (ImGui::BeginPopup("NodeGraphAddNodeMenu")) {

		// 検索文字列に一致するNode定義だけ表示する
		ImGui::TextUnformatted("Add Node");
		ImGui::SetNextItemWidth(220.0f);
		ImGui::InputText("##NodeSearch", nodeSearchText_, sizeof(nodeSearchText_));
		ImGui::Separator();

		for (const GraphNodeDefinition* definition : desc.registry->GetCreatableDefinitions()) {
			if (!definition || !ContainsText(definition->displayName, nodeSearchText_)) {
				continue;
			}

			if (!definition->category.empty()) {
				ImGui::TextDisabled("%s", definition->category.c_str());
			}
			if (ImGui::MenuItem(definition->displayName.c_str())) {
				desc.addNodeRequested(definition->type, ed::ScreenToCanvas(ImGui::GetMousePos()));
				nodeSearchText_[0] = '\0';
			}
		}
		ImGui::EndPopup();
	}
	ed::Resume();
}
