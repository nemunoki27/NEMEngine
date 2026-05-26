#include "NodeGraphStyle.h"

// c++
#include <algorithm>

//============================================================================
//	NodeGraphStyle classMethods
//============================================================================

namespace {

	nlohmann::json ImVec2ToJson(const ImVec2& value) {

		// ImVec2はx/y配列ではなく名前付きObjectで保存する
		return nlohmann::json{
			{"x", value.x},
			{"y", value.y},
		};
	}

	nlohmann::json ImVec4ToJson(const ImVec4& value) {

		// Color / Paddingのどちらでも読めるようにx/y/z/wで保存する
		return nlohmann::json{
			{"x", value.x},
			{"y", value.y},
			{"z", value.z},
			{"w", value.w},
		};
	}

	void ImVec2FromJson(const nlohmann::json& data, ImVec2& value) {

		if (!data.is_object()) {
			return;
		}
		value.x = data.value("x", value.x);
		value.y = data.value("y", value.y);
	}

	void ImVec4FromJson(const nlohmann::json& data, ImVec4& value) {

		if (!data.is_object()) {
			return;
		}
		value.x = data.value("x", value.x);
		value.y = data.value("y", value.y);
		value.z = data.value("z", value.z);
		value.w = data.value("w", value.w);
	}

	void ReadFloat(const nlohmann::json& data, const char* key, float& value) {

		if (data.contains(key)) {
			value = data.value(key, value);
		}
	}

	void ReadVec2(const nlohmann::json& data, const char* key, ImVec2& value) {

		if (data.contains(key)) {
			ImVec2FromJson(data[key], value);
		}
	}

	void ReadVec4(const nlohmann::json& data, const char* key, ImVec4& value) {

		if (data.contains(key)) {
			ImVec4FromJson(data[key], value);
		}
	}
}

Engine::NodeGraphStyle::NodeGraphStyle() {

	// imgui-node-editor標準Styleから色と一部特殊値を取り込む
	ax::NodeEditor::Style defaultStyle{};
	pinCorners = defaultStyle.PinCorners;
	for (int32_t i = 0; i < ax::NodeEditor::StyleColor_Count; ++i) {
		editorColors[static_cast<size_t>(i)] = defaultStyle.Colors[i];
	}
}

void Engine::NodeGraphStyle::PushEditorStyle() const {

	namespace ed = ax::NodeEditor;

	// NodeEditor本体の余白 / 枠 / Link挙動を現在の調整値へ差し替える
	ed::PushStyleVar(ed::StyleVar_NodePadding, nodePadding);
	ed::PushStyleVar(ed::StyleVar_NodeRounding, nodeRounding);
	ed::PushStyleVar(ed::StyleVar_NodeBorderWidth, nodeBorderWidth);
	ed::PushStyleVar(ed::StyleVar_HoveredNodeBorderWidth, hoveredNodeBorderWidth);
	ed::PushStyleVar(ed::StyleVar_SelectedNodeBorderWidth, selectedNodeBorderWidth);
	ed::PushStyleVar(ed::StyleVar_HoveredNodeBorderOffset, hoveredNodeBorderOffset);
	ed::PushStyleVar(ed::StyleVar_SelectedNodeBorderOffset, selectedNodeBorderOffset);
	ed::PushStyleVar(ed::StyleVar_PinRounding, pinRounding);
	ed::PushStyleVar(ed::StyleVar_PinBorderWidth, pinBorderWidth);
	ed::PushStyleVar(ed::StyleVar_LinkStrength, linkStrength);
	ed::PushStyleVar(ed::StyleVar_SourceDirection, sourceDirection);
	ed::PushStyleVar(ed::StyleVar_TargetDirection, targetDirection);
	ed::PushStyleVar(ed::StyleVar_ScrollDuration, scrollDuration);
	ed::PushStyleVar(ed::StyleVar_FlowMarkerDistance, flowMarkerDistance);
	ed::PushStyleVar(ed::StyleVar_FlowSpeed, flowSpeed);
	ed::PushStyleVar(ed::StyleVar_FlowDuration, flowDuration);
	ed::PushStyleVar(ed::StyleVar_PivotAlignment, outputPivotAlignment);
	ed::PushStyleVar(ed::StyleVar_PivotSize, pivotSize);
	ed::PushStyleVar(ed::StyleVar_PivotScale, pivotScale);
	ed::PushStyleVar(ed::StyleVar_PinCorners, pinCorners);
	ed::PushStyleVar(ed::StyleVar_PinRadius, pinRadius);
	ed::PushStyleVar(ed::StyleVar_PinArrowSize, pinArrowSize);
	ed::PushStyleVar(ed::StyleVar_PinArrowWidth, pinArrowWidth);
	ed::PushStyleVar(ed::StyleVar_GroupRounding, groupRounding);
	ed::PushStyleVar(ed::StyleVar_GroupBorderWidth, groupBorderWidth);
	ed::PushStyleVar(ed::StyleVar_HighlightConnectedLinks, highlightConnectedLinks);
	ed::PushStyleVar(ed::StyleVar_SnapLinkToPinDir, snapLinkToPinDir);

	// Node背景やGridなど、NodeEditorが直接描画する色をまとめて反映する
	for (int32_t i = 0; i < ed::StyleColor_Count; ++i) {
		ed::PushStyleColor(static_cast<ed::StyleColor>(i), editorColors[static_cast<size_t>(i)]);
	}
}

void Engine::NodeGraphStyle::PopEditorStyle() const {

	namespace ed = ax::NodeEditor;

	// PushEditorStyle()で積んだStyleを全て戻す
	ed::PopStyleColor(ed::StyleColor_Count);
	ed::PopStyleVar(27);
}

ImVec4 Engine::NodeGraphStyle::GetNodeAccentColor(const std::string& nodeType) const {

	// RenderPathの主なPass種別ごとにNode色を分ける
	if (nodeType.find("Clear") != std::string::npos) {
		return clearNodeColor;
	}
	if (nodeType.find("DepthPrepass") != std::string::npos) {
		return depthPrepassNodeColor;
	}
	if (nodeType.find("Draw") != std::string::npos || nodeType.find("RenderScene") != std::string::npos) {
		return drawNodeColor;
	}
	if (nodeType.find("Compute") != std::string::npos) {
		return computeNodeColor;
	}
	if (nodeType.find("PostProcess") != std::string::npos) {
		return postProcessNodeColor;
	}
	if (nodeType.find("Blit") != std::string::npos || nodeType.find("FullscreenCopy") != std::string::npos) {
		return blitNodeColor;
	}
	if (nodeType.find("Raytracing") != std::string::npos) {
		return raytracingNodeColor;
	}
	if (nodeType.find("Temporary") != std::string::npos) {
		return temporaryNodeColor;
	}
	if (nodeType.find("View") != std::string::npos) {
		return viewNodeColor;
	}
	return defaultNodeColor;
}

ImVec4 Engine::NodeGraphStyle::GetPinColor(GraphValueType valueType) const {

	// Pinの型が近いものは同じ色にして、接続できる種類を見分けやすくする
	switch (valueType) {
	case GraphValueType::Flow:
		return flowPinColor;
	case GraphValueType::Texture2D:
	case GraphValueType::Texture2DUAV:
	case GraphValueType::RenderTarget:
		return texturePinColor;
	case GraphValueType::DepthTexture:
		return depthPinColor;
	case GraphValueType::MaterialAsset:
	case GraphValueType::ShaderAsset:
	case GraphValueType::Asset:
		return assetPinColor;
	case GraphValueType::View:
		return viewPinColor;
	default:
		return defaultPinColor;
	}
}

ImVec4 Engine::NodeGraphStyle::GetLinkColor(GraphValueType valueType) const {

	// LinkはPin色を少し透過させて使用する
	ImVec4 color = GetPinColor(valueType);
	color.w = linkAlpha;
	return color;
}

ImVec4 Engine::NodeGraphStyle::GetErrorColor() const {

	// Validation Error表示色
	return errorColor;
}

ImVec4 Engine::NodeGraphStyle::GetWarningColor() const {

	// Validation Warning表示色
	return warningColor;
}

nlohmann::json Engine::NodeGraphStyle::ToJson() const {

	nlohmann::json data = nlohmann::json::object();
	data["version"] = 1;

	// NodeEditorのStyleVar系
	data["nodePadding"] = ImVec4ToJson(nodePadding);
	data["nodeWidth"] = nodeWidth;
	data["nodeLabelWidth"] = nodeLabelWidth;
	data["nodeRounding"] = nodeRounding;
	data["nodeBorderWidth"] = nodeBorderWidth;
	data["hoveredNodeBorderWidth"] = hoveredNodeBorderWidth;
	data["selectedNodeBorderWidth"] = selectedNodeBorderWidth;
	data["hoveredNodeBorderOffset"] = hoveredNodeBorderOffset;
	data["selectedNodeBorderOffset"] = selectedNodeBorderOffset;
	data["pinRounding"] = pinRounding;
	data["pinBorderWidth"] = pinBorderWidth;
	data["linkStrength"] = linkStrength;
	data["sourceDirection"] = ImVec2ToJson(sourceDirection);
	data["targetDirection"] = ImVec2ToJson(targetDirection);
	data["scrollDuration"] = scrollDuration;
	data["flowMarkerDistance"] = flowMarkerDistance;
	data["flowSpeed"] = flowSpeed;
	data["flowDuration"] = flowDuration;
	data["inputPivotAlignment"] = ImVec2ToJson(inputPivotAlignment);
	data["outputPivotAlignment"] = ImVec2ToJson(outputPivotAlignment);
	data["pivotSize"] = ImVec2ToJson(pivotSize);
	data["pivotScale"] = ImVec2ToJson(pivotScale);
	data["pinCorners"] = pinCorners;
	data["pinRadius"] = pinRadius;
	data["pinArrowSize"] = pinArrowSize;
	data["pinArrowWidth"] = pinArrowWidth;
	data["groupRounding"] = groupRounding;
	data["groupBorderWidth"] = groupBorderWidth;
	data["highlightConnectedLinks"] = highlightConnectedLinks;
	data["snapLinkToPinDir"] = snapLinkToPinDir;

	// RenderPath Nodeのアクセント色
	data["clearNodeColor"] = ImVec4ToJson(clearNodeColor);
	data["depthPrepassNodeColor"] = ImVec4ToJson(depthPrepassNodeColor);
	data["drawNodeColor"] = ImVec4ToJson(drawNodeColor);
	data["computeNodeColor"] = ImVec4ToJson(computeNodeColor);
	data["postProcessNodeColor"] = ImVec4ToJson(postProcessNodeColor);
	data["blitNodeColor"] = ImVec4ToJson(blitNodeColor);
	data["raytracingNodeColor"] = ImVec4ToJson(raytracingNodeColor);
	data["temporaryNodeColor"] = ImVec4ToJson(temporaryNodeColor);
	data["viewNodeColor"] = ImVec4ToJson(viewNodeColor);
	data["defaultNodeColor"] = ImVec4ToJson(defaultNodeColor);

	// Pin / Link / Validation表示色
	data["flowPinColor"] = ImVec4ToJson(flowPinColor);
	data["texturePinColor"] = ImVec4ToJson(texturePinColor);
	data["depthPinColor"] = ImVec4ToJson(depthPinColor);
	data["assetPinColor"] = ImVec4ToJson(assetPinColor);
	data["viewPinColor"] = ImVec4ToJson(viewPinColor);
	data["defaultPinColor"] = ImVec4ToJson(defaultPinColor);
	data["errorColor"] = ImVec4ToJson(errorColor);
	data["warningColor"] = ImVec4ToJson(warningColor);
	data["linkAlpha"] = linkAlpha;
	data["linkThickness"] = linkThickness;
	data["flowLinkThickness"] = flowLinkThickness;
	data["createLinkThickness"] = createLinkThickness;
	data["disabledNodeAlpha"] = disabledNodeAlpha;

	data["editorColors"] = nlohmann::json::array();
	for (const ImVec4& color : editorColors) {
		data["editorColors"].push_back(ImVec4ToJson(color));
	}

	return data;
}

void Engine::NodeGraphStyle::FromJson(const nlohmann::json& data) {

	if (!data.is_object()) {
		return;
	}

	// 保存されていない項目は現在値を維持し、古いConfigでも読み込めるようにする
	ReadVec4(data, "nodePadding", nodePadding);
	ReadFloat(data, "nodeWidth", nodeWidth);
	ReadFloat(data, "nodeLabelWidth", nodeLabelWidth);
	ReadFloat(data, "nodeRounding", nodeRounding);
	ReadFloat(data, "nodeBorderWidth", nodeBorderWidth);
	ReadFloat(data, "hoveredNodeBorderWidth", hoveredNodeBorderWidth);
	ReadFloat(data, "selectedNodeBorderWidth", selectedNodeBorderWidth);
	ReadFloat(data, "hoveredNodeBorderOffset", hoveredNodeBorderOffset);
	ReadFloat(data, "selectedNodeBorderOffset", selectedNodeBorderOffset);
	ReadFloat(data, "pinRounding", pinRounding);
	ReadFloat(data, "pinBorderWidth", pinBorderWidth);
	ReadFloat(data, "linkStrength", linkStrength);
	ReadVec2(data, "sourceDirection", sourceDirection);
	ReadVec2(data, "targetDirection", targetDirection);
	ReadFloat(data, "scrollDuration", scrollDuration);
	ReadFloat(data, "flowMarkerDistance", flowMarkerDistance);
	ReadFloat(data, "flowSpeed", flowSpeed);
	ReadFloat(data, "flowDuration", flowDuration);
	if (data.contains("outputPivotAlignment")) {
		ReadVec2(data, "outputPivotAlignment", outputPivotAlignment);
	} else {
		// 旧ConfigのpivotAlignmentは出力Pin用として扱う
		ReadVec2(data, "pivotAlignment", outputPivotAlignment);
	}

	if (data.contains("inputPivotAlignment")) {
		ReadVec2(data, "inputPivotAlignment", inputPivotAlignment);
	} else {
		// 入力Pinの既定値は出力PinのXだけ反転して作る
		inputPivotAlignment.x = -outputPivotAlignment.x;
		inputPivotAlignment.y = outputPivotAlignment.y;
	}
	ReadVec2(data, "pivotSize", pivotSize);
	ReadVec2(data, "pivotScale", pivotScale);
	ReadFloat(data, "pinCorners", pinCorners);
	ReadFloat(data, "pinRadius", pinRadius);
	ReadFloat(data, "pinArrowSize", pinArrowSize);
	ReadFloat(data, "pinArrowWidth", pinArrowWidth);
	ReadFloat(data, "groupRounding", groupRounding);
	ReadFloat(data, "groupBorderWidth", groupBorderWidth);
	ReadFloat(data, "highlightConnectedLinks", highlightConnectedLinks);
	ReadFloat(data, "snapLinkToPinDir", snapLinkToPinDir);

	ReadVec4(data, "clearNodeColor", clearNodeColor);
	ReadVec4(data, "depthPrepassNodeColor", depthPrepassNodeColor);
	ReadVec4(data, "drawNodeColor", drawNodeColor);
	ReadVec4(data, "computeNodeColor", computeNodeColor);
	ReadVec4(data, "postProcessNodeColor", postProcessNodeColor);
	ReadVec4(data, "blitNodeColor", blitNodeColor);
	ReadVec4(data, "raytracingNodeColor", raytracingNodeColor);
	ReadVec4(data, "temporaryNodeColor", temporaryNodeColor);
	ReadVec4(data, "viewNodeColor", viewNodeColor);
	ReadVec4(data, "defaultNodeColor", defaultNodeColor);

	ReadVec4(data, "flowPinColor", flowPinColor);
	ReadVec4(data, "texturePinColor", texturePinColor);
	ReadVec4(data, "depthPinColor", depthPinColor);
	ReadVec4(data, "assetPinColor", assetPinColor);
	ReadVec4(data, "viewPinColor", viewPinColor);
	ReadVec4(data, "defaultPinColor", defaultPinColor);
	ReadVec4(data, "errorColor", errorColor);
	ReadVec4(data, "warningColor", warningColor);
	ReadFloat(data, "linkAlpha", linkAlpha);
	ReadFloat(data, "linkThickness", linkThickness);
	ReadFloat(data, "flowLinkThickness", flowLinkThickness);
	ReadFloat(data, "createLinkThickness", createLinkThickness);
	ReadFloat(data, "disabledNodeAlpha", disabledNodeAlpha);

	if (data.contains("editorColors") && data["editorColors"].is_array()) {
		const nlohmann::json& colors = data["editorColors"];
		const size_t count = std::min(colors.size(), editorColors.size());
		for (size_t i = 0; i < count; ++i) {
			ImVec4FromJson(colors[i], editorColors[i]);
		}
	}
}
