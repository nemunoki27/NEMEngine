#include "RenderPathGraphTool.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Commands/Scene/SetScenePassOrderCommand.h>
#include <Engine/Editor/RenderPathGraph/RenderPathGraphAssetDragDrop.h>
#include <Engine/Editor/RenderPathGraph/RenderPathGraphNodeFactory.h>
#include <Engine/Editor/RenderPathGraph/RenderPathGraphSerializer.h>
#include <Engine/Editor/RenderPathGraph/RenderPathGraphTypes.h>
#include <Engine/Editor/UI/Panels/Core/IEditorPanelHost.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonSerializer.h>
#include <Engine/Core/Foundation/Identity/UUID.h>
#include <Engine/Core/Tools/ImGui/ImGuiHelpers.h>

// c++
#include <filesystem>
#include <memory>
// imgui
#include <imgui.h>
#include <imgui_node_editor.h>

//============================================================================
//	RenderPathGraphTool classMethods
//============================================================================

namespace {

	constexpr float kSidePanelWidth = 330.0f;
	constexpr const char* kNodeStyleConfigPath = "Tools/RenderPathGraph/renderPathGraphStyle.exeConfig.json";

	constexpr const char* kEditorColorLabels[] = {
		"背景色",
		"グリッド色",
		"ノード背景色",
		"ノード枠線色",
		"ホバー中ノード枠線色",
		"選択中ノード枠線色",
		"ノード選択矩形色",
		"ノード選択矩形枠色",
		"ホバー中リンク枠色",
		"選択中リンク枠色",
		"強調リンク枠色",
		"リンク選択矩形色",
		"リンク選択矩形枠色",
		"ピン矩形色",
		"ピン矩形枠色",
		"フロー色",
		"フローマーカー色",
		"グループ背景色",
		"グループ枠線色",
	};

	void DragFloatStyle(const char* label, float& value, float speed, float minValue, float maxValue) {

		// Style値は即時反映したいので、DragFloatでそのまま参照を編集する
		ImGui::DragFloat(label, &value, speed, minValue, maxValue, "%.3f");
	}

	void DragVec2Style(const char* label, ImVec2& value, float speed, float minValue, float maxValue) {

		// ImVec2はfloat[2]として編集する
		float values[2] = { value.x, value.y };
		if (ImGui::DragFloat2(label, values, speed, minValue, maxValue, "%.3f")) {
			value.x = values[0];
			value.y = values[1];
		}
	}

	void DragVec4Style(const char* label, ImVec4& value, float speed, float minValue, float maxValue) {

		// NodePaddingなどのImVec4調整用
		float values[4] = { value.x, value.y, value.z, value.w };
		if (ImGui::DragFloat4(label, values, speed, minValue, maxValue, "%.3f")) {
			value.x = values[0];
			value.y = values[1];
			value.z = values[2];
			value.w = values[3];
		}
	}

	void ColorEditStyle(const char* label, ImVec4& color) {

		// Alphaも含めてNode色を調整できるようにする
		ImGui::ColorEdit4(label, &color.x, ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_AlphaPreviewHalf);
	}

	bool InputJsonString(const char* label, nlohmann::json& properties, const char* key) {

		// JSON文字列をImGui InputTextで編集するため、一旦固定長Bufferへ移す
		std::string value = properties.value(key, "");
		char buffer[512]{};
		std::snprintf(buffer, sizeof(buffer), "%s", value.c_str());

		if (ImGui::InputText(label, buffer, sizeof(buffer))) {
			properties[key] = buffer;
			return true;
		}
		return false;
	}

	void TextJsonValue(const char* label, const nlohmann::json& properties, const char* key) {

		// 表示専用のJSON値。Object / Arrayはdumpして中身を確認できるようにする
		if (!properties.contains(key)) {
			return;
		}

		const nlohmann::json& value = properties[key];
		if (value.is_string()) {
			ImGui::Text("%s : %s", label, value.get<std::string>().c_str());
		} else {
			ImGui::Text("%s : %s", label, value.dump().c_str());
		}
	}

	nlohmann::json PassOrderToJson(const std::vector<Engine::ScenePassDesc>& passOrder) {

		// Compile結果のPreviewはSceneHeaderの既存ToJsonを通して表示する
		Engine::SceneHeader header{};
		header.name = "CompilePreview";
		header.passOrder = passOrder;
		return Engine::ToJson(header)["passOrder"];
	}

	std::filesystem::path MakeBackupPath(const std::filesystem::path& scenePath) {

		// Apply前に同じフォルダへ簡易Backupを残す
		std::filesystem::path backup = scenePath;
		backup += ".renderpath.bak";
		return backup;
	}
}

Engine::RenderPathGraphTool::RenderPathGraphTool() {

	// Tool起動前にRenderPath用Node定義を登録しておく
	RenderPathGraphNodeFactory::RegisterDefinitions(registry_);
	LoadNodeStyleConfig();
}

void Engine::RenderPathGraphTool::OpenEditorTool() {

	// Tool Managerから開かれた時にWindowを表示する
	openWindow_ = true;
}

void Engine::RenderPathGraphTool::DrawEditorTool(const EditorToolContext& context) {

	// Windowを閉じた後は描画だけ止め、内部状態は保持する
	if (openWindow_) {
		DrawWindow(context);
	}
}

void Engine::RenderPathGraphTool::DrawWindow(const EditorToolContext& context) {

	DrawNodeStyleEditWindow();

	if (!ImGui::Begin("RenderPath Graph", &openWindow_)) {
		ImGui::End();
		return;
	}

	// Active Sceneが無い場合はGraphを生成できない
	const SceneHeader* header = context.toolContext.activeSceneHeader;
	const std::string scenePath(context.toolContext.activeScenePath);
	if (!header) {
		ImGui::TextDisabled("Active scene is not available.");
		ImGui::End();
		return;
	}

	// 開いているシーンが変わった時だけ自動Importする。毎フレーム再生成はしない
	if (!imported_ || importedScenePath_ != scenePath) {
		ImportFromCurrentScene(context);
	}

	DrawToolbar(context);
	ImGui::Separator();

	// 左側は検証結果とPreview、右側はNode Canvasとして分ける
	ImGui::BeginChild("RenderPathGraphSidePanel", ImVec2(kSidePanelWidth, 0.0f), true);
	DrawSidePanel(context);
	ImGui::EndChild();
	ImGui::SameLine();

	ImGui::BeginChild("RenderPathGraphCanvas", ImVec2(0.0f, 0.0f), true);
	DrawNodeGraph(context);
	ImGui::EndChild();

	ImGui::End();
}

void Engine::RenderPathGraphTool::DrawToolbar(const EditorToolContext& context) {

	// 現在のSceneHeaderからGraphを作り直す
	if (ImGui::Button("Import From Current Scene")) {
		ImportFromCurrentScene(context);
	}
	ImGui::SameLine();
	if (ImGui::Button("Validate")) {
		ValidateCurrentGraph(context);
	}
	ImGui::SameLine();
	if (ImGui::Button("Compile Preview")) {
		CompilePreview(context);
	}
	ImGui::SameLine();

	// ApplyはScene編集可能かつValidation Errorが無い時だけ許可する
	const bool canApply = context.CanEditScene() && !validationResult_.HasError();
	if (!canApply) {
		ImGui::BeginDisabled();
	}
	if (ImGui::Button("Apply To Scene")) {
		ApplyToScene(context);
	}
	if (!canApply) {
		ImGui::EndDisabled();
	}

	ImGui::SameLine();
	if (ImGui::Button("Save Scene")) {
		SaveScene(context);
	}
	ImGui::SameLine();
	if (ImGui::Button("Export Graph")) {
		ExportGraph(context);
	}
	ImGui::SameLine();
	if (ImGui::Button("Import Graph")) {
		ImportGraph(context);
	}
	ImGui::SameLine();
	if (ImGui::Button("Reset Layout")) {
		ResetLayout();
	}

	if (!statusMessage_.empty()) {
		// 操作結果はToolbar右側へ短く表示する
		ImGui::SameLine();
		ImGui::TextDisabled("%s", statusMessage_.c_str());
	}
}

void Engine::RenderPathGraphTool::DrawSidePanel(const EditorToolContext& context) {

	// Graph全体の状態を簡単に確認できるようにする
	const SceneHeader* header = context.toolContext.activeSceneHeader;
	ImGui::Text("Scene : %s", header ? header->name.c_str() : "None");
	ImGui::Text("Pass : %zu", document_.nodes.size());
	ImGui::Text("Link : %zu", document_.links.size());
	ImGui::Text("Graph : %s", graphDirty_ ? "Dirty" : "Clean");

	ImGui::Separator();
	DrawValidationMessages();
	ImGui::Separator();
	DrawCompilePreview();
}

void Engine::RenderPathGraphTool::DrawNodeStyleEditWindow() {

	NodeGraphStyle& style = graphView_.GetStyle();

	ImGui::Begin("Node StyleEdit");

	if (ImGui::Button("初期値に戻す")) {
		// 調整中に崩れた時すぐ戻せるように、Styleだけ作り直す
		style = NodeGraphStyle{};
	}
	if (ImGui::Button("Save Json##RenderPathGraphToolStyle")) {

		SaveNodeStyleConfig();
	}

	if (ImGui::CollapsingHeader("ノード全体", ImGuiTreeNodeFlags_DefaultOpen)) {
		DragVec4Style("ノード余白", style.nodePadding, 0.1f, 0.0f, 64.0f);
		DragFloatStyle("ノード横幅", style.nodeWidth, 1.0f, 140.0f, 700.0f);
		DragFloatStyle("ノードラベル幅", style.nodeLabelWidth, 1.0f, 30.0f, 240.0f);
		DragFloatStyle("ノード角丸", style.nodeRounding, 0.1f, 0.0f, 48.0f);
		DragFloatStyle("通常枠線幅", style.nodeBorderWidth, 0.05f, 0.0f, 12.0f);
		DragFloatStyle("ホバー枠線幅", style.hoveredNodeBorderWidth, 0.05f, 0.0f, 16.0f);
		DragFloatStyle("選択枠線幅", style.selectedNodeBorderWidth, 0.05f, 0.0f, 16.0f);
		DragFloatStyle("ホバー枠線オフセット", style.hoveredNodeBorderOffset, 0.05f, -16.0f, 16.0f);
		DragFloatStyle("選択枠線オフセット", style.selectedNodeBorderOffset, 0.05f, -16.0f, 16.0f);
		DragFloatStyle("無効ノード透明度", style.disabledNodeAlpha, 0.01f, 0.0f, 1.0f);
	}

	if (ImGui::CollapsingHeader("ノード背景 / エディター色", ImGuiTreeNodeFlags_DefaultOpen)) {
		static_assert(sizeof(kEditorColorLabels) / sizeof(kEditorColorLabels[0]) == ax::NodeEditor::StyleColor_Count);
		for (int32_t i = 0; i < ax::NodeEditor::StyleColor_Count; ++i) {
			ColorEditStyle(kEditorColorLabels[i], style.editorColors[static_cast<size_t>(i)]);
		}
	}

	if (ImGui::CollapsingHeader("ノード種別カラー", ImGuiTreeNodeFlags_DefaultOpen)) {
		ColorEditStyle("Clear", style.clearNodeColor);
		ColorEditStyle("DepthPrepass", style.depthPrepassNodeColor);
		ColorEditStyle("Draw / RenderScene", style.drawNodeColor);
		ColorEditStyle("Compute", style.computeNodeColor);
		ColorEditStyle("PostProcess", style.postProcessNodeColor);
		ColorEditStyle("Blit / FullscreenCopy", style.blitNodeColor);
		ColorEditStyle("Raytracing", style.raytracingNodeColor);
		ColorEditStyle("TemporaryTarget", style.temporaryNodeColor);
		ColorEditStyle("View", style.viewNodeColor);
		ColorEditStyle("Default", style.defaultNodeColor);
	}

	if (ImGui::CollapsingHeader("ピン", ImGuiTreeNodeFlags_DefaultOpen)) {
		DragFloatStyle("ピン角丸", style.pinRounding, 0.1f, 0.0f, 48.0f);
		DragFloatStyle("ピン枠線幅", style.pinBorderWidth, 0.05f, 0.0f, 12.0f);
		DragFloatStyle("ピン角丸対象", style.pinCorners, 1.0f, 0.0f, 32.0f);
		DragFloatStyle("ピン半径", style.pinRadius, 0.1f, 0.0f, 32.0f);
		DragFloatStyle("ピン矢印サイズ", style.pinArrowSize, 0.1f, 0.0f, 32.0f);
		DragFloatStyle("ピン矢印幅", style.pinArrowWidth, 0.1f, 0.0f, 32.0f);
		DragVec2Style("入力ピン基準位置", style.inputPivotAlignment, 0.01f, -2.0f, 2.0f);
		DragVec2Style("出力ピン基準位置", style.outputPivotAlignment, 0.01f, -2.0f, 2.0f);
		DragVec2Style("ピン基準サイズ", style.pivotSize, 0.1f, 0.0f, 64.0f);
		DragVec2Style("ピン基準スケール", style.pivotScale, 0.01f, 0.0f, 4.0f);

		ImGui::Separator();
		ColorEditStyle("Flow Pin色", style.flowPinColor);
		ColorEditStyle("Texture / RenderTarget Pin色", style.texturePinColor);
		ColorEditStyle("Depth Pin色", style.depthPinColor);
		ColorEditStyle("Asset Pin色", style.assetPinColor);
		ColorEditStyle("View Pin色", style.viewPinColor);
		ColorEditStyle("Default Pin色", style.defaultPinColor);
	}

	if (ImGui::CollapsingHeader("リンク", ImGuiTreeNodeFlags_DefaultOpen)) {
		DragFloatStyle("リンク曲がり強さ", style.linkStrength, 1.0f, 0.0f, 300.0f);
		DragFloatStyle("通常リンク太さ", style.linkThickness, 0.05f, 0.1f, 12.0f);
		DragFloatStyle("Flowリンク太さ", style.flowLinkThickness, 0.05f, 0.1f, 12.0f);
		DragFloatStyle("作成中リンク太さ", style.createLinkThickness, 0.05f, 0.1f, 12.0f);
		DragFloatStyle("リンク透明度", style.linkAlpha, 0.01f, 0.0f, 1.0f);
		DragVec2Style("リンク始点方向", style.sourceDirection, 0.01f, -2.0f, 2.0f);
		DragVec2Style("リンク終点方向", style.targetDirection, 0.01f, -2.0f, 2.0f);
		DragFloatStyle("接続リンク強調", style.highlightConnectedLinks, 0.01f, 0.0f, 1.0f);
		DragFloatStyle("ピン方向へ吸着", style.snapLinkToPinDir, 0.01f, 0.0f, 1.0f);
	}

	if (ImGui::CollapsingHeader("スクロール / フロー")) {
		DragFloatStyle("スクロール時間", style.scrollDuration, 0.01f, 0.0f, 3.0f);
		DragFloatStyle("フローマーカー間隔", style.flowMarkerDistance, 0.5f, 0.0f, 300.0f);
		DragFloatStyle("フローマーカー速度", style.flowSpeed, 1.0f, 0.0f, 600.0f);
		DragFloatStyle("フローマーカー時間", style.flowDuration, 0.01f, 0.0f, 8.0f);
	}

	if (ImGui::CollapsingHeader("グループ")) {
		DragFloatStyle("グループ角丸", style.groupRounding, 0.1f, 0.0f, 48.0f);
		DragFloatStyle("グループ枠線幅", style.groupBorderWidth, 0.05f, 0.0f, 12.0f);
	}

	if (ImGui::CollapsingHeader("警告 / エラー色")) {
		ColorEditStyle("エラー色", style.errorColor);
		ColorEditStyle("警告色", style.warningColor);
	}

	ImGui::End();
}

void Engine::RenderPathGraphTool::DrawNodeGraph(const EditorToolContext& context) {

	// 汎用NodeGraphViewへRenderPath専用の描画処理を渡す
	NodeGraphViewDesc desc{};
	desc.editorId = "RenderPathGraphEditor";
	desc.registry = &registry_;
	desc.drawNodeProperty = [&context, this](GraphNode& node) {
		DrawNodeProperties(node, context);
	};
	desc.drawNodeDropTarget = [&context, this](GraphNode& node) {
		if (RenderPathGraphAssetDragDrop::AcceptMaterial(node, context)) {
			// Node上へMaterialをDropしたら再検証対象にする
			graphDirty_ = true;
			validationDirty_ = true;
		}
	};
	desc.addNodeRequested = [this](const std::string& nodeType, const ImVec2& position) {
		// 背景メニューからNodeを追加する
		GraphNode node = registry_.CreateNode(document_, nodeType, position);
		document_.nodes.emplace_back(std::move(node));
		graphDirty_ = true;
		validationDirty_ = true;
	};

	graphView_.Draw(graphContext_, document_, desc);
}

void Engine::RenderPathGraphTool::DrawNodeProperties(GraphNode& node, const EditorToolContext& context) {

	bool changed = false;
	const NodeGraphStyle& style = graphView_.GetStyle();
	const PropertyRowSetting propertyRowSetting{
		.labelWidth = style.nodeLabelWidth,
		.rowWidth = style.nodeWidth,
	};
	const TextEditSetting textSetting{
		.propertyRow = propertyRowSetting,
	};
	const AssetEditSetting assetSetting{
		.propertyRow = propertyRowSetting,
	};

	// 有効/無効はScenePassDesc.enabledへ反映する
	bool enabled = node.properties.value("enabled", node.enabled);
	if (MyGUI::Checkbox("Enabled", enabled, propertyRowSetting)) {
		node.enabled = enabled;
		node.properties["enabled"] = enabled;
		changed = true;
	}

	if (node.type == RenderPathGraph::kDraw || node.type == RenderPathGraph::kDepthPrepass) {
		// Draw系はQueueとPassNameを簡易編集する
		std::string queue = node.properties.value("queue", "");
		if (MyGUI::InputText("Queue", queue, textSetting).editFinished) {
			node.properties["queue"] = queue;
			changed = true;
		}

		std::string passName = node.properties.value("passName", "");
		if (MyGUI::InputText("Pass", passName, textSetting).editFinished) {
			node.properties["passName"] = passName;
			changed = true;
		}
	}
	if (node.type == RenderPathGraph::kCompute ||
		node.type == RenderPathGraph::kPostProcess ||
		node.type == RenderPathGraph::kBlit ||
		node.type == RenderPathGraph::kRaytracing) {
		// Materialを使うPassはAssetReferenceFieldで編集する
		AssetID material = FromString16Hex(node.properties.value("material", ""));
		const AssetDatabase* assetDatabase = context.panelContext && context.panelContext->editorContext ?
			context.panelContext->editorContext->assetDatabase : nullptr;
		if (MyGUI::AssetReferenceField("Material", material, assetDatabase, { AssetType::Material }, assetSetting).editFinished) {
			node.properties["material"] = ToString(material);
			changed = true;
		}
	}
	if (node.type == RenderPathGraph::kRenderScene) {
		// RenderSceneはSubScene Slotを編集する
		std::string subSceneSlot = node.properties.value("subSceneSlot", "");
		if (MyGUI::InputText("SubScene", subSceneSlot, textSetting).editFinished) {
			node.properties["subSceneSlot"] = subSceneSlot;
			changed = true;
		}
	}
	if (node.type == RenderPathGraph::kClear) {
		// Clear値は現状Preview表示のみ
		TextJsonValue("Target", node.properties, "target");
		TextJsonValue("ClearColor", node.properties, "clearColor");
		TextJsonValue("ClearDepth", node.properties, "clearDepth");
	}

	if (changed) {
		// Property変更後はCompile / Validationをやり直す
		graphDirty_ = true;
		validationDirty_ = true;
	}
}

void Engine::RenderPathGraphTool::DrawValidationMessages() {

	ImGui::TextUnformatted("Validation");
	if (validationResult_.messages.empty()) {
		ImGui::TextDisabled("No messages.");
		return;
	}

	for (const GraphValidationMessage& message : validationResult_.messages) {
		// 重要度に合わせて色を変える
		const char* label = "Info";
		ImVec4 color{ 0.60f, 0.72f, 0.90f, 1.0f };
		if (message.severity == GraphValidationMessage::Severity::Warning) {
			label = "Warning";
			color = graphView_.GetStyle().GetWarningColor();
		} else if (message.severity == GraphValidationMessage::Severity::Error) {
			label = "Error";
			color = graphView_.GetStyle().GetErrorColor();
		}
		ImGui::TextColored(color, "%s node=%llu : %s",
			label, static_cast<unsigned long long>(message.nodeID), message.message.c_str());
	}
}

void Engine::RenderPathGraphTool::DrawCompilePreview() {

	ImGui::TextUnformatted("Compile Preview");
	if (compilePreviewText_.empty()) {
		ImGui::TextDisabled("No preview.");
		return;
	}

	ImGui::BeginChild("RenderPathCompilePreviewText", ImVec2(0.0f, 230.0f), true);
	// JSON文字列をそのまま表示して、Apply前にpassOrderを確認できるようにする
	ImGui::TextUnformatted(compilePreviewText_.c_str());
	ImGui::EndChild();
}

void Engine::RenderPathGraphTool::ImportFromCurrentScene(const EditorToolContext& context) {

	const SceneHeader* header = context.toolContext.activeSceneHeader;
	if (!header) {
		statusMessage_ = "Active scene is not available.";
		return;
	}

	// Import時はSceneHeaderをGraphへ変換し、Node配置情報も初期化する
	importer_.Import(*header, std::string(context.toolContext.activeScenePath), document_);
	graphContext_.ResetPlacedNodes();
	imported_ = true;
	importedScenePath_ = std::string(context.toolContext.activeScenePath);
	graphDirty_ = false;
	validationDirty_ = true;
	compilePreviewText_.clear();
	ValidateCurrentGraph(context);
	statusMessage_ = "Imported.";
}

void Engine::RenderPathGraphTool::ValidateCurrentGraph(const EditorToolContext& context) {

	// ValidatorはNode上のメッセージも更新するため、Documentを渡す
	validationResult_ = validator_.Validate(document_, context.toolContext.activeSceneHeader);
	validationDirty_ = false;
	statusMessage_ = validationResult_.HasError() ? "Validation error." : "Validation passed.";
}

void Engine::RenderPathGraphTool::CompilePreview(const EditorToolContext& context) {

	if (validationDirty_) {
		// 古い検証結果でCompileしない
		ValidateCurrentGraph(context);
	}
	if (validationResult_.HasError()) {
		statusMessage_ = "Compile stopped by validation error.";
		return;
	}
	if (!context.toolContext.activeSceneHeader) {
		statusMessage_ = "Active scene is not available.";
		return;
	}

	std::string error{};
	if (!compiler_.Compile(document_, *context.toolContext.activeSceneHeader, compiledPassOrder_, &error)) {
		statusMessage_ = error;
		return;
	}

	// Compile結果はSceneHeader.passOrder JSONとしてPreview表示する
	compilePreviewText_ = PassOrderToJson(compiledPassOrder_).dump(2);
	statusMessage_ = "Compiled.";
}

void Engine::RenderPathGraphTool::ApplyToScene(const EditorToolContext& context) {

	if (!context.panelContext || !context.panelContext->host || !context.toolContext.activeSceneHeader) {
		statusMessage_ = "Editor host is not available.";
		return;
	}

	// Apply前に必ず最新GraphをCompileする
	CompilePreview(context);
	if (validationResult_.HasError() || compiledPassOrder_.empty()) {
		statusMessage_ = "Apply stopped.";
		return;
	}

	// passOrder変更はUndo/Redoに乗せるためCommand経由で実行する
	std::vector<ScenePassDesc> before = context.toolContext.activeSceneHeader->passOrder;
	auto command = std::make_unique<SetScenePassOrderCommand>(std::move(before), compiledPassOrder_);
	if (!context.panelContext->host->ExecuteEditorCommand(std::move(command))) {
		statusMessage_ = "Apply failed.";
		return;
	}

	graphDirty_ = false;
	statusMessage_ = "Applied to scene.";
}

void Engine::RenderPathGraphTool::SaveScene(const EditorToolContext& context) {

	if (!context.panelContext || !context.panelContext->host) {
		statusMessage_ = "Editor host is not available.";
		return;
	}
	if (validationDirty_) {
		// Errorが残ったGraphを保存しないように先に検証する
		ValidateCurrentGraph(context);
	}
	if (validationResult_.HasError()) {
		statusMessage_ = "Save stopped by validation error.";
		return;
	}

	// Scene保存前にBackupを残しておく
	if (!MakeSceneBackup(context)) {
		return;
	}
	context.panelContext->host->RequestSaveScene();
	statusMessage_ = "Save requested.";
}

void Engine::RenderPathGraphTool::ExportGraph(const EditorToolContext& context) {

	const std::string path = MakeActiveGraphPath(context);
	std::string error{};
	// Graph単体の保存。SceneHeaderへはまだ反映しない
	if (!RenderPathGraphSerializer::ExportGraph(path, document_, &error)) {
		statusMessage_ = error;
		return;
	}
	statusMessage_ = "Graph exported.";
}

void Engine::RenderPathGraphTool::ImportGraph(const EditorToolContext& context) {

	const std::string path = MakeActiveGraphPath(context);
	std::string error{};
	// 保存済みGraphを読み込み、次描画でNode座標を反映する
	if (!RenderPathGraphSerializer::ImportGraph(path, document_, &error)) {
		statusMessage_ = error;
		return;
	}
	graphContext_.ResetPlacedNodes();
	graphDirty_ = false;
	validationDirty_ = true;
	ValidateCurrentGraph(context);
	statusMessage_ = "Graph imported.";
}

void Engine::RenderPathGraphTool::ResetLayout() {

	uint32_t passIndex = 0;
	for (GraphNode& node : document_.nodes) {
		// Pass順に横並びへ戻す
		node.position = ImVec2(80.0f + static_cast<float>(passIndex) * 300.0f, 120.0f);
		++passIndex;
	}
	graphContext_.ResetPlacedNodes();
	statusMessage_ = "Layout reset.";
}

bool Engine::RenderPathGraphTool::MakeSceneBackup(const EditorToolContext& context) {

	const std::string scenePath(context.toolContext.activeScenePath);
	if (scenePath.empty()) {
		statusMessage_ = "Scene path is empty.";
		return false;
	}

	const std::filesystem::path fullPath = RuntimePaths::ResolveAssetPath(scenePath);
	if (fullPath.empty() || !std::filesystem::exists(fullPath)) {
		statusMessage_ = "Scene file is not found.";
		return false;
	}

	const std::filesystem::path backupPath = MakeBackupPath(fullPath);
	std::error_code ec{};
	// 失敗しても例外で落とさず、Tool上に状態を表示する
	std::filesystem::copy_file(fullPath, backupPath,
		std::filesystem::copy_options::overwrite_existing, ec);
	if (ec) {
		statusMessage_ = "Scene backup failed.";
		Logger::Output(LogType::Engine, spdlog::level::warn,
			"RenderPathGraphTool: failed to backup scene. path={} error={}",
			fullPath.generic_string(), ec.message());
		return false;
	}
	return true;
}

std::string Engine::RenderPathGraphTool::MakeActiveGraphPath(const EditorToolContext& context) const {

	// Active Sceneに対応するGraph保存Pathを返す
	return RenderPathGraphSerializer::MakeDefaultGraphPath(std::string(context.toolContext.activeScenePath));
}

std::filesystem::path Engine::RenderPathGraphTool::MakeNodeStyleConfigPath() const {

	// Engine/Assets/Tools/RenderPathGraph配下へNode Style用のexeConfigを保存する
	return RuntimePaths::GetEngineAssetPath(kNodeStyleConfigPath);
}

void Engine::RenderPathGraphTool::LoadNodeStyleConfig() {

	const std::filesystem::path path = MakeNodeStyleConfigPath();
	if (!JsonAdapter::Check(path.string(), false)) {
		return;
	}

	const nlohmann::json data = JsonAdapter::Load(path.string(), false);
	if (!data.is_object()) {
		return;
	}

	graphView_.GetStyle().FromJson(data);
}

void Engine::RenderPathGraphTool::SaveNodeStyleConfig() {

	const std::filesystem::path path = MakeNodeStyleConfigPath();
	JsonAdapter::Save(path.string(), graphView_.GetStyle().ToJson());
	statusMessage_ = "Node Style saved.";
}
