#include "RenderPathGraphTool.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Commands/Scene/SetSceneRenderPathCommand.h>
#include <Engine/Editor/Graph/GraphSerializer.h>
#include <Engine/Editor/RenderPathGraph/RenderPathGraphAssetDragDrop.h>
#include <Engine/Editor/RenderPathGraph/RenderPathGraphNodeFactory.h>
#include <Engine/Editor/RenderPathGraph/RenderPathGraphSerializer.h>
#include <Engine/Editor/RenderPathGraph/RenderPathGraphTypes.h>
#include <Engine/Editor/UI/Panels/Core/IEditorPanelHost.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonSerializer.h>
#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>
#include <Engine/Core/Foundation/Identity/UUID.h>
#include <Engine/Core/Tools/ImGui/ImGuiHelpers.h>

// c++
#include <filesystem>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <memory>
#include <unordered_map>
#include <unordered_set>
// imgui
#include <imgui.h>
#include <imgui_node_editor.h>

//============================================================================
//	RenderPathGraphTool classMethods
//============================================================================

namespace {

	namespace ed = ax::NodeEditor;

	constexpr float kSidePanelWidth = 330.0f;
	constexpr const char* kNodeStyleConfigPath = "Tools/RenderPathGraph/renderPathGraphStyle.exeConfig.json";
	const std::vector<std::string> kRenderTargetFormats{
		"RGBA8_UNORM",
		"RGBA16_FLOAT",
		"RGBA32_FLOAT",
	};
	const std::vector<std::string> kRenderTargetSizeModes{
		"ViewRelative",
		"Fixed",
	};
	const std::vector<std::string> kComputeDispatchModes{
		"Fixed",
		"FromSourceSize",
		"FromDestSize",
	};

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

	std::string FirstColorName(const nlohmann::json& targetSet) {

		if (!targetSet.is_object() || !targetSet.contains("colors") || !targetSet["colors"].is_array() ||
			targetSet["colors"].empty()) {
			return {};
		}
		return targetSet["colors"][0].is_string() ? targetSet["colors"][0].get<std::string>() : "";
	}

	nlohmann::json EnsureTargetSet(nlohmann::json targetSet) {

		if (!targetSet.is_object()) {
			targetSet = nlohmann::json::object();
		}
		if (!targetSet.contains("colors") || !targetSet["colors"].is_array()) {
			targetSet["colors"] = nlohmann::json::array();
		}
		return targetSet;
	}

	void SetFirstColor(nlohmann::json& targetSet, const std::string& name) {

		targetSet = EnsureTargetSet(targetSet);
		if (name.empty()) {
			targetSet["colors"] = nlohmann::json::array();
			return;
		}
		if (targetSet["colors"].empty()) {
			targetSet["colors"].push_back(name);
		} else {
			targetSet["colors"][0] = name;
		}
	}

	void SetDepth(nlohmann::json& targetSet, const std::string& name) {

		targetSet = EnsureTargetSet(targetSet);
		if (name.empty()) {
			targetSet.erase("depth");
			return;
		}
		targetSet["depth"] = name;
	}

	std::string DepthName(const nlohmann::json& targetSet) {

		if (!targetSet.is_object() || !targetSet.contains("depth") || !targetSet["depth"].is_string()) {
			return {};
		}
		return targetSet["depth"].get<std::string>();
	}

	std::string ToLowerCopy(std::string text) {

		std::transform(text.begin(), text.end(), text.begin(),
			[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		return text;
	}

	bool ContainsIgnoreCase(const std::string& text, const char* filter) {

		if (!filter || filter[0] == '\0') {
			return true;
		}
		return ToLowerCopy(text).find(ToLowerCopy(filter)) != std::string::npos;
	}

	Engine::GraphPin* FindInputPin(Engine::GraphNode& node, const char* name) {

		for (Engine::GraphPin& pin : node.inputs) {
			if (pin.name == name) {
				return &pin;
			}
		}
		return nullptr;
	}

	Engine::GraphPin* FindOutputPin(Engine::GraphNode& node, const char* name) {

		for (Engine::GraphPin& pin : node.outputs) {
			if (pin.name == name) {
				return &pin;
			}
		}
		return nullptr;
	}

	bool ColorEditRow(const char* label, Engine::Color4& value, const Engine::PropertyRowSetting& setting) {

		if (!Engine::MyGUI::BeginPropertyRow(label, setting)) {
			return false;
		}

		float color[4] = { value.r, value.g, value.b, value.a };
		// NodeEditor Canvas内ではPopupがCanvas座標で開くためSuspend/Resumeで回避する
		ed::Suspend();
		const bool changed = ImGui::ColorEdit4("##Value", color, ImGuiColorEditFlags_Float);
		ed::Resume();
		if (changed) {
			value = Engine::Color4(color[0], color[1], color[2], color[3]);
		}

		Engine::MyGUI::EndPropertyRow();
		return changed;
	}

	bool ResourceCombo(const char* label, std::string& value,
		const std::vector<std::string>& resources, const Engine::PropertyRowSetting& setting);

	bool TargetDepthCombo(const char* label, nlohmann::json& properties, const char* key,
		const std::vector<std::string>& resources, const Engine::PropertyRowSetting& setting) {

		nlohmann::json targetSet = properties.value(key, nlohmann::json::object());
		std::string value = DepthName(targetSet);
		if (!ResourceCombo(label, value, resources, setting)) {
			return false;
		}
		SetDepth(targetSet, value);
		properties[key] = targetSet;
		return true;
	}

	std::vector<std::string> CollectResourceNames(const Engine::GraphDocument& document, const Engine::SceneHeader* header) {

		std::vector<std::string> resources{
			"SceneMain",
			"SceneFinal",
			"SceneColorFinal",
			"View",
		};
		std::unordered_set<std::string> used(resources.begin(), resources.end());

		if (header) {
			for (const Engine::SceneRenderTargetDesc& target : header->renderTargets) {
				if (!target.name.empty() && used.insert(target.name).second) {
					resources.emplace_back(target.name);
				}
				for (const Engine::SceneRenderTargetColorDesc& color : target.colors) {
					if (!color.name.empty() && used.insert(color.name).second) {
						resources.emplace_back(color.name);
					}
				}
			}
		}

		for (const Engine::GraphNode& node : document.nodes) {
			if (node.type != Engine::RenderPathGraph::kTemporaryTarget) {
				continue;
			}
			const std::string name = node.properties.value("name", "");
			if (!name.empty() && used.insert(name).second) {
				resources.emplace_back(name);
			}
		}
		return resources;
	}

	bool ResourceCombo(const char* label, std::string& value,
		const std::vector<std::string>& resources, const Engine::PropertyRowSetting& setting) {

		bool changed = false;
		ImGui::PushID(label);
		if (Engine::MyGUI::BeginPropertyRow(label, setting)) {
			const char* preview = value.empty() ? "<none>" : value.c_str();
			const float width = ImGui::GetContentRegionAvail().x;
			const bool shouldOpen = ImGui::Button(preview, ImVec2(width, ImGui::GetFrameHeight()));
			const ImVec2 itemMin = ImGui::GetItemRectMin();
			const ImVec2 itemMax = ImGui::GetItemRectMax();
			const ImVec2 popupPos = ed::CanvasToScreen(ImVec2(itemMin.x, itemMax.y));
			const float popupWidth = (std::max)(1.0f, ed::CanvasToScreen(itemMax).x - ed::CanvasToScreen(itemMin).x);

			ed::Suspend();
			if (shouldOpen) {
				ImGui::OpenPopup("##ResourceComboPopup");
			}
			ImGui::SetNextWindowPos(popupPos, ImGuiCond_Appearing);
			ImGui::SetNextWindowSizeConstraints(ImVec2(popupWidth, 0.0f), ImVec2(popupWidth, 360.0f));
			if (ImGui::BeginPopup("##ResourceComboPopup")) {
				if (ImGui::Selectable("<none>", value.empty())) {
					value.clear();
					changed = true;
					ImGui::CloseCurrentPopup();
				}
				for (const std::string& resource : resources) {
					const bool selected = value == resource;
					if (ImGui::Selectable(resource.c_str(), selected)) {
						value = resource;
						changed = true;
						ImGui::CloseCurrentPopup();
					}
					if (selected) {
						ImGui::SetItemDefaultFocus();
					}
				}
				ImGui::EndPopup();
			}
			ed::Resume();
			Engine::MyGUI::EndPropertyRow();
		}
		ImGui::PopID();
		return changed;
	}

	bool TargetSetCombo(const char* label, nlohmann::json& properties, const char* key,
		const std::vector<std::string>& resources, const Engine::PropertyRowSetting& setting) {

		nlohmann::json targetSet = properties.value(key, nlohmann::json::object());
		std::string value = FirstColorName(targetSet);
		if (!ResourceCombo(label, value, resources, setting)) {
			return false;
		}
		SetFirstColor(targetSet, value);
		properties[key] = targetSet;
		return true;
	}

	std::string MakeCompileDiffText(const Engine::SceneHeader& baseHeader,
		const Engine::RenderPathGraphCompileResult& result) {

		const nlohmann::json beforeJson = Engine::ToJson(baseHeader);
		Engine::SceneHeader afterSceneHeader = baseHeader;
		afterSceneHeader.renderTargets = result.renderTargets;
		afterSceneHeader.passOrder = result.passOrder;
		const nlohmann::json afterJson = Engine::ToJson(afterSceneHeader);

		const std::string rtDiff = beforeJson["renderTargets"].dump() == afterJson["renderTargets"].dump()
			? "No change" : "Changed";

		// Pass種別+代表フィールドでKeyを作り、Added/Removed/Changed/Reorderedを判定する
		auto passKey = [](const nlohmann::json& pass) {
			std::string key = pass.value("type", "Unknown");
			for (const char* field : { "queue", "passName", "material", "subSceneSlot" }) {
				if (pass.contains(field) && pass[field].is_string() && !pass[field].get<std::string>().empty()) {
					key += ":" + pass[field].get<std::string>();
					break;
				}
			}
			return key;
		};

		const nlohmann::json& before = beforeJson.value("passOrder", nlohmann::json::array());
		const nlohmann::json& after = afterJson.value("passOrder", nlohmann::json::array());

		std::unordered_map<std::string, std::pair<size_t, std::string>> beforeMap{};
		std::unordered_map<std::string, std::pair<size_t, std::string>> afterMap{};
		for (size_t i = 0; i < before.size(); ++i) {
			std::string key = passKey(before[i]);
			while (beforeMap.contains(key)) {
				key += "_" + std::to_string(i);
			}
			beforeMap[key] = { i, before[i].dump() };
		}
		for (size_t i = 0; i < after.size(); ++i) {
			std::string key = passKey(after[i]);
			while (afterMap.contains(key)) {
				key += "_" + std::to_string(i);
			}
			afterMap[key] = { i, after[i].dump() };
		}

		std::string addedText, removedText, changedText, reorderedText;
		for (const auto& [key, afterInfo] : afterMap) {
			if (!beforeMap.contains(key)) {
				addedText += "  - " + key + "\n";
			}
		}
		for (const auto& [key, beforeInfo] : beforeMap) {
			if (!afterMap.contains(key)) {
				removedText += "  - " + key + "\n";
			} else {
				const auto& [beforeIdx, beforeDump] = beforeInfo;
				const auto& [afterIdx, afterDump] = afterMap.at(key);
				if (beforeDump != afterDump) {
					changedText += "  - " + key + "\n";
				}
				if (beforeIdx != afterIdx) {
					reorderedText += "  - " + key + " : "
						+ std::to_string(beforeIdx) + " -> " + std::to_string(afterIdx) + "\n";
				}
			}
		}

		std::string text;
		text += "RenderTargets : " + rtDiff + "\n";
		if (!addedText.empty()) {
			text += "Added\n" + addedText;
		}
		if (!removedText.empty()) {
			text += "Removed\n" + removedText;
		}
		if (!changedText.empty()) {
			text += "Changed\n" + changedText;
		}
		if (!reorderedText.empty()) {
			text += "Reordered\n" + reorderedText;
		}
		if (addedText.empty() && removedText.empty() && changedText.empty() && reorderedText.empty()) {
			text += "PassOrder : No change\n";
		}
		return text;
	}

	nlohmann::json RenderPathToJson(const Engine::RenderPathGraphCompileResult& result) {

		// Compile結果のPreviewはSceneHeaderの既存ToJsonを通して表示する
		Engine::SceneHeader header{};
		header.name = "CompilePreview";
		header.renderTargets = result.renderTargets;
		header.passOrder = result.passOrder;
		nlohmann::json data = nlohmann::json::object();
		const nlohmann::json headerJson = Engine::ToJson(header);
		data["renderTargets"] = headerJson["renderTargets"];
		data["passOrder"] = headerJson["passOrder"];
		return data;
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
		ImGui::TextDisabled("アクティブシーンがありません");
		ImGui::End();
		return;
	}

	// 開いているシーンが変わった時だけ自動Importする。毎フレーム再生成はしない
	if (!imported_ || importedScenePath_ != scenePath) {
		ImportFromCurrentScene(context, true);
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

	AutoSaveGraph(context);

	// マウスボタンが離れたら保留中のUndoスナップショットを確定する
	if (undoPending_ && !ImGui::IsMouseDown(ImGuiMouseButton_Left) && !ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
		PushUndoSnapshot(std::move(preDrawSnapshot_));
		preDrawSnapshotValid_ = false;
		undoPending_ = false;
	}

	ImGui::End();
}

void Engine::RenderPathGraphTool::DrawToolbar(const EditorToolContext& context) {

	// 現在のSceneHeaderからGraphを作り直す
	if (ImGui::Button("取り込む")) {
		ImportFromCurrentScene(context, false);
	}
	ImGui::SameLine();
	if (ImGui::Button("検証")) {
		ValidateCurrentGraph(context);
	}
	ImGui::SameLine();
	if (ImGui::Button("プレビュー")) {
		CompilePreview(context);
	}
	ImGui::SameLine();

	// ApplyはScene編集可能かつValidation Errorが無い時だけ許可する
	const bool canApply = context.CanEditScene() && !validationResult_.HasError();
	if (!canApply) {
		ImGui::BeginDisabled();
	}
	if (ImGui::Button("適用")) {
		ApplyToScene(context);
	}
	if (!canApply) {
		ImGui::EndDisabled();
	}

	ImGui::SameLine();
	if (ImGui::Button("保存")) {
		SaveScene(context);
	}
	ImGui::SameLine();
	if (ImGui::Button("書き出し")) {
		ExportGraph(context);
	}
	ImGui::SameLine();
	if (ImGui::Button("読み込み")) {
		ImportGraph(context);
	}
	ImGui::SameLine();
	if (ImGui::Button("整列")) {
		ResetLayout();
	}
	ImGui::SameLine();
	if (ImGui::Button("全体表示")) {
		requestFitToGraph_ = true;
	}
	ImGui::SameLine();

	// Undo / Redo
	if (undoStack_.empty()) {
		ImGui::BeginDisabled();
	}
	if (ImGui::Button("元に戻す")) {
		UndoGraph();
	}
	if (undoStack_.empty()) {
		ImGui::EndDisabled();
	}
	ImGui::SameLine();
	if (redoStack_.empty()) {
		ImGui::BeginDisabled();
	}
	if (ImGui::Button("やり直し")) {
		RedoGraph();
	}
	if (redoStack_.empty()) {
		ImGui::EndDisabled();
	}
	ImGui::SameLine();

	// Minimap toggle — Button呼び出し前に状態を確定してPush/Popを対称にする
	const bool wasShowMinimap = showMinimap_;
	if (wasShowMinimap) {
		ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
	}
	if (ImGui::Button("マップ")) {
		showMinimap_ = !showMinimap_;
	}
	if (wasShowMinimap) {
		ImGui::PopStyleColor();
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
	ImGui::Text("シーン : %s", header ? header->name.c_str() : "なし");
	ImGui::Text("パス数 : %zu", document_.nodes.size());
	ImGui::Text("リンク数 : %zu", document_.links.size());
	ImGui::Text("グラフ : %s", graphDirty_ ? "未保存" : "保存済み");

	ImGui::Separator();
	DrawFilterPanel();
	ImGui::Separator();
	DrawTemplatePanel(context);
	ImGui::Separator();
	DrawResourceBlackboard(context);
	ImGui::Separator();
	DrawValidationMessages();
	ImGui::Separator();
	DrawCompilePreview();
	ImGui::Separator();
	DrawResourceAnalysis();
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
		bool changed = false;
		if (RenderPathGraphAssetDragDrop::AcceptMaterial(node, context)) {
			// Node上へMaterialをDropしたら再検証対象にする
			changed = true;
			Logger::Output(LogType::Engine, spdlog::level::debug,
				"RenderPathGraphTool: material dropped. node={}", static_cast<unsigned long long>(node.id));
		}
		// Resource Blackboard からのDrop
		if (ImGui::BeginDragDropTarget()) {
			if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("RENDERPATH_RESOURCE")) {
				if (payload->IsDelivery()) {
					const char* resourceName = static_cast<const char*>(payload->Data);
					if (node.type == RenderPathGraph::kClear) {
						node.properties["target"] = std::string(resourceName);
					} else if (RenderPathGraphNodeFactory::IsScenePassNode(node.type)) {
						nlohmann::json dest = node.properties.value("dest", nlohmann::json::object());
						SetFirstColor(dest, std::string(resourceName));
						node.properties["dest"] = dest;
					}
					changed = true;
					Logger::Output(LogType::Engine, spdlog::level::debug,
						"RenderPathGraphTool: resource dropped. node={} resource={}",
						static_cast<unsigned long long>(node.id), resourceName);
				}
			}
			ImGui::EndDragDropTarget();
		}
		if (changed) {
			graphDirty_ = true;
			validationDirty_ = true;
			graphSaveDirty_ = true;
			compilePreviewText_.clear();
			compileDiffText_.clear();
			resourceLifetimeText_.clear();
			barrierPreviewText_.clear();
		}
	};
	desc.addNodeRequested = [this](const std::string& nodeType, const ImVec2& position) {
		// 背景メニューからNodeを追加する
		GraphNode node = registry_.CreateNode(document_, nodeType, position);
		document_.AddNode(std::move(node));
		graphDirty_ = true;
		validationDirty_ = true;
		graphSaveDirty_ = true;
		compilePreviewText_.clear();
		compileDiffText_.clear();
		resourceLifetimeText_.clear();
		barrierPreviewText_.clear();
		Logger::Output(LogType::Engine, spdlog::level::debug,
			"RenderPathGraphTool: add node. type={}", nodeType);
	};
	desc.isGroupNode = [](const GraphNode& node) {
		return node.type == RenderPathGraph::kGroup;
	};
	desc.isNodeHighlighted = [this](const GraphNode& node) {
		const bool textMatched =
			ContainsIgnoreCase(node.displayName, filterText_) ||
			ContainsIgnoreCase(node.type, filterText_);
		if (!textMatched) {
			return false;
		}
		if (filterHasValidationMessage_ && node.validationMessages.empty()) {
			return false;
		}
		if (filterUsesMaterial_ && !node.properties.contains("material")) {
			return false;
		}
		if (filterReadsDepth_ && DepthName(node.properties.value("source", nlohmann::json::object())).empty()) {
			return false;
		}
		if (filterWritesView_ && FirstColorName(node.properties.value("dest", nlohmann::json::object())) != "View") {
			return false;
		}
		return filterText_[0] != '\0' || filterHasValidationMessage_ || filterUsesMaterial_ ||
			filterReadsDepth_ || filterWritesView_;
	};
	desc.navigateToContent = requestFitToGraph_;

	// Undo用: Draw前にドキュメントのスナップショットを1フレームに1回だけ取得する
	if (!preDrawSnapshotValid_) {
		preDrawSnapshot_ = GraphSerializer::ToJson(document_);
		preDrawSnapshotValid_ = true;
	}

	if (graphView_.Draw(graphContext_, document_, desc)) {
		undoPending_ = true;
		graphDirty_ = true;
		validationDirty_ = true;
		graphSaveDirty_ = true;
		compilePreviewText_.clear();
		compileDiffText_.clear();
		resourceLifetimeText_.clear();
		barrierPreviewText_.clear();
		Logger::Output(LogType::Engine, spdlog::level::debug,
			"RenderPathGraphTool: graph edited.");
	}
	requestFitToGraph_ = false;

	if (showMinimap_) {
		DrawMinimap();
	}
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
		.showTooltip = false,
		.propertyRow = propertyRowSetting,
	};
	const std::vector<std::string> resources = CollectResourceNames(document_, context.toolContext.activeSceneHeader);

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
		changed |= TargetSetCombo("Dest", node.properties, "dest", resources, propertyRowSetting);
		changed |= TargetDepthCombo("DestDepth", node.properties, "dest", resources, propertyRowSetting);
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
		changed |= TargetSetCombo("Source", node.properties, "source", resources, propertyRowSetting);
		changed |= TargetDepthCombo("SourceDepth", node.properties, "source", resources, propertyRowSetting);
		changed |= TargetSetCombo("Dest", node.properties, "dest", resources, propertyRowSetting);
		changed |= TargetDepthCombo("DestDepth", node.properties, "dest", resources, propertyRowSetting);
	}
	if (node.type == RenderPathGraph::kRenderScene) {
		// RenderSceneはSubScene Slotを編集する
		std::string subSceneSlot = node.properties.value("subSceneSlot", "");
		if (MyGUI::InputText("SubScene", subSceneSlot, textSetting).editFinished) {
			node.properties["subSceneSlot"] = subSceneSlot;
			changed = true;
		}
		changed |= TargetSetCombo("Dest", node.properties, "dest", resources, propertyRowSetting);
		changed |= TargetDepthCombo("DestDepth", node.properties, "dest", resources, propertyRowSetting);
	}
	if (node.type == RenderPathGraph::kCompute) {
		std::string passName = node.properties.value("passName", "");
		if (MyGUI::InputText("Pass", passName, textSetting).editFinished) {
			node.properties["passName"] = passName;
			changed = true;
		}
		std::string dispatchMode = node.properties.value("dispatchMode", "FromDestSize");
		if (ResourceCombo("Dispatch", dispatchMode, kComputeDispatchModes, propertyRowSetting)) {
			node.properties["dispatchMode"] = dispatchMode;
			changed = true;
		}
		int32_t groupCountX = static_cast<int32_t>(node.properties.value("groupCountX", 1u));
		if (MyGUI::DragInt("GroupX", groupCountX, { .minValue = 1, .propertyRow = propertyRowSetting }).editFinished) {
			node.properties["groupCountX"] = static_cast<uint32_t>(groupCountX);
			changed = true;
		}
		int32_t groupCountY = static_cast<int32_t>(node.properties.value("groupCountY", 1u));
		if (MyGUI::DragInt("GroupY", groupCountY, { .minValue = 1, .propertyRow = propertyRowSetting }).editFinished) {
			node.properties["groupCountY"] = static_cast<uint32_t>(groupCountY);
			changed = true;
		}
		int32_t groupCountZ = static_cast<int32_t>(node.properties.value("groupCountZ", 1u));
		if (MyGUI::DragInt("GroupZ", groupCountZ, { .minValue = 1, .propertyRow = propertyRowSetting }).editFinished) {
			node.properties["groupCountZ"] = static_cast<uint32_t>(groupCountZ);
			changed = true;
		}
	}
	if (node.type == RenderPathGraph::kClear) {
		// Clear値をScenePassDescへ反映する
		std::string target = node.properties.value("target", "");
		if (ResourceCombo("Target", target, resources, propertyRowSetting)) {
			node.properties["target"] = target;
			changed = true;
		}
		bool clearColor = node.properties.value("clearColor", true);
		if (MyGUI::Checkbox("ClearColor", clearColor, propertyRowSetting)) {
			node.properties["clearColor"] = clearColor;
			changed = true;
		}
		Color4 clearColorValue = Color4::Black();
		if (node.properties.contains("clearColorValue") && !node.properties["clearColorValue"].is_null()) {
			clearColorValue = Color4::FromJson(node.properties["clearColorValue"]);
		}
		if (ColorEditRow("ColorValue", clearColorValue, propertyRowSetting)) {
			node.properties["clearColorValue"] = clearColorValue.ToJson();
			changed = true;
		}
		bool clearDepth = node.properties.value("clearDepth", false);
		if (MyGUI::Checkbox("ClearDepth", clearDepth, propertyRowSetting)) {
			node.properties["clearDepth"] = clearDepth;
			changed = true;
		}
		float clearDepthValue = node.properties.value("clearDepthValue", 1.0f);
		if (MyGUI::DragFloat("DepthValue", clearDepthValue, { .dragSpeed = 0.01f, .minValue = 0.0f, .maxValue = 1.0f, .propertyRow = propertyRowSetting }).editFinished) {
			node.properties["clearDepthValue"] = clearDepthValue;
			changed = true;
		}
		bool clearStencil = node.properties.value("clearStencil", false);
		if (MyGUI::Checkbox("ClearStencil", clearStencil, propertyRowSetting)) {
			node.properties["clearStencil"] = clearStencil;
			changed = true;
		}
		int32_t clearStencilValue = static_cast<int32_t>(node.properties.value("clearStencilValue", 0u));
		if (MyGUI::DragInt("StencilValue", clearStencilValue, { .minValue = 0, .maxValue = 255, .propertyRow = propertyRowSetting }).editFinished) {
			node.properties["clearStencilValue"] = static_cast<uint32_t>(clearStencilValue);
			changed = true;
		}
	}
	if (node.type == RenderPathGraph::kTemporaryTarget) {
		// TemporaryTargetはSceneHeader.renderTargetsへ変換する
		std::string name = node.properties.value("name", "");
		if (MyGUI::InputText("Name", name, textSetting).editFinished) {
			node.properties["name"] = name;
			node.displayName = name.empty() ? "TemporaryTarget" : "RT : " + name;
			changed = true;
		}

		std::string format = node.properties.value("format", "RGBA32_FLOAT");
		if (ResourceCombo("Format", format, kRenderTargetFormats, propertyRowSetting)) {
			node.properties["format"] = format;
			changed = true;
		}
		std::string sizeMode = node.properties.value("sizeMode", "ViewRelative");
		if (ResourceCombo("SizeMode", sizeMode, kRenderTargetSizeModes, propertyRowSetting)) {
			node.properties["sizeMode"] = sizeMode;
			changed = true;
		}

		float widthScale = node.properties.value("widthScale", 1.0f);
		if (MyGUI::DragFloat("WidthScale", widthScale, { .dragSpeed = 0.01f, .minValue = 0.01f, .propertyRow = propertyRowSetting }).editFinished) {
			node.properties["widthScale"] = widthScale;
			changed = true;
		}
		float heightScale = node.properties.value("heightScale", 1.0f);
		if (MyGUI::DragFloat("HeightScale", heightScale, { .dragSpeed = 0.01f, .minValue = 0.01f, .propertyRow = propertyRowSetting }).editFinished) {
			node.properties["heightScale"] = heightScale;
			changed = true;
		}
		int32_t fixedWidth = static_cast<int32_t>(node.properties.value("fixedWidth", 0u));
		if (MyGUI::DragInt("FixedWidth", fixedWidth, { .minValue = 0, .propertyRow = propertyRowSetting }).editFinished) {
			node.properties["fixedWidth"] = static_cast<uint32_t>(fixedWidth);
			changed = true;
		}
		int32_t fixedHeight = static_cast<int32_t>(node.properties.value("fixedHeight", 0u));
		if (MyGUI::DragInt("FixedHeight", fixedHeight, { .minValue = 0, .propertyRow = propertyRowSetting }).editFinished) {
			node.properties["fixedHeight"] = static_cast<uint32_t>(fixedHeight);
			changed = true;
		}

		bool createUAV = node.properties.value("createUAV", true);
		if (MyGUI::Checkbox("CreateUAV", createUAV, propertyRowSetting)) {
			node.properties["createUAV"] = createUAV;
			changed = true;
		}
		bool withDepth = node.properties.value("withDepth", false);
		if (MyGUI::Checkbox("WithDepth", withDepth, propertyRowSetting)) {
			node.properties["withDepth"] = withDepth;
			changed = true;
		}
		bool persistent = node.properties.value("persistent", false);
		if (MyGUI::Checkbox("Persistent", persistent, propertyRowSetting)) {
			node.properties["persistent"] = persistent;
			changed = true;
		}
	}
	if (node.type == RenderPathGraph::kView) {
		std::string name = node.properties.value("name", "View");
		if (MyGUI::InputText("Name", name, textSetting).editFinished) {
			node.properties["name"] = name;
			node.displayName = name.empty() ? "View" : name;
			changed = true;
		}
	}
	if (node.type == RenderPathGraph::kComment) {
		std::string text = node.properties.value("text", "");
		if (MyGUI::InputText("Text", text, textSetting).editFinished) {
			node.properties["text"] = text;
			node.displayName = text.empty() ? "Comment" : text;
			changed = true;
		}
	}
	if (node.type == RenderPathGraph::kGroup) {
		std::string title = node.properties.value("title", "Group");
		if (MyGUI::InputText("Title", title, textSetting).editFinished) {
			node.properties["title"] = title;
			node.displayName = title.empty() ? "Group" : title;
			changed = true;
		}
		float r = node.properties.value("colorR", 0.25f);
		float g = node.properties.value("colorG", 0.38f);
		float b = node.properties.value("colorB", 0.55f);
		float a = node.properties.value("colorA", 0.30f);
		Color4 groupColor(r, g, b, a);
		if (ColorEditRow("Color", groupColor, propertyRowSetting)) {
			node.properties["colorR"] = groupColor.r;
			node.properties["colorG"] = groupColor.g;
			node.properties["colorB"] = groupColor.b;
			node.properties["colorA"] = groupColor.a;
			changed = true;
		}
	}

	if (changed) {
		// Property変更後はCompile / Validationをやり直す
		undoPending_ = true;
		graphDirty_ = true;
		validationDirty_ = true;
		graphSaveDirty_ = true;
		compilePreviewText_.clear();
		compileDiffText_.clear();
		resourceLifetimeText_.clear();
		barrierPreviewText_.clear();
		Logger::Output(LogType::Engine, spdlog::level::debug,
			"RenderPathGraphTool: node property edited. node={} type={}",
			static_cast<unsigned long long>(node.id), node.type);
	}
}

void Engine::RenderPathGraphTool::DrawResourceBlackboard(const EditorToolContext& context) {

	ImGui::TextUnformatted("リソース一覧");
	ImGui::TextDisabled("ドラッグしてNodeへ Dest を設定できます");

	const ImVec4 disabledColor = ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);

	// IDが同名のリソースで衝突しないよう連番IDでPushIDする
	int rsIdx = 0;
	auto resourceSelectable = [&rsIdx](const char* name, const ImVec4& color) {
		ImGui::PushID(rsIdx++);
		ImGui::PushStyleColor(ImGuiCol_Text, color);
		ImGui::Selectable(name, false, ImGuiSelectableFlags_None);
		ImGui::PopStyleColor();
		if (ImGui::BeginDragDropSource()) {
			ImGui::SetDragDropPayload("RENDERPATH_RESOURCE", name, std::strlen(name) + 1);
			ImGui::Text("→ Dest: %s", name);
			ImGui::EndDragDropSource();
		}
		ImGui::PopID();
	};

	resourceSelectable("SceneMain", disabledColor);
	resourceSelectable("SceneFinal", disabledColor);
	resourceSelectable("SceneColorFinal", disabledColor);
	resourceSelectable("View", disabledColor);

	const SceneHeader* header = context.toolContext.activeSceneHeader;
	if (header) {
		for (const SceneRenderTargetDesc& target : header->renderTargets) {
			if (target.name.empty()) {
				continue;
			}
			const std::string label = target.name + (target.createUAV ? "  UAV" : "");
			resourceSelectable(label.c_str(), disabledColor);
			for (const SceneRenderTargetColorDesc& color : target.colors) {
				if (!color.name.empty() && color.name != target.name) {
					const std::string subLabel = "  " + color.name + (color.createUAV ? "  UAV" : "");
					resourceSelectable(subLabel.c_str(), disabledColor);
				}
			}
		}
	}

	const ImVec4& tmpColor = graphView_.GetStyle().temporaryNodeColor;
	for (const GraphNode& node : document_.nodes) {
		if (node.type != RenderPathGraph::kTemporaryTarget) {
			continue;
		}
		const std::string name = node.properties.value("name", "");
		if (name.empty()) {
			continue;
		}
		const std::string format = node.properties.value("format", "RGBA32_FLOAT");
		const std::string label = name + "  " + format + (node.properties.value("createUAV", false) ? "  UAV" : "");
		resourceSelectable(label.c_str(), tmpColor);
	}
}

void Engine::RenderPathGraphTool::DrawTemplatePanel(const EditorToolContext& context) {

	(void)context;

	ImGui::TextUnformatted("テンプレート");
	if (ImGui::Button("フォワードレンダリング基本", ImVec2(ImGui::GetContentRegionAvail().x, 0.0f))) {
		ApplyTemplate("ForwardBasic");
	}
	if (ImGui::Button("Depth優先描画 + Blit", ImVec2(ImGui::GetContentRegionAvail().x, 0.0f))) {
		ApplyTemplate("DepthDrawBlit");
	}
	if (ImGui::Button("HDRブルームチェーン", ImVec2(ImGui::GetContentRegionAvail().x, 0.0f))) {
		ApplyTemplate("HDRBloom");
	}
	if (ImGui::Button("コンピュートポストチェーン", ImVec2(ImGui::GetContentRegionAvail().x, 0.0f))) {
		ApplyTemplate("ComputePost");
	}
}

void Engine::RenderPathGraphTool::DrawFilterPanel() {

	ImGui::TextUnformatted("検索フィルター");
	ImGui::SetNextItemWidth(-1.0f);
	ImGui::InputText("##RenderPathGraphFilter", filterText_, sizeof(filterText_));
	MyGUI::SmallCheckbox("検証あり", filterHasValidationMessage_);
	ImGui::SameLine();
	MyGUI::SmallCheckbox("マテリアルあり", filterUsesMaterial_);
	MyGUI::SmallCheckbox("Depth使用", filterReadsDepth_);
	ImGui::SameLine();
	MyGUI::SmallCheckbox("View出力", filterWritesView_);
}

void Engine::RenderPathGraphTool::DrawValidationMessages() {

	ImGui::TextUnformatted("検証結果");
	if (validationResult_.messages.empty()) {
		ImGui::TextDisabled("問題なし");
		return;
	}

	ImGui::PushTextWrapPos(0.0f);
	for (const GraphValidationMessage& message : validationResult_.messages) {
		// 重要度に合わせて色を変える
		const char* label = "情報";
		ImVec4 color{ 0.60f, 0.72f, 0.90f, 1.0f };
		if (message.severity == GraphValidationMessage::Severity::Warning) {
			label = "警告";
			color = graphView_.GetStyle().GetWarningColor();
		} else if (message.severity == GraphValidationMessage::Severity::Error) {
			label = "エラー";
			color = graphView_.GetStyle().GetErrorColor();
		}
		ImGui::TextColored(color, "%s node=%llu : %s",
			label, static_cast<unsigned long long>(message.nodeID), message.message.c_str());
	}
	ImGui::PopTextWrapPos();
}

void Engine::RenderPathGraphTool::DrawCompilePreview() {

	ImGui::TextUnformatted("コンパイル結果");
	if (compilePreviewText_.empty()) {
		ImGui::TextDisabled("プレビューボタンで確認できます");
		return;
	}

	if (!compileDiffText_.empty()) {
		ImGui::TextUnformatted("変更点");
		ImGui::TextUnformatted(compileDiffText_.c_str());
	}

	ImGui::BeginChild("RenderPathCompilePreviewText", ImVec2(0.0f, 230.0f), true);
	// JSON文字列をそのまま表示して、Apply前にpassOrderを確認できるようにする
	ImGui::TextUnformatted(compilePreviewText_.c_str());
	ImGui::EndChild();
}

void Engine::RenderPathGraphTool::DrawResourceAnalysis() {

	ImGui::TextUnformatted("リソース解析");
	if (resourceLifetimeText_.empty()) {
		ImGui::TextDisabled("コンパイル後に解析結果が更新されます");
		return;
	}

	if (ImGui::CollapsingHeader("リソース使用範囲", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::TextUnformatted(resourceLifetimeText_.c_str());
	}
	if (ImGui::CollapsingHeader("バリア確認")) {
		ImGui::TextUnformatted(barrierPreviewText_.c_str());
	}
	if (ImGui::CollapsingHeader("ランタイムキャプチャ")) {
		if (capturedPasses_.empty()) {
			ImGui::TextDisabled("コンパイル後に表示されます");
		} else {
			ImGui::TextDisabled("静的解析 (実行時計測なし)");
			ImGui::Separator();
			for (size_t i = 0; i < capturedPasses_.size(); ++i) {
				const RenderPathFrameCapturePass& pass = capturedPasses_[i];
				ImGui::PushID(static_cast<int>(i));
				ImGui::PushTextWrapPos(0.0f);
				ImGui::TextUnformatted(pass.name.c_str());
				ImGui::TextDisabled("種別 : %s", pass.type.c_str());
				if (!pass.reads.empty()) {
					std::string reads;
					for (const std::string& r : pass.reads) {
						reads += r + " ";
					}
					ImGui::TextDisabled("  読 : %s", reads.c_str());
				}
				if (!pass.writes.empty()) {
					std::string writes;
					for (const std::string& w : pass.writes) {
						writes += w + " ";
					}
					ImGui::TextDisabled("  書 : %s", writes.c_str());
				}
				ImGui::PopTextWrapPos();
				ImGui::Separator();
				ImGui::PopID();
			}
		}
	}
}

void Engine::RenderPathGraphTool::ImportFromCurrentScene(const EditorToolContext& context, bool preferSavedGraph) {

	const SceneHeader* header = context.toolContext.activeSceneHeader;
	if (!header) {
		statusMessage_ = "アクティブシーンがありません";
		return;
	}

	const std::string graphPath = MakeActiveGraphPath(context);
	if (preferSavedGraph && std::filesystem::exists(graphPath)) {
		std::string error{};
		if (RenderPathGraphSerializer::ImportGraph(graphPath, document_, &error)) {
			graphContext_.ResetPlacedNodes();
			imported_ = true;
			importedScenePath_ = std::string(context.toolContext.activeScenePath);
			graphDirty_ = false;
			graphSaveDirty_ = false;
			validationDirty_ = true;
			compilePreviewText_.clear();
			compileDiffText_.clear();
			resourceLifetimeText_.clear();
			barrierPreviewText_.clear();
			compiledResult_ = RenderPathGraphCompileResult{};
			capturedPasses_.clear();
			undoStack_.clear();
			redoStack_.clear();
			preDrawSnapshotValid_ = false;
			ValidateCurrentGraph(context);
			statusMessage_ = "グラフを復元しました";
			Logger::Output(LogType::Engine, spdlog::level::info,
				"RenderPathGraphTool: graph restored. path={}", graphPath);
			return;
		}
		Logger::Output(LogType::Engine, spdlog::level::warn,
			"RenderPathGraphTool: failed to restore graph. path={} error={}", graphPath, error);
	}

	// Import時はSceneHeaderをGraphへ変換し、Node配置情報も初期化する
	importer_.Import(*header, std::string(context.toolContext.activeScenePath), document_);
	graphContext_.ResetPlacedNodes();
	imported_ = true;
	importedScenePath_ = std::string(context.toolContext.activeScenePath);
	graphDirty_ = false;
	graphSaveDirty_ = true;
	validationDirty_ = true;
	compilePreviewText_.clear();
	compileDiffText_.clear();
	resourceLifetimeText_.clear();
	barrierPreviewText_.clear();
	compiledResult_ = RenderPathGraphCompileResult{};
	capturedPasses_.clear();
	undoStack_.clear();
	redoStack_.clear();
	preDrawSnapshotValid_ = false;
	ValidateCurrentGraph(context);
	statusMessage_ = "取り込み完了";
	Logger::Output(LogType::Engine, spdlog::level::info,
		"RenderPathGraphTool: imported from scene. scene={}", importedScenePath_);
}

void Engine::RenderPathGraphTool::ValidateCurrentGraph(const EditorToolContext& context) {

	// ValidatorはNode上のメッセージも更新するため、Documentを渡す
	validationResult_ = validator_.Validate(document_, context.toolContext.activeSceneHeader);

	// Material Asset存在チェック (AssetDatabaseが使える時だけ実行する)
	const AssetDatabase* assetDatabase = context.panelContext && context.panelContext->editorContext ?
		context.panelContext->editorContext->assetDatabase : nullptr;
	if (assetDatabase) {
		for (GraphNode& node : document_.nodes) {
			const std::string materialStr = node.properties.value("material", "");
			if (materialStr.empty()) {
				continue;
			}
			const AssetID materialID = FromString16Hex(materialStr);
			if (!assetDatabase->Find(materialID)) {
				constexpr char kMsg[] = "Materialアセットが見つかりません";
				validationResult_.Add(GraphValidationMessage::Severity::Warning, node.id, 0, kMsg);
				node.validationMessages.emplace_back(kMsg);
			}
		}
	}

	validationDirty_ = false;
	statusMessage_ = validationResult_.HasError() ? "検証エラーがあります" : "検証通過";
	Logger::Output(LogType::Engine, spdlog::level::info,
		"RenderPathGraphTool: validate. messages={} hasError={}",
		validationResult_.messages.size(), validationResult_.HasError());
}

void Engine::RenderPathGraphTool::CompilePreview(const EditorToolContext& context) {

	if (validationDirty_) {
		// 古い検証結果でCompileしない
		ValidateCurrentGraph(context);
	}
	if (validationResult_.HasError()) {
		statusMessage_ = "検証エラーのためコンパイル停止";
		return;
	}
	if (!context.toolContext.activeSceneHeader) {
		statusMessage_ = "アクティブシーンがありません";
		return;
	}

	std::string error{};
	if (!compiler_.Compile(document_, *context.toolContext.activeSceneHeader, compiledResult_, &error)) {
		statusMessage_ = error;
		Logger::Output(LogType::Engine, spdlog::level::warn,
			"RenderPathGraphTool: compile failed. error={}", error);
		return;
	}

	// Compile結果はSceneHeaderの描画情報JSONとしてPreview表示する
	compilePreviewText_ = RenderPathToJson(compiledResult_).dump(2);
	compileDiffText_ = MakeCompileDiffText(*context.toolContext.activeSceneHeader, compiledResult_);
	UpdateResourceAnalysisText();

	// ランタイムキャプチャ表示用にパス情報を静的解析で構築する
	capturedPasses_.clear();
	capturedPasses_.reserve(compiledResult_.passOrder.size());
	auto addRef = [](const RenderTargetSetReference& ref, std::vector<std::string>& out) {
		for (const std::string& color : ref.colors) {
			if (!color.empty()) {
				out.emplace_back(color);
			}
		}
		if (ref.depth.has_value() && !ref.depth->empty()) {
			out.emplace_back(*ref.depth + " (Depth)");
		}
	};
	for (const ScenePassDesc& pass : compiledResult_.passOrder) {
		if (!pass.enabled) {
			continue;
		}
		RenderPathFrameCapturePass captured{};
		captured.type = EnumAdapter<ScenePassType>::ToString(pass.type);
		switch (pass.type) {
		case ScenePassType::Clear:
			captured.name = "Clear";
			addRef(pass.clear.dest, captured.writes);
			break;
		case ScenePassType::DepthPrepass:
			captured.name = pass.depthPrepass.queue.empty() ? "DepthPrepass" : "DepthPrepass : " + pass.depthPrepass.queue;
			addRef(pass.depthPrepass.dest, captured.writes);
			break;
		case ScenePassType::Draw:
			captured.name = pass.draw.queue.empty() ? "Draw" : "Draw : " + pass.draw.queue;
			addRef(pass.draw.dest, captured.writes);
			break;
		case ScenePassType::PostProcess:
			captured.name = "PostProcess";
			addRef(pass.postProcess.source, captured.reads);
			addRef(pass.postProcess.dest, captured.writes);
			break;
		case ScenePassType::Compute:
			captured.name = pass.compute.passName.empty() ? "Compute" : "Compute : " + pass.compute.passName;
			addRef(pass.compute.source, captured.reads);
			addRef(pass.compute.dest, captured.writes);
			break;
		case ScenePassType::RenderScene:
			captured.name = pass.renderScene.subSceneSlot.empty() ? "RenderScene" : "RenderScene : " + pass.renderScene.subSceneSlot;
			addRef(pass.renderScene.dest, captured.writes);
			break;
		case ScenePassType::Blit:
			captured.name = "Blit";
			addRef(pass.blit.source, captured.reads);
			addRef(pass.blit.dest, captured.writes);
			break;
		case ScenePassType::Raytracing:
			captured.name = pass.raytracing.passName.empty() ? "Raytracing" : "Raytracing : " + pass.raytracing.passName;
			addRef(pass.raytracing.source, captured.reads);
			addRef(pass.raytracing.dest, captured.writes);
			break;
		default:
			break;
		}
		capturedPasses_.emplace_back(std::move(captured));
	}

	statusMessage_ = "コンパイル完了";
	Logger::Output(LogType::Engine, spdlog::level::info,
		"RenderPathGraphTool: compiled. passes={} renderTargets={}",
		compiledResult_.passOrder.size(), compiledResult_.renderTargets.size());
}

void Engine::RenderPathGraphTool::ApplyToScene(const EditorToolContext& context) {

	if (!context.panelContext || !context.panelContext->host || !context.toolContext.activeSceneHeader) {
		statusMessage_ = "エディターホストがありません";
		return;
	}

	// Apply前に必ず最新GraphをCompileする
	CompilePreview(context);
	if (validationResult_.HasError() || compiledResult_.passOrder.empty()) {
		statusMessage_ = "適用を中断しました";
		return;
	}

	// 描画情報変更はUndo/Redoに乗せるためCommand経由で実行する
	std::vector<SceneRenderTargetDesc> beforeRenderTargets = context.toolContext.activeSceneHeader->renderTargets;
	std::vector<ScenePassDesc> beforePassOrder = context.toolContext.activeSceneHeader->passOrder;
	auto command = std::make_unique<SetSceneRenderPathCommand>(
		std::move(beforeRenderTargets), std::move(beforePassOrder),
		compiledResult_.renderTargets, compiledResult_.passOrder);
	if (!context.panelContext->host->ExecuteEditorCommand(std::move(command))) {
		statusMessage_ = "適用に失敗しました";
		Logger::Output(LogType::Engine, spdlog::level::warn,
			"RenderPathGraphTool: apply failed.");
		return;
	}

	graphDirty_ = false;
	graphSaveDirty_ = true;
	statusMessage_ = "シーンへ適用しました";
	Logger::Output(LogType::Engine, spdlog::level::info,
		"RenderPathGraphTool: applied to scene. passes={} renderTargets={}",
		compiledResult_.passOrder.size(), compiledResult_.renderTargets.size());
}

void Engine::RenderPathGraphTool::SaveScene(const EditorToolContext& context) {

	if (!context.panelContext || !context.panelContext->host) {
		statusMessage_ = "エディターホストがありません";
		return;
	}
	if (validationDirty_) {
		// Errorが残ったGraphを保存しないように先に検証する
		ValidateCurrentGraph(context);
	}
	if (validationResult_.HasError()) {
		statusMessage_ = "検証エラーのため保存停止";
		return;
	}

	// Scene保存前にBackupを残しておく
	if (!MakeSceneBackup(context)) {
		return;
	}
	context.panelContext->host->RequestSaveScene();
	statusMessage_ = "保存を要求しました";
	Logger::Output(LogType::Engine, spdlog::level::info,
		"RenderPathGraphTool: save scene requested.");
}

void Engine::RenderPathGraphTool::ExportGraph(const EditorToolContext& context) {

	const std::string path = MakeActiveGraphPath(context);
	std::string error{};
	// Graph単体の保存。SceneHeaderへはまだ反映しない
	if (!RenderPathGraphSerializer::ExportGraph(path, document_, &error)) {
		statusMessage_ = error;
		Logger::Output(LogType::Engine, spdlog::level::warn,
			"RenderPathGraphTool: export graph failed. path={} error={}", path, error);
		return;
	}
	graphSaveDirty_ = false;
	statusMessage_ = "グラフを書き出しました";
	Logger::Output(LogType::Engine, spdlog::level::info,
		"RenderPathGraphTool: graph exported. path={}", path);
}

void Engine::RenderPathGraphTool::ImportGraph(const EditorToolContext& context) {

	const std::string path = MakeActiveGraphPath(context);
	std::string error{};
	// 保存済みGraphを読み込み、次描画でNode座標を反映する
	if (!RenderPathGraphSerializer::ImportGraph(path, document_, &error)) {
		statusMessage_ = error;
		Logger::Output(LogType::Engine, spdlog::level::warn,
			"RenderPathGraphTool: import graph failed. path={} error={}", path, error);
		return;
	}
	graphContext_.ResetPlacedNodes();
	graphDirty_ = false;
	graphSaveDirty_ = false;
	validationDirty_ = true;
	compilePreviewText_.clear();
	compileDiffText_.clear();
	resourceLifetimeText_.clear();
	barrierPreviewText_.clear();
	compiledResult_ = RenderPathGraphCompileResult{};
	capturedPasses_.clear();
	undoStack_.clear();
	redoStack_.clear();
	preDrawSnapshotValid_ = false;
	ValidateCurrentGraph(context);
	statusMessage_ = "グラフを読み込みました";
	Logger::Output(LogType::Engine, spdlog::level::info,
		"RenderPathGraphTool: graph imported. path={}", path);
}

void Engine::RenderPathGraphTool::AutoSaveGraph(const EditorToolContext& context) {

	if (!graphSaveDirty_) {
		return;
	}
	if (ImGui::IsMouseDown(ImGuiMouseButton_Left) || ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
		return;
	}
	if (!context.toolContext.activeSceneHeader) {
		return;
	}

	const std::string path = MakeActiveGraphPath(context);
	std::string error{};
	if (!RenderPathGraphSerializer::ExportGraph(path, document_, &error)) {
		Logger::Output(LogType::Engine, spdlog::level::warn,
			"RenderPathGraphTool: auto save graph failed. path={} error={}", path, error);
		return;
	}

	graphSaveDirty_ = false;
	Logger::Output(LogType::Engine, spdlog::level::debug,
		"RenderPathGraphTool: graph auto saved. path={}", path);
}

void Engine::RenderPathGraphTool::ApplyTemplate(const std::string& templateName) {

	// Undo用にリセット前のスナップショットを保存する
	PushUndoSnapshot(GraphSerializer::ToJson(document_));
	preDrawSnapshotValid_ = false;

	// 既存Graphをリセットしてからテンプレートを適用する
	document_.nodes.clear();
	document_.links.clear();
	document_.nextId = 1;
	document_.graphType = RenderPathGraph::kGraphType;
	document_.version = 1;
	graphContext_.ResetPlacedNodes();

	AppendTemplate(templateName);
	statusMessage_ = "テンプレートを適用しました";
	Logger::Output(LogType::Engine, spdlog::level::info,
		"RenderPathGraphTool: template applied. name={}", templateName);
}

void Engine::RenderPathGraphTool::AppendTemplate(const std::string& templateName) {

	const float originX = 80.0f + static_cast<float>(document_.nodes.size()) * 24.0f;
	const float passY = 120.0f;
	const float resourceY = -140.0f;

	auto addNode = [this](const std::string& type, const ImVec2& position) -> GraphID {
		return document_.AddNode(registry_.CreateNode(document_, type, position)).id;
	};
	auto node = [this](GraphID id) -> GraphNode* {
		return document_.FindNode(id);
	};
	auto setColor = [&](GraphID nodeID, const char* key, const std::string& name) {
		if (GraphNode* graphNode = node(nodeID)) {
			nlohmann::json target = graphNode->properties.value(key, nlohmann::json::object());
			SetFirstColor(target, name);
			graphNode->properties[key] = target;
		}
	};
	auto linkFlow = [&](GraphID fromNodeID, GraphID toNodeID) {
		GraphNode* fromNode = node(fromNodeID);
		GraphNode* toNode = node(toNodeID);
		GraphPin* from = fromNode ? FindOutputPin(*fromNode, "Out") : nullptr;
		GraphPin* to = toNode ? FindInputPin(*toNode, "In") : nullptr;
		if (from && to) {
			document_.AddLink(from->id, to->id);
		}
	};
	auto linkColor = [&](GraphID fromNodeID, GraphID toNodeID) {
		GraphNode* fromNode = node(fromNodeID);
		GraphNode* toNode = node(toNodeID);
		GraphPin* from = fromNode ? FindOutputPin(*fromNode, "DestColor") : nullptr;
		GraphPin* to = toNode ? FindInputPin(*toNode, "SourceColor") : nullptr;
		if (from && to) {
			document_.AddLink(from->id, to->id);
		}
	};

	if (templateName == "ForwardBasic") {
		const GraphID clear = addNode(RenderPathGraph::kClear, ImVec2(originX, passY));
		const GraphID draw = addNode(RenderPathGraph::kDraw, ImVec2(originX + 300.0f, passY));
		const GraphID blit = addNode(RenderPathGraph::kBlit, ImVec2(originX + 600.0f, passY));
		addNode(RenderPathGraph::kView, ImVec2(originX + 900.0f, 360.0f));
		setColor(clear, "dest", "SceneMain");
		setColor(draw, "dest", "SceneMain");
		setColor(blit, "source", "SceneMain");
		setColor(blit, "dest", "View");
		linkFlow(clear, draw);
		linkFlow(draw, blit);
	} else if (templateName == "DepthDrawBlit") {
		const GraphID clear = addNode(RenderPathGraph::kClear, ImVec2(originX, passY));
		const GraphID depth = addNode(RenderPathGraph::kDepthPrepass, ImVec2(originX + 300.0f, passY));
		const GraphID draw = addNode(RenderPathGraph::kDraw, ImVec2(originX + 600.0f, passY));
		const GraphID blit = addNode(RenderPathGraph::kBlit, ImVec2(originX + 900.0f, passY));
		addNode(RenderPathGraph::kView, ImVec2(originX + 1200.0f, 360.0f));
		setColor(clear, "dest", "SceneMain");
		setColor(depth, "dest", "SceneMain");
		setColor(draw, "dest", "SceneMain");
		setColor(blit, "source", "SceneMain");
		setColor(blit, "dest", "View");
		linkFlow(clear, depth);
		linkFlow(depth, draw);
		linkFlow(draw, blit);
	} else if (templateName == "HDRBloom") {
		const GraphID hdr = addNode(RenderPathGraph::kTemporaryTarget, ImVec2(originX + 300.0f, resourceY));
		const GraphID tempA = addNode(RenderPathGraph::kTemporaryTarget, ImVec2(originX + 600.0f, resourceY));
		const GraphID tempB = addNode(RenderPathGraph::kTemporaryTarget, ImVec2(originX + 900.0f, resourceY));
		if (GraphNode* graphNode = node(hdr)) {
			graphNode->properties["name"] = "HDRScene";
			graphNode->properties["format"] = "RGBA16_FLOAT";
			graphNode->displayName = "RT : HDRScene";
		}
		if (GraphNode* graphNode = node(tempA)) {
			graphNode->properties["name"] = "BloomTempA";
			graphNode->properties["format"] = "RGBA16_FLOAT";
			graphNode->displayName = "RT : BloomTempA";
		}
		if (GraphNode* graphNode = node(tempB)) {
			graphNode->properties["name"] = "BloomTempB";
			graphNode->properties["format"] = "RGBA16_FLOAT";
			graphNode->displayName = "RT : BloomTempB";
		}
		const GraphID clear = addNode(RenderPathGraph::kClear, ImVec2(originX, passY));
		const GraphID draw = addNode(RenderPathGraph::kDraw, ImVec2(originX + 300.0f, passY));
		const GraphID prefilter = addNode(RenderPathGraph::kPostProcess, ImVec2(originX + 600.0f, passY));
		const GraphID blur = addNode(RenderPathGraph::kPostProcess, ImVec2(originX + 900.0f, passY));
		const GraphID tonemap = addNode(RenderPathGraph::kPostProcess, ImVec2(originX + 1200.0f, passY));
		const GraphID blit = addNode(RenderPathGraph::kBlit, ImVec2(originX + 1500.0f, passY));
		setColor(clear, "dest", "HDRScene");
		setColor(draw, "dest", "HDRScene");
		setColor(prefilter, "source", "HDRScene");
		setColor(prefilter, "dest", "BloomTempA");
		setColor(blur, "source", "BloomTempA");
		setColor(blur, "dest", "BloomTempB");
		setColor(tonemap, "source", "BloomTempB");
		setColor(tonemap, "dest", "SceneFinal");
		setColor(blit, "source", "SceneFinal");
		setColor(blit, "dest", "View");
		linkFlow(clear, draw);
		linkFlow(draw, prefilter);
		linkFlow(prefilter, blur);
		linkFlow(blur, tonemap);
		linkFlow(tonemap, blit);
		linkColor(prefilter, blur);
		linkColor(blur, tonemap);
		linkColor(tonemap, blit);
	} else if (templateName == "ComputePost") {
		const GraphID temp = addNode(RenderPathGraph::kTemporaryTarget, ImVec2(originX + 300.0f, resourceY));
		if (GraphNode* graphNode = node(temp)) {
			graphNode->properties["name"] = "ComputeTemp";
			graphNode->displayName = "RT : ComputeTemp";
		}
		const GraphID clear = addNode(RenderPathGraph::kClear, ImVec2(originX, passY));
		const GraphID compute = addNode(RenderPathGraph::kCompute, ImVec2(originX + 300.0f, passY));
		const GraphID post = addNode(RenderPathGraph::kPostProcess, ImVec2(originX + 600.0f, passY));
		const GraphID blit = addNode(RenderPathGraph::kBlit, ImVec2(originX + 900.0f, passY));
		setColor(clear, "dest", "SceneMain");
		setColor(compute, "source", "SceneMain");
		setColor(compute, "dest", "ComputeTemp");
		setColor(post, "source", "ComputeTemp");
		setColor(post, "dest", "SceneFinal");
		setColor(blit, "source", "SceneFinal");
		setColor(blit, "dest", "View");
		linkFlow(clear, compute);
		linkFlow(compute, post);
		linkFlow(post, blit);
		linkColor(compute, post);
		linkColor(post, blit);
	}

	graphDirty_ = true;
	validationDirty_ = true;
	graphSaveDirty_ = true;
	requestFitToGraph_ = true;
	compilePreviewText_.clear();
	compileDiffText_.clear();
	resourceLifetimeText_.clear();
	barrierPreviewText_.clear();
	statusMessage_ = "テンプレートを追加しました";
	Logger::Output(LogType::Engine, spdlog::level::info,
		"RenderPathGraphTool: template appended. name={}", templateName);
}

void Engine::RenderPathGraphTool::UpdateResourceAnalysisText() {

	std::unordered_map<std::string, std::vector<std::string>> lifetime{};
	std::unordered_map<std::string, std::string> lastState{};
	std::string barriers{};

	auto passName = [](const ScenePassDesc& pass, size_t index) {
		std::string name = std::to_string(index) + " : " + EnumAdapter<ScenePassType>::ToString(pass.type);
		switch (pass.type) {
		case ScenePassType::Draw:
			if (!pass.draw.queue.empty()) {
				name += " " + pass.draw.queue;
			}
			break;
		case ScenePassType::Compute:
			if (!pass.compute.passName.empty()) {
				name += " " + pass.compute.passName;
			}
			break;
		case ScenePassType::RenderScene:
			if (!pass.renderScene.subSceneSlot.empty()) {
				name += " " + pass.renderScene.subSceneSlot;
			}
			break;
		case ScenePassType::Raytracing:
			if (!pass.raytracing.passName.empty()) {
				name += " " + pass.raytracing.passName;
			}
			break;
		default:
			break;
		}
		return name;
	};
	auto addResource = [&](const std::string& resource, const std::string& pass, const char* usage) {
		if (resource.empty()) {
			return;
		}
		lifetime[resource].emplace_back(pass + " " + usage);
		const std::string state = usage;
		const auto it = lastState.find(resource);
		if (it != lastState.end() && it->second != state) {
			barriers += resource + " : " + it->second + " -> " + state + " @ " + pass + "\n";
		}
		lastState[resource] = state;
	};
	auto addRead = [&](const RenderTargetSetReference& target, const std::string& pass) {
		for (const std::string& color : target.colors) {
			addResource(color, pass, "Read");
		}
		if (target.depth.has_value()) {
			addResource(*target.depth, pass, "ReadDepth");
		}
	};
	auto addWrite = [&](const RenderTargetSetReference& target, const std::string& pass) {
		for (const std::string& color : target.colors) {
			addResource(color, pass, "Write");
		}
		if (target.depth.has_value()) {
			addResource(*target.depth, pass, "WriteDepth");
		}
	};

	for (size_t i = 0; i < compiledResult_.passOrder.size(); ++i) {
		const ScenePassDesc& pass = compiledResult_.passOrder[i];
		const std::string name = passName(pass, i);
		switch (pass.type) {
		case ScenePassType::Clear:
			addWrite(pass.clear.dest, name);
			break;
		case ScenePassType::DepthPrepass:
			addWrite(pass.depthPrepass.dest, name);
			break;
		case ScenePassType::Draw:
			addWrite(pass.draw.dest, name);
			break;
		case ScenePassType::PostProcess:
			addRead(pass.postProcess.source, name);
			for (const auto& [binding, resource] : pass.postProcess.extraSources) {
				(void)binding;
				addResource(resource, name, "Read");
			}
			addWrite(pass.postProcess.dest, name);
			break;
		case ScenePassType::Compute:
			addRead(pass.compute.source, name);
			addWrite(pass.compute.dest, name);
			break;
		case ScenePassType::RenderScene:
			addWrite(pass.renderScene.dest, name);
			break;
		case ScenePassType::Blit:
			addRead(pass.blit.source, name);
			addWrite(pass.blit.dest, name);
			break;
		case ScenePassType::Raytracing:
			addRead(pass.raytracing.source, name);
			addWrite(pass.raytracing.dest, name);
			break;
		default:
			break;
		}
	}

	resourceLifetimeText_.clear();
	for (const auto& [resource, usages] : lifetime) {
		resourceLifetimeText_ += resource + "\n";
		for (const std::string& usage : usages) {
			resourceLifetimeText_ += "  " + usage + "\n";
		}
	}
	if (resourceLifetimeText_.empty()) {
		resourceLifetimeText_ = "No resource usage.\n";
	}
	barrierPreviewText_ = barriers.empty() ? "No state transition.\n" : barriers;
}

void Engine::RenderPathGraphTool::ResetLayout() {

	// Undo用に整列前のスナップショットを保存する
	PushUndoSnapshot(GraphSerializer::ToJson(document_));
	preDrawSnapshotValid_ = false;

	// Flow Linkから実行順を取得してPassを並べる
	std::unordered_map<GraphID, GraphID> nextNode{};
	std::unordered_map<GraphID, uint32_t> incomingFlow{};

	for (const GraphNode& node : document_.nodes) {
		for (const GraphPin& pin : node.inputs) {
			if (pin.valueType == GraphValueType::Flow) {
				incomingFlow[node.id] = 0;
				break;
			}
		}
	}
	for (const GraphLink& link : document_.links) {
		const GraphPin* from = document_.FindPin(link.fromPinID);
		const GraphPin* to = document_.FindPin(link.toPinID);
		if (!from || !to || from->valueType != GraphValueType::Flow || to->valueType != GraphValueType::Flow) {
			continue;
		}
		nextNode[from->nodeID] = to->nodeID;
		++incomingFlow[to->nodeID];
	}

	GraphID start = 0;
	for (const auto& [nodeID, count] : incomingFlow) {
		if (count == 0) {
			start = nodeID;
			break;
		}
	}

	// 開始NodeからFlow順にPass配置X座標を決める
	std::unordered_map<GraphID, float> passXByNode{};
	{
		std::unordered_set<GraphID> visited{};
		float x = 80.0f;
		GraphID current = start;
		while (current != 0 && !visited.contains(current)) {
			visited.insert(current);
			passXByNode[current] = x;
			x += 300.0f;
			const auto nextIt = nextNode.find(current);
			current = nextIt != nextNode.end() ? nextIt->second : 0;
		}
	}

	float resourceX = 80.0f + 300.0f;
	float viewX = passXByNode.empty() ? 80.0f : 80.0f + static_cast<float>(passXByNode.size()) * 300.0f;
	float orphanX = viewX + 300.0f;

	for (GraphNode& node : document_.nodes) {
		if (node.type == RenderPathGraph::kGroup) {
			// グループNodeは整列対象外
		} else if (node.type == RenderPathGraph::kTemporaryTarget) {
			node.position = ImVec2(resourceX, -140.0f);
			resourceX += 300.0f;
		} else if (node.type == RenderPathGraph::kView) {
			node.position = ImVec2(viewX, 360.0f);
			viewX += 300.0f;
		} else if (passXByNode.contains(node.id)) {
			node.position = ImVec2(passXByNode.at(node.id), 120.0f);
		} else {
			node.position = ImVec2(orphanX, 120.0f);
			orphanX += 300.0f;
		}
	}

	graphContext_.ResetPlacedNodes();
	graphDirty_ = true;
	graphSaveDirty_ = true;
	statusMessage_ = "レイアウトを整列しました";
	Logger::Output(LogType::Engine, spdlog::level::info,
		"RenderPathGraphTool: layout reset.");
}

bool Engine::RenderPathGraphTool::MakeSceneBackup(const EditorToolContext& context) {

	const std::string scenePath(context.toolContext.activeScenePath);
	if (scenePath.empty()) {
		statusMessage_ = "シーンパスが空です";
		return false;
	}

	const std::filesystem::path fullPath = RuntimePaths::ResolveAssetPath(scenePath);
	if (fullPath.empty() || !std::filesystem::exists(fullPath)) {
		statusMessage_ = "シーンファイルが見つかりません";
		return false;
	}

	const std::filesystem::path backupPath = MakeBackupPath(fullPath);
	std::error_code ec{};
	// 失敗しても例外で落とさず、Tool上に状態を表示する
	std::filesystem::copy_file(fullPath, backupPath,
		std::filesystem::copy_options::overwrite_existing, ec);
	if (ec) {
		statusMessage_ = "シーンバックアップに失敗しました";
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
	statusMessage_ = "ノードスタイルを保存しました";
}

void Engine::RenderPathGraphTool::PushUndoSnapshot(nlohmann::json snapshot) {

	// Redoスタックを破棄して新しい変更を積む
	constexpr size_t kMaxUndoSteps = 50;
	redoStack_.clear();
	undoStack_.push_back(std::move(snapshot));
	if (undoStack_.size() > kMaxUndoSteps) {
		undoStack_.erase(undoStack_.begin());
	}
}

void Engine::RenderPathGraphTool::UndoGraph() {

	if (undoStack_.empty()) {
		return;
	}
	// 現在の状態をRedoスタックへ退避してからUndoする
	redoStack_.push_back(GraphSerializer::ToJson(document_));
	GraphSerializer::FromJson(undoStack_.back(), document_);
	undoStack_.pop_back();

	graphContext_.ResetPlacedNodes();
	preDrawSnapshotValid_ = false;
	undoPending_ = false;
	graphDirty_ = true;
	validationDirty_ = true;
	graphSaveDirty_ = true;
	compilePreviewText_.clear();
	compileDiffText_.clear();
	resourceLifetimeText_.clear();
	barrierPreviewText_.clear();
	compiledResult_ = RenderPathGraphCompileResult{};
	capturedPasses_.clear();
	statusMessage_ = "元に戻しました";
	Logger::Output(LogType::Engine, spdlog::level::info,
		"RenderPathGraphTool: undo. undoStack={} redoStack={}",
		undoStack_.size(), redoStack_.size());
}

void Engine::RenderPathGraphTool::RedoGraph() {

	if (redoStack_.empty()) {
		return;
	}
	// 現在の状態をUndoスタックへ退避してからRedoする
	undoStack_.push_back(GraphSerializer::ToJson(document_));
	GraphSerializer::FromJson(redoStack_.back(), document_);
	redoStack_.pop_back();

	graphContext_.ResetPlacedNodes();
	preDrawSnapshotValid_ = false;
	undoPending_ = false;
	graphDirty_ = true;
	validationDirty_ = true;
	graphSaveDirty_ = true;
	compilePreviewText_.clear();
	compileDiffText_.clear();
	resourceLifetimeText_.clear();
	barrierPreviewText_.clear();
	compiledResult_ = RenderPathGraphCompileResult{};
	capturedPasses_.clear();
	statusMessage_ = "やり直しました";
	Logger::Output(LogType::Engine, spdlog::level::info,
		"RenderPathGraphTool: redo. undoStack={} redoStack={}",
		undoStack_.size(), redoStack_.size());
}

void Engine::RenderPathGraphTool::DrawMinimap() {

	if (document_.nodes.empty()) {
		return;
	}

	// 全Nodeの座標範囲を計算する
	float minX = 1e9f, minY = 1e9f;
	float maxX = -1e9f, maxY = -1e9f;
	for (const GraphNode& node : document_.nodes) {
		minX = (std::min)(minX, node.position.x);
		minY = (std::min)(minY, node.position.y);
		maxX = (std::max)(maxX, node.position.x + 180.0f);
		maxY = (std::max)(maxY, node.position.y + 60.0f);
	}

	const float rangeX = (std::max)(1.0f, maxX - minX);
	const float rangeY = (std::max)(1.0f, maxY - minY);

	// CanvasウィンドウのMinimapパネル配置
	constexpr float kWidth = 160.0f;
	constexpr float kHeight = 100.0f;
	constexpr float kPadding = 8.0f;

	const ImVec2 winPos = ImGui::GetWindowPos();
	const ImVec2 winSize = ImGui::GetWindowSize();
	const ImVec2 minimapMin(winPos.x + winSize.x - kWidth - kPadding,
		winPos.y + winSize.y - kHeight - kPadding);
	const ImVec2 minimapMax(minimapMin.x + kWidth, minimapMin.y + kHeight);

	ImDrawList* dl = ImGui::GetWindowDrawList();
	dl->AddRectFilled(minimapMin, minimapMax, IM_COL32(18, 18, 18, 210), 4.0f);
	dl->AddRect(minimapMin, minimapMax, IM_COL32(80, 80, 80, 220), 4.0f);

	// 各NodeをMinimapへ描画する
	const NodeGraphStyle& style = graphView_.GetStyle();
	for (const GraphNode& node : document_.nodes) {
		const float nx = (node.position.x - minX) / rangeX * (kWidth - 6.0f);
		const float ny = (node.position.y - minY) / rangeY * (kHeight - 6.0f);
		const ImVec2 dotMin(minimapMin.x + 3.0f + nx, minimapMin.y + 3.0f + ny);
		const ImVec2 dotMax(dotMin.x + 9.0f, dotMin.y + 5.0f);
		const ImVec4 col = style.GetNodeAccentColor(node.type);
		dl->AddRectFilled(dotMin, dotMax, ImGui::ColorConvertFloat4ToU32(col), 1.0f);
	}

	dl->AddText(ImVec2(minimapMin.x + 4.0f, minimapMin.y + 2.0f),
		IM_COL32(160, 160, 160, 200), "MAP");
}
