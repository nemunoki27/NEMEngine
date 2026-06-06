#include "NodeGraphView.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Graph/View/NodeGraphDrawUtils.h>
#include <Engine/Editor/Graph/View/NodeGraphInteraction.h>

// c++
#include <algorithm>
#include <cmath>
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

	bool IsSameVec2(const ImVec2& lhs, const ImVec2& rhs) {

		constexpr float kEpsilon = 0.01f;
		return std::abs(lhs.x - rhs.x) <= kEpsilon && std::abs(lhs.y - rhs.y) <= kEpsilon;
	}

	void DrawNodeSeparator(float width) {

		const ImVec2 cursor = ImGui::GetCursorScreenPos();
		const float y = cursor.y + ImGui::GetStyle().ItemSpacing.y * 0.5f;
		ImGui::GetWindowDrawList()->AddLine(ImVec2(cursor.x, y), ImVec2(cursor.x + width, y),
			ImGui::GetColorU32(ImGuiCol_Separator));
		ImGui::Dummy(ImVec2(width, ImGui::GetStyle().ItemSpacing.y));
	}
}

bool Engine::NodeGraphView::Draw(NodeGraphContext& context, GraphDocument& document, const NodeGraphViewDesc& desc) {

	bool changed = false;

	// NodeEditorのContextをこのViewに切り替える
	ed::SetCurrentEditor(context.Get());
	style_.PushEditorStyle();
	ed::Begin(desc.editorId, ImVec2(0.0f, 0.0f));

	// グループNodeは他のNodeより先に描画して背面に表示する
	for (GraphNode& node : document.nodes) {
		if (!desc.isGroupNode || !desc.isGroupNode(node)) {
			continue;
		}
		if (!context.IsNodePlaced(node.id)) {
			ed::SetNodePosition(ToNodeID(node.id), node.position);
			context.MarkNodePlaced(node.id);
		}
		DrawNode(document, node, desc);
		const ImVec2 newPosition = ed::GetNodePosition(ToNodeID(node.id));
		const ImVec2 newSize = ed::GetNodeSize(ToNodeID(node.id));
		if (!IsSameVec2(node.position, newPosition) || !IsSameVec2(node.size, newSize)) {
			changed = true;
		}
		node.position = newPosition;
		node.size = newSize;
	}
	for (GraphNode& node : document.nodes) {
		if (desc.isGroupNode && desc.isGroupNode(node)) {
			continue;
		}
		if (!context.IsNodePlaced(node.id)) {
			// Import直後やResetLayout後の初回だけ保存座標をNodeEditorへ反映する
			ed::SetNodePosition(ToNodeID(node.id), node.position);
			context.MarkNodePlaced(node.id);
		}
		DrawNode(document, node, desc);
		// Drag後の位置はDocumentへ戻して、Export時に保存できるようにする
		const ImVec2 newPosition = ed::GetNodePosition(ToNodeID(node.id));
		const ImVec2 newSize = ed::GetNodeSize(ToNodeID(node.id));
		if (!IsSameVec2(node.position, newPosition) || !IsSameVec2(node.size, newSize)) {
			changed = true;
		}
		node.position = newPosition;
		node.size = newSize;
	}

	// Node描画後にLinkと操作系を処理する
	DrawLinks(document);
	changed |= DrawCreateLink(document);
	changed |= DrawDelete(document);
	changed |= DrawBackgroundMenu(desc);

	ed::End();
	if (desc.navigateToContent) {
		ed::NavigateToContent(style_.scrollDuration);
	}
	style_.PopEditorStyle();
	ed::SetCurrentEditor(nullptr);

	return changed;
}

void Engine::NodeGraphView::DrawGroupNode(GraphNode& node, const NodeGraphViewDesc& desc) {

	(void)desc;

	const float r = node.properties.value("colorR", 0.25f);
	const float g = node.properties.value("colorG", 0.38f);
	const float b = node.properties.value("colorB", 0.55f);
	const float a = node.properties.value("colorA", 0.30f);

	// グループ背景 / 枠線色をNodeごとに上書きする
	ed::PushStyleColor(ed::StyleColor_GroupBg, ImVec4(r, g, b, a));
	ed::PushStyleColor(ed::StyleColor_GroupBorder,
		ImVec4((std::min)(1.0f, r + 0.15f), (std::min)(1.0f, g + 0.15f), (std::min)(1.0f, b + 0.15f), 0.80f));

	ed::BeginNode(ToNodeID(node.id));
	ImGui::PushID(reinterpret_cast<void*>(static_cast<uintptr_t>(node.id)));
	ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 1.0f);

	const std::string& title = node.properties.value("title", node.displayName);
	const ImVec4 titleColor{
		(std::min)(1.0f, r + 0.45f),
		(std::min)(1.0f, g + 0.45f),
		(std::min)(1.0f, b + 0.45f),
		1.0f,
	};
	ImGui::TextColored(titleColor, "%s", title.empty() ? "Group" : title.c_str());

	// グループ内部サイズ: 保存済みの合計サイズから余白とタイトル高さを差し引く
	constexpr float kDefaultGroupW = 300.0f;
	constexpr float kDefaultGroupH = 150.0f;
	const float titleH = ImGui::GetTextLineHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y;
	const float padX = style_.nodePadding.x + style_.nodePadding.z;
	const float padY = style_.nodePadding.y + style_.nodePadding.w;
	const float groupW = node.size.x > padX + 20.0f ? node.size.x - padX : kDefaultGroupW;
	const float groupH = node.size.y > titleH + padY + 20.0f ? node.size.y - titleH - padY : kDefaultGroupH;
	ed::Group(ImVec2(groupW, groupH));

	ImGui::PopStyleVar();
	ImGui::PopID();
	ed::EndNode();
	ed::PopStyleColor(2);
}

void Engine::NodeGraphView::DrawNode(GraphDocument& document, GraphNode& node, const NodeGraphViewDesc& desc) {

	(void)document;

	if (desc.isGroupNode && desc.isGroupNode(node)) {
		DrawGroupNode(node, desc);
		return;
	}

	ed::BeginNode(ToNodeID(node.id));
	ImGui::PushID(reinterpret_cast<void*>(static_cast<uintptr_t>(node.id)));

	const ImVec4 accentColor = style_.GetNodeAccentColor(node.type);
	ImGui::PushStyleVar(ImGuiStyleVar_Alpha, node.enabled ? 1.0f : style_.disabledNodeAlpha);

	// タイトル部。Unity系GraphViewに寄せて、種類色を左に出す
	ImGui::TextColored(accentColor, node.displayName.empty() ? node.type.c_str() : node.displayName.c_str());
	if (desc.isNodeHighlighted && desc.isNodeHighlighted(node)) {
		ImGui::SameLine();
		ImGui::TextColored(style_.GetWarningColor(), "*");
	}

	if (!node.type.empty()) {
		ImGui::TextDisabled("%s", node.type.c_str());
	}

	if (desc.drawNodeProperty) {
		// Tool固有の簡易PropertyはNode内部へ表示する
		DrawNodeSeparator(style_.nodeWidth);
		desc.drawNodeProperty(node);
	}

	// 入力ピンと出力ピンは左右に並べる
	DrawNodeSeparator(style_.nodeWidth);
	const size_t lineCount = std::max(node.inputs.size(), node.outputs.size());
	for (size_t i = 0; i < lineCount; ++i) {

		ImGui::PushID(static_cast<int>(i));

		if (i < node.inputs.size()) {
			GraphPin& pin = node.inputs[i];
			ed::BeginPin(ToPinID(pin.id), ed::PinKind::Input);
			// 入力Pinは左側へ接続口を出すため、Outputとは別のPivotを使う
			ed::PinPivotAlignment(style_.inputPivotAlignment);
			NodeGraphDrawUtils::DrawPinIcon(pin, style_);
			ImGui::SameLine();
			ImGui::TextUnformatted(pin.name.c_str());
			ed::EndPin();
		} else {
			ImGui::Dummy(ImVec2(80.0f, ImGui::GetTextLineHeight()));
		}

		ImGui::SameLine((std::max)(120.0f, style_.nodeWidth - 80.0f));

		if (i < node.outputs.size()) {
			GraphPin& pin = node.outputs[i];
			ed::BeginPin(ToPinID(pin.id), ed::PinKind::Output);
			// 出力Pinは右側へ接続口を出すため、正方向のPivotを使う
			ed::PinPivotAlignment(style_.outputPivotAlignment);
			ImGui::TextUnformatted(pin.name.c_str());
			ImGui::SameLine();
			NodeGraphDrawUtils::DrawPinIcon(pin, style_);
			ed::EndPin();
		}

		ImGui::PopID();
	}

	// Nodeの横幅を揃えるため、内容が少ないNodeにも最小幅を持たせる
	ImGui::Dummy(ImVec2(style_.nodeWidth, 0.0f));

	if (desc.drawNodeDropTarget) {
		// Materialなど、Node上へ直接Dropする処理はTool側へ委譲する
		desc.drawNodeDropTarget(node);
	}

	// Validationのメッセージはノード下部に出して、どのノードの問題かすぐ分かるようにする
	if (!node.validationMessages.empty()) {
		DrawNodeSeparator(style_.nodeWidth);
		for (const std::string& message : node.validationMessages) {
			ImGui::TextColored(style_.GetErrorColor(), "! %s", message.c_str());
		}
	}

	ImGui::PopStyleVar();
	ImGui::PopID();
	ed::EndNode();
}

void Engine::NodeGraphView::DrawLinks(const GraphDocument& document) {

	for (const GraphLink& link : document.links) {

		// 接続元Pinの型でLink色と太さを決める
		const GraphPin* pin = document.FindPin(link.fromPinID);
		const GraphValueType valueType = pin ? pin->valueType : GraphValueType::Unknown;
		const float thickness = valueType == GraphValueType::Flow ? style_.flowLinkThickness : style_.linkThickness;
		ed::Link(ToLinkID(link.id), ToPinID(link.fromPinID), ToPinID(link.toPinID),
			style_.GetLinkColor(valueType), thickness);
	}
}

bool Engine::NodeGraphView::DrawCreateLink(GraphDocument& document) {

	bool changed = false;
	// imgui-node-editorはBeginCreateがfalseを返すフレームでもEndCreateが必要
	// ここで早期returnすると内部のCreateItemActionが閉じず、次フレームのBeginCreateでassertする
	const bool creating = ed::BeginCreate(style_.GetLinkColor(GraphValueType::Flow), style_.createLinkThickness);
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
				if (ed::AcceptNewItem(style_.GetLinkColor(document.FindPin(startPin)->valueType), style_.createLinkThickness)) {
					changed |= NodeGraphInteraction::TryCreateLink(document, startPin, endPin);
				}
			} else {
				ed::RejectNewItem(style_.GetErrorColor(), style_.createLinkThickness);
			}
		}
	}
	ed::EndCreate();
	return changed;
}

bool Engine::NodeGraphView::DrawDelete(GraphDocument& document) {

	bool changed = false;
	// BeginDeleteもBeginCreateと同じくBegin/Endの対を守る
	const bool deleting = ed::BeginDelete();
	if (deleting) {

		ed::LinkId linkID{};
		while (ed::QueryDeletedLink(&linkID)) {
			// Deleteキーなどで削除されたLinkをDocumentへ反映する
			if (ed::AcceptDeletedItem()) {
				changed |= NodeGraphInteraction::TryDeleteLink(document, FromLinkID(linkID));
			}
		}

		ed::NodeId nodeID{};
		while (ed::QueryDeletedNode(&nodeID)) {
			// Node削除時はDocument側で関連Linkもまとめて削除する
			if (ed::AcceptDeletedItem()) {
				changed |= NodeGraphInteraction::TryDeleteNode(document, FromNodeID(nodeID));
			}
		}
	}
	ed::EndDelete();
	return changed;
}

bool Engine::NodeGraphView::DrawBackgroundMenu(const NodeGraphViewDesc& desc) {

	if (!desc.registry || !desc.addNodeRequested) {
		return false;
	}

	bool changed = false;
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
				changed = true;
			}
		}
		ImGui::EndPopup();
	}
	ed::Resume();
	return changed;
}
