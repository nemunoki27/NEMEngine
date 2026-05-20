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

// c++
#include <filesystem>
#include <memory>
// imgui
#include <imgui.h>

//============================================================================
//	RenderPathGraphTool classMethods
//============================================================================

namespace {

	constexpr float kSidePanelWidth = 330.0f;

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

void Engine::RenderPathGraphTool::DrawNodeGraph(const EditorToolContext& context) {

	// 汎用NodeGraphViewへRenderPath専用の描画処理を渡す
	NodeGraphViewDesc desc{};
	desc.editorId = "RenderPathGraphEditor";
	desc.registry = &registry_;
	desc.drawNodeProperty = [this](GraphNode& node) {
		DrawNodeProperties(node);
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

void Engine::RenderPathGraphTool::DrawNodeProperties(GraphNode& node) {

	bool changed = false;
	// 有効/無効はScenePassDesc.enabledへ反映する
	bool enabled = node.properties.value("enabled", node.enabled);
	if (ImGui::Checkbox("Enabled", &enabled)) {
		node.enabled = enabled;
		node.properties["enabled"] = enabled;
		changed = true;
	}

	if (node.type == RenderPathGraph::kDraw || node.type == RenderPathGraph::kDepthPrepass) {
		// Draw系はQueueとPassNameを簡易編集する
		changed |= InputJsonString("Queue", node.properties, "queue");
		changed |= InputJsonString("Pass", node.properties, "passName");
	}
	if (node.type == RenderPathGraph::kCompute ||
		node.type == RenderPathGraph::kPostProcess ||
		node.type == RenderPathGraph::kBlit ||
		node.type == RenderPathGraph::kRaytracing) {
		// Materialを使うPassはAsset GUID文字列を編集/表示する
		changed |= InputJsonString("Material", node.properties, "material");
		TextJsonValue("Source", node.properties, "source");
		TextJsonValue("Dest", node.properties, "dest");
	}
	if (node.type == RenderPathGraph::kRenderScene) {
		// RenderSceneはSubScene Slotを編集する
		changed |= InputJsonString("SubScene", node.properties, "subSceneSlot");
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
